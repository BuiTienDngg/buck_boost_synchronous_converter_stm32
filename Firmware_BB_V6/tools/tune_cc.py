"""Bounded CC bench experiment on the existing firmware, with full RAM restore.

Default only reads. --run permits 1.5 V / 2 A with temporary 2.8 A raw peak,
2.2 A continuous and 5 W protection. Host aborts above 2.5 A filtered.
Never clears faults, retries a command, or saves experimental gains to Flash.
"""
from pathlib import Path
import argparse,datetime,json,statistics
from tune_cv import Board,discover


class CurrentBoard(Board):
    started_ms=None

    def guard(self,m):
        if m.get('fault') or m.get('config'):raise RuntimeError('Board fault/config lock')
        if m.get('temp_dC',1000)>500 or (m.get('enabled') and m.get('temp_valid')!=1):raise RuntimeError('Temperature invalid or >50 C')
        if not 7500<=m.get('vin_mV',0)<=39000:raise RuntimeError('VIN outside bounds')
        if m.get('vout_mV',99999)>2000:raise RuntimeError('VOUT >2 V')
        if m.get('iout_mA',99999)>2500:raise RuntimeError('Filtered current >2.5 A')
        if m.get('cv_sat'):raise RuntimeError('Conversion command saturated')
        if m.get('enabled'):
            if m.get('adc_age',999)>=3:raise RuntimeError('ADC stale')
            if self.started_ms is None:self.started_ms=m['ms']
            if ((m['ms']-self.started_ms)&0xffffffff)>=200 and m['vout_mV']<350:
                raise RuntimeError('VOUT <0.35 V: no sustained short test')


def metric(samples,target=2000):
    on=[s for s in samples if s.get('enabled')]
    if not on:return dict(valid=False,reason='No running samples')
    first=on[0]['ms'];last=on[-1]['ms']
    steady=[s for s in on if ((s['ms']-first)&0xffffffff)>=500]
    tail=[s for s in steady if ((last-s['ms'])&0xffffffff)<=1000]
    result=dict(valid=False,samples=len(on),duration_ms=(last-first)&0xffffffff,
                max_filtered_mA=max(s['iout_mA'] for s in on),tail_samples=len(tail))
    if len(tail)<10:return dict(**result,reason='Insufficient settled samples')
    current=[s['iout_mA'] for s in tail];gaps=[(b['ms']-a['ms'])&0xffffffff for a,b in zip(on,on[1:])]
    result.update(mean_mA=statistics.mean(current),std_mA=statistics.pstdev(current),
                  min_mA=min(current),max_mA=max(current),p2p_mA=max(current)-min(current),
                  mae_mA=statistics.mean(abs(i-target) for i in current),
                  cc_fraction=sum(s['state']==3 for s in tail)/len(tail),
                  median_gap_ms=statistics.median(gaps),vout_mean_mV=statistics.mean(s['vout_mV'] for s in tail),
                  temp_max_dC=max(s['temp_dC'] for s in on))
    current_steady=[s['iout_mA'] for s in steady]
    result['steady']=dict(samples=len(steady),mean_mA=statistics.mean(current_steady),
        min_mA=min(current_steady),max_mA=max(current_steady),std_mA=statistics.pstdev(current_steady),
        mae_mA=statistics.mean(abs(i-target) for i in current_steady),
        cc_fraction=sum(s['state']==3 for s in steady)/len(steady))
    result['valid']=result['duration_ms']>=1500 and result['steady']['mae_mA']<=30 and max(current_steady)-min(current_steady)<=120 and result['steady']['cc_fraction']>=.95
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--run',action='store_true')
    p.add_argument('--kp',type=float,default=.01);p.add_argument('--ki',type=float,default=1.0);p.add_argument('--kd',type=float,default=0)
    p.add_argument('--seconds',type=float,default=2)
    args=p.parse_args()
    if not (.005<=args.kp<=.08 and .05<=args.ki<=3 and 0<=args.kd<=.001 and .1<=args.seconds<=20):p.error('Outside bounded experiment gains/duration')
    root=Path(__file__).resolve().parents[1];folder=root/'build/tuning_cc'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ')
    folder.mkdir(parents=True);board=None;original=None;temporary=None;changed=False;result={}
    try:
        board=CurrentBoard(discover(),folder);original=board.request('GET')
        (folder/'original.json').write_text(json.dumps(original,indent=2))
        if not args.run:
            result.update(ok=True,state=original);return 0
        if original['fw'] not in ('BBV6.5','BBV6.6'):raise RuntimeError('Unsupported firmware')
        if original['enabled'] or original['fault']:raise RuntimeError('Board must be OFF and fault-free')
        board.capture(.2);board.guard(board.latest)
        if board.latest['vout_mV']>100 or board.latest['iout_mA']>20:raise RuntimeError('Output not discharged')
        board.owned=True;board.stop();board.capture(.15)
        temporary=dict(original['settings']);temporary.update(cc_kp=round(args.kp*1e6),cc_ki=round(args.ki*1e6),cc_kd=round(args.kd*1e6),
            vout_max=2000,i_max=2200,i_peak=2800,power_max=5000,input_current_max=500)
        changed=True;state=board.request('CFG',original['rev'],settings=temporary)
        assert state['settings']==temporary
        board.request('SET',1500,2000);board.capture(.35);state=board.request('GET')
        if state['settings']!=temporary or state['enabled'] or state['fault']:raise RuntimeError('Preflight state mismatch')
        board.guard(board.latest)
        if board.latest.get('ready_ms',0)<100:raise RuntimeError('Sensors not settled')
        board.checked=True;board.request('ON');board.capture(args.seconds);board.stop();board.checked=False
        result.update(ok=True,measurement=metric(board.messages))
    except BaseException as exc:
        result.update(ok=False,error=str(exc));print(json.dumps(dict(error=str(exc))),flush=True)
        if board:result['measurement']=metric(board.messages)
    finally:
        if board:
            try:
                if board.owned:board.stop();board.checked=False;board.capture(.15)
                current=board.request('GET');result['after_test']=current
                if changed:
                    if current['settings'] not in (temporary,original['settings']):raise RuntimeError('External settings edit; do not overwrite')
                    if current['settings']!=original['settings']:board.request('CFG',current['rev'],settings=original['settings'])
                    board.capture(.3);board.request('SET',original['set_mV'],original['set_mA'])
                    for slot,(mv,ma) in enumerate(original['presets']):board.request('PRESET',slot,mv,ma)
                    final=board.request('GET');result['final']=final
                    assert not final['enabled'] and final['settings']==original['settings'] and final['presets']==original['presets']
                    result['restored']=True
            except BaseException as exc:result['cleanup_error']=str(exc)
            finally:board.close()
        (folder/'results.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(dict(folder=str(folder),ok=result.get('ok'),measurement=result.get('measurement'),restored=result.get('restored'),cleanup_error=result.get('cleanup_error')),indent=2),flush=True)
    return 0 if result.get('ok') else 1


if __name__=='__main__':raise SystemExit(main())
