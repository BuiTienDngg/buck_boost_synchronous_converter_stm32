"""One bounded short-load characterization, not a sustained PID autotune.

Default only reads. --pulse requests 1.5 V / 2 A for at most 250 ms plus
USB/ACK latency, while MCU peak protection is temporarily reduced to 2.8 A.
--seconds may extend to 2 s only while VOUT remains above 0.35 V after
200 ms: a true low-voltage short is never used for sustained regulation.
The MCU's short protection stays enabled. Host timing is not a hardware timer.
Always stops output; restores settings, presets and setpoint in RAM; no SAVE,
automatic fault clear or automatic retry of a mutating command.
"""
from pathlib import Path
import argparse,datetime,json,statistics
from tune_cv import Board,discover


class ShortProbe(Board):
    started_ms=None

    def guard(self,m):
        if m.get('fault') or m.get('config'):raise RuntimeError('Board latched fault/config lock')
        if m.get('temp_dC',1000)>500 or (m.get('enabled') and m.get('temp_valid')!=1):raise RuntimeError('Temperature invalid or >50 C')
        if not 7500<=m.get('vin_mV',0)<=39000:raise RuntimeError('VIN outside experiment bounds')
        if m.get('vout_mV',99999)>2000:raise RuntimeError('VOUT exceeded 2 V experiment ceiling')
        if m.get('iout_mA',99999)>2200:raise RuntimeError('Measured current exceeded 2.2 A host stop threshold')
        if m.get('enabled') and m.get('adc_age',999)>=3:raise RuntimeError('ADC stale')
        if m.get('cv_sat'):raise RuntimeError('Actuator saturated')
        if m.get('enabled') and 'ms' in m:
            if self.started_ms is None:self.started_ms=m['ms']
            if (m['ms']-self.started_ms)&0xffffffff>=200 and m['vout_mV']<350:
                raise RuntimeError('Sustained short below 0.35 V: stop characterization')


def summarize(samples):
    on=[s for s in samples if s.get('enabled')]
    if not on:return dict(samples=0,conclusion='No running samples; inspect fault snapshot')
    ratios=[s['vout_mV']/s['iout_mA'] for s in on if s['iout_mA']>=500]
    return dict(samples=len(on),current_min_mA=min(s['iout_mA'] for s in on),
                current_max_mA=max(s['iout_mA'] for s in on),current_mean_mA=statistics.mean(s['iout_mA'] for s in on),
                vout_max_mV=max(s['vout_mV'] for s in on),estimated_R_ohm=statistics.median(ratios) if ratios else None,
                cc_samples=sum(s['state']==3 for s in on),
                conclusion='Screening only: short duration/low-rate USB data is insufficient to select PID gains')


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--pulse',action='store_true')
    p.add_argument('--kp',type=float);p.add_argument('--ki',type=float)
    p.add_argument('--seconds',type=float,default=.25)
    args=p.parse_args();root=Path(__file__).resolve().parents[1]
    if not .1<=args.seconds<=2:p.error('seconds must be 0.1..2')
    if (args.kp is None)!=(args.ki is None):p.error('Specify both kp and ki')
    if args.kp is not None and (not args.pulse or not .005<=args.kp<=.03 or not .05<=args.ki<=.3):
        p.error('Gain changes require --pulse; Kp=0.005..0.03, Ki=0.05..0.3')
    folder=root/'build/tuning_cc'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ');folder.mkdir(parents=True)
    board=None;original=None;temporary=None;changed=False;result={}
    try:
        board=ShortProbe(discover(),folder);original=board.request('GET')
        (folder/'original.json').write_text(json.dumps(original,indent=2),encoding='utf-8')
        if not args.pulse:
            result.update(ok=True,read_only=original);return 0
        if original.get('fw') not in ('BBV6.5','BBV6.6'):raise RuntimeError('Expected BBV6.5/BBV6.6 firmware')
        if original['enabled'] or original['fault']:raise RuntimeError('Output must already be OFF and fault-free')
        board.capture(.2);board.guard(board.latest)
        if board.latest['vout_mV']>100 or board.latest['iout_mA']>20:raise RuntimeError('Output is not discharged before short test')
        board.owned=True;board.stop();board.capture(.15)
        temporary=dict(original['settings']);temporary.update(vout_max=2000,i_max=2000,i_peak=2800,power_max=5000,input_current_max=500)
        if args.kp is not None:temporary.update(cc_kp=round(args.kp*1e6),cc_ki=round(args.ki*1e6),cc_kd=0)
        changed=True;configured=board.request('CFG',original['rev'],settings=temporary)
        assert configured['settings']==temporary
        board.request('SET',1500,2000);board.capture(.35);state=board.request('GET')
        if state['enabled'] or state['fault']:raise RuntimeError('Preflight OFF/fault check failed')
        if state['settings']!=temporary:raise RuntimeError('Temporary protection limits did not match')
        board.guard(board.latest)
        if board.latest.get('ready_ms',0)<100:raise RuntimeError('Sensors not settled before pulse')
        board.checked=True;start=len(board.messages)
        board.request('ON');board.capture(args.seconds);board.stop();board.checked=False
        samples=board.messages[start:];result.update(ok=True,measurement=summarize(samples))
    except BaseException as exc:
        result.update(ok=False,error=str(exc));print(json.dumps(dict(error=str(exc))),flush=True)
        if board:result['measurement']=summarize(board.messages)
    finally:
        if board:
            try:
                if board.owned:board.stop();board.checked=False;board.capture(.15)
                current=board.request('GET');result['after_pulse']=current
                if changed:
                    if current['settings'] not in (temporary,original['settings']):raise RuntimeError('External settings change; restoration requires review')
                    if current['settings']!=original['settings']:board.request('CFG',current['rev'],settings=original['settings'])
                    board.capture(.3)
                    board.request('SET',original['set_mV'],original['set_mA'])
                    for slot,(mv,ma) in enumerate(original['presets']):board.request('PRESET',slot,mv,ma)
                    final=board.request('GET');result['final']=final
                    assert not final['enabled'] and final['settings']==original['settings'] and final['presets']==original['presets']
                    result['ram_configuration_restored']=True
            except BaseException as exc:result['cleanup_error']=str(exc)
            finally:
                try:board.close()
                except BaseException as exc:result['close_error']=str(exc)
        (folder/'results.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        print(json.dumps(dict(folder=str(folder),**result),indent=2),flush=True)
    return 0 if result.get('ok') else 1


if __name__=='__main__':raise SystemExit(main())
