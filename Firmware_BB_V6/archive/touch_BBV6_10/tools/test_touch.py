"""Execute actual touch SPI/gesture/calibration C on a Cortex-M3 bus model."""
from pathlib import Path
import sys,subprocess,shutil,struct,json
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools/_deps'),str(ROOT.parent/'tools/_deps')]
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
out=ROOT/'build/tests';out.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('arm-none-eabi-gcc');tools=Path(gcc).parent
inc=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F1xx/Include']
elf=out/'touch.elf';binary=elf.with_suffix('.bin')
subprocess.run([gcc,'-mcpu=cortex-m3','-mthumb','-Os','-std=c99','-ffunction-sections','-fdata-sections','-DUSE_HAL_DRIVER','-DSTM32F103xB',*['-I'+str(ROOT/p) for p in inc],str(ROOT/'Core/Src/psu_touch.c'),str(ROOT/'tests/touch_fixture.c'),'-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,-e,test_main','-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-Wl,--gc-sections','-o',str(elf)],check=True)
symbols={p[2]:int(p[0],16) for line in subprocess.check_output([str(tools/'arm-none-eabi-nm.exe'),str(elf)],text=True).splitlines() if len(p:=line.split())==3 and p[1].lower()!='u'}
emu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
for address,size in [(0,0x100000),(0x20000000,0x10000),(0x40000000,0x30000)]:emu.mem_map(address,size)
for sections,name,address in [(['.text','.rodata'],'touch.bin',0x10000),(['.data'],'touch.data',0x20000000)]:
    path=out/name;subprocess.run([str(tools/'arm-none-eabi-objcopy.exe'),'-O','binary',*['--only-section='+s for s in sections],str(elf),str(path)],check=True)
    if path.stat().st_size:emu.mem_write(address,path.read_bytes())
emu.reg_write(UC_ARM_REG_SP,0x2000fff0);emu.reg_write(UC_ARM_REG_LR,0x101)
emu.emu_start(symbols['test_main']|1,0x100,count=10_000_000);assert emu.reg_read(UC_ARM_REG_PC)==0x100
u32=lambda name:struct.unpack('<I',emu.mem_read(symbols[name],4))[0]
total,failed=u32('test_count'),u32('test_failed');assert 5<total<256
lines=struct.unpack('<'+'I'*total,emu.mem_read(symbols['test_lines'],4*total));failures=[x for x in lines if x]
print(f'Touch SPI/gesture/calibration: {total-failed}/{total}, failed lines: {failures}')
(out/'touch_results.json').write_text(json.dumps(dict(passed=total-failed,total=total,failed_lines=failures),indent=2))
if failed:raise SystemExit(1)
