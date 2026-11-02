"""ARM execution tests of paired D / (1-D) PWM. No 500Hz refresh.
Needs arm-none-eabi-gcc and pip install unicorn.
Optional --baseline ZIP tests the backed-up original PowerStage_Init too.
"""
from pathlib import Path
import argparse, re, shutil, struct, subprocess, tempfile, zipfile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_MEM_WRITE, UC_HOOK_INTR, UcError
from unicorn.arm_const import UC_ARM_REG_SP, UC_ARM_REG_LR, UC_ARM_REG_PC, UC_CPU_ARM_CORTEX_M3

ROOT = Path(__file__).resolve().parents[1]

def run(baseline=None):
    prefix = shutil.which('arm-none-eabi-gcc')
    if not prefix:
        raise RuntimeError('arm-none-eabi-gcc not found')
    compiler = Path(prefix)
    tool = lambda name: compiler.with_name('arm-none-eabi-' + name + '.exe')
    with tempfile.TemporaryDirectory(prefix='psu_refresh_') as folder:
        tmp = Path(folder)
        if baseline:
            with zipfile.ZipFile(baseline) as z:
                (tmp/'baseline_power_stage.c').write_bytes(z.read('Core/Src/power_stage.c'))
        includes = ['Core/Inc', 'Drivers/STM32F1xx_HAL_Driver/Inc',
                    'Drivers/CMSIS/Device/ST/STM32F1xx/Include', 'Drivers/CMSIS/Include']
        flags = ['-mcpu=cortex-m3', '-mthumb', '-O0', '-ffunction-sections', '-fdata-sections',
                 '-fno-builtin', '-DUSE_HAL_DRIVER', '-DSTM32F103xB', '-I'+str(tmp)]
        flags += ['-I'+str(ROOT/p) for p in includes]
        if baseline:
            flags += ['-DBASELINE']
        sources = ['tests/refresh_fixture.c',
                   'Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_tim.c',
                   'Drivers/STM32F1xx_HAL_Driver/Src/stm32f1xx_hal_dma.c']
        elf, binary = tmp/'test.elf', tmp/'test.bin'
        subprocess.run([str(compiler), *flags, *[str(ROOT/p) for p in sources],
                        '-nostdlib', '-Wl,--gc-sections', '-Wl,-Ttext=0x10000',
                        '-Wl,-Tdata=0x20000000', '-Wl,-e,main', '-lgcc', '-o', str(elf)], check=True)
        nm = subprocess.check_output([str(tool('nm')), '-n', str(elf)], text=True)
        syms = {m[2]:int(m[0],16) for m in re.findall(r'^([0-9a-fA-F]+)\s+(\w)\s+(\S+)$', nm, re.M)}
        # ELF PT_LOAD segments retain separate RAM/flash addresses without a huge raw binary.
        data = elf.read_bytes()
        phoff = struct.unpack_from('<I',data,28)[0]
        phentsize, phnum = struct.unpack_from('<HH',data,42)
        uc = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        uc.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M3)
        uc.mem_map(0x10000,0x100000)
        uc.mem_map(0x20000000,0x100000)
        uc.mem_map(0x40000000,0x30000)
        uc.mem_map(0xE000E000,0x2000)
        for i in range(phnum):
            typ,off,vaddr,paddr,filesz,memsz,flg,align = struct.unpack_from('<8I',data,phoff+i*phentsize)
            if typ==1 and filesz:
                uc.mem_write(vaddr,data[off:off+filesz])
        def write_hook(u,access,address,size,value,user):
            # Implement F1 GPIO atomic SET/RESET and DMA interrupt-flag clears.
            for base in (0x40010800,0x40011000):
                if address in (base+0x10,base+0x14):
                    odr=struct.unpack('<I',u.mem_read(base+0xC,4))[0]
                    if address==base+0x10:
                        odr=(odr|(value&0xFFFF))&~(value>>16)
                    else:
                        odr &= ~value
                    u.mem_write(base+0xC,struct.pack('<I',odr&0xFFFF))
            if address==0x40020004:
                isr=struct.unpack('<I',u.mem_read(0x40020000,4))[0]
                u.mem_write(0x40020000,struct.pack('<I',isr&~value))
        uc.hook_add(UC_HOOK_MEM_WRITE,write_hook)
        uc.hook_add(UC_HOOK_INTR,lambda u,n,user:u.emu_stop())
        uc.reg_write(UC_ARM_REG_SP,0x200FF000)
        uc.reg_write(UC_ARM_REG_LR,0x10001)
        try:
            uc.emu_start(syms['main']|1,0,count=200000000)
        except UcError:
            print('PC',hex(uc.reg_read(UC_ARM_REG_PC)),'LR',hex(uc.reg_read(UC_ARM_REG_LR)))
            print('SP',hex(uc.reg_read(UC_ARM_REG_SP)))
            print('PWM',hex(syms['test_pwm']),bytes(uc.mem_read(syms['test_pwm'],24)).hex())
            lr=uc.reg_read(UC_ARM_REG_LR)
            print(subprocess.check_output([str(tool('objdump')),'-d',
                '--start-address='+hex((lr-40)&~1),'--stop-address='+hex((lr+30)&~1),str(elf)],text=True))
            raise
        get = lambda name:struct.unpack('<I',uc.mem_read(syms[name],4))[0]
        if get('test_completed')!=0x50535554:
            raise RuntimeError('Emulation stopped before the test suite completed')
        count,failed=get('result_count'),get('failures')
        lines=struct.unpack('<'+'I'*failed,uc.mem_read(syms['failed_lines'],4*failed)) if failed else ()
        print(('Original' if baseline else 'Updated')+f': {count} assertions, {failed} failures; lines={lines}')
        if not baseline:
            worst=struct.unpack('<3I',uc.mem_read(syms['ripple_worst_vticks'],12))
            print('Worst per-frame ideal inductor-current drift at 24->12, 12->24, 24->22 V (A):',
                  [round(x/(72000000*98e-6),6) for x in worst])
        if not count or failed:
            raise SystemExit(1)
        return count

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--baseline',type=Path)
    args=parser.parse_args()
    if args.baseline:run(args.baseline)
    run()

