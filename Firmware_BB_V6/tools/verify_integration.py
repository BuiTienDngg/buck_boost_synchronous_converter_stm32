"""Check the final integration and memory boundaries without hardware access."""
from pathlib import Path
import hashlib,subprocess,xml.etree.ElementTree as ET,struct,json,re
ROOT=Path(__file__).resolve().parents[1]
files=['Core/Inc/main.h','Core/Src/main.c','Core/Src/stm32f1xx_it.c','Core/Src/stm32f1xx_hal_msp.c','USB_DEVICE/App/usbd_cdc_if.c','Firmware_BB_V6.ioc','MDK-ARM/Firmware_BB_V6.uvprojx','MDK-ARM/startup_stm32f103xb.s']
def hashes():return {f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files}
before=hashes();subprocess.run(['python',str(ROOT/'tools/integrate.py')],check=True);assert hashes()==before,'Integration script must be idempotent'
main=(ROOT/'Core/Src/main.c').read_text();app=(ROOT/'Core/Src/psu_app.c').read_text()
assert 'ADC_CHANNEL_4;\n  sConfig.Rank = ADC_REGULAR_RANK_3' in main
assert 'hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;' in main
assert 'hadc1.Init.ContinuousConvMode = ENABLE;' in main
assert 'ADC_SAMPLETIME_239CYCLES_5' in main
assert 'tim->Instance != TIM4' in app
assert 'HAL_TIM_Base_Start_IT(&htim4)' in app
assert 'if(!PSU_LCD_SOFT_SPI) MX_SPI1_Init();' in main
assert '#define PSU_LCD_SOFT_SPI 1' in (ROOT/'Core/Inc/psu_config.h').read_text()
assert '__HAL_RCC_SPI1_CLK_DISABLE();' in (ROOT/'Core/Src/psu_display.c').read_text()
assert 'SPI1_MISO' not in (ROOT/'Core/Src/stm32f1xx_hal_msp.c').read_text()
assert 'TIM1->CCER &= ~(TIM_CCER_CC1NE | TIM_CCER_CC2NE | TIM_CCER_CC3E | TIM_CCER_CC3NE)' in app
assert 'GPIOA->BSRR=SD_Pin' in app and 'GPIOA->BRR=SD_Pin' in app
assert '#define HEATER_Pin GPIO_PIN_6' in (ROOT/'Core/Inc/main.h').read_text()
assert 'PsuApp_SysTick();' in (ROOT/'Core/Src/stm32f1xx_it.c').read_text()
assert 'ADC2->JSQR = 3u << ADC_JSQR_JSQ4_Pos' in main
assert 'TIM_OSSI_ENABLE' in main and 'TIM_OSSR_ENABLE' in main
assert 'HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3)' not in main
assert 'GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10;' not in (ROOT/'Core/Src/stm32f1xx_hal_msp.c').read_text()
assert 'htim1.Init.Period = 7199;' in main and re.search(r'htim3.Init.Prescaler\s*=\s*0;',main) and re.search(r'htim3.Init.Period\s*=\s*7199;',main) and 'htim4.Init.Prescaler = 71;' in main and 'htim4.Init.Period = 999;' in main
config=(ROOT/'Core/Inc/psu_config.h').read_text()
assert int(re.search(r'#define\s+PSU_PWM_HZ\s+(\d+)u',config)[1])==10000
assert int(re.search(r'#define\s+PSU_PWM_PERIOD\s+(\d+)u',config)[1])==7199
assert app.index('PsuDisplay_Init();')<app.index('HAL_ADC_Start_DMA(&hadc1')
project=ET.parse(ROOT/'MDK-ARM/Firmware_BB_V6.uvprojx')
for name in ['psu_control.c','psu_app.c','psu_display.c','psu_storage.c','psu_settings.c','psu_remote.c','psu_modes.c']:
    assert sum(e.text==name for e in project.findall('.//FileName'))==1
assert not any(e.text=='psu_touch.c' for e in project.findall('.//FileName'))
assert 'PsuTouch' not in app and 'touch_inputs' not in app
assert 'HAL_NVIC_EnableIRQ(EXTI15_10_IRQn)' not in main
assert project.find('.//ScatterFile').text=='psu.sct'
assert project.find('.//Cads/Optim').text=='6', 'Keil size optimization'
assert '-Oz' in project.find('.//Cads/VariousControls/MiscControls').text.split()
assert project.find('.//ArmAdsMisc/useUlib').text=='1', 'Keil must use MicroLIB'
assert project.find('.//Cads/v6Lto').text=='1', 'Keil must use LTO to retain Flash headroom'
assert int(re.search(r'Stack_Size\s+EQU\s+(0x[0-9a-fA-F]+)',(ROOT/'MDK-ARM/startup_stm32f103xb.s').read_text())[1],16)>=3072
data=(ROOT/'build/Firmware_BB_V6.bin').read_bytes();assert len(data)<=62*1024
stack,reset=struct.unpack('<II',data[:8]);assert stack==0x20005000 and 0x08000000<=reset<0x0800F800 and reset&1
irq=(ROOT/'Core/Src/stm32f1xx_it.c').read_text();assert 'void TIM4_IRQHandler(void)' in irq and 'HAL_TIM_IRQHandler(&htim4)' in irq
tim4_vector=struct.unpack_from('<I',data,(16+30)*4)[0];assert tim4_vector&1 and 0x08000000<=tim4_vector<0x0800F800
manifest={str(p.relative_to(ROOT)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(ROOT.rglob('*')) if p.is_file() and 'build' not in p.relative_to(ROOT).parts and '_deps' not in p.relative_to(ROOT).parts and '__pycache__' not in p.parts and p.relative_to(ROOT).parts[:2]!=('MDK-ARM','Firmware_BB_V6') and p.name!='source_manifest.json'}
(ROOT/'source_manifest.json').write_text(json.dumps(manifest,indent=2))
print('Integration, vector table, 62 KiB boundary and source manifest: PASS')
