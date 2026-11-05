"""Exercise PC-generated requests against the compiled Cortex-M3 application."""
from pathlib import Path
import sys,subprocess,shutil,struct,json
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'PC_App'),str(ROOT/'tools/_deps'),str(ROOT.parent/'tools/_deps')]
from protocol import command,config_command,defaults,Framer
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3,UC_ARM_REG_PC
BUILD=ROOT/'build/tests';BUILD.mkdir(parents=True,exist_ok=True)
gcc=shutil.which('arm-none-eabi-gcc');bin_dir=Path(gcc).parent
includes=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F1xx/Include','USB_DEVICE/App','USB_DEVICE/Target','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc']
flags=['-mcpu=cortex-m3','-mthumb','-Os','-g','-std=c99','-ffunction-sections','-fdata-sections','-DUSE_HAL_DRIVER','-DSTM32F103xB']+['-I'+str(ROOT/p) for p in includes]
elf=BUILD/'remote.elf'
functions=['remote_fixture_init','remote_fixture_tick','remote_fixture_input','remote_fixture_sense','remote_fixture_buttons','remote_fixture_tip']
subprocess.run([gcc,*flags,*[str(ROOT/'Core/Src'/f'{name}.c') for name in ['psu_app','psu_control','psu_settings','psu_remote','psu_modes']],str(ROOT/'tests/remote_fixture.c'),'-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,-e,remote_fixture_init','-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-Wl,--gc-sections',*['-Wl,-u,'+f for f in functions],'-lm','-o',str(elf)],check=True)
symbols={p[2]:int(p[0],16) for l in subprocess.check_output([str(bin_dir/'arm-none-eabi-nm.exe'),str(elf)],text=True).splitlines() if len(p:=l.split())==3 and p[1].lower()!='u'}
emu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS)
for address,size in [(0,0x100000),(0x20000000,0x10000),(0x40000000,0x30000),(0xE0000000,0x100000)]:emu.mem_map(address,size)
for sections,name,address in [(['.text','.rodata'],'remote.bin',0x10000),(['.data'],'remote.data',0x20000000)]:
    out=BUILD/name
    subprocess.run([str(bin_dir/'arm-none-eabi-objcopy.exe'),'-O','binary',*['--only-section='+s for s in sections],str(elf),str(out)],check=True)
    if out.stat().st_size:emu.mem_write(address,out.read_bytes())
messages=[];framer=Framer();transmissions=[]
def tx(uc,address,size,user):
    data=bytes(uc.mem_read(uc.reg_read(UC_ARM_REG_R0),uc.reg_read(UC_ARM_REG_R1)))
    transmissions.append(data);messages.extend(framer.feed(data))
at=symbols['CDC_Transmit_FS']&~1;emu.hook_add(UC_HOOK_CODE,tx,begin=at,end=at)
def call(name,*args):
    emu.reg_write(UC_ARM_REG_SP,0x2000fff0);emu.reg_write(UC_ARM_REG_LR,0x101)
    for reg,value in zip([UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_R3],args):emu.reg_write(reg,value)
    emu.emu_start(symbols[name]|1,0x100,count=30_000_000)
    assert emu.reg_read(UC_ARM_REG_PC)==0x100,name+' did not return'
def feed(data):
    emu.mem_write(0x2000c000,data);call('remote_fixture_input',0x2000c000,len(data))
def tick(ms=3):call('remote_fixture_tick',ms)
count=0
def check(value):
    global count
    assert value
    count+=1
identifier=0
def request(op,*args,settings=None,expect=True):
    global identifier
    identifier+=1
    wire=config_command(identifier,args[0],settings) if op=='CFG' and settings is not None else command(identifier,op,*args)
    # Packet fragmentation including a single-byte prefix; no partial execution.
    for start in range(0,len(wire),37):feed(wire[start:start+37]);tick(1)
    for _ in range(15):
        found=[m for m in messages if m.get('type')=='reply' and m.get('id')==identifier]
        if found:
            reply=found[-1];check(reply['ok']==expect);return reply
        tick(1)
    raise AssertionError(f'No reply for {op} #{identifier}; last messages: {messages[-2:]}')

call('remote_fixture_init')
r=request('GET');check(r['settings']==defaults());check(not r['enabled'] and r['softstart'] is True and r['protocol']==1)
check(len(transmissions[-1])<2304)
request('SET',5000,1000);r=request('ON');check(r['enabled']==1)
tick(110)
t=[m for m in messages if m.get('type')=='telemetry'][-1]
check(0<t['ref_mV']<5000 and t['ramp_active']==1 and t['state']==1 and t['ramp_mV_s']==10000)
call('remote_fixture_sense',268,0,960,3413);tick(610)
t=[m for m in messages if m.get('type')=='telemetry'][-1]
check(t['ref_mV']==5000 and t['state'] in (2,3) and t['softstart'] is True)
check(t['vout_div_pm']==23000 and t['adc']==dict(vo=268,io=0,vi=960))
check(t['pwm']['arr']==2879 and t['pwm']['psc']==0)
check(t['pwm']['ccr1']>0 and t['pwm']['ccr2']==2592)
check(t['pwm']['ccmr1']&0x7070==0x6060 and t['pwm']['bdtr']&0x8000)
r=request('CFG',0,settings=defaults(),expect=False);check(r['error']=='output_off_required' and r['rev']==0)
request('SAVE',expect=False);request('PRESET',0,6000,1500,expect=False)
request('SET',60000,1000,expect=False);request('SET',5000,16000,expect=False)
request('OFF');tick(25)
r=request('CFG',0,settings=defaults(),expect=False);check(r['error']=='output_off_required')
feed(b'!\n');tick(25)
new=defaults();new['cv_kp']=20000;new['discharge_enabled']=0
r=request('CFG',0,settings=new);check(r['settings']==new and r['rev']==1 and not r['enabled'])
r=request('CFG',0,settings=defaults(),expect=False);check(r['error']=='revision_changed' and r['settings']==new)
for tokens,error in [('i_peak=21000','invalid_setting'),('vout_max=60000','invalid_setting'),('i_max=8000 i_peak=5000','current_order'),('temp_warn=800 temp_trip=750','temperature_order'),('i_cal=499999','invalid_setting'),('i_max=4000 i_max=5000','duplicate_setting'),('cv_kp=nan','syntax'),('cv_kp=2147483648','syntax')]:
    r=request('CFG',1,tokens,expect=False);check(r['error']==error and r['settings']==new)
r=request('SAVE');check(r['ok'])
r=request('PRESET',0,6000,1500);check(r['presets'][0]==[6000,1500])
r=request('DEFAULTS',1);check(r['settings']==defaults() and r['rev']==2)
tick(160)
# Emergency OFF cancels an already parsed request AND a partial line.
identifier+=1;feed(command(identifier,'ON'));tick(1);feed(b'!\n');tick(3)
r=request('GET');check(not r['enabled'])
feed(b'@999 ON');tick(3);feed(b'!\n');tick(3);r=request('GET');check(not r['enabled'])
# Buffer flood must not hide emergency OFF or execute a truncated command.
tick(160);request('ON');feed(b'x'*2500+b'!\n');tick(3);r=request('GET');check(not r['enabled'])
feed(b'@1999 CFG 2 i_max=2000 '+b'x'*1300+b'\n');tick(4)
r=request('GET');check(r['settings']==defaults())
check(framer.errors==0)
# Runtime ramp is acknowledged, used by TIM3 and reflected in telemetry.
call('remote_fixture_sense',0,0,960,3413);tick(160)
ramp_cfg=defaults();ramp_cfg['ramp_v_per_s']=2500
r=request('CFG',2,settings=ramp_cfg);check(r['settings']['ramp_v_per_s']==2500)
tick(160);request('SET',5000,1000);request('ON');tick(100)
t=[m for m in messages if m.get('type')=='telemetry'][-1]
check(t['ramp_mV_s']==2500 and 150<=t['ref_mV']<=300 and t['ramp_active']==1)
r=request('CFG',3,settings=defaults(),expect=False);check(r['error']=='output_off_required')
feed(b'!\n');tick(25);r=request('SAVE');check(r['ok'])
check('touch' not in [m for m in messages if m.get('type')=='telemetry'][-1])
check(r['caps']==dict(vout_mV=59900,i_mA=15000,ipeak_mA=20000,vin_mV=60000,power_mW=600000,input_mA=10000,ovp_mV=60000))
# The complete expanded profile can be configured and persisted while OFF.
high=defaults();high.update(vin_max=60000,vout_max=59900,i_max=15000,i_peak=20000,power_max=600000,input_current_max=10000)
r=request('CFG',3,settings=high);check(r['settings']==high)
r=request('SET',59900,15000);check(r['set_mV']==59900 and r['set_mA']==15000 and not r['enabled'])
r=request('SAVE');check(r['ok'])
# SOLDER rail configuration uses CFG while OFF, not LAB SET, and cannot
# reconfigure an active heater or a pending route even before DC-DC is ON.
call('remote_fixture_sense',0,0,1295,3413);tick(160)
call('remote_fixture_buttons',1<<13,1026);tick(60)
# SOLDER now starts automatically; no extra short ON press here.
r=request('CFG',r['rev'],settings=high,expect=False)
check(r['error']=='output_off_required')
request('OFF');tick(20)
solder_cfg=dict(high,solder_voltage=7500,solder_current=1200)
r=request('CFG',r['rev'],settings=solder_cfg)
check(r['settings']==solder_cfg and not r['enabled'])
r=request('SAVE');check(r['settings']['solder_voltage']==7500 and r['settings']['solder_current']==1200)
request('SET',8000,1300,expect=False)
tick(160);call('remote_fixture_buttons',1<<13,60);tick(60)
call('remote_fixture_sense',405,0,1295,3413);tick(1000)
r=request('GET');check(r['enabled'] and r['set_mV']==7500 and r['set_mA']==1200)
r=request('CFG',r['rev'],settings=high,expect=False)
check(r['error']=='output_off_required' and r['settings']==solder_cfg)
request('OFF');tick(20)
for tokens in ['solder_voltage=60000','solder_current=15001','solder_current=0']:
    r=request('CFG',r['rev'],tokens,expect=False)
    check(r['error']=='invalid_setting' and r['settings']==solder_cfg)
# The commissioning lease expires on-device with no host polling/renewal.
for duration,target in [(99,200),(10001,200),(600,149),(600,251)]:
    r=request('HEAT',duration,target,expect=False);check(r['error']=='bench_range')
tick(160);r=request('HEAT',600,200);check(r['op']=='HEAT')
r=request('HEAT',600,200,expect=False);check(r['error']=='output_off_required')
tick(650);r=request('GET');check(not r['enabled'] and r['change'] and not r['fault'])
feed(b'?');tick(110)
t=[m for m in messages if m.get('type')=='telemetry'][-1]
check(t['solder']['target_C']==300 and not t['solder']['heater'] and t['solder']['rises']>0)
# Priority OFF cancels an already queued HEAT just as it cancels ON.
identifier+=1;feed(command(identifier,'HEAT',600,200));tick(1);feed(b'!\n');tick(3)
r=request('GET');check(not r['enabled'] and r['change'])
# Thermal errors survive lease cleanup, so a failed run is not silently reset.
tick(160);request('HEAT',600,200);call('remote_fixture_tip',4095);tick(10)
feed(b'?');tick(110)
t=[m for m in messages if m.get('type')=='telemetry'][-1]
check(t['solder']['reason']==3 and not t['enabled'] and t['change'])
request('HEAT',600,200,expect=False)
request('OFF');call('remote_fixture_tip',1273);tick(160)
check(all(k in t['solder'] for k in ['load_limit_pm','rail_recover','rail_events','zero_cause']))
print(f'PC protocol -> actual Cortex-M3 app: {count}/{count} checks passed.')
(BUILD/'remote_results.json').write_text(json.dumps({'passed':count,'total':count,'protocol':1,'max_tx_bytes':max(map(len,transmissions))},indent=2))

