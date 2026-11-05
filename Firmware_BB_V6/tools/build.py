"""Build with the installed Arm GNU Toolchain, no downloaded dependencies."""
from pathlib import Path
import subprocess, shutil, json
ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build'; BUILD.mkdir(exist_ok=True)
gcc=shutil.which('arm-none-eabi-gcc')
if not gcc: raise SystemExit('Add Arm GNU Toolchain bin directory to PATH')
includes=['Core/Inc','Drivers/STM32F1xx_HAL_Driver/Inc','Drivers/STM32F1xx_HAL_Driver/Inc/Legacy','Drivers/CMSIS/Include','Drivers/CMSIS/Device/ST/STM32F1xx/Include','USB_DEVICE/App','USB_DEVICE/Target','Middlewares/ST/STM32_USB_Device_Library/Core/Inc','Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Inc']
common=['-mcpu=cortex-m3','-mthumb','-mfloat-abi=soft','-Os','-g3','-std=c99','-ffunction-sections','-fdata-sections','-Wall','-Wextra','-Wno-unused-parameter','-DUSE_HAL_DRIVER','-DSTM32F103xB']
common += ['-I'+str(ROOT/p) for p in includes]
sources=[]
for folder in ['Core/Src','USB_DEVICE/App','USB_DEVICE/Target','Drivers/STM32F1xx_HAL_Driver/Src','Middlewares/ST/STM32_USB_Device_Library/Core/Src','Middlewares/ST/STM32_USB_Device_Library/Class/CDC/Src','gcc']:
    sources+=sorted((ROOT/folder).glob('*.c'))
objects=[]; commands=[]
for src in sources:
    obj=BUILD/(src.stem+'.o');objects.append(str(obj))
    args=[gcc,*common,'-c',str(src),'-o',str(obj)]
    commands.append({'directory':str(ROOT),'file':str(src),'arguments':args})
    subprocess.run(args,check=True)
elf=BUILD/'Firmware_BB_V6.elf'
subprocess.run([gcc,*common,*objects,'-T'+str(ROOT/'gcc/STM32F103C8Tx.ld'),'--specs=nano.specs','--specs=nosys.specs','-Wl,--gc-sections','-Wl,-Map='+str(BUILD/'Firmware_BB_V6.map'),'-Wl,--print-memory-usage','-lm','-o',str(elf)],check=True)
for format,extension in [('ihex','hex'),('binary','bin')]:
    subprocess.run([str(Path(gcc).with_name('arm-none-eabi-objcopy.exe')),'-O',format,str(elf),str(elf.with_suffix('.'+extension))],check=True)
subprocess.run([str(Path(gcc).with_name('arm-none-eabi-size.exe')),str(elf)],check=True)
(BUILD/'compile_commands.json').write_text(json.dumps(commands,indent=2))
print('Built:',elf)
