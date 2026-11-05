"""One bounded real-hardware solder test. No automatic fault retries or Flash saves.

All temporary settings are restored after priority OFF. Native HEAT lease is 4s.
"""
import argparse, datetime, json, statistics, time
from pathlib import Path
from tune_cv import Board, ROOT, discover

class SolderBoard(Board):
    def guard(self,m):
        if m.get('fault') or m.get('config'):raise RuntimeError('Native fault/config lock')
        if not 7500 <= m.get('vin_mV',0) <= 30000:raise RuntimeError('VIN outside test range')
        if m.get('temp_dC',9999)>500:raise RuntimeError('FET above 50 C')
        if m.get('vout_mV',99999)>self.ceiling:raise RuntimeError('VOUT test ceiling')
        if m.get('iout_mA',99999)>self.current*1.4+300:raise RuntimeError('IOUT test ceiling')
        if m.get('enabled') and (m.get('adc_age',999)>=3 or not m.get('temp_valid')):raise RuntimeError('Invalid ADC/FET sensor')
        s=m.get('solder',{})
        if s.get('temp_dC',0)>2700 or s.get('reason'):raise RuntimeError('Tip temperature/error guard')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--run',action='store_true',required=True)
    ap.add_argument('--mv',type=int,default=9000);ap.add_argument('--ma',type=int,default=2000)
    ap.add_argument('--kp',type=int,default=30000)
    ap.add_argument('--kd',type=int,default=0)
    ap.add_argument('--alpha',type=int,default=250000)
    ap.add_argument('--clear-previous',action='store_true',help='One deliberate native CLEAR after recording the existing fault; never retry')
    a=ap.parse_args()
    if not (6000<=a.mv<=18000 and 500<=a.ma<=3000 and 15000<=a.kp<=100000 and 0<=a.kd<=500 and 250000<=a.alpha<=500000):ap.error('Outside commissioning limits')
    folder=ROOT/'build/tuning_solder'/datetime.datetime.now().strftime('run_%Y%m%d_%H%M%S_%f')
    folder.mkdir(parents=True);b=None;original=None;changed=False;result={}
    try:
        b=SolderBoard(discover(),folder);b.ceiling=a.mv*1.05+500;b.current=a.ma
        original=b.request('GET');(folder/'original.json').write_text(json.dumps(original,indent=2))
        if original['fw']!='BBV6.30-SOLDER-BENCH':raise RuntimeError('Unsupported lease firmware')
        b.capture(.2)
        if b.latest['vout_mV']>a.mv+20:raise RuntimeError('Wait for precharged output to fall below target +20 mV')
        if a.clear_previous and b.latest.get('fault'):
            if b.latest['enabled'] or b.latest['temp_dC']>500 or not 7500<=b.latest['vin_mV']<=30000:
                raise RuntimeError('Fault clear health preconditions failed')
            b.stop();b.request('CLEAR');b.capture(.2)
        if b.latest['enabled'] or b.latest['fault']:raise RuntimeError('Board must be OFF/fault-free')
        b.guard(b.latest);b.owned=True;b.stop();b.capture(.2)
        settings=dict(original['settings']);settings.update(solder_voltage=a.mv,solder_current=a.ma,
            cv_kp=a.kp,cv_kd=a.kd,adc_alpha=a.alpha,ovp_percent=min(5000,settings['ovp_percent']))
        changed=True;b.request('CFG',original['rev'],settings=settings);b.capture(.25)
        b.checked=True;start=len(b.messages);b.request('HEAT',4000,200);b.capture(4.5)
        samples=b.messages[start:];active=[m for m in samples if m['enabled'] and not m['ramp_active']]
        result.update(ok=True,voltage_mV=a.mv,current_mA=a.ma,kp=a.kp,kd=a.kd,alpha=a.alpha,samples=len(active),
            rises=max(m['solder']['rises'] for m in samples)-min(m['solder']['rises'] for m in samples))
        if active:
            v=[m['vout_mV'] for m in active]
            result.update(mean_mV=statistics.mean(v),max_mV=max(v),min_mV=min(v),
                mae_mV=statistics.mean(abs(x-a.mv) for x in v),p2p_mV=max(v)-min(v),
                mean_iout_mA=statistics.mean(m['iout_mA'] for m in active),
                max_tip_dC=max(m['solder']['temp_dC'] for m in active))
            result['cc_fraction']=sum(m['state']==3 for m in active)/len(active)
        result['valid']=len(active)>=20 and result['rises']>=5 and result.get('mean_iout_mA',0)>100
        result['valid_cv']=result['valid'] and result.get('cc_fraction',1)<.05
    except BaseException as e:result.update(ok=False,error=str(e))
    finally:
        if b:
            try:
                b.checked=False
                if b.owned:b.stop();b.capture(.3)
                if changed:
                    now=b.request('GET')
                    b.request('CFG',now['rev'],settings=original['settings'])
                    result['settings_restored']=True
                result['final']=b.latest
            except BaseException as e:result['cleanup_error']=str(e)
            finally:b.close()
        (folder/'results.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(dict(folder=str(folder),**{k:v for k,v in result.items() if k!='final'})),flush=True)
    return 0 if result.get('ok') and result.get('valid') else 1

if __name__=='__main__':raise SystemExit(main())
