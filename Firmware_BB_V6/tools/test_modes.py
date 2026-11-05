"""Run charger state machine and physical menu integration on Cortex-M3."""
from pathlib import Path
import sys,subprocess,shutil,struct,json
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools/_deps'),str(ROOT.parent/'tools/_deps')]
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_PC
out=ROOT/'build/tests';out.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('arm-none-eabi-gcc');tools=Path(gcc).parent
inc=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F1xx/Include','USB_DEVICE/App','USB_DEVICE/Target','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc']
fixtures = ['modes','menu','shortcut','foreground','solder_pwm','solder_pwm_normal','bench','pin_test','lcd_bus','duty','solder_ramp','ovp4','relay_latch','health_detail','solder_config','hold_mode','r1r','ratio95','thermal_slew','solder_sleep','solder_beep','solder_settle','solder_load']
unknown = set(sys.argv[1:])-set(fixtures)
if unknown:raise SystemExit('Unknown/superseded fixture: '+', '.join(sorted(unknown))+'. Use solder_pwm for the current heater.')
for name in fixtures:
    if len(sys.argv)>1 and name not in sys.argv[1:]:continue
    sources=['psu_modes','psu_control','psu_settings']+(['psu_app','psu_remote'] if name!='modes' else [])
    if name=='foreground':sources+=['psu_display']
    elf=out/(name+'.elf')
    fixture = name.removesuffix('_normal')
    subprocess.run([gcc,'-mcpu=cortex-m3','-mthumb','-Os','-std=c99','-ffunction-sections','-fdata-sections','-DUSE_HAL_DRIVER','-DSTM32F103xB',*(['-DPSU_SOLDER_RAMP_MS=0','-DPSU_SOLDER_QUIET_MS=10'] if name in ('solder_pwm','solder_pwm_normal') else []),*(['-DPSU_LCD_SOFT_SPI=0'] if name=='foreground' else []),*(['-DPSU_PA6_PWM_PIN_TEST=1'] if name=='pin_test' else []),*(['-DPSU_SOLDER_TEST18=0'] if name not in ('foreground','solder_pwm') else []),*['-I'+str(ROOT/p) for p in inc],*[str(ROOT/'Core/Src'/f'{s}.c') for s in sources],str(ROOT/'tests'/f'{fixture}_fixture.c'),'-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,-e,test_main','-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-Wl,--gc-sections','-lm','-o',str(elf)],check=True)
    symbols={p[2]:int(p[0],16) for line in subprocess.check_output([str(tools/'arm-none-eabi-nm.exe'),str(elf)],text=True).splitlines() if len(p:=line.split())==3 and p[1].lower()!='u'}
    emu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
    for address,size in [(0,0x100000),(0x20000000,0x10000),(0x40000000,0x30000),(0xE0000000,0x100000)]:emu.mem_map(address,size)
    for sections,suffix,address in [(['.text','.rodata'],'bin',0x10000),(['.data'],'data',0x20000000)]:
        path=out/(name+'.'+suffix);subprocess.run([str(tools/'arm-none-eabi-objcopy.exe'),'-O','binary',*['--only-section='+s for s in sections],str(elf),str(path)],check=True)
        if path.stat().st_size:emu.mem_write(address,path.read_bytes())
    quiet_checks={'count':0,'active':False,'since':0}
    if name.startswith('solder'):
        read=lambda at:struct.unpack('<I',emu.mem_read(at,4))[0]
        def carrier_write(uc,access,address,size,value,user):
            active=(value&0x7000)==0x6000 and read(0x40000840)>0
            if quiet_checks['active'] and not active:
                quiet_checks['since']=read(symbols['now'])
            quiet_checks['active']=active
        def adc_sample(uc,address,size,user):
            if 'dma' not in symbols or not read(symbols['dma']):return
            enabled=bool(read(0x40000820)&1)
            pwm=(read(0x4000081c)&0x7000)==0x600000
            assert not (enabled and pwm and read(0x40000840)>0),'Tip ADC sampled with TIM3_CH1 PWM active'
            assert ((read(symbols['now'])-quiet_checks['since'])&0xffffffff)>=2,'Tip ADC sampled before 2 ms settling'
            quiet_checks['count']+=1
        emu.hook_add(UC_HOOK_MEM_WRITE,carrier_write,begin=0x4000081c,end=0x4000081f)
        addr=symbols['PsuSolder_Sample']&~1;emu.hook_add(UC_HOOK_CODE,adc_sample,begin=addr,end=addr)
    wire=[]
    if name=='lcd_bus':
        level={'pins':0,'bits':0,'byte':0}
        def lcd_write(uc,access,address,size,value,user):
            assert not value & ~0x00a000a0, 'LCD write touched PA6 or another pin'
            pins=(level['pins']|(value&0xffff)) & ~(value>>16)
            if pins&32 and not level['pins']&32:
                assert not struct.unpack('<I',uc.mem_read(0x40010c0c,4))[0]&1024,'Clock while CS high'
                level['byte']=(level['byte']<<1)|bool(pins&128);level['bits']+=1
                if level['bits']==8:
                    dc=bool(struct.unpack('<I',uc.mem_read(0x40010c0c,4))[0]&1)
                    wire.append((dc,level['byte']));level['bits']=level['byte']=0
            level['pins']=pins
        emu.hook_add(UC_HOOK_MEM_WRITE,lcd_write,begin=0x40010810,end=0x40010813)
    emu.reg_write(UC_ARM_REG_SP,0x2000fff0);emu.reg_write(UC_ARM_REG_LR,0x101)
    emu.emu_start(symbols['test_main']|1,0x100,count=1_500_000_000);assert emu.reg_read(UC_ARM_REG_PC)==0x100
    u32=lambda n:struct.unpack('<I',emu.mem_read(symbols[n],4))[0]
    if name=='lcd_bus':
        assert wire==[(False,0x2a),*[(True,v) for v in [0,0x28,1,0x18]],(False,0x36),*[(True,v) for v in [0,255,128,1,0x55,0xaa]]],wire
        print('LCD GPIO wire: command/data bytes, MSB order, CS and PA6 isolation PASS')
    total,failed=u32('test_count'),u32('test_failed');assert 1<total<256
    lines=struct.unpack('<'+'I'*total,emu.mem_read(symbols['test_lines'],4*total));failures=[n for n in lines if n]
    print(f'{name}: {total-failed}/{total}, failed lines: {failures}')
    result=dict(passed=total-failed,total=total,failed_lines=failures,quiet_adc_checks=quiet_checks['count'])
    if name=='foreground':
        result['simulated_metrics']={n:u32('foreground_'+n) for n in ['max_draw_ms','usb_off_ms','button_off_ms']}
        print(result['simulated_metrics'])
    (out/(name+'_results.json')).write_text(json.dumps(result,indent=2))
    if failed:raise SystemExit(1)
