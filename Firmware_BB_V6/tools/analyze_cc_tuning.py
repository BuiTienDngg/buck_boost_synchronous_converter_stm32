"""Plot measured USB current traces; never infer switching peaks from telemetry."""
from pathlib import Path
import argparse,json,statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from tune_cc import metric


def analyze(folder):
    events=[json.loads(line) for line in (folder/'capture.jsonl').read_text().splitlines()]
    samples=[e['data'] for e in events if e.get('data',{}).get('type')=='telemetry' and e['data'].get('enabled')]
    configs=[e['settings'] for e in events if e.get('op')=='CFG' and e.get('settings')]
    original=json.loads((folder/'original.json').read_text())
    gains=configs[0] if configs else original['settings']
    metadata=dict(folder=folder.name,firmware=original['fw'],
                  kp=gains['cc_kp']/1e6,ki=gains['cc_ki']/1e6,kd=gains['cc_kd']/1e6)
    if not samples:return [],dict(**metadata,samples=0)
    target=samples[-1]['set_mA'];tail=samples[len(samples)//2:]
    gaps=[(b['ms']-a['ms'])&0xffffffff for a,b in zip(samples,samples[1:])]
    stats=dict(**metadata,samples=len(samples),target_mA=target,
               max_filtered_mA=max(s['iout_mA'] for s in samples),
               duration_observed_ms=(samples[-1]['ms']-samples[0]['ms'])&0xffffffff,
               cc_samples=sum(s['state']==3 for s in samples),
               tail_mean_mA=statistics.mean(s['iout_mA'] for s in tail),
               tail_std_mA=statistics.pstdev(s['iout_mA'] for s in tail),
               median_gap_ms=statistics.median(gaps) if gaps else None,
               tuning_valid=False,reason='Screening capture; sustained CC/step validation still required')
    regulation=metric(samples,target)
    stats.update(regulation=regulation,tuning_valid=regulation['valid'])
    if regulation['valid']:stats['reason']='Stable sampled CC at this load/setpoint; not full-range or raw peak validation'
    return samples,stats


def main():
    p=argparse.ArgumentParser();p.add_argument('folders',nargs='+',type=Path);p.add_argument('--out',required=True,type=Path)
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    fig,axes=plt.subplots(3,1,figsize=(10,8),sharex=True,layout='constrained');results=[]
    for folder in args.folders:
        samples,stats=analyze(folder);results.append(stats)
        if not samples:continue
        t=[(s['ms']-samples[0]['ms'])&0xffffffff for s in samples]
        label=f"{stats['firmware']} Kp={stats['kp']:.3f} Ki={stats['ki']:.3f}"
        axes[0].plot(t,[s['iout_mA']/1000 for s in samples],'.-',label=label)
        axes[1].plot(t,[s['vout_mV']/1000 for s in samples],'.-')
        axes[2].plot(t,[s['ratio_pm']/1000 for s in samples],'.-')
    axes[0].axhline(2,color='black',ls='--',label='2 A target')
    for ax,title in zip(axes,['Filtered IOUT (A)','VOUT (V)','Conversion command']):
        ax.set_ylabel(title);ax.grid(alpha=.3)
    axes[0].legend();axes[-1].set_xlabel('Time since first running USB sample (ms)')
    fig.suptitle('CC bench measurements on tweezers\nFiltered USB samples; not instantaneous current peaks or full-range validation')
    fig.savefig(args.out/'cc_screening.png',dpi=150);plt.close(fig)
    (args.out/'results.json').write_text(json.dumps(results,indent=2))
    print(json.dumps(results,indent=2))


if __name__=='__main__':main()
