"""Averaged LC screening only; never connects to hardware.

User-supplied L=998uH/Cout=1000uF. Unknown ESR, winding resistance and
sensor placement are varied; switching spikes/deadtime are not represented.
"""
import json,itertools
import numpy as np
from scipy.linalg import expm
from pathlib import Path

def plant(on,rl,load,target):
    g=1/load if on else 1/40000
    # State: inductor current, output capacitor voltage, input duty ratio.
    a=np.array([[-rl/.000998,-.9/.000998,.9*20.4/.000998],
                [.9/.001,-g/.001,0],[0,0,0]])
    return expm(a*.001)

def simulate(kp,ki,kd,alpha,dalpha,rl=.15,load=3.5,sensor='inductor'):
    matrices=[plant(on,rl,load,9) for on in [False,True]]
    x=np.array([0.,9.,9/20.4]);v=previous=9.;i=dv=di=cv_i=cc_i=0.
    volts=[];peak_i=0.
    for tick in range(500):
        on=tick%50>=15
        raw_i=max(0,.9*x[0] if sensor=='inductor' else x[1]/load if on else 0)
        v+=alpha*(x[1]-v);i+=alpha*(raw_i-i)
        dv+=dalpha*((v-previous)*1000-dv);previous=v
        ev=9-v;ei=3-i;ff=9/20.4
        cv=ff+kp*ev+cv_i-kd*dv;cc=ff+.01*ei+cc_i
        binding=cc<cv
        if ei>.1 and cc<cv:cc=cv;cc_i=cc-ff-.01*ei;binding=False
        selected=max(0,min(cv,cc,1.))
        cv_i=max(-11.25,min(11.25,cv_i+ki*.001*ev+.05*(selected-cv)))
        cc_i=selected-ff if not binding else max(-11.25,min(11.25,cc_i+.001*ei+.05*(selected-cc)))
        if selected*.9<.025:
            # Both high sides off: inductor recirculates, capacitor supplies load.
            x[0]*=np.exp(-rl*.001/.000998)
            x[1]*=np.exp(-(.001/(.001*load)) if on else -(.001/(.001*40000)))
        else:
            x[2]=selected;x=matrices[on]@x
        peak_i=max(peak_i,abs(x[0]));volts.append(x[1])
        if abs(x[1])>15 or peak_i>10:return {'valid':False,'max_v':max(volts),'peak_il':peak_i}
    return dict(valid=True,max_v=max(volts),min_v=min(volts),mae=float(np.mean(np.abs(np.array(volts)-9))),peak_il=peak_i)

def main():
    rows=[]
    for kp,ki,kd,alpha,dalpha in itertools.product([.015,.03,.06,.1],[.05,.2],[0,.00005,.0001,.0002,.0005],[.25,.5],[.1,.5,1.]):
        runs=[simulate(kp,ki,kd,alpha,dalpha,rl,load,sensor) for rl,load,sensor in itertools.product([.05,.2],[3.,4.],['inductor','load'])]
        row=dict(kp=kp,ki=ki,kd=kd,alpha=alpha,derivative_alpha=dalpha,
            max_v=max(r['max_v'] for r in runs),peak_il=max(r['peak_il'] for r in runs),
            valid=all(r['valid'] for r in runs))
        row['score']=row['max_v']+(.1*max(r.get('mae',20) for r in runs)) if row['valid'] else 100
        rows.append(row)
    rows.sort(key=lambda r:r['score']);p=Path('build/tuning_solder/lc_screening.json')
    p.write_text(json.dumps(dict(assumptions=__doc__,baseline=simulate(.03,.2,0,.25,.1),best=rows[:20]),indent=2))
    print(json.dumps(dict(baseline=simulate(.03,.2,0,.25,.1),best=rows[:3])))

if __name__=='__main__':main()
