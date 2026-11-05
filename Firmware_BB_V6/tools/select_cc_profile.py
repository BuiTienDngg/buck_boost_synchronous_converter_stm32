"""Apply the measured BBV6.5 CC gains and save only after verified OFF/config ACK.

Requires --apply; no automatic command retries or fault clears. Preserves
all existing limits, CV gains, calibration and presets. Leaves output OFF.
"""
from pathlib import Path
import argparse,datetime,json,sys
from tune_cv import Board,discover
from tune_cc import metric
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'PC_App'))
from protocol import save_profile


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--apply',action='store_true');args=p.parse_args()
    if not args.apply:p.error('Explicit --apply required')
    root=Path(__file__).resolve().parents[1];evidence=root/'build/tuning_cc/20261018T163742Z'
    measured=json.loads((evidence/'results.json').read_text());assert measured['ok'] and measured['restored']
    samples=[e['data'] for line in (evidence/'capture.jsonl').read_text().splitlines()
             if (e:=json.loads(line)).get('data',{}).get('type')=='telemetry']
    stats=metric(samples);assert stats['valid'] and stats['duration_ms']>=9900
    folder=root/'build/tuning_cc'/('selected_'+datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
    folder.mkdir(parents=True);board=None;result=dict(evidence=str(evidence),measurement=stats)
    try:
        board=Board(discover(),folder);original=board.request('GET');result['original']=original
        if original['fw']!='BBV6.5' or original['enabled'] or original['fault']:raise RuntimeError('Expected fault-free OFF BBV6.5 board')
        expected=json.loads((evidence/'original.json').read_text())
        if original['settings']!=expected['settings'] or original['presets']!=expected['presets']:raise RuntimeError('External configuration change; do not overwrite')
        chosen=dict(original['settings']);chosen.update(cc_kp=10000,cc_ki=1000000,cc_kd=0)
        board.owned=True;board.stop();board.capture(.15)
        ack=board.request('CFG',original['rev'],settings=chosen);assert ack['settings']==chosen and not ack['enabled']
        board.capture(.3);board.request('SET',1500,2000)
        result['save_ack']=board.request('SAVE');final=board.request('GET')
        assert final['settings']==chosen and final['presets']==original['presets'] and not final['enabled'] and not final['fault']
        assert final['set_mV']==1500 and final['set_mA']==2000
        profile=root/'PC_App/Profiles/CC_2A_tweezers_BBV6_5.json'
        save_profile(profile,chosen,1500,2000)
        result.update(ok=True,final=final,profile=str(profile),flash_saved=True)
    except BaseException as exc:result.update(ok=False,error=str(exc))
    finally:
        if board:board.close()
        (folder/'results.json').write_text(json.dumps(result,indent=2))
        print(json.dumps(dict(folder=str(folder),ok=result.get('ok'),error=result.get('error'),flash_saved=result.get('flash_saved'),measurement=stats),indent=2))
    return 0 if result.get('ok') else 1


if __name__=='__main__':raise SystemExit(main())
