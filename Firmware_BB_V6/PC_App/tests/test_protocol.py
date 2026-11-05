from pathlib import Path
import sys,unittest,tempfile,json,queue,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from protocol import defaults,validate,upgrade_settings,scaled,command,config_command,Framer,save_profile,load_profile
from transport import Request,SerialWorker,DemoBackend

class ConfigurationTests(unittest.TestCase):
    def test_solder_supply_limits_and_wire(self):
        s=defaults();self.assertEqual((s['solder_voltage'],s['solder_current']),(18000,6000))
        s.update(solder_voltage=7500,solder_current=1200);validate(s)
        wire=config_command(21,0,s)
        self.assertIn(b'solder_voltage=7500',wire);self.assertIn(b'solder_current=1200',wire)
        for key,value in [('solder_voltage','1.49'),('solder_voltage','60'),('solder_current','0'),('solder_current','15.01'),('solder_current','nan')]:
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):scaled(key,value)

    def test_solder_legacy_profiles_and_old_firmware_wire(self):
        old=defaults();old.pop('solder_voltage');old.pop('solder_current');old['cc_ki']=800000
        expanded=upgrade_settings(old)
        self.assertEqual((expanded['solder_voltage'],expanded['solder_current'],expanded['cc_ki']),(18000,6000,800000))
        self.assertNotIn(b'solder_voltage=',config_command(22,0,old))
        old.pop('ramp_v_per_s');expanded=upgrade_settings(old)
        self.assertEqual(expanded['ramp_v_per_s'],10000)

    def test_default_round_trip(self):
        s=defaults();validate(s)
        wire=config_command(7,0,s)
        self.assertLessEqual(len(wire),1024);self.assertTrue(wire.startswith(b'@7 CFG 0 '))
        self.assertIn(b'cv_kp=15000',wire)

    def test_hard_limits_and_nonfinite(self):
        for key,value in [('i_peak','20.01'),('i_max','15.01'),('vout_max','60'),('vin_max','60.01'),('power_max','600.01'),('input_current_max','10.01'),('cv_kp','nan'),('vdda','inf'),('ntc_to_gnd','.5')]:
            with self.subTest(key=key),self.assertRaises(ValueError):scaled(key,value)

    def test_coupled_limits(self):
        for changes in [dict(i_max=8000,i_peak=5000),dict(temp_reset=750,temp_warn=700),dict(i_cal=499999),dict(vin_min=20000,vin_warn=7800)]:
            s=defaults();s.update(changes)
            with self.subTest(changes=changes),self.assertRaises(ValueError):validate(s)
        s=defaults();s['i_peak']=10000;s['i_max']=8000;s['i_cal']=500000;validate(s)

    def test_reject_wrong_schema(self):
        s=defaults();s['unknown']=5
        with self.assertRaises(ValueError):validate(s)
        with self.assertRaises(ValueError):validate({})

    def test_profiles(self):
        with tempfile.TemporaryDirectory() as path:
            file=Path(path)/'profile.json';s=defaults();save_profile(file,s,5000,1000)
            self.assertEqual(load_profile(file)['settings'],s)
            with self.assertRaises(ValueError):save_profile(file,s,60000,1000)
            file.write_text('{"schema":1,"settings":{}}')
            with self.assertRaises(ValueError):load_profile(file)

    def test_fragmented_json(self):
        f=Framer();self.assertEqual(f.feed(b'{"type":"tele'),[])
        self.assertEqual(f.feed(b'metry","ms":1}\r\n')[0]['ms'],1)
        self.assertEqual(len(f.feed(b'{"ms":2}\n{"ms":3}\n')),2)

    def test_full_power_profile_and_adc_headroom(self):
        s=defaults();validate(s)
        self.assertEqual({k:s[k] for k in ('vin_max','vout_max','i_max','i_peak','power_max','input_current_max')},dict(vin_max=60000,vout_max=59900,i_max=15000,i_peak=20000,power_max=600000,input_current_max=10000))
        for change in (dict(i_cal=500000),dict(vdda=2700,vin_cal=800000),dict(vdda=2700,vout_cal=800000)):
            bad=dict(s,**change)
            with self.assertRaises(ValueError):validate(bad)
        with tempfile.TemporaryDirectory() as path:
            file=Path(path)/'full.json';save_profile(file,s,59900,15000)
            self.assertEqual(load_profile(file)['settings'],s)

    def test_legacy_lower_limits_remain_valid(self):
        s=defaults();s.update(vin_max=40000,vout_max=36000,i_max=8000,i_peak=10000,power_max=60000,input_current_max=5000,i_cal=500000,vdda=2700,vin_cal=800000,vout_cal=800000)
        validate(s)

    def test_ramp_limits_and_legacy_profile(self):
        self.assertEqual(scaled('ramp_v_per_s','2.50'),2500)
        for value in ['0','-1','100.01','nan']:
            with self.assertRaises(ValueError):scaled('ramp_v_per_s',value)
        old=defaults();old.pop('ramp_v_per_s');old['cc_ki']=800000
        self.assertEqual(upgrade_settings(old)['cc_ki'],800000)
        self.assertEqual(upgrade_settings(old)['ramp_v_per_s'],10000)
        self.assertNotIn(b'ramp_v_per_s=',config_command(9,1,old))
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'old.json'
            path.write_text(json.dumps(dict(schema=1,settings=old,set_mV=1500,set_mA=2000)))
            self.assertEqual(load_profile(path)['settings']['ramp_v_per_s'],10000)

    def test_corrupt_and_oversized_json(self):
        f=Framer();self.assertEqual(f.feed(b'not-json\n'),[])
        self.assertEqual(f.feed(b'x'*9000+b'\n{"ms":4}\n'),[{'ms':4}]);self.assertEqual(f.errors,2)

    def test_injection_and_invalid_id(self):
        with self.assertRaises(ValueError):command(0,'GET')
        with self.assertRaises(ValueError):command(1,'SET','5000\nON',1000)
        with self.assertRaises(ValueError):command(1,'BAD')

class FakeSerial:
    def __init__(self,**kwargs):self.backend=DemoBackend();self.writes=[];self.rx=bytearray();self.closed=False;self.hold_on=False;self.held=b'';self.drop_on=False
    @property
    def in_waiting(self):return len(self.rx)
    def write(self,data):
        self.writes.append(data)
        if data==b'!\n':
            self.backend.enabled=False;self.rx.extend(self.held);self.held=b'';return len(data)
        tokens=data.decode().split();identifier=int(tokens[0][1:]);op=tokens[1]
        args=tuple(map(int,tokens[2:])) if op in ('SET','PRESET','DEFAULTS') else ()
        settings=None
        if op=='CFG':args=(int(tokens[2]),);settings=dict(pair.split('=') for pair in tokens[3:]);settings={k:int(v) for k,v in settings.items()}
        reply=self.backend.reply(Request(identifier,op,args,settings));raw=json.dumps(reply).encode()+b'\n'
        if op=='ON' and self.hold_on:self.held=raw
        elif op!='ON' or not self.drop_on:self.rx.extend(raw)
        return len(data)
    def read(self,size):
        if not self.rx:time.sleep(.005);return b''
        data=bytes(self.rx[:11]);del self.rx[:11];return data
    def close(self):self.closed=True

class LinkTests(unittest.TestCase):
    def setUp(self):
        self.events=queue.Queue();self.fake=FakeSerial();self.worker=SerialWorker('FAKE',self.events,lambda **kw:self.fake);self.worker.start();self.wait(lambda:self.fake.writes==[])
    def tearDown(self):self.worker.stop();self.worker.join(1);self.assertTrue(self.fake.closed)
    def wait(self,predicate,timeout=1):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            if predicate():return
            time.sleep(.005)
        self.fail('Timed out waiting for serial worker')
    def read_event(self,kind,timeout=1):
        end=time.monotonic()+timeout
        while time.monotonic()<end:
            try:event=self.events.get(timeout=.05)
            except queue.Empty:continue
            if event[0]==kind:return event[1]
        self.fail('No event '+kind)
    def test_connect_never_enables_output(self):
        self.worker.submit('GET');reply=self.read_event('reply')
        self.assertFalse(reply['enabled']);self.assertEqual(len(self.fake.writes),1)
        self.assertNotIn(b' ON',self.fake.writes[0])
    def test_configuration_acknowledged(self):
        cfg=defaults();cfg['cv_kp']=25000;self.worker.submit('CFG',0,settings=cfg);cfg['cv_kp']=999999
        reply=self.read_event('reply');self.assertTrue(reply['ok']);self.assertEqual(reply['settings']['cv_kp'],25000)
    def test_panic_cancels_late_on_ack(self):
        self.fake.hold_on=True;self.worker.submit('ON');self.wait(lambda:bool(self.fake.held));self.worker.panic()
        self.read_event('notice');reply=self.read_event('reply')
        self.assertEqual(reply['op'],'GET');self.assertFalse(reply['enabled']);self.assertIn(b'!\n',self.fake.writes)
    def test_lost_ack_does_not_repeat_on(self):
        self.fake.drop_on=True;self.worker.submit('ON');error=self.read_event('error',4)
        self.assertIn('không tự gửi lại',error);reply=self.read_event('reply')
        self.assertEqual(reply['op'],'GET');self.assertEqual(sum(b' ON' in w for w in self.fake.writes),1)

if __name__=='__main__':unittest.main(verbosity=2)
