"""One acknowledged request at a time; no retries of state-changing commands."""
from __future__ import annotations
from dataclasses import dataclass
import queue,threading,time
from copy import deepcopy
import serial
from serial.tools import list_ports
from protocol import Framer,command,config_command,defaults,validate,upgrade_settings,SCHEMA

@dataclass
class Request:
    identifier:int
    op:str
    args:tuple=()
    settings:dict|None=None

def ports():
    return sorted(list_ports.comports(),key=lambda p:(p.vid!=0x0483,p.device))

class SerialWorker(threading.Thread):
    def __init__(self,port:str,events:queue.Queue,serial_factory=None):
        super().__init__(daemon=True,name='BBV6-USB')
        self.port=port;self.events=events;self.requests=queue.Queue(maxsize=8)
        self.stopping=threading.Event();self.emergency=threading.Event();self.next_id=1
        self.serial_factory=serial_factory or serial.Serial

    def submit(self,op,*args,settings=None):
        identifier=self.next_id;self.next_id=self.next_id%2147483647+1
        req=Request(identifier,op,args,deepcopy(settings))
        # Validate/encode before queuing; user errors never reach the wire.
        self.encode(req);self.requests.put_nowait(req);return identifier

    @staticmethod
    def encode(req):
        return config_command(req.identifier,req.args[0],req.settings) if req.op=='CFG' else command(req.identifier,req.op,*req.args)

    def panic(self):self.emergency.set()
    def stop(self):self.stopping.set()
    def clear_queue(self):
        while True:
            try:self.requests.get_nowait()
            except queue.Empty:return

    def run(self):
        link=None;pending=None;deadline=0;framer=Framer()
        try:
            link=self.serial_factory(port=self.port,baudrate=115200,timeout=.04,write_timeout=.5)
            self.events.put(('connected',self.port))
            while not self.stopping.is_set():
                if self.emergency.is_set():
                    self.emergency.clear();self.clear_queue()
                    if link.write(b'!\n')!=2:raise OSError('USB write incomplete')
                    if pending and pending.op=='ON':pending=None
                    self.events.put(('notice','Đã gửi OFF ưu tiên; chờ dữ liệu xác nhận.'))
                    self.submit('GET')
                if pending is None:
                    try:pending=self.requests.get_nowait()
                    except queue.Empty:pass
                    if pending:
                        wire=self.encode(pending)
                        if link.write(wire)!=len(wire):raise OSError('USB write incomplete')
                        deadline=time.monotonic()+3
                data=link.read(max(1,min(getattr(link,'in_waiting',0),4096)))
                for message in framer.feed(data):
                    if message.get('type')=='reply':
                        if pending and message.get('id')==pending.identifier:
                            self.events.put(('reply',message));pending=None
                        else:self.events.put(('orphan_reply',message))
                    elif 'ms' in message:
                        message['_received']=time.monotonic();self.events.put(('telemetry',message))
                if pending and time.monotonic()>deadline:
                    op=pending.op;identifier=pending.identifier;pending=None;self.clear_queue()
                    self.events.put(('error',f'Không nhận ACK cho {op} #{identifier}. Chưa xác nhận kết quả; không tự gửi lại.'))
                    if op!='GET':self.submit('GET')
        except (serial.SerialException,OSError,ValueError) as exc:self.events.put(('error',str(exc)))
        finally:
            if link:
                try:link.close()
                except OSError:pass
            self.events.put(('disconnected',None))

class DemoBackend:
    """A labelled UI demo, not a simulation of the converter's power stage."""
    def __init__(self):
        self.settings=defaults();self.revision=0;self.enabled=False;self.mv=5000;self.ma=1000
        self.presets=[[5000,1000],[12000,3000],[24000,2000]];self.vo=0.;self.drain=0
        self.start=time.monotonic();self.saved=None;self.reference=0.;self.last_telemetry=self.start

    def reply(self,req):
        error='ok';s=self.settings
        idle=not self.enabled and self.drain not in (1,2)
        if req.op=='SET':
            mv,ma=req.args
            if not 1500<=mv<=s['vout_max'] or not 50<=ma<=s['i_max']:error='setpoint_range'
            else:self.mv,self.ma=mv,ma
        elif req.op=='ON':self.enabled=True;self.drain=0;self.reference=min(self.vo,self.mv/1000);self.last_telemetry=time.monotonic()
        elif req.op=='OFF':self.enabled=False;self.drain=2 if s['discharge_enabled'] and self.vo>.5 else 3
        elif req.op in ('CFG','DEFAULTS'):
            if not idle:error='output_off_required'
            elif req.args[0]!=self.revision:error='revision_changed'
            else:
                candidate=defaults() if req.op=='DEFAULTS' else upgrade_settings(req.settings)
                validate(candidate);self.settings=candidate;self.revision+=1
                self.mv=min(self.mv,candidate['vout_max']);self.ma=min(self.ma,candidate['i_max'])
                self.presets=[[min(v,candidate['vout_max']),min(a,candidate['i_max'])] for v,a in self.presets]
        elif req.op=='SAVE':
            if not idle:error='output_off_required'
            else:self.saved=deepcopy(self.settings)
        elif req.op=='PRESET':
            k,mv,ma=req.args
            if not idle:error='output_off_required'
            elif not 0<=k<=2 or not 1500<=mv<=s['vout_max'] or not 50<=ma<=s['i_max']:error='setpoint_range'
            else:self.presets[k]=[mv,ma]
        elif req.op not in ('GET','CLEAR'):error='unknown_command'
        return dict(type='reply',protocol=1,fw='DEMO',id=req.identifier,op=req.op,ok=error=='ok',error=error,rev=self.revision,enabled=int(self.enabled),fault=0,start_block=0,set_mV=self.mv,set_mA=self.ma,softstart=True,settings=deepcopy(self.settings),presets=deepcopy(self.presets),caps=deepcopy(SCHEMA['caps']))

    def telemetry(self):
        now=time.monotonic();dt=max(0,min(.25,now-self.last_telemetry));self.last_telemetry=now
        self.reference=min(self.mv/1000,self.reference+self.settings['ramp_v_per_s']/1000*dt) if self.enabled else 0
        target=min(self.reference,self.ma/1000*20) if self.enabled else 0
        if self.enabled:self.vo+=.18*(target-self.vo)
        elif self.drain==2:
            self.vo*=.92
            if self.vo<=.5:self.drain=3
        else:self.vo*=.99
        current=self.vo/20 if self.enabled else 0
        ratio=self.vo/17.8 if self.enabled else 0
        ms=int((time.monotonic()-self.start)*1000)
        return dict(type='telemetry',protocol=1,ms=ms,enabled=int(self.enabled),rev=self.revision,softstart=True,vin_mV=17800,vout_mV=int(self.vo*1000),iout_mA=int(current*1000),temp_dC=320,temp_valid=1,temp_adc=1800,set_mV=self.mv,set_mA=self.ma,state=(3 if self.enabled and self.ma/1000*20<self.mv/1000 else 1 if self.reference<self.mv/1000 else 2) if self.enabled else 0,fault=0,config=0,start_block=0,ready_ms=150,vin_warn=0,buck_pm=int(min(ratio,1)*900),boost_ho_pm=900 if self.enabled else 0,ratio_pm=int(ratio*1000),cv_pm=int(ratio*1000),cc_pm=int(ratio*1000),ref_mV=int(self.reference*1000),ramp_active=int(self.enabled and self.reference<self.mv/1000),ramp_mV_s=self.settings['ramp_v_per_s'],ilim_mA=self.ma,imax_mA=self.settings['i_max'],ipeak_mA=self.settings['i_peak'],adc_age=0,samples=ms*15,cycles=13000,drops=0,rx_drops=0,drain=dict(state=self.drain,pulses=int(ms/20),ticks=900),trip={},_received=time.monotonic())

class DemoWorker(SerialWorker):
    def __init__(self,events):super().__init__('DEMO',events);self.backend=DemoBackend()
    def run(self):
        self.events.put(('connected','DEMO'));next_log=0
        while not self.stopping.wait(.015):
            if self.emergency.is_set():
                self.emergency.clear();self.clear_queue();self.backend.enabled=False;self.backend.drain=7
                self.events.put(('notice','DEMO: đã OFF và dừng xả.'));self.submit('GET')
            try:req=self.requests.get_nowait()
            except queue.Empty:req=None
            if req:self.events.put(('reply',self.backend.reply(req)))
            if time.monotonic()>=next_log:
                self.events.put(('telemetry',self.backend.telemetry()));next_log=time.monotonic()+.1
        self.events.put(('disconnected',None))
