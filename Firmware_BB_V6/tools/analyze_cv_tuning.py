"""Plot real USB captures and summarize measured CV step trials."""
from pathlib import Path
import argparse,json,statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt


def main():
    parser=argparse.ArgumentParser();parser.add_argument('folders',nargs='+',type=Path)
    parser.add_argument('--out',required=True,type=Path);args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    fig,axes=plt.subplots(3,1,figsize=(11,9),sharex=True,layout='constrained')
    report=[]
    for number,folder in enumerate(args.folders,1):
        records=[json.loads(line) for line in (folder/'capture.jsonl').read_text(encoding='utf-8').splitlines()]
        samples=[r['data'] for r in records if r.get('kind')=='rx' and r['data'].get('type')=='telemetry']
        original=json.loads((folder/'original.json').read_text());config=original['settings'];effective=dict(config)
        for r in records:
            if r.get('kind')=='command' and r.get('op')=='CFG':effective.update(r['settings']);break
        gains={k:effective[k]/1e6 for k in ['cv_kp','cv_ki','cv_kd']}
        label=f"#{number} {original.get('fw','?')}: Kp={gains['cv_kp']:g}, Ki={gains['cv_ki']:g}, Kd={gains['cv_kd']:g}"
        on=[s for s in samples if s.get('enabled')]
        if not on:continue
        start=on[0]['ms'];t=[((s['ms']-start)&0xffffffff)/1000 for s in on]
        axes[0].plot(t,[s['vout_mV']/1000 for s in on],label=label,linewidth=1)
        axes[1].plot(t,[s['vout_mV']-s['set_mV'] for s in on],label=label,linewidth=1)
        axes[2].plot(t,[s['iout_mA'] for s in on],label=label,linewidth=1)
        summary=json.loads((folder/'results.json').read_text());phases=summary.get('phases',[])
        report.append(dict(folder=str(folder),firmware=original.get('fw'),gains=gains,ok=summary.get('ok'),
                           original_gains_restored=summary.get('original_gains_restored'),phases=phases,
                           mean_step_mae_mV=statistics.mean(p['mae_mV'] for p in phases[1:]) if len(phases)>1 else None,
                           mean_step_std_mV=statistics.mean(p['tail_std_mV'] for p in phases[1:]) if len(phases)>1 else None,
                           max_overshoot_mV=max((p['overshoot_mV'] for p in phases),default=None),
                           all_phases_valid=bool(phases) and all(p.get('valid') for p in phases)))
    axes[0].set_ylabel('VOUT (V)');axes[1].set_ylabel('VOUT - target (mV)')
    axes[2].set_ylabel('IOUT (mA)');axes[2].set_xlabel('Time from ON (s)')
    axes[1].axhline(0,color='black',linewidth=.7);axes[1].axhspan(-50,50,color='green',alpha=.06)
    axes[1].set_ylim(-100,250);axes[1].set_title('Tracking error zoom; startup error is outside this panel',fontsize=9)
    for ax in axes:ax.grid(alpha=.25)
    axes[0].legend(fontsize=8);fig.suptitle('Measured USB CV trials near 5 V — low-rate telemetry, not switching ripple')
    fig.savefig(args.out/'cv_comparison.png',dpi=160);plt.close(fig)
    (args.out/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps([{k:v for k,v in trial.items() if k!='phases'} for trial in report],indent=2))


if __name__=='__main__':main()
