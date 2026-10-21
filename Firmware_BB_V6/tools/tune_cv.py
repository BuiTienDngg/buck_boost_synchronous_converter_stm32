"""Capture the real board; optionally compare bounded CV step experiments.

Default is read-only (GET and '?'). --run permits 4.9..5.1 V / <=1 A tests.
No command is retried, faults are never cleared, Flash is never written.
"""
from pathlib import Path
import argparse, datetime, json, sys, time, statistics
import serial
from serial.tools import list_ports

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'PC_App'))
from protocol import Framer, command, config_command


def discover():
    ports=[p.device for p in list_ports.comports() if p.vid==0x0483 and p.pid==0x5740]
    if len(ports)!=1:raise RuntimeError(f'Expected one STM32 CDC port, found {ports}')
    return ports[0]


class Board:
    def __init__(self,port,folder):
        self.link=serial.Serial(port,115200,timeout=.005,write_timeout=.5)
        self.log=(folder/'capture.jsonl').open('w',encoding='utf-8')
        self.framer=Framer();self.identifier=820000000;self.latest=None
        self.received=0.;self.next_query=0.;self.messages=[];self.owned=False
        self.checked=False;self.target=5000

    def event(self,kind,**data):
        self.log.write(json.dumps(dict(host_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),host_s=time.monotonic(),kind=kind,**data))+'\n')
        self.log.flush()

    def poll(self):
        now=time.monotonic()
        if now>=self.next_query:
            if self.link.write(b'?')!=1:raise RuntimeError('Incomplete query write')
            self.next_query=now+.02
        data=self.link.read(max(1,min(self.link.in_waiting,8192)))
        packets=self.framer.feed(data)
        for m in packets:
            self.event('rx',data=m)
            if m.get('type')=='telemetry':
                self.latest=m;self.received=time.monotonic();self.messages.append(m)
                if self.checked:self.guard(m)
        if self.checked and time.monotonic()-self.received>.5:raise RuntimeError('Telemetry stale >500 ms')
        return packets

    def guard(self,m):
        if m.get('fault') or m.get('config'):raise RuntimeError('Board fault/config lock')
        if m.get('temp_dC',1000)>500 or (m.get('enabled') and m.get('temp_valid')!=1):raise RuntimeError('Temperature invalid or >50 C')
        if not 7500<=m.get('vin_mV',0)<=39000:raise RuntimeError('VIN outside experiment limits')
        if m.get('vout_mV',99999)>5500:raise RuntimeError('VOUT exceeded 5.5 V experiment ceiling')
        if m.get('iout_mA',99999)>1200:raise RuntimeError('IOUT exceeded 1.2 A experiment ceiling')
        if m.get('cv_sat'):raise RuntimeError('Actuator saturated: do not tune a nonresponsive stage')
        if m.get('enabled') and m.get('adc_age',999)>=3:raise RuntimeError('ADC data stale')

    def request(self,op,*args,settings=None):
        self.identifier+=1
        wire=config_command(self.identifier,args[0],settings) if op=='CFG' else command(self.identifier,op,*args)
        self.event('command',id=self.identifier,op=op,args=args,settings=settings)
        if self.link.write(wire)!=len(wire):raise RuntimeError('Incomplete command write')
        end=time.monotonic()+3
        while time.monotonic()<end:
            for m in self.poll():
                if m.get('type')=='reply' and m.get('id')==self.identifier:
                    if not m.get('ok'):raise RuntimeError(f'{op} rejected: {m.get("error")}')
                    return m
        raise RuntimeError(f'{op} ACK timeout; command is NOT retried')

    def capture(self,seconds):
        start=len(self.messages);end=time.monotonic()+seconds
        while time.monotonic()<end:self.poll()
        return self.messages[start:]

    def stop(self):
        # The firmware's priority byte also cancels a queued ON and discharge.
        self.event('priority_off')
        if self.link.write(b'!\n')!=2:raise RuntimeError('Incomplete priority OFF')

    def close(self):
        try:
            if self.owned:self.stop()
        finally:self.link.close();self.log.close()


def metric(samples,target):
    on=[s for s in samples if s.get('enabled') and not s.get('ramp_active')]
    if len(on)<10:return dict(valid=False,reason='Too few running samples')
    tail=on[len(on)//2:];v=[s['vout_mV'] for s in tail]
    gaps=[(b['ms']-a['ms'])&0xffffffff for a,b in zip(on,on[1:])]
    active=sum(s.get('buck_pm',0)>0 for s in tail)/len(tail)
    result=dict(valid=active>.1 and max(s['iout_mA'] for s in tail)>=20,
                target_mV=target,tail_mean_mV=statistics.mean(v),tail_std_mV=statistics.pstdev(v),
                tail_p2p_mV=max(v)-min(v),mae_mV=statistics.mean(abs(x-target) for x in v),
                overshoot_mV=max(0,max(s['vout_mV'] for s in on)-target),
                active_fraction=active,cc_fraction=sum(s['state']==3 for s in on)/len(on),
                mean_iout_mA=statistics.mean(s['iout_mA'] for s in tail),samples=len(on),
                median_gap_ms=statistics.median(gaps),max_gap_ms=max(gaps))
    band=max(25,target*.01)
    settled=next((i for i in range(len(on)) if len(on)-i>=5 and all(abs(s['vout_mV']-target)<=band for s in on[i:])),None)
    result['settling_ms']=(on[settled]['ms']-on[0]['ms'])&0xffffffff if settled is not None else None
    if result['cc_fraction']>.05:result.update(valid=False,reason='Current limiting during CV test')
    elif not result['valid']:result['reason']='No sustained loaded regulation / actuator mostly held off'
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port',default='auto');parser.add_argument('--seconds',type=float,default=15)
    parser.add_argument('--run',action='store_true',help='Permit bounded SET/ON/OFF and optional CV CFG in RAM')
    parser.add_argument('--kp',type=float);parser.add_argument('--ki',type=float);parser.add_argument('--kd',type=float,default=0)
    parser.add_argument('--out',type=Path)
    args=parser.parse_args()
    if not .1<=args.seconds<=60:parser.error('seconds must be 0.1..60')
    if (args.kp is None)!=(args.ki is None):parser.error('Specify both kp and ki')
    if args.kp is not None and not args.run:parser.error('Changing gains requires --run')
    if args.kp is not None and not (0<=args.kp<=.015 and 0<=args.ki<=.2 and 0<=args.kd<=.001):
        parser.error('Experiment bounds: Kp<=0.015, Ki<=0.2, filtered measurement Kd<=0.001')
    stamp=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    folder=args.out or ROOT/'build/tuning'/stamp;folder.mkdir(parents=True,exist_ok=False)
    board=None;original=None;changed=False;result={}
    try:
        board=Board(discover() if args.port=='auto' else args.port,folder)
        original=board.request('GET')
        (folder/'original.json').write_text(json.dumps(original,indent=2),encoding='utf-8')
        board.capture(.3)
        if args.run:
            if original.get('fw') not in ('BBV6.4','BBV6.5'):raise RuntimeError('Expected supported BBV6.4/BBV6.5 firmware')
            board.guard(board.latest)
            if original['set_mV']!=5000 or original['set_mA']>1000:raise RuntimeError('Expected existing 5 V / <=1 A bench setting')
            board.owned=True;board.checked=True;board.stop();board.capture(.15)
            if args.kp is not None:
                config=dict(original['settings']);config.update(cv_kp=round(args.kp*1e6),cv_ki=round(args.ki*1e6),cv_kd=round(args.kd*1e6))
                changed=True;board.request('CFG',original['rev'],settings=config)
            board.capture(.3)
            current=board.request('GET')
            if current.get('enabled') or current.get('fault'):raise RuntimeError('Not OFF and fault-free before test')
            board.request('ON')
            result['phases']=[]
            for mv in [5000,5100,4900,5000]:
                board.target=mv;board.request('SET',mv,min(original['set_mA'],1000))
                samples=board.capture(args.seconds);stats=metric(samples,mv)
                result['phases'].append(stats);print(json.dumps(dict(target_mV=mv,**{k:v for k,v in stats.items() if k!='target_mV'})),flush=True)
            board.stop();board.capture(.15);board.request('SET',5000,original['set_mA'])
        else:
            samples=board.capture(args.seconds)
            result['capture']=dict(samples=len(samples),first=samples[0] if samples else None,last=samples[-1] if samples else None)
        result['ok']=True
    except BaseException as exc:
        result.update(ok=False,error=str(exc));print(json.dumps(dict(error=str(exc))),flush=True)
    finally:
        if board:
            # Keep experiment changes temporary; restore only the CV gains if
            # all other settings still match. A timeout/config change never
            # causes an unchecked retry or overwrites an external edit.
            try:
                if board.owned:board.stop();board.checked=False;board.capture(.15)
                if changed:
                    current=board.request('GET');candidate=dict(current['settings'])
                    for k in ['cv_kp','cv_ki','cv_kd']:candidate[k]=original['settings'][k]
                    if any(current['settings'][k]!=v for k,v in original['settings'].items() if not k.startswith('cv_')):
                        raise RuntimeError('External config edit: automatic restoration skipped')
                    board.request('CFG',current['rev'],settings=candidate);result['original_gains_restored']=True
            except BaseException as exc:result['cleanup_error']=str(exc)
            finally:
                try:board.close()
                except BaseException as exc:result['close_error']=str(exc)
        (folder/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps(dict(folder=str(folder.resolve()),ok=result.get('ok',False))),flush=True)
    return 0 if result.get('ok') else 1


if __name__=='__main__':raise SystemExit(main())
