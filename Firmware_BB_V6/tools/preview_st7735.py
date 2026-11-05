"""Render the actual C SPI display driver on a software ST7789 bus model."""
from pathlib import Path
import sys,subprocess,shutil
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/_deps'),str(ROOT.parent/'tools/_deps')]
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_PC
from PIL import Image
BUILD=ROOT/'build/preview_st7735';BUILD.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('arm-none-eabi-gcc');tools=Path(gcc).parent
includes=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F1xx/Include']
args=['-mcpu=cortex-m3','-mthumb','-Os','-std=c99','-ffunction-sections','-fdata-sections','-DUSE_HAL_DRIVER','-DSTM32F103xB','-DPSU_LCD_SOFT_SPI=0']+['-I'+str(ROOT/p) for p in includes]
elf=BUILD/'display.elf';binary=elf.with_suffix('.bin')
subprocess.run([gcc,*args,str(ROOT/'Core/Src/psu_display.c'),str(ROOT/'Core/Src/psu_modes.c'),str(ROOT/'Core/Src/psu_control.c'),str(ROOT/'Core/Src/psu_settings.c'),str(ROOT/'tests/display_fixture.c'),'-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,-e,preview','-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-Wl,--gc-sections','-lm','-o',str(elf)],check=True)
symbols={p[2]:int(p[0],16) for l in subprocess.check_output([str(tools/'arm-none-eabi-nm.exe'),str(elf)],text=True).splitlines() if len(p:=l.split())==3 and p[1].lower()!='u'}
subprocess.run([str(tools/'arm-none-eabi-objcopy.exe'),'-O','binary','--only-section=.text','--only-section=.rodata',str(elf),str(binary)],check=True)
for scenario,name in enumerate(['normal','fault','locked','blocked','ovp','discharge','discharge_stopped','discharge_done','ramp','digit_voltage','digit_current','digit_moved','output_off','large_values','menu_root','menu_ramp','menu_charge_edit','menu_charge_back','charge_cc','charge_full','charge_error','solder','charge_pre','menu_stop_first','menu_fault','graph_cv','graph_cc','graph_fault','graph_gaps','menu_display','graph_clock_wrap','graph_full_scale','graph_back_to_number','graph_long_gap','graph_off','menu_solder','solder_stand','solder_no_rise','solder_c210','solder_back_lab','solder_adc_wait','menu_solder_target','solder_sensor_invalid','solder_overtemperature','solder_temperature_changed','solder_temperature_static']):
    emu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);emu.mem_map(0,0x100000);emu.mem_map(0x20000000,0x10000);emu.mem_write(0x10000,binary.read_bytes())
    emu.reg_write(UC_ARM_REG_SP,0x2000FFF0);emu.reg_write(UC_ARM_REG_LR,0x101);emu.reg_write(UC_ARM_REG_R0,scenario)
    screen=Image.new('RGB',(160,128));pixels=screen.load()
    bus={'cs':1,'dc':1,'command':0,'params':bytearray(),'x0':0,'x1':319,'y0':0,'y1':239,'x':0,'y':0,'pending':None}
    def hook(uc,address,size,user):
        if address==(symbols['HAL_GPIO_WritePin']&~1):
            port=uc.reg_read(UC_ARM_REG_R0);pin=uc.reg_read(UC_ARM_REG_R1);level=uc.reg_read(UC_ARM_REG_R2)
            if port==0x40010C00:
                if pin&1:bus['dc']=level
                if pin&(1<<10):bus['cs']=level
        elif address==(symbols['HAL_SPI_Transmit']&~1):
            data=uc.mem_read(uc.reg_read(UC_ARM_REG_R1),uc.reg_read(UC_ARM_REG_R2))
            assert bus['cs']==0
            if not bus['dc']:
                bus['command']=data[0];bus['params']=bytearray()
                if data[0]==0x2C:bus['x']=bus['x0'];bus['y']=bus['y0'];bus['pending']=None
            elif bus['command'] in (0x2A,0x2B):
                bus['params']+=data
                if len(bus['params'])==4:
                    a,b,c,d=bus['params'];prefix='x' if bus['command']==0x2A else 'y';bus[prefix+'0']=a*256+b;bus[prefix+'1']=c*256+d
            elif bus['command']==0x2C:
                for byte in data:
                    if bus['pending'] is None:bus['pending']=byte
                    else:
                        color=bus['pending']*256+byte;bus['pending']=None;x=bus['x'];y=bus['y']
                        assert 0<=x<160 and 0<=y<128,(x,y)
                        pixels[x,y]=((color>>11)*255//31,((color>>5)&63)*255//63,(color&31)*255//31)
                        bus['x']+=1
                        if bus['x']>bus['x1']:bus['x']=bus['x0'];bus['y']+=1
    # Hook only the two bus entry points, keeping rendering fast.
    for function in ['HAL_GPIO_WritePin','HAL_SPI_Transmit']:
        at=symbols[function]&~1;emu.hook_add(UC_HOOK_CODE,hook,begin=at,end=at)
    emu.emu_start(symbols['preview']|1,0x100,count=30_000_000)
    assert emu.reg_read(UC_ARM_REG_PC)==0x100
    screen.resize((800,640),Image.Resampling.NEAREST).save(BUILD/(name+'.png'))
    print('Rendered actual C driver:',name)

