"""Power-cut tests of actual C flash journal using a 1->0 flash bus model."""
from pathlib import Path
import sys,subprocess,shutil,struct,json,zlib
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'tools/_deps'),str(ROOT.parent/'tools/_deps')]
from unicorn import Uc,UC_ARCH_ARM,UC_MODE_THUMB,UC_MODE_MCLASS,UC_HOOK_CODE
from unicorn.arm_const import UC_ARM_REG_SP,UC_ARM_REG_LR,UC_ARM_REG_R0,UC_ARM_REG_R1,UC_ARM_REG_R2,UC_ARM_REG_PC
BUILD=ROOT/'build/tests';BUILD.mkdir(parents=True,exist_ok=True)
schema=json.loads((ROOT/'PC_App/settings_schema.json').read_text(encoding='utf-8'))
settings_bytes=4*len(schema['fields'])
def pack_settings(values):return struct.pack('<'+str(len(values))+'f',*values)
gcc=shutil.which('arm-none-eabi-gcc');tools=Path(gcc).parent
includes=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F1xx/Include']
args=['-mcpu=cortex-m3','-mthumb','-Os','-std=c99','-ffunction-sections','-fdata-sections','-DUSE_HAL_DRIVER','-DSTM32F103xB']+['-I'+str(ROOT/p) for p in includes]
elf=BUILD/'storage.elf';binary=elf.with_suffix('.bin')
subprocess.run([gcc,*args,str(ROOT/'Core/Src/psu_storage.c'),str(ROOT/'Core/Src/psu_settings.c'),str(ROOT/'tests/storage_fixture.c'),'-nostartfiles','--specs=nano.specs','--specs=nosys.specs','-Wl,-e,fixture','-Wl,-Ttext=0x10000','-Wl,-Tdata=0x20000000','-Wl,--gc-sections','-o',str(elf)],check=True)
symbols={p[2]:int(p[0],16) for l in subprocess.check_output([str(tools/'arm-none-eabi-nm.exe'),str(elf)],text=True).splitlines() if len(p:=l.split())==3 and p[1].lower()!='u'}
subprocess.run([str(tools/'arm-none-eabi-objcopy.exe'),'-O','binary','--only-section=.text','--only-section=.rodata',str(elf),str(binary)],check=True)
code=binary.read_bytes();A=struct.pack('<6H',5000,1000,12000,3000,24000,2000);B=struct.pack('<6H',6000,1500,13000,3500,25000,2500)
def run(function,presets,flash=None,cut=None,settings=None):
    emu=Uc(UC_ARCH_ARM,UC_MODE_THUMB|UC_MODE_MCLASS);emu.mem_map(0,0x100000);emu.mem_map(0x20000000,0x10000);emu.mem_map(0x08000000,0x10000)
    emu.mem_write(0x10000,code);emu.mem_write(0x08000000,b'\xFF'*0x10000);emu.mem_write(0x20000100,presets)
    if flash:emu.mem_write(0x0800F800,flash)
    emu.reg_write(UC_ARM_REG_SP,0x2000FFF0);emu.reg_write(UC_ARM_REG_LR,0x101);emu.reg_write(UC_ARM_REG_R0,0x20000100)
    if function.endswith('All'):
        emu.reg_write(UC_ARM_REG_R1,0x20001000)
        emu.mem_write(0x20001000,settings or bytes(settings_bytes))
    operations=[]
    def hook(uc,address,size,user):
        if address==(symbols['HAL_FLASHEx_Erase']&~1):
            # STM32F1 erase struct: TypeErase, Banks, PageAddress, NbPages.
            fields=struct.unpack('<4I',uc.mem_read(uc.reg_read(UC_ARM_REG_R0),16));at=fields[2]
            assert at in [0x0800F800,0x0800FC00],hex(at)
            operations.append('erase')
            if len(operations)==cut:uc.emu_stop();return
            uc.mem_write(at,b'\xFF'*1024)
        elif address==(symbols['HAL_FLASH_Program']&~1):
            at=uc.reg_read(UC_ARM_REG_R1);word=uc.reg_read(UC_ARM_REG_R2)&65535
            assert 0x0800F800<=at<0x08010000 and at%2==0
            old=struct.unpack('<H',uc.mem_read(at,2))[0];assert old&word==word
            operations.append('program')
            if len(operations)==cut:uc.emu_stop();return
            uc.mem_write(at,struct.pack('<H',word))
    for function_name in ['HAL_FLASHEx_Erase','HAL_FLASH_Program']:
        at=symbols[function_name]&~1;emu.hook_add(UC_HOOK_CODE,hook,begin=at,end=at)
    emu.emu_start(symbols[function]|1,0x100,count=2_000_000)
    assert emu.reg_read(UC_ARM_REG_PC)==0x100 or len(operations)==cut
    result=(emu.reg_read(UC_ARM_REG_R0),bytes(emu.mem_read(0x20000100,12)),bytes(emu.mem_read(0x0800F800,2048)),len(operations))
    return result+(bytes(emu.mem_read(0x20001000,settings_bytes)),) if function.endswith('All') else result
passed=0
def check(condition):
    global passed
    assert condition
    passed+=1
check(run('PsuStorage_Load',b'\0'*12)[0]==0)
ok,_,first,ops=run('PsuStorage_Save',A);check(ok==1);check(run('PsuStorage_Load',b'\0'*12,first)[:2]==(1,A))
ok,_,second,ops=run('PsuStorage_Save',B,first);check(ok==1);check(run('PsuStorage_Load',b'\0'*12,second)[:2]==(1,B))
for base,expected,new in [(first,A,B),(second,B,A)]:
    for cut in range(1,ops+1):
        _,_,interrupted,_=run('PsuStorage_Save',new,base,cut)
        check(run('PsuStorage_Load',b'\0'*12,interrupted)[:2]==(1,expected))
    check(run('PsuStorage_Load',b'\0'*12,run('PsuStorage_Save',new,base)[2])[:2]==(1,new))
# Corrupt newest CRC; older page must remain usable.
corrupt=bytearray(second);corrupt[1024+16]^=1
check(run('PsuStorage_Load',b'\0'*12,bytes(corrupt))[:2]==(1,A))
# Legacy preset-only records must survive migration, including power loss.
factory=pack_settings([f['default'] for f in schema['fields']])
legacy_data=struct.pack('<I',17)+A
legacy=legacy_data+struct.pack('<II',zlib.crc32(legacy_data),0x42503631)+b'\xff'*(2048-24)
old=run('PsuStorage_LoadAll',bytes(12),legacy)
check(old[0]==1 and old[1]==A and old[4]==factory)
values=[f['default'] for f in schema['fields']]
for key,value in [('cv_kp',.025),('i_max',4.),('i_peak',6.),('vout_max',30.),('ramp_v_per_s',2.5),('solder_voltage',9.),('solder_current',2.)]:
    values[next(i for i,f in enumerate(schema['fields']) if f['key']==key)]=value
custom=pack_settings(values)
saved=run('PsuStorage_SaveAll',B,legacy,settings=custom)
check(saved[0]==1)
loaded=run('PsuStorage_LoadAll',bytes(12),saved[2])
check(loaded[0]==1 and loaded[1]==B and loaded[4]==custom)
for cut in range(1,saved[3]+1):
    damaged=run('PsuStorage_SaveAll',B,legacy,cut,settings=custom)
    fallback=run('PsuStorage_LoadAll',bytes(12),damaged[2])
    check(fallback[0]==1 and fallback[1]==A and fallback[4]==factory)
bad_values=values.copy();bad_values[0]=float('nan')
bad=run('PsuStorage_SaveAll',B,saved[2],settings=pack_settings(bad_values))
check(bad[0]==0 and bad[3]==0 and bad[2]==saved[2])
# Existing BBV6.5/6.7 V2 pages: preserve all 36 floats, initialize only ramp.
old_values=[f['default'] for f in schema['fields'][:36]]
old_values[0]=.027;old_values[4]=.8
for key,value in dict(vin_max=40.,vout_max=36.,i_max=8.,i_peak=10.,power_max=60.,input_current_max=5.,i_cal=.5).items():
    old_values[[f['key'] for f in schema['fields']].index(key)]=value
v2_data=struct.pack('<I',29)+A+struct.pack('<I',2)+pack_settings(old_values)
v2=v2_data+struct.pack('<II',zlib.crc32(v2_data),0x42503632)
v2+=b'\xff'*(2048-len(v2))
thermal_defaults=[f['default'] for f in schema['fields'][39:]]
expanded=pack_settings(old_values+[10.,18.,6.]+thermal_defaults)
loaded=run('PsuStorage_LoadAll',bytes(12),v2)
check(loaded[0]==1 and loaded[1]==A and loaded[4]==expanded)
migrated=run('PsuStorage_SaveAll',B,v2,settings=custom)
loaded=run('PsuStorage_LoadAll',bytes(12),migrated[2])
check(loaded[0]==1 and loaded[1]==B and loaded[4]==custom)
for cut in range(1,migrated[3]+1):
    interrupted=run('PsuStorage_SaveAll',B,v2,cut,settings=custom)
    loaded=run('PsuStorage_LoadAll',bytes(12),interrupted[2])
    check(loaded[0]==1 and loaded[1]==A and loaded[4]==expanded)
# Newest V3 CRC corruption falls back to the intact V2 PID/presets.
damaged=bytearray(migrated[2]);damaged[1024+24]^=1
loaded=run('PsuStorage_LoadAll',bytes(12),bytes(damaged))
check(loaded[0]==1 and loaded[1]==A and loaded[4]==expanded)
# A genuine lower-limit V3 profile survives the 6.14 upgrade without losing PID.
old_v3=struct.pack('<I',41)+A+struct.pack('<I',3)+pack_settings(old_values+[10.])
old_v3+=struct.pack('<II',zlib.crc32(old_v3),0x42503633)
old_v3+=b'\xff'*(2048-len(old_v3))
loaded=run('PsuStorage_LoadAll',bytes(12),old_v3)
check(loaded[0]==1 and loaded[1]==A and loaded[4]==expanded)
# V3 -> V4 preserves PID, ramp and presets, and writes the selected solder
# rail atomically. Interrupt every erase/program operation during migration.
v4=run('PsuStorage_SaveAll',B,old_v3,settings=custom)
check(v4[0]==1)
v4_loaded=run('PsuStorage_LoadAll',bytes(12),v4[2])
check(v4_loaded[0]==1 and v4_loaded[1]==B and v4_loaded[4]==custom)
for cut in range(1,v4[3]+1):
    interrupted=run('PsuStorage_SaveAll',B,old_v3,cut,settings=custom)
    fallback=run('PsuStorage_LoadAll',bytes(12),interrupted[2])
    check(fallback[0]==1 and fallback[1]==A and fallback[4]==expanded)
# Expanded preset values still fit the existing uint16 wire/storage format.
high_presets=struct.pack('<6H',59900,15000,50000,12000,24000,15000)
high_saved=run('PsuStorage_SaveAll',high_presets,old_v3,settings=factory)
check(high_saved[0]==1)
high_loaded=run('PsuStorage_LoadAll',bytes(12),high_saved[2])
check(high_loaded[0]==1 and high_loaded[1]==high_presets and high_loaded[4]==factory)
# Existing V4 pages retain all 39 settings while initializing the thermal PID.
v4_values=[f['default'] for f in schema['fields'][:39]]
v4_values[0]=0.027
v4_data=struct.pack('<I',52)+A+struct.pack('<I',4)+pack_settings(v4_values)
v4_page=v4_data+struct.pack('<II',zlib.crc32(v4_data),0x42503634)
v4_page+=b'\xff'*(2048-len(v4_page))
restored=run('PsuStorage_LoadAll',bytes(12),v4_page)
check(restored[0]==1 and restored[4]==pack_settings(v4_values+thermal_defaults))
v5_settings=values.copy()
for key,value in [('solder_kp',0.02),('solder_ki',0.003),('solder_kd',0.004),('solder_duty_max',42.)]:v5_settings[[f['key'] for f in schema['fields']].index(key)]=value
v5_saved=run('PsuStorage_SaveAll',B,v4_page,settings=pack_settings(v5_settings))
check(v5_saved[0]==1)
restored=run('PsuStorage_LoadAll',bytes(12),v5_saved[2])
check(restored[0]==1 and restored[4]==pack_settings(v5_settings))
for cut in range(1,v5_saved[3]+1):
    interrupted=run('PsuStorage_SaveAll',B,v4_page,cut,settings=pack_settings(v5_settings))
    restored=run('PsuStorage_LoadAll',bytes(12),interrupted[2])
    check(restored[0]==1 and restored[4]==pack_settings(v4_values+thermal_defaults))
# OVP values must round-trip exactly; rejected values must not erase Flash.
keys=[f['key'] for f in schema['fields']]
values=list(struct.unpack('<'+str(len(keys))+'f',factory))
values[keys.index('ovp_percent')]=8.0;values[keys.index('ovp_offset')]=0.8
ovp_settings=pack_settings(values)
ovp_saved=run('PsuStorage_SaveAll',A,settings=ovp_settings)
check(ovp_saved[0]==1)
ovp_loaded=run('PsuStorage_LoadAll',bytes(12),ovp_saved[2])
check(ovp_loaded[0]==1 and ovp_loaded[4]==ovp_settings)
for key,value in [('ovp_percent',80.0),('ovp_offset',10.0)]:
    bad=values.copy();bad[keys.index(key)]=value
    rejected=run('PsuStorage_SaveAll',A,ovp_saved[2],settings=pack_settings(bad))
    check(rejected[0]==0 and rejected[2]==ovp_saved[2])
# Genuine V5 record with old 10k default migrates only R25, not pull/Beta/PID.
keys=[f['key'] for f in schema['fields']]
legacy50=[f['default'] for f in schema['fields']]
legacy50[keys.index('ntc_r25')]=10000.
legacy50[keys.index('ntc_pull')]=12000.
legacy50[keys.index('ntc_beta')]=3435.
legacy50[keys.index('cv_kp')]=.031
legacy50[keys.index('solder_kp')]=.017
def v5_page(values):
    data=struct.pack('<I',70)+A+struct.pack('<I',5)+pack_settings(values)
    record=data+struct.pack('<II',zlib.crc32(data),0x42503635)
    return record+b'\xff'*(2048-len(record))
old_page=v5_page(legacy50)
expected=legacy50.copy();expected[keys.index('ntc_r25')]=50000.
upgraded=run('PsuStorage_LoadAll',bytes(12),old_page)
check(upgraded[0]==1 and upgraded[1]==A and upgraded[4]==pack_settings(expected))
committed=run('PsuStorage_SaveAll',A,old_page,settings=upgraded[4])
check(committed[0]==1)
check(run('PsuStorage_LoadAll',bytes(12),committed[2])[4]==pack_settings(expected))
for cut in range(1,committed[3]+1):
    damaged=run('PsuStorage_SaveAll',A,old_page,cut,settings=upgraded[4])
    check(run('PsuStorage_LoadAll',bytes(12),damaged[2])[4]==pack_settings(expected))
# Custom old R25 is retained; explicit 10k in revision 6 also remains editable.
legacy50[keys.index('ntc_r25')]=47000.
check(run('PsuStorage_LoadAll',bytes(12),v5_page(legacy50))[4]==pack_settings(legacy50))
legacy50[keys.index('ntc_r25')]=10000.
explicit=run('PsuStorage_SaveAll',A,settings=pack_settings(legacy50))
check(run('PsuStorage_LoadAll',bytes(12),explicit[2])[4]==pack_settings(legacy50))
print(f'Flash journal: {passed}/{passed} checks passed (power loss at every erase/program boundary).')
(BUILD/'storage_results.json').write_text(json.dumps({'passed':passed,'total':passed},indent=2))
