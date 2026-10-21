"""Idempotent integration of generated CubeMX files. Review after regeneration."""
from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
def edit(path, function):
    p = ROOT / path
    s = p.read_text(encoding='utf-8-sig')
    p.write_text(function(s), encoding='utf-8', newline='\n')
def user(s, region, body):
    pattern = r'(/\* USER CODE BEGIN '+re.escape(region)+r' \*/).*?(/\* USER CODE END '+re.escape(region)+r' \*/)'
    s, n = re.subn(pattern, lambda m:m[1]+'\n'+body+'\n  '+m[2], s, count=1, flags=re.S)
    assert n, region
    return s
def main(s):
    s = user(s, 'Includes', '#include "psu_app.h"')
    s = s.replace('  MX_SPI1_Init();', '  if(!PSU_LCD_SOFT_SPI) MX_SPI1_Init();')
    s = user(s, 'SysInit', '''  if(PSU_PA6_PWM_PIN_TEST){
    /* Diagnostic only: no peripheral initializer, including SPI1, runs. */
    PsuApp_PinTestInit();
    for(;;){__WFI();}
  }''')
    s = user(s, '2', '  if(PSU_PA6_PWM_PIN_TEST) PsuApp_PinTestInit();\n  else PsuApp_Init();')
    s = user(s, 'SPI1_Init 2', '  /* Transmit Only Master: MOSI output, no receive path. */\n  SPI_1LINE_TX(&hspi1);')
    s = user(s, '3', '    if(PSU_PA6_PWM_PIN_TEST) { __WFI(); }\n    else PsuApp_Poll();\n  }')
    s = user(s, 'Error_Handler_Debug', '  PsuApp_EmergencyOff();\n  __disable_irq();\n  while (1) { }')
    s = user(s, 'ADC2_Init 2', '''  /* PA3 tip temperature: injected conversion, regular PA0 remains NTC.
     JL=0 uses JSQ4; JEXTSEL=111 selects JSWSTART (RM0008). */
  ADC2->JSQR = 3u << ADC_JSQR_JSQ4_Pos;
  ADC2->SMPR2 |= ADC_SMPR2_SMP3;
  ADC2->CR2 = (ADC2->CR2 & ~ADC_CR2_JEXTSEL) | ADC_INJECTED_SOFTWARE_START;''')
    s=re.sub(r'  if \(HAL_TIM_PWM_ConfigChannel\(&htim1, &sConfigOC, TIM_CHANNEL_3\) != HAL_OK\)\n  \{\n    Error_Handler\(\);\n  \}\n','',s)
    s=s.replace('TIM_OSSR_DISABLE','TIM_OSSR_ENABLE').replace('TIM_OSSI_DISABLE','TIM_OSSI_ENABLE')
    s=re.sub(r'  /\* Unused touch IRQ:.*?HAL_GPIO_Init\(GPIOB, &GPIO_InitStruct\);\n','',s,flags=re.S)
    s=s.replace('  /* PA10 is TIM1_CH3 heater. Driver /SD is tied HIGH externally. */','')
    s=re.sub(r'  /\*Configure GPIO pin : SD_Pin \*/.*?HAL_GPIO_Init\(SD_GPIO_Port, &GPIO_InitStruct\);\n','',s,flags=re.S)
    s=s.replace('|stand_inp_Pin','').replace('stand_inp_Pin|','')
    s=user(s,'MX_GPIO_Init_2','''  /* Driver SD LOW; active-HIGH heater latched LOW (OFF). */
  HAL_GPIO_WritePin(SD_GPIO_Port,SD_Pin,GPIO_PIN_RESET);
  HAL_GPIO_WritePin(HEATER_GPIO_Port,HEATER_Pin,GPIO_PIN_RESET);
  GPIO_InitStruct.Pin=SD_Pin;
  GPIO_InitStruct.Mode=GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull=GPIO_NOPULL;
  GPIO_InitStruct.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SD_GPIO_Port,&GPIO_InitStruct);
  GPIO_InitStruct.Pin=HEATER_Pin;
  HAL_GPIO_Init(HEATER_GPIO_Port,&GPIO_InitStruct);
  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_13,GPIO_PIN_RESET);
  GPIO_InitStruct.Pin=GPIO_PIN_13;
  HAL_GPIO_Init(GPIOB,&GPIO_InitStruct);
  GPIO_InitStruct.Pin=GPIO_PIN_11;
  GPIO_InitStruct.Mode=GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull=GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB,&GPIO_InitStruct);''')
    s = re.sub(r'hadc1\.Init\.ContinuousConvMode = \w+;', 'hadc1.Init.ContinuousConvMode = ENABLE;', s)
    s = re.sub(r'hadc1\.Init\.ExternalTrigConv = \w+;', 'hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;', s)
    s = s.replace('ADC_SAMPLETIME_55CYCLES_5', 'ADC_SAMPLETIME_239CYCLES_5')
    s = s.replace('hadc2.Init.ContinuousConvMode = DISABLE;', 'hadc2.Init.ContinuousConvMode = ENABLE;')
    s = s.replace('sConfig.Channel = ADC_CHANNEL_1;\n  sConfig.Rank = ADC_REGULAR_RANK_3;', 'sConfig.Channel = ADC_CHANNEL_4;\n  sConfig.Rank = ADC_REGULAR_RANK_3;')
    s = re.sub(r'htim1\.Init\.Period = \d+;', 'htim1.Init.Period = 7199;', s)
    s = s.replace('htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;', 'htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;')
    s = re.sub(r'htim3\.Init\.Prescaler = \d+;', 'htim3.Init.Prescaler = 0;',s)
    s = re.sub(r'htim3\.Init\.Period = \d+;', 'htim3.Init.Period = 7199;',s)
    s = re.sub(r'htim4\.Init\.Prescaler = \d+;', 'htim4.Init.Prescaler = 71;',s)
    s = re.sub(r'htim4\.Init\.Period = \d+;', 'htim4.Init.Period = 999;',s)
    # CubeMX regeneration may replace the compact TIM3 initializer.
    a=s.index('static void MX_TIM3_Init(void)\n{');z=s.index('static void MX_TIM4_Init(void)\n{',a)
    s=s[:a]+s[a:z].replace('TIM_OCPOLARITY_LOW','TIM_OCPOLARITY_HIGH')+s[z:]

    s = re.sub(r'TIM_ENCODERMODE_TI12*\b', 'TIM_ENCODERMODE_TI12', s).replace('sConfig.IC1Filter = 0;', 'sConfig.IC1Filter = 8;').replace('sConfig.IC2Filter = 0;', 'sConfig.IC2Filter = 8;')
    s = s.replace('HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);','HAL_NVIC_SetPriority(EXTI4_IRQn, 4, 0);').replace('HAL_NVIC_SetPriority(EXTI9_5_IRQn, 0, 0);','HAL_NVIC_SetPriority(EXTI9_5_IRQn, 4, 0);').replace('HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);','HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);')
    s = s.replace('GPIO_MODE_IT_RISING;', 'GPIO_MODE_IT_RISING_FALLING;')
    # Touch is removed; legacy PB13 stays LOW, PA6 carries heater PWM.
    s = s.replace('GPIO_InitStruct.Pin = T_IRQ_Pin|GPIO_PIN_4', 'GPIO_InitStruct.Pin = GPIO_PIN_4')
    s = s.replace('HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);', 'HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);')
    return s
edit('Core/Src/main.c', main)
def msp(s):
    s=s.replace('__HAL_AFIO_REMAP_TIM3_DISABLE();','MODIFY_REG(AFIO->MAPR,AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG,AFIO_MAPR_SWJ_CFG_JTAGDISABLE);')
    s=s.replace('GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10;','GPIO_InitStruct.Pin = GPIO_PIN_8|GPIO_PIN_9;')
    s = s.replace('DMA_PRIORITY_LOW','DMA_PRIORITY_VERY_HIGH').replace('HAL_NVIC_SetPriority(TIM3_IRQn, 0, 0);','HAL_NVIC_SetPriority(TIM3_IRQn, 1, 0);').replace('HAL_NVIC_SetPriority(TIM2_IRQn, 0, 0);','HAL_NVIC_SetPriority(TIM2_IRQn, 4, 0);')

    # Do not briefly connect an unstarted timer to an active-HIGH driver.
    a=s.index('void HAL_TIM_MspPostInit');z=s.index('void HAL_TIM_Base_MspDeInit',a)
    part=s[a:z]
    start=part.index('else if(htim->Instance==TIM3)') if 'else if(htim->Instance==TIM3)' in part else -1
    if start>=0:
        part=part[:start]+part[start:].replace('GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;','GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;').replace('    GPIO_InitStruct.Pin = GPIO_PIN_6;', '    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,GPIO_PIN_RESET);\n    GPIO_InitStruct.Pin = GPIO_PIN_6;')
        # Normalize the pre-latch so running integration twice is idempotent.
        part=part.replace('    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,GPIO_PIN_RESET);\n    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,GPIO_PIN_RESET);','    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,GPIO_PIN_RESET);')
    s=s[:a]+part+s[z:]
    if 'TIM3_MspPostInit 0' in s:
        s=user(s,'TIM3_MspPostInit 1', '''  /* Keep active-HIGH heater LOW until the app authorizes PWM. */
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_6,GPIO_PIN_RESET);
    GPIO_InitStruct.Pin=GPIO_PIN_6;GPIO_InitStruct.Mode=GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed=GPIO_SPEED_FREQ_LOW;HAL_GPIO_Init(GPIOA,&GPIO_InitStruct);
    MODIFY_REG(AFIO->MAPR,AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG,AFIO_MAPR_SWJ_CFG_JTAGDISABLE);''')
    # Encoder inputs only; SPI1 TX-only still conflicts with PA6 AF on STM32F1.
    start=s.index('void HAL_TIM_Encoder_MspInit');end=s.index('void HAL_TIM_MspPostInit',start)
    s=s[:start]+s[start:end].replace('GPIO_NOPULL','GPIO_PULLUP')+s[end:]
    return s
edit('Core/Src/stm32f1xx_hal_msp.c',msp)
def header(s):
    s=re.sub(r'#define (?:SD|HEATER|T_IRQ)_(?:Pin|GPIO_Port|EXTI_IRQn) .*\n','',s)
    return user(s,'Private defines','#define SD_Pin GPIO_PIN_10\n#define SD_GPIO_Port GPIOA\n#define HEATER_Pin GPIO_PIN_6\n#define HEATER_GPIO_Port GPIOA')
edit('Core/Inc/main.h',header)
def interrupts(s):
    s=user(s,'Includes','#include "psu_app.h"')
    for name in ['NonMaskableInt_IRQn 0','HardFault_IRQn 0','MemoryManagement_IRQn 0','BusFault_IRQn 0','UsageFault_IRQn 0']:
        s=user(s,name,'  PsuApp_EmergencyOff();')
    s=user(s,'SysTick_IRQn 1','  PsuApp_SysTick();')
    s=s.replace('HAL_GPIO_EXTI_IRQHandler(T_IRQ_Pin);','PsuApp_EmergencyOff(); /* PB13 heater is not an EXTI input. */')
    return user(s,'DMA1_Channel1_IRQn 0', '  if (__HAL_DMA_GET_FLAG(&hdma_adc1, DMA_FLAG_HT1) &&\n      __HAL_DMA_GET_FLAG(&hdma_adc1, DMA_FLAG_TC1)) {\n    HAL_ADC_ErrorCallback((ADC_HandleTypeDef *)hdma_adc1.Parent);\n  }')
edit('Core/Src/stm32f1xx_it.c',interrupts)
edit('USB_DEVICE/Target/usbd_conf.c',lambda s:s.replace('HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 0, 0);','HAL_NVIC_SetPriority(USB_LP_CAN1_RX0_IRQn, 3, 0);'))
def cdc(s):
    s=user(s,'INCLUDE','#include "psu_app.h"')
    s=user(s,'PRIVATE_VARIABLES','static uint8_t line_coding[7] = {0x00,0xC2,0x01,0x00,0x00,0x00,0x08};')
    s=s.replace('case CDC_SET_LINE_CODING:\n\n', 'case CDC_SET_LINE_CODING:\n      if (length == 7) for (unsigned k=0;k<7;++k) line_coding[k]=pbuf[k];\n\n')
    s=s.replace('case CDC_GET_LINE_CODING:\n\n', 'case CDC_GET_LINE_CODING:\n      if (length == 7) for (unsigned k=0;k<7;++k) pbuf[k]=line_coding[k];\n\n')
    s=user(s,'6','  PsuApp_UsbRx(Buf, *Len);\n  USBD_CDC_SetRxBuffer(&hUsbDeviceFS, Buf);\n  USBD_CDC_ReceivePacket(&hUsbDeviceFS);\n  return USBD_OK;')
    return user(s,'7','  USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;\n  if (hUsbDeviceFS.dev_state != USBD_STATE_CONFIGURED || !hcdc) return USBD_FAIL;\n  if (hcdc->TxState != 0) return USBD_BUSY;\n  USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);\n  result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);')
edit('USB_DEVICE/App/usbd_cdc_if.c',cdc)
edit('USB_DEVICE/App/usbd_cdc_if.h',lambda s:re.sub(r'APP_RX_DATA_SIZE  \d+','APP_RX_DATA_SIZE  128',re.sub(r'APP_TX_DATA_SIZE  \d+','APP_TX_DATA_SIZE  512',s)))
def project(s):
    s=re.sub(r'<File>\s*<FileName>psu_touch.c</FileName>.*?</File>\s*','',s,flags=re.S)
    for name in ['psu_control','psu_app','psu_display','psu_storage','psu_settings','psu_remote','psu_modes']:
        if f'{name}.c</FileName>' not in s:
            target='<FileName>main.c</FileName>'
            at=s.index('<File>',s.index(target)) if False else s.rfind('<File>',0,s.index(target))
            s=s[:at]+f'<File><FileName>{name}.c</FileName><FileType>1</FileType><FilePath>../Core/Src/{name}.c</FilePath></File>\n            '+s[at:]
    # Reserve final two 1 KiB pages. Both Keil's memory definitions and linker UI.
    s=s.replace('IROM(0x8000000-0x800FFFF)','IROM(0x8000000-0x800F7FF)')
    s=s.replace('<Size>0x10000</Size>','<Size>0xF800</Size>')
    s=s.replace('<umfTarg>1</umfTarg>','<umfTarg>0</umfTarg>')
    s=s.replace('<ScatterFile />','<ScatterFile>psu.sct</ScatterFile>')
    # ArmClang: Keil Optim=6 selects -Os. -O3/full libc overflows the C8 Flash.
    s=re.sub(r'<Optim>\d+</Optim>', '<Optim>6</Optim>', s)
    # Prefer minimum size; retain IEEE float semantics (no fast-math).
    s=re.sub(r'(<Cads>.*?<MiscControls>)(.*?)(</MiscControls>)',
             lambda m:m[1]+m[2]+('' if '-Oz' in m[2].split() else ' -Oz')+m[3],s,count=1,flags=re.S)

    s=re.sub(r'<useUlib>\d+</useUlib>', '<useUlib>1</useUlib>', s)
    # V4 settings and legacy readers must stay below the 62 KiB boundary.
    s=re.sub(r'<v6Lto>\d+</v6Lto>', '<v6Lto>1</v6Lto>', s)
    return s
edit('MDK-ARM/Firmware_BB_V6.uvprojx',project)
def startup(s):
    s=re.sub(r'(Stack_Size\s+EQU\s+)0x0*(?:400|800)\b',r'\g<1>0xC00',s)
    target='USBWakeUp_IRQHandler\n\n                B       .'
    s=s.replace(target,'USBWakeUp_IRQHandler\n\n                IMPORT  PsuApp_EmergencyOff\n                BL      PsuApp_EmergencyOff\n                B       .')
    return s
edit('MDK-ARM/startup_stm32f103xb.s',startup)
def ioc(s):
    s=re.sub(r'^SH\.GPXTI13\..*\n','',s,flags=re.M)
    if not re.search(r'^Mcu.IP\d+=TIM4$',s,re.M):
        n=int(re.search(r'^Mcu.IPNb=(\d+)',s,re.M)[1]);s+=f'Mcu.IP{n}=TIM4\n'
        s=re.sub(r'^Mcu.IPNb=\d+',f'Mcu.IPNb={n+1}',s,flags=re.M)
    if 'VP_TIM4_VS_ClockSourceINT.Signal=' not in s:
        n=int(re.search(r'^Mcu.PinsNb=(\d+)',s,re.M)[1]);s+=f'Mcu.Pin{n}=VP_TIM4_VS_ClockSourceINT\n'
        s=re.sub(r'^Mcu.PinsNb=\d+',f'Mcu.PinsNb={n+1}',s,flags=re.M)
        s+='VP_TIM4_VS_ClockSourceINT.Mode=Internal\nVP_TIM4_VS_ClockSourceINT.Signal=TIM4_VS_ClockSourceINT\n'

    values={
      'PA10.Signal':'GPIO_Output','PA10.GPIO_Label':'SD',
      'PB11.Signal':'GPIO_Input','PB11.GPIO_Label':'solder_stand','PB11.GPIO_PuPd':'GPIO_PULLUP',
      'PB11.GPIOParameters':'GPIO_Label,GPIO_PuPd',
      'TIM1.OffStateRunMode':'TIM_OSSR_ENABLE','TIM1.OffStateIDLEMode':'TIM_OSSI_ENABLE',
      'ADC1.Channel-2\\#ChannelRegularConversion':'ADC_CHANNEL_4',
      'ADC1.ContinuousConvMode':'ENABLE','ADC1.ExternalTrigConv':'ADC_SOFTWARE_START',
      'ADC1.SamplingTime-0\\#ChannelRegularConversion':'ADC_SAMPLETIME_239CYCLES_5',
      'ADC1.SamplingTime-1\\#ChannelRegularConversion':'ADC_SAMPLETIME_239CYCLES_5',
      'ADC1.SamplingTime-2\\#ChannelRegularConversion':'ADC_SAMPLETIME_239CYCLES_5',
      'ADC2.ContinuousConvMode':'ENABLE','Dma.ADC1.0.Priority':'DMA_PRIORITY_VERY_HIGH',
      'TIM1.Prescaler':'0','TIM1.Period':'7199','TIM1.AutoReloadPreload':'TIM_AUTORELOAD_PRELOAD_ENABLE',
      'TIM2.EncoderMode':'TIM_ENCODERMODE_TI12','TIM2.IC1Filter':'8','TIM2.IC2Filter':'8',
      'TIM3.Prescaler':'0','TIM3.Period':'7199','TIM3.AutoReloadPreload':'TIM_AUTORELOAD_PRELOAD_ENABLE',
      'TIM4.Prescaler':'71','TIM4.Period':'999',
      'SPI1.Direction':'SPI_DIRECTION_1LINE',
      'PA5.Mode':'Half_Duplex_Master','PA7.Mode':'Half_Duplex_Master',
      'PA6.Mode':'PWM Generation1 CH1','PA6.Signal':'S_TIM3_CH1',
      'TIM3.Channel-PWM Generation1 CH1':'TIM_CHANNEL_1','TIM3.OCPolarity_1':'TIM_OCPOLARITY_HIGH','TIM3.Pulse-PWM Generation1 CH1':'0',
      'SH.S_TIM3_CH1.0':'TIM3_CH1,PWM Generation1 CH1','SH.S_TIM3_CH1.ConfNb':'1',
      'NVIC.TIM3_IRQn':r'false\:1\:0\:false\:false\:true\:true\:true\:true',
      'NVIC.TIM4_IRQn':r'true\:1\:0\:false\:false\:true\:true\:true\:true',
      'ProjectManager.StackSize':'0xC00',
      'PB13.GPIOParameters':'GPIO_Label,GPIO_PuPd','PB13.GPIO_Label':'HEATER_LEGACY_OFF','PB13.Signal':'GPIO_Output','PB13.GPIO_PuPd':'GPIO_NOPULL',
      'NVIC.EXTI15_10_IRQn':r'false\:5\:0\:false\:false\:true\:true\:true\:true',
    }
    for key,value in values.items():
        pattern=r'^'+re.escape(key)+r'=.*$'
        if re.search(pattern,s,re.M):s=re.sub(pattern,lambda m:key+'='+value,s,flags=re.M)
        else:s+=key+'='+value+'\n'
        peripheral,param=key.split('.',1)
        ipkey=peripheral+'.IPParameters'
        if ipkey+'=' in s and peripheral in ['ADC1','ADC2','TIM1','TIM2','TIM3','TIM4']:
            s=re.sub(r'^'+re.escape(ipkey)+r'=(.*)$', lambda m:ipkey+'='+m[1]+('' if param in m[1].split(',') else ','+param),s,flags=re.M)
    for peripheral,params in [('TIM2','EncoderMode,IC1Filter,IC2Filter'),('TIM3','Prescaler,Period,AutoReloadPreload,Channel-PWM Generation1 CH1'),('TIM4','Prescaler,Period')]:
        if peripheral+'.IPParameters=' not in s:s+=peripheral+'.IPParameters='+params+'\n'
    for irq,priority in [('TIM4',1),('USB_LP_CAN1_RX0',3),('EXTI4',4),('EXTI9_5',4),('TIM2',4)]:
        s=s.replace('NVIC.'+irq+'_IRQn=true\\:0\\:0', 'NVIC.'+irq+'_IRQn=true\\:'+str(priority)+'\\:0')
    for pin in ['PB4','PB5','PB6','PB7','PB8','PB9']:
        for key,value in [('GPIOParameters','GPIO_PuPd,GPIO_ModeDefaultEXTI'),('GPIO_PuPd','GPIO_PULLUP'),('GPIO_ModeDefaultEXTI','GPIO_MODE_IT_RISING_FALLING')]:
            s=re.sub(r'^'+pin+r'\.'+key+r'=.*\n','',s,flags=re.M);s+=pin+'.'+key+'='+value+'\n'
    s=re.sub(r'^PA10.GPIOParameters=.*$', 'PA10.GPIOParameters=GPIO_Label',s,flags=re.M)
    if not re.search(r'^Mcu.Pin\d+=PB11$',s,re.M):
        n=int(re.search(r'^Mcu.PinsNb=(\d+)',s,re.M)[1]);s+=f'Mcu.Pin{n}=PB11\n';s=re.sub(r'^Mcu.PinsNb=\d+',f'Mcu.PinsNb={n+1}',s,flags=re.M)
    s=re.sub(r'^(?:SH\.S_TIM1_CH3\..*|TIM1\.Channel-PWM Generation3 CH3=.*)\n','',s,flags=re.M)
    s=s.replace(',Channel-PWM Generation3 CH3','')
    return s
edit('Firmware_BB_V6.ioc',ioc)
print('CubeMX integration complete.')
