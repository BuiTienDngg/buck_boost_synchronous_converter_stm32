"""Capture only the app's DEMO window; never connects to a physical board."""
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'PC_App'))
from app import Studio
from fault_log import FaultJournal
from PIL import ImageGrab
OUT=ROOT/'build/preview';OUT.mkdir(parents=True,exist_ok=True)
app=Studio(True);app.geometry('1160x810+30+30');app.attributes('-topmost',True)
app.error_journal=FaultJournal(OUT/'demo_fault_log')
def shot(name):
    app.update_idletasks()
    x,y=app.winfo_rootx(),app.winfo_rooty();ImageGrab.grab(bbox=(x,y,x+app.winfo_width(),y+app.winfo_height())).save(OUT/name)
def overview():
    assert app.ready and app.demo
    app.toggle_output();app.after(180,capture_ramp)

def capture_ramp():
    shot('pc_ramp.png');app.after(1620,capture_dashboard)
def capture_dashboard():
    shot('pc_dashboard.png');app.tabs.select(app.config_page);app.after(300,capture_settings)
def capture_settings():
    shot('pc_settings.png')
    sample=dict(app.latest);sample.update(fault=2,state=4,enabled=0,ms=sample['ms']+1,trip=dict(cause=2,vo_adc=334,ovp_adc=310))
    app.telemetry(sample);app.tabs.select(app.diagnostics);app.after(80,capture_errors)

def capture_errors():
    for row,event in app.fault_rows.items():
        if event['kind']=='protection':app.fault_tree.selection_set(row);app.show_error_detail();break
    shot('pc_fault_log.png');app.close();print('PC app DEMO previews saved.')
app.after(1100,overview);app.after(9000,app.close);app.mainloop()
