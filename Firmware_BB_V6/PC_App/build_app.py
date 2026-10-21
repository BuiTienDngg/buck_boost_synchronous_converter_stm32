"""Build a standalone Windows EXE with the installed PyInstaller."""
from pathlib import Path
import subprocess,sys,os,shutil
ROOT=Path(__file__).resolve().parent
subprocess.run([sys.executable,str(ROOT.parent/'tools/export_settings_schema.py')],check=True)
env=dict(os.environ);env['PYINSTALLER_CONFIG_DIR']=str(ROOT/'build/cache')
subprocess.run([sys.executable,'-m','PyInstaller','--noconfirm','--clean','--onefile','--windowed','--log-level','WARN','--name','BB_V6_Studio_v6_28','--distpath',str(ROOT/'dist'),'--workpath',str(ROOT/'build/pyinstaller_v6_28'),'--specpath',str(ROOT/'build'),'--add-data',str(ROOT/'settings_schema.json')+os.pathsep+'.',str(ROOT/'app.py')],check=True,env=env)
print(ROOT/'dist/BB_V6_Studio_v6_28.exe')
shutil.copy2(ROOT/'dist/BB_V6_Studio_v6_28.exe',ROOT/'dist/BB_V6_Studio.exe')
