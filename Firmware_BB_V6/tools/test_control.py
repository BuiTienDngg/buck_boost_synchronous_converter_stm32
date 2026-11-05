"""Execute compiled Cortex-M3 C tests using Unicorn; no mocked Python controller."""
from pathlib import Path
import sys, subprocess, shutil, re, struct
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/_deps'),str(ROOT.parent/'tools/_deps')]
try:
    from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS
    from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC
except ImportError:
    raise SystemExit('Install test dependency: python -m pip install --target tools/_deps unicorn==2.1.4')
BUILD=ROOT/'build/tests';BUILD.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('arm-none-eabi-gcc');nm=str(Path(gcc).with_name('arm-none-eabi-nm.exe'));objcopy=str(Path(gcc).with_name('arm-none-eabi-objcopy.exe'))
common=['-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-Os','-g','-std=c99','-ffunction-sections','-fdata-sections','-I'+str(ROOT/'Core/Inc')]
config=(ROOT/'Core/Inc/psu_config.h').read_text()
def fixture(name,replacements):
    text=config
    for key,value in replacements.items():text=re.sub(r'(#define '+key+r')\s+[^\n]+',lambda m:m[1]+' '+value,text)
    path=BUILD/(name+'.h');path.write_text(text);return ['-include',str(path)]
fixtures=[('temporary_board',[]),
          ('verified_board',fixture('verified',{'PSU_BENCH_MODE':'0','PSU_CURRENT_PATH_VERIFIED':'1','PSU_POWER_STAGE_VERIFIED':'1'})),
          ('unverified_production',['-DTEST_LOCKED_CONFIG',*fixture('unverified',{'PSU_BENCH_MODE':'0'})])]
all_results=[]
for name,args in fixtures:
    elf=BUILD/(name+'.elf');binary=elf.with_suffix('.bin')
    subprocess.run([gcc,*common,*args,str(ROOT/'Core/Src/psu_control.c'),str(ROOT/'Core/Src/psu_settings.c'),str(ROOT/'tests/control_tests.c'),'-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,-e,test_main','-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-Wl,--gc-sections','-lm','-o',str(elf)],check=True)
    symbols={}
    for line in subprocess.check_output([nm,str(elf)],text=True).splitlines():
        parts=line.split()
        if len(parts)==3:
            try:symbols[parts[2]]=int(parts[0],16)
            except ValueError:pass
    # Tests have only zero-initialized globals; exclude the distant RAM segment.
    subprocess.run([objcopy,'-O','binary','--only-section=.text','--only-section=.rodata',str(elf),str(binary)],check=True)
    emu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    emu.mem_map(0,0x100000);emu.mem_map(0x20000000,0x10000)
    emu.mem_write(0x10000,binary.read_bytes());emu.reg_write(UC_ARM_REG_SP,0x2000FFF0);emu.reg_write(UC_ARM_REG_LR,0x101)
    emu.emu_start(symbols['test_main']|1,0x100,count=250_000_000)
    assert emu.reg_read(UC_ARM_REG_PC)==0x100, 'Test did not return before instruction limit'
    def u32(symbol):return struct.unpack('<I',emu.mem_read(symbols[symbol],4))[0]
    count,failed=u32('test_count'),u32('test_failed')
    lines=struct.unpack('<'+'I'*count,emu.mem_read(symbols['test_lines'],4*count))
    assert count>5 and count<256
    failures=[n for n in lines if n]
    print(f'{name}: {count-failed}/{count} assertions passed; failed source lines: {failures}')
    all_results.append({'fixture':name,'passed':count-failed,'total':count,'failed_lines':failures})
    if failed:raise SystemExit(1)
import json
(BUILD/'results.json').write_text(json.dumps(all_results,indent=2))
