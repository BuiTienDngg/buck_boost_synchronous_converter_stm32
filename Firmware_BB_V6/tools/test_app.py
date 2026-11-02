"""Exercise actual BTN1/debounce/control/DMA code on an emulated Cortex-M3."""
from pathlib import Path
import sys,subprocess,shutil,struct,json
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools/_deps'),str(ROOT.parent/'tools/_deps')]
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
BUILD=ROOT/'build/tests';BUILD.mkdir(parents=True,exist_ok=True)
main=(ROOT/'Core/Src/main.c').read_text()
assert 'hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;' in main
assert 'hadc1.Init.ContinuousConvMode = ENABLE;' in main
assert 'sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;' in main
gcc=shutil.which('arm-none-eabi-gcc');tools=Path(gcc).parent
includes=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F1xx/Include','USB_DEVICE/App','USB_DEVICE/Target','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc']
flags=['-mcpu=cortex-m3','-mthumb','-Os','-g','-std=c99','-ffunction-sections','-fdata-sections','-DUSE_HAL_DRIVER','-DSTM32F103xB']+['-I'+str(ROOT/p) for p in includes]
diagnostic='--diagnostic' in sys.argv
if diagnostic:flags+=['-DPSU_LOW_SIDE_TEST=1']
no_discharge='--no-discharge' in sys.argv
if no_discharge:flags+=['-DPSU_DISCHARGE_ENABLED=0']
name='application_diagnostic' if diagnostic else 'application_no_discharge' if no_discharge else 'application'
elf=BUILD/(name+'.elf');binary=elf.with_suffix('.bin')
subprocess.run([gcc,*flags,*[str(ROOT/'Core/Src'/f'{name}.c') for name in ['psu_app','psu_control','psu_settings','psu_remote','psu_modes']],str(ROOT/'tests/app_fixture.c'),'-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,-e,test_main','-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-Wl,--gc-sections','-lm','-o',str(elf)],check=True)
symbols={p[2]:int(p[0],16) for l in subprocess.check_output([str(tools/'arm-none-eabi-nm.exe'),str(elf)],text=True).splitlines() if len(p:=l.split())==3 and p[1].lower()!='u'}
# Read both flash and initialized RAM sections without a sparse 512 MB image.
subprocess.run([str(tools/'arm-none-eabi-objcopy.exe'),'-O','binary','--only-section=.text','--only-section=.rodata',str(elf),str(binary)],check=True)
ram=BUILD/(name+'.data');subprocess.run([str(tools/'arm-none-eabi-objcopy.exe'),'-O','binary','--only-section=.data',str(elf),str(ram)],check=True)
emu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
for address,size in [(0,0x100000),(0x20000000,0x10000),(0x40000000,0x30000),(0xE0000000,0x100000)]:emu.mem_map(address,size)
emu.mem_write(0x10000,binary.read_bytes())
if ram.stat().st_size:emu.mem_write(0x20000000,ram.read_bytes())
# Check the actual register-write ordering, not only the final CCR values.
# This models the UG preload transfer, not the MOSFET/driver analog timing.
timer=0x40012C00;gpioa=0x40010800
gate={'sd':False,'loaded':None,'checks':0,'shots':0,'last_shot':None}
def register(address):return struct.unpack('<I',emu.mem_read(address,4))[0]
def trace_gate(uc,access,address,size,value,user):
    mode=register(timer+0x18)&0x7070
    live=gate['sd'] and (register(timer+0x44)&0x8000)
    if address in (gpioa+0x10,gpioa+0x14):
        odr=register(gpioa+0x0c)
        if address==gpioa+0x10:
            odr=(odr | (value&0xffff)) & ~(value>>16)
            if value&(1<<10):
                assert mode in (0x4040,0x6060,0x7040),'SD enabled before timer mode is safe'
                gate['sd']=True;gate['checks']+=1
            if value&(1<<26):gate['sd']=False
        else:
            odr &= ~value
            if value&(1<<10):gate['sd']=False
        emu.mem_write(gpioa+0x0c,struct.pack('<I',odr))
    elif address==timer+0x44 and value&0x8000:
        assert mode in (0x4040,0x6060,0x7040),'MOE enabled with unexpected timer mode'
        if mode==0x4040:assert register(timer+0x34)==register(timer+0x38)==0
        gate['checks']+=1
    elif address==timer+0x14 and value&1:
        assert not(register(timer)&2),'UG requested with update events disabled'
        gate['loaded']=(register(timer+0x34),register(timer+0x38))
    elif address==timer+0x18 and (value&0x7070)==0x6060 and mode!=0x6060:
        assert gate['loaded']==(register(timer+0x34),register(timer+0x38)), 'PWM restored before both compares latched'
        if gate['last_shot'] is not None:
            assert (register(symbols['now'])-gate['last_shot'])&0xffffffff>=20,'RUN resumed before discharge current decay interval'
        gate['checks']+=1
    elif address in (timer+0x34,timer+0x38) and live and mode==0x6060:
        assert register(timer)&2,'Live compare pair updated without UDIS'
        gate['checks']+=1
    elif address==timer and value&9==9 and not(register(timer)&1):
        assert mode==0x7040 and register(timer+0x34)==0,'Discharge must hold buck IN low and use boost PWM2'
        assert register(timer+0x30)==0 and register(timer+0x28)==0,'OPM must have RCR=0 and PSC=0'
        assert register(timer+0x2c)==2879 and not(value&2),'Unexpected OPM period or disabled update'
        assert 144<=2880-register(timer+0x38)<=1440,'Shot width outside 2..20 us'
        assert gate['sd'] and register(timer+0x44)&0x8000,'Shot not driving IN pins'
        assert gate['loaded']==(0,register(timer+0x38)),'OPM started before both compares latched'
        stamp=register(symbols['now'])
        assert gate['last_shot'] is None or (stamp-gate['last_shot'])&0xffffffff>=20,'Shots closer than 20 ms'
        gate['last_shot']=stamp;gate['shots']+=1
emu.hook_add(UC_HOOK_MEM_WRITE,trace_gate,begin=timer,end=timer+0x47)
emu.hook_add(UC_HOOK_MEM_WRITE,trace_gate,begin=gpioa+0x10,end=gpioa+0x17)
emu.reg_write(UC_ARM_REG_SP,0x2000FFF0);emu.reg_write(UC_ARM_REG_LR,0x101)
emu.emu_start(symbols['test_main']|1,0x100,count=100_000_000)
assert emu.reg_read(UC_ARM_REG_PC)==0x100,'Application fixture did not return'
def u32(s):return struct.unpack('<I',emu.mem_read(symbols[s],4))[0]
total,failed=u32('test_count'),u32('test_failed')
payload=bytes(emu.mem_read(symbols['last_tx'],2304)).split(b'\0',1)[0]
if payload:
    log=json.loads(payload)
    assert 'drain' in log and all(k in log['drain'] for k in ['state','pulses','ticks'])
    assert all(isinstance(log[k],int) for k in ['ilim_mA','ratio_pm','cv_pm','cc_pm','ref_mV'])
    assert log['vout_div_pm']==23000 and all(k in log['adc'] for k in ['vo','io','vi'])
    assert all(k in log['pwm'] for k in ['arr','psc','ccr1','ccr2','ccmr1','ccer','bdtr','gpio_crh','afio_mapr','sd'])
    assert 'touch' not in log
assert 5<total<256
lines=struct.unpack('<'+'I'*total,emu.mem_read(symbols['test_lines'],4*total));failed_lines=[x for x in lines if x]
print(f'{name}/BTN1/DMA: {total-failed}/{total} passed; failed lines: {failed_lines}')
(BUILD/(name+'_results.json')).write_text(json.dumps({'passed':total-failed,'total':total,'failed_lines':failed_lines,'timer_transition_checks':gate['checks'],'discharge_shots_checked':gate['shots']},indent=2))
if failed:raise SystemExit(1)
