/* Real application/control code with only hardware and USB transport stubbed. */
#include "psu_app.h"
#include "usbd_cdc_if.h"
#include <string.h>
ADC_HandleTypeDef hadc1,hadc2;
TIM_HandleTypeDef htim1,htim2,htim3,htim4;
SPI_HandleTypeDef hspi1={.Instance=SPI1};
USBD_HandleTypeDef hUsbDeviceFS;
static USBD_CDC_HandleTypeDef cdc;
static uint16_t *dma;
static uint32_t now;
static unsigned dma_at,adc_elapsed_us;
static uint16_t buttons,tip_code=1273,ntc=3413,vout_code,current_code,vin_code=1295;
static char last_tx[2304];
static unsigned saved_bad;
static unsigned sleep_pin_reads,sleep_pin_reads_heating;
static unsigned buzzer_rises,buzzer_falls;
volatile uint32_t test_count,test_failed,test_lines[256];
#define CHECK(x) do{uint32_t pass=(x)?1:0;test_lines[test_count++]=pass?0:__LINE__;if(!pass)++test_failed;}while(0)
void Error_Handler(void){for(;;){}}
uint32_t HAL_GetTick(void){return now;}
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *g,uint16_t pin){
  if(g==GPIOB && pin==GPIO_PIN_11){
    ++sleep_pin_reads;
    if((TIM4->CCER&TIM_CCER_CC4E) && (TIM4->CCMR2&TIM_CCMR2_OC4M)==(TIM_OCMODE_PWM1<<8) && TIM4->CCR4)++sleep_pin_reads_heating;
  }
  return (buttons&pin)?GPIO_PIN_RESET:GPIO_PIN_SET;}
void HAL_GPIO_WritePin(GPIO_TypeDef *g,uint16_t pin,GPIO_PinState s){
 if(g==GPIOC && pin==GPIO_PIN_14){if(s && !(g->ODR&pin))++buzzer_rises;if(!s && (g->ODR&pin))++buzzer_falls;}
 if(s){g->ODR|=pin;g->IDR|=pin;}else{g->ODR&=~pin;g->IDR&=~pin;}}
void HAL_GPIO_Init(GPIO_TypeDef *g,GPIO_InitTypeDef *i){
  if(i->Mode==GPIO_MODE_OUTPUT_PP || i->Mode==GPIO_MODE_AF_PP)for(unsigned n=0;n<16;++n)if(i->Pin&(1u<<n)){
    volatile uint32_t *cr=n<8?&g->CRL:&g->CRH;unsigned shift=(n&7u)*4u;
    *cr=(*cr&~(15u<<shift))|((i->Speed|(i->Mode==GPIO_MODE_AF_PP?8u:0u))<<shift);
  }
}
void HAL_NVIC_SetPriority(IRQn_Type i,uint32_t p,uint32_t sub){(void)i;(void)p;(void)sub;}
void HAL_NVIC_EnableIRQ(IRQn_Type i){(void)i;}
void HAL_NVIC_DisableIRQ(IRQn_Type i){(void)i;}
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *a){(void)a;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *a){(void)a;return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *a,uint32_t *p,uint32_t n){dma=(uint16_t *)p;dma_at=0;DMA1_Channel1->CNDTR=n;a->Instance->CR2=ADC_CR2_ADON|ADC_CR2_DMA|ADC_CR2_EXTTRIG|a->Init.ExternalTrigConv|(a->Init.ContinuousConvMode?ADC_CR2_CONT:0);return HAL_OK;}
HAL_StatusTypeDef HAL_ADC_Stop_DMA(ADC_HandleTypeDef *a){a->Instance->CR2&=~(ADC_CR2_ADON|ADC_CR2_DMA|ADC_CR2_EXTTRIG);return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Encoder_Start(TIM_HandleTypeDef *t,uint32_t c){(void)t;(void)c;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *t){(void)t;return HAL_OK;}
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *t){(void)t;return HAL_OK;}
void PsuDisplay_Init(void){}
void PsuDisplay_Update(const PsuView *v){(void)v;}
int PsuStorage_Load(PsuPreset p[3]){p[0]=(PsuPreset){12000,12000};p[1]=(PsuPreset){5000,15000};p[2]=(PsuPreset){24000,2000};return 1;}
int PsuStorage_Save(const PsuPreset p[3]){for(unsigned i=0;i<3;++i)if(p[i].milliamps>8000)saved_bad=1;return 1;}
int PsuStorage_LoadAll(PsuPreset p[3],PsuSettings *cfg){
  PsuSettings_Defaults(cfg);
#ifndef TEST_FACTORY_CONFIG
  /* Existing Flash profiles retain their previous safe limits after upgrade. */
  cfg->vin_max=40;cfg->vout_max=36;cfg->i_max=8;cfg->i_peak=10;cfg->power_max=60;cfg->input_current_max=5;
#endif
  return PsuStorage_Load(p);
}
int PsuStorage_SaveAll(const PsuPreset p[3],const PsuSettings *cfg){(void)cfg;return PsuStorage_Save(p);}
uint8_t CDC_Transmit_FS(uint8_t *p,uint16_t n){if(n>=sizeof(last_tx))return USBD_FAIL;memcpy(last_tx,p,n);last_tx[n]=0;return USBD_OK;}
static void frame(void){for(unsigned k=0;k<24;k+=3){dma[k]=0;dma[k+1]=0;dma[k+2]=1295;}}
static int sampling_active(void){
  return (ADC1->CR2&(ADC_CR2_ADON|ADC_CR2_CONT|ADC_CR2_DMA|ADC_CR2_EXTTRIG|ADC_CR2_EXTSEL))==
      (ADC_CR2_ADON|ADC_CR2_CONT|ADC_CR2_DMA|ADC_CR2_EXTTRIG|ADC_SOFTWARE_START);
}
static void sample(void){
  if(!sampling_active())return;
  /* Three channels, (239.5+12.5)*3 cycles at 12 MHz: 63 us/scan.
     Continuous ADC has no dependency on any PWM timer register. */
  dma[dma_at++]=vout_code;dma[dma_at++]=current_code;dma[dma_at++]=vin_code;
  DMA1_Channel1->CNDTR=24-dma_at;
  if(dma_at==12)HAL_ADC_ConvHalfCpltCallback(&hadc1);
  if(dma_at==24){dma_at=0;DMA1_Channel1->CNDTR=24;HAL_ADC_ConvCpltCallback(&hadc1);}
}
static void run(uint16_t keys,unsigned ms){
  buttons=keys;
  while(ms--){
    /* Each 1 ms step includes the timer's 50 us OPM update. PWM2 ends
       inactive with CNT=0; this is peripheral behavior, not application code. */
    if((TIM1->CR1&(TIM_CR1_CEN|TIM_CR1_OPM))==(TIM_CR1_CEN|TIM_CR1_OPM)){
      TIM1->CNT=0;TIM1->CR1&=~TIM_CR1_CEN;
    }
    adc_elapsed_us+=1000;while(adc_elapsed_us>=63){adc_elapsed_us-=63;sample();}if(ADC2->CR2&ADC_CR2_JSWSTART){ADC2->CR2&=~ADC_CR2_JSWSTART;ADC2->JDR1=tip_code;ADC2->SR|=ADC_SR_JEOC;}ADC2->SR|=ADC_SR_EOC;ADC2->DR=ntc;++now;PsuApp_SysTick();HAL_TIM_PeriodElapsedCallback(&htim3);PsuApp_Poll();
  }
}
static void tap(uint16_t pin){run(pin,60);run(0,60);}
static void clear_fault_via_usb(void){
  /* Long ON/OFF now changes mode, so regression fault clears use USB CLEAR. */
  uint8_t state=hUsbDeviceFS.dev_state;void *klass=hUsbDeviceFS.pClassData;
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;
  run(0,1100);const uint8_t clear[]="@987654 CLEAR\n";PsuApp_UsbRx(clear,sizeof(clear)-1);run(0,60);
  hUsbDeviceFS.dev_state=state;hUsbDeviceFS.pClassData=klass;
}
static void check_idle(void){
  CHECK(TIM1->BDTR&TIM_BDTR_MOE);CHECK(GPIOA->BSRR==SD_Pin && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  CHECK(TIM1->CCR1==0 && TIM1->CCR2==0);
  CHECK((TIM1->CCMR1&(TIM_CCMR1_OC1M|TIM_CCMR1_OC2M))==
      (TIM_OCMODE_FORCED_INACTIVE|(TIM_OCMODE_FORCED_INACTIVE<<8)));
}
int test_main(void){
  PsuView s;
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;htim3.Instance=TIM3;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  PsuApp_Init();PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled);CHECK(!s.control.config_errors);CHECK(!s.control.fault);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));CHECK(sampling_active());
  CHECK(GPIOA->BRR==SD_Pin && !(TIM1->CCER&TIM_CCER_CC3E) && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  CHECK(s.presets[0].milliamps==8000 && s.presets[1].milliamps==8000);
  ADC1->CR2&=~ADC_CR2_CONT;run(0,30);PsuApp_Snapshot(&s);
  CHECK(!s.control.samples);CHECK(s.control.temp_valid);ADC1->CR2|=ADC_CR2_CONT;
  run(0,160);PsuApp_Snapshot(&s);CHECK(s.control.ready_ms>=100);CHECK(!s.control.enabled);
  CHECK(s.control.settings.ntc_r25==50000.0f && s.control.settings.ntc_pull==10000.0f);
  CHECK(s.control.temp_c>24.8f && s.control.temp_c<25.2f);
  CHECK(s.control.samples>=2536 && s.control.samples<=2540);CHECK(s.control.adc_age_ms<3);
  check_idle();
  CHECK(s.discharge_state==PSU_D_IDLE && !s.discharge_pulses);
  /* An unused input, even held, cannot arm output. */
  run(GPIO_PIN_12,1100);run(0,60);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled && s.edit_digit==0 && !s.edit_current);
  CHECK(GPIOB->ODR&T_CS_Pin);
#if PSU_LOW_SIDE_TEST
  CHECK(TIM1->BDTR&TIM_BDTR_MOE);CHECK(GPIOA->BSRR==SD_Pin && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  CHECK(TIM1->CCR1==0 && TIM1->CCR2==0);CHECK(TIM1->EGR==TIM_EGR_UG);
  /* Button ON, preset edits and encoder movement never start switching. */
  tap(GPIO_PIN_13);tap(GPIO_PIN_7);TIM2->CNT+=4000;run(0,10);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled && !s.control.fault);CHECK(TIM1->CCR1==0 && TIM1->CCR2==0);
  CHECK(TIM1->BDTR&TIM_BDTR_MOE);
  /* Zero PWM must be actively driven; timer stop or stale ADC removes SD. */
  TIM1->CR1&=~TIM_CR1_CEN;run(0,1);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  TIM1->CR1|=TIM_CR1_CEN;run(0,1);CHECK(TIM1->BDTR&TIM_BDTR_MOE);
  ADC1->CR2&=~ADC_CR2_CONT;run(0,6);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  ADC1->CR2|=ADC_CR2_CONT;run(0,160);CHECK(TIM1->BDTR&TIM_BDTR_MOE);
  ntc=0;run(0,30);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  ntc=3413;run(0,160);CHECK(TIM1->BDTR&TIM_BDTR_MOE);
  /* Two consecutive peak samples still latch all-driver shutdown. */
  frame();dma[10]=2507;HAL_ADC_ConvHalfCpltCallback(&hadc1);
  PsuApp_Snapshot(&s);CHECK(!s.control.fault);
  dma[13]=2507;HAL_ADC_ConvCpltCallback(&hadc1);PsuApp_Snapshot(&s);
  CHECK(s.control.fault&PSU_F_OCP);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  run(0,200);tap(GPIO_PIN_13);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  clear_fault_via_usb();PsuApp_Snapshot(&s);
  CHECK(!s.control.fault && !s.control.enabled);CHECK(TIM1->BDTR&TIM_BDTR_MOE);
  /* VIN overvoltage also shuts down in this test, with no regulator RUN. */
  vin_code=2200;run(0,1);PsuApp_Snapshot(&s);CHECK(s.control.fault&PSU_F_VIN_OV);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  vin_code=1295;run(0,160);clear_fault_via_usb();
  CHECK(TIM1->BDTR&TIM_BDTR_MOE);
  /* Foreground stalled: TIM4 must remove SD even though enabled stays 0. */
  now+=300;HAL_TIM_PeriodElapsedCallback(&htim3);PsuApp_Snapshot(&s);
  CHECK(s.control.fault&PSU_F_TIMING);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  CHECK(TIM1->CCR1==0 && TIM1->CCR2==0);
#elif PSU_DISCHARGE_ENABLED
  CHECK((s.discharge_state==PSU_D_IDLE || s.discharge_state==PSU_D_DONE) && !s.discharge_pulses);
  vout_code=268;vin_code=809;run(0,160);tap(GPIO_PIN_13);run(0,160);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && !s.control.fault);CHECK(!(TIM1->CR1&TIM_CR1_OPM));
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
  CHECK(s.discharge_state==PSU_D_ACTIVE && s.discharge_pulses>0);
  CHECK(TIM1->CR1&TIM_CR1_OPM);CHECK(TIM1->RCR==0);
  CHECK(TIM1->CCR1==0 && TIM1->CCR2>0);
  CHECK((TIM1->CCMR1&(TIM_CCMR1_OC1M|TIM_CCMR1_OC2M))==
      (TIM_OCMODE_FORCED_INACTIVE|(TIM_OCMODE_PWM2<<8)));
  CHECK(s.discharge_ticks>=1390 && s.discharge_ticks<=1410);
  {uint32_t count=s.discharge_pulses;run(0,100);PsuApp_Snapshot(&s);CHECK(s.discharge_pulses==count+5);}
  CHECK(!(TIM1->CR1&TIM_CR1_CEN)); /* Counter stopped, both PWM2 inputs low. */
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;run(0,110);
  CHECK(strstr(last_tx,"\"drain\":{\"state\":2,")!=0);
  vout_code=25;run(0,3);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_DONE);
  CHECK(!(TIM1->CR1&TIM_CR1_OPM));check_idle();
  {uint32_t count=s.discharge_pulses;run(0,100);PsuApp_Snapshot(&s);CHECK(s.discharge_pulses==count);}
  /* A fresh ON works; a following OFF restarts pulse count. */
  vout_code=268;run(0,160);tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.control.enabled);
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_ACTIVE);
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.control.enabled && s.discharge_state==PSU_D_IDLE);
  CHECK(!(TIM1->CR1&TIM_CR1_OPM));CHECK(TIM1->CR1&TIM_CR1_CEN);
  CHECK((TIM1->CCMR1&(TIM_CCMR1_OC1M|TIM_CCMR1_OC2M))==(TIM_OCMODE_PWM1|(TIM_OCMODE_PWM1<<8)));
  /* No progress / external voltage source: bounded stop, no auto retry. */
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_ACTIVE);
  now+=PSU_DISCHARGE_PROGRESS_MS;PsuApp_Poll();run(0,1);PsuApp_Snapshot(&s);
  CHECK(s.discharge_state==PSU_D_NO_DROP);check_idle();
  /* Voltage rising must stop the sink. */
  tap(GPIO_PIN_13);tap(GPIO_PIN_13);vout_code=297;run(0,2);PsuApp_Snapshot(&s);
  CHECK(s.discharge_state==PSU_D_RISING);CHECK(!s.control.fault);check_idle();
  vout_code=268;run(0,160);tap(GPIO_PIN_13);tap(GPIO_PIN_13);
  now+=PSU_DISCHARGE_MAX_MS;PsuApp_Poll();run(0,1);PsuApp_Snapshot(&s);
  CHECK(s.discharge_state==PSU_D_TIMEOUT);check_idle();
  /* Sensor loss aborts, leaves SD low, and cannot resume the old discharge. */
  tap(GPIO_PIN_13);tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_ACTIVE);
  ADC1->CR2&=~ADC_CR2_CONT;run(0,6);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_ABORTED);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));CHECK(!(TIM1->CR1&TIM_CR1_OPM));
  ADC1->CR2|=ADC_CR2_CONT;run(0,160);PsuApp_Snapshot(&s);
  CHECK(s.discharge_state==PSU_D_ABORTED);check_idle();
  /* At 36 V the adaptive shot is much shorter than the 20 us cap. */
  TIM2->CNT-=12400;run(0,3);vout_code=1942;run(0,160);tap(GPIO_PIN_13);
  PsuApp_Snapshot(&s);CHECK(s.control.voltage_set==36.0f && s.control.enabled);
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_ACTIVE);
  CHECK(s.discharge_ticks>=190 && s.discharge_ticks<=196);
  vout_code=25;run(0,3);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_DONE);
  /* OFF after real load current must WAIT for healthy settling, not cancel
     or fire immediately. The first shot also waits 20 ms at zero PWM. */
  vout_code=268;run(0,160);tap(GPIO_PIN_13);current_code=100;run(0,10);tap(GPIO_PIN_13);
  PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_WAIT && !s.discharge_pulses);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));current_code=0;run(0,80);
  PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_WAIT && !s.discharge_pulses);
  run(0,50);PsuApp_Snapshot(&s);CHECK(s.discharge_state==PSU_D_ACTIVE && s.discharge_pulses>0);
  /* A timer that fails to stop in OPM is a latched timing fault. */
  now+=20;PsuApp_Poll();TIM1->CR1|=TIM_CR1_CEN;HAL_TIM_PeriodElapsedCallback(&htim3);
  PsuApp_Snapshot(&s);CHECK(s.control.fault&PSU_F_TIMING);CHECK(s.discharge_state==PSU_D_ABORTED);
  CHECK(s.timing_sources==PSU_TIMING_DISCHARGE_WAIT && s.timing_value>=20);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));run(0,160);clear_fault_via_usb();
  PsuApp_Snapshot(&s);CHECK(!s.control.fault);check_idle();
  /* Peak protection cancels shots without starting an automatic fault drain. */
  tap(GPIO_PIN_13);tap(GPIO_PIN_13);frame();dma[10]=2507;HAL_ADC_ConvHalfCpltCallback(&hadc1);
  dma[13]=2507;HAL_ADC_ConvCpltCallback(&hadc1);PsuApp_Snapshot(&s);
  CHECK(s.control.fault&PSU_F_OCP);CHECK(s.discharge_state==PSU_D_ABORTED);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));CHECK(!(TIM1->CR1&TIM_CR1_OPM));
  run(0,160);clear_fault_via_usb();PsuApp_Snapshot(&s);CHECK(!s.control.fault);
  CHECK(s.discharge_state==PSU_D_ABORTED);check_idle();
  /* Temperature loss during drain also aborts and does not auto resume. */
  tap(GPIO_PIN_13);tap(GPIO_PIN_13);ntc=0;run(0,30);PsuApp_Snapshot(&s);
  CHECK(s.discharge_state==PSU_D_ABORTED);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  ntc=3413;run(0,160);check_idle();
  /* Foreground watchdog applies during discharge too. */
  tap(GPIO_PIN_13);tap(GPIO_PIN_13);now+=300;HAL_TIM_PeriodElapsedCallback(&htim3);PsuApp_Snapshot(&s);
  CHECK(s.control.fault&PSU_F_TIMING);CHECK(s.discharge_state==PSU_D_ABORTED);
  CHECK((s.timing_sources&PSU_TIMING_UI) && s.timing_value>=300);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));CHECK(!(TIM1->CR1&TIM_CR1_OPM));
#else
  {uint32_t old=s.control.samples;TIM1->CR1&=~TIM_CR1_CEN;run(0,10);PsuApp_Snapshot(&s);
   CHECK(s.control.samples>old);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));TIM1->CR1|=TIM_CR1_CEN;}
  /* DMA publishes one block mean; TIM4 only consumes that completed result. */
  for(unsigned k=0;k<4;++k){dma[3*k]=100*k;dma[3*k+1]=500*k;dma[3*k+2]=1000+100*k;} /* gain 100: mean 750 counts is ~6.04A */
  HAL_ADC_ConvHalfCpltCallback(&hadc1);
  {float previous=s.control.vin_raw;PsuApp_Snapshot(&s);CHECK(s.control.vin_raw==previous);}
  HAL_TIM_PeriodElapsedCallback(&htim3);PsuApp_Snapshot(&s);
  CHECK(s.control.vout_raw>2.77f && s.control.vout_raw<2.79f);
  CHECK(s.control.current_raw>6.04f && s.control.current_raw<6.05f);
  CHECK(s.control.vin_raw>21.31f && s.control.vin_raw<21.32f);
  CHECK(s.control.continuous_i2>0.01f);
  /* 4.97 V output present before any ON request; 15 V VIN. */
  vout_code=268;vin_code=809;run(0,160);PsuApp_Snapshot(&s);
  CHECK(s.control.vout_raw>4.96f && s.control.vout_raw<4.99f);CHECK(s.control.ready_ms>=100);
  CHECK(!s.control.enabled && !s.control.start_block);
  run(GPIO_PIN_13,60);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
  run(0,60);PsuApp_Snapshot(&s);CHECK(s.control.enabled);CHECK(!s.control.start_block);
  run(0,180);CHECK(TIM1->BDTR&TIM_BDTR_MOE);
  CHECK(TIM1->CCR1>0 && TIM1->CCR2>0);
  CHECK((TIM1->CCMR1&(TIM_CCMR1_OC1M|TIM_CCMR1_OC2M))==
      (TIM_OCMODE_PWM1|(TIM_OCMODE_PWM1<<8)));
  TIM1->EGR=0;
  run(GPIO_PIN_13,30);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);check_idle();
  CHECK(TIM1->EGR==TIM_EGR_UG); /* Old nonzero preloads were replaced. */
  run(0,60);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
  run(0,1160);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
  tap(GPIO_PIN_7);PsuApp_Snapshot(&s);CHECK(s.control.current_set==8.0f);CHECK(!s.control.enabled);
  /* Reversed direction, digit selection and independent V/A selection. */
  CHECK(!s.edit_current && s.edit_digit==0);
  TIM2->CNT-=4;run(0,3);PsuApp_Snapshot(&s);
  CHECK(s.control.voltage_set>12.009f && s.control.voltage_set<12.011f);
  TIM2->CNT+=4;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.voltage_set>11.999f && s.control.voltage_set<12.001f);
  tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(!s.edit_current && s.edit_digit==1);
  TIM2->CNT-=4;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.voltage_set>12.099f && s.control.voltage_set<12.101f);
  tap(GPIO_PIN_4);TIM2->CNT-=4;run(0,3);PsuApp_Snapshot(&s);
  CHECK(s.edit_digit==2 && s.control.voltage_set>13.099f && s.control.voltage_set<13.101f);
  tap(GPIO_PIN_4);TIM2->CNT-=4;run(0,3);PsuApp_Snapshot(&s);
  CHECK(s.edit_digit==3 && s.control.voltage_set>23.099f && s.control.voltage_set<23.101f);
  tap(GPIO_PIN_8);PsuApp_Snapshot(&s);CHECK(s.edit_current && s.edit_digit==0);
  TIM2->CNT+=4;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.current_set>7.989f && s.control.current_set<7.991f);
  tap(GPIO_PIN_4);TIM2->CNT+=4;run(0,3);PsuApp_Snapshot(&s);CHECK(s.edit_current && s.edit_digit==1);
  CHECK(s.control.current_set>7.889f && s.control.current_set<7.891f);
  tap(GPIO_PIN_4);TIM2->CNT+=4;run(0,3);PsuApp_Snapshot(&s);
  CHECK(s.edit_digit==2 && s.control.current_set>6.889f && s.control.current_set<6.891f);
  tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.edit_current && s.edit_digit==0);
  tap(GPIO_PIN_8);PsuApp_Snapshot(&s);CHECK(!s.edit_current && s.edit_digit==3);
  tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(!s.edit_current && s.edit_digit==0);
  /* Counter wrap and partial counts still produce one detent, not a jump. */
  TIM2->CNT=1;run(0,3);tap(GPIO_PIN_7);tap(GPIO_PIN_4); /* clear partial detent; select 0.1 V */
  TIM2->CNT=65535;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.voltage_set>11.999f && s.control.voltage_set<12.001f);
  TIM2->CNT=65533;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.voltage_set>12.099f && s.control.voltage_set<12.101f);
  TIM2->CNT-=4000;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.voltage_set==36.0f);
  TIM2->CNT+=8000;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.voltage_set>=1.5f && s.control.voltage_set<1.501f);
  tap(GPIO_PIN_8);TIM2->CNT+=4000;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.current_set==0.05f);
  TIM2->CNT-=8000;run(0,3);PsuApp_Snapshot(&s);CHECK(s.control.current_set==8.0f && !s.control.enabled);
  tap(GPIO_PIN_7); /* Restore preset before the existing ON/protection fixtures. */
  run(GPIO_PIN_7,1100);run(0,60);CHECK(!saved_bad);
  check_idle(); /* Flash save/restart returns to clamped standby. */
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.control.enabled);run(0,180);
  /* A single >10A sample does not trip; two consecutive samples straddling
     DMA half/full callbacks do. Uses the actual application callbacks. */
  frame();dma[1]=2507;HAL_ADC_ConvHalfCpltCallback(&hadc1);PsuApp_Snapshot(&s);CHECK(!s.control.fault);
  frame();dma[10]=2507;HAL_ADC_ConvHalfCpltCallback(&hadc1);PsuApp_Snapshot(&s);CHECK(!s.control.fault);
  dma[13]=2507;HAL_ADC_ConvCpltCallback(&hadc1);PsuApp_Snapshot(&s);CHECK(s.control.fault&PSU_F_OCP);CHECK(!s.control.enabled);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  run(0,200);tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);CHECK(s.control.start_block&PSU_B_FAULT);
  clear_fault_via_usb();PsuApp_Snapshot(&s);CHECK(!s.control.fault);CHECK(!s.control.enabled);
  check_idle();
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.control.enabled);
  tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
  {uint32_t old=s.control.samples;run(0,10);PsuApp_Snapshot(&s);CHECK(s.control.samples>old);check_idle();}
  /* Losing the continuous stream must block ON and fault during RUN. */
  ADC1->CR2&=~ADC_CR2_CONT;run(0,6);tap(GPIO_PIN_13);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled);CHECK(s.control.start_block&PSU_B_ADC);CHECK(!s.control.fault);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  ADC1->CR2|=ADC_CR2_CONT;run(0,160);tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(s.control.enabled);CHECK(!s.control.start_block);
  ADC1->CR2&=~ADC_CR2_CONT;run(0,4);PsuApp_Snapshot(&s);CHECK(s.control.fault&PSU_F_ADC);CHECK(!s.control.enabled);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  ADC1->CR2|=ADC_CR2_CONT;run(0,160);clear_fault_via_usb();PsuApp_Snapshot(&s);CHECK(!s.control.fault);CHECK(!s.control.enabled);
  /* OVP diagnostics retain the exact triggering sample and its threshold
     after output turns off, across later DMA callbacks and USB logging. */
  tap(GPIO_PIN_6);run(0,160);tap(GPIO_PIN_13);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && s.control.voltage_set==5.0f);
  vout_code=334;run(0,1);PsuApp_Snapshot(&s);
  CHECK(s.control.fault&PSU_F_OVP);CHECK(!s.control.enabled);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  CHECK(s.control.fault_sample.cause==PSU_F_OVP);
  CHECK(s.control.fault_sample.vout_adc==334 && s.control.fault_sample.ovp_adc==310);
  {uint32_t sample=s.control.fault_sample.sample_index;
   hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;run(0,110);PsuApp_Snapshot(&s);
   CHECK(s.control.fault_sample.sample_index==sample);}
  CHECK(strstr(last_tx,"\"trip\":{\"cause\":2,")!=0);
  CHECK(strstr(last_tx,"\"vo_adc\":334,")!=0);CHECK(strstr(last_tx,"\"ovp_adc\":310}")!=0);
  vout_code=268;run(0,160);clear_fault_via_usb();PsuApp_Snapshot(&s);
  CHECK(!s.control.fault && !s.control.enabled);CHECK(!s.control.fault_sample.cause);
  ntc=0;run(0,30);tap(GPIO_PIN_13);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);CHECK(s.control.start_block&PSU_B_TEMP);CHECK(!s.control.fault);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;run(0,110);
  CHECK(strstr(last_tx,"\"imax_mA\":8000")!=0);CHECK(strstr(last_tx,"\"ipeak_mA\":10000")!=0);CHECK(strstr(last_tx,"\"start_block\":")!=0);
  CHECK(strstr(last_tx,"\"adc_src\":\"cont\"")!=0);CHECK(strstr(last_tx,"\"adc_age\":")!=0);
  ntc=3413;run(0,160);check_idle();
  /* OFF has active gate drive too: a stalled foreground must latch shutdown. */
  now+=300;HAL_TIM_PeriodElapsedCallback(&htim3);PsuApp_Snapshot(&s);
  CHECK(s.control.fault&PSU_F_TIMING);CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  CHECK((s.timing_sources&PSU_TIMING_UI) && s.timing_value>=300);
#endif
  return 0;
}
