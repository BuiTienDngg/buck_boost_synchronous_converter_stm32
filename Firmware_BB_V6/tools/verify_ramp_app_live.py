"""Read-only GUI/CDC compatibility check; connects/GET only, never ON/CFG/SAVE."""
from pathlib import Path
import json,sys
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'PC_App'),str(ROOT/'tools')]
from app import Studio
from protocol import save_profile
from tune_cv import discover

result={};app=Studio();app.withdraw()
def close_error(*args):
    result.update(ok=False,error=str(args[1] if len(args)>1 else args[0]));app.close()
app.report_callback_exception=close_error
def connect():
    port=discover();result['port']=port;app.scan_ports()
    app.port_var.set(next(label for label,value in app.port_items.items() if value==port))
    app.toggle_connection()
def verify():
    assert app.connected and app.ready and not app.demo
    assert not app.latest.get('enabled')
    assert app.settings['cv_kp']==15000 and app.settings['cv_ki']==200000 and app.settings['cv_kd']==0
    assert app.settings['cc_kp']==10000 and app.settings['cc_ki']==1000000 and app.settings['cc_kd']==0
    profile=ROOT/'PC_App/Profiles/PID_tuned_ramp_10Vps.json'
    save_profile(profile,app.settings,app.latest['set_mV'],app.latest['set_mA'])
    result.update(ok=True,device=app.device_label.cget('text'),ramp_supported=app.ramp_supported,
                  enabled=app.latest['enabled'],profile=str(profile))
    app.close()
app.after(100,connect);app.after(1800,verify);app.after(5000,lambda:close_error('Timeout'))
app.mainloop()
(ROOT/'build/pc_ramp_live_readonly.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result))
if not result.get('ok'):raise SystemExit(1)
