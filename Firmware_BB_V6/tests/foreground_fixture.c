#define TEST_FACTORY_CONFIG 1
#define PsuDisplay_Init unused_display_init
#define PsuDisplay_Update unused_display_update
#define run run_base
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
#undef PsuDisplay_Init
#undef PsuDisplay_Update
#undef run
/* Run the real renderer, application, control and ADC path. Model a slow SPI
   transfer while TIM4, DMA and SysTick continue; the main poll cannot recurse. */
static unsigned spi_us,draw_spi_calls,draw_spi_bytes;
static uint8_t inject_usb_off,inject_button_off,observed_off;
static uint32_t inject_at,off_at;
volatile uint32_t foreground_max_draw_ms,foreground_usb_off_ms,foreground_button_off_ms;
static void observe_off(void){
  if(!observed_off){PsuView s;PsuApp_Snapshot(&s);if(!s.control.enabled && !s.modes.solder_request){observed_off=1;off_at=now;}}
}
static void run(uint16_t keys,unsigned ms){
  buttons=keys;
  while(ms--){run_base(buttons,1);observe_off();}
}
static void tick_without_main(void){
  adc_elapsed_us+=1000;
  while(adc_elapsed_us>=63){adc_elapsed_us-=63;sample();}
  if(ADC2->CR2&ADC_CR2_JSWSTART){ADC2->CR2&=~ADC_CR2_JSWSTART;ADC2->JDR1=tip_code;ADC2->SR|=ADC_SR_JEOC;}
  ADC2->SR|=ADC_SR_EOC;ADC2->DR=ntc;++now;
  PsuApp_SysTick();HAL_TIM_PeriodElapsedCallback(&htim3);
  if(inject_usb_off && now>=inject_at){
    const char off[]="@91 OFF\n";
    PsuApp_UsbRx((const uint8_t *)off,sizeof(off)-1);inject_usb_off=0;
  }
  if(inject_button_off && now>=inject_at){buttons=GPIO_PIN_13;inject_button_off=0;}
  observe_off();
}
void HAL_Delay(uint32_t ms){if(dma)while(ms--)tick_without_main();else now+=ms;}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi,const uint8_t *p,uint16_t n,uint32_t timeout){
  (void)spi;(void)p;(void)timeout;
  ++draw_spi_calls;draw_spi_bytes+=n;
  if(dma){
    spi_us+=n*2u+20u;
    while(spi_us>=1000u){spi_us-=1000u;tick_without_main();}
  }
  return HAL_OK;
}
int test_main(void){
  PsuView s;
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;htim3.Instance=TIM3;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  tip_code=1440; /* Near target: exercise redraws without a no-rise fault. */
  PsuApp_Init();run(0,160);PsuApp_Snapshot(&s);
  CHECK(!s.control.fault && s.control.initialized);
  run(GPIO_PIN_13,1100);run(0,60);tap(GPIO_PIN_13);run(0,60);vout_code=971;run(0,1800);PsuApp_Snapshot(&s);
  CHECK(s.max_display_ms>250); /* Reproduces the long SOLDER redraw. */
  foreground_max_draw_ms=s.max_display_ms;
  CHECK(s.modes.mode==PSU_MODE_SOLDER && s.control.enabled && s.modes.solder.phase==PSU_SOL_RUN);
  CHECK(!s.control.fault && !s.timing_sources && s.modes.solder.raw==tip_code);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;
  run(0,200);CHECK(strstr(last_tx,"\"ui_draw_ms\":") && strstr(last_tx,"\"pwm_hz\":10000"));
  /* A normal USB OFF line must be serviced during a long redraw too.
     Rotate target: PA3 readings intentionally stay frozen during long ON. */
  observed_off=0;inject_usb_off=1;inject_at=now+110;
  TIM2->CNT-=PSU_ENCODER_COUNTS;
  tip_code=1441;run(0,150);PsuApp_Snapshot(&s);
  CHECK(observed_off && off_at-inject_at<=10 && !s.control.enabled && !s.modes.solder_request);
  foreground_usb_off_ms=off_at-inject_at;
  CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !s.control.fault);
  /* A button OFF remains responsive while SPI consumes foreground time. */
  tap(GPIO_PIN_13);run(0,400);PsuApp_Snapshot(&s);CHECK(s.control.enabled);
  observed_off=0;inject_button_off=1;inject_at=now+110;
  TIM2->CNT-=PSU_ENCODER_COUNTS;
  tip_code=1442;run(0,150);PsuApp_Snapshot(&s);
  CHECK(observed_off && off_at-inject_at<=40 && !s.control.enabled && !s.modes.solder_request);
  foreground_button_off_ms=off_at-inject_at;
  buttons=0;run(0,100);
  /* Runtime LCD reset must preserve live control and service USB OFF. */
  tap(GPIO_PIN_13);run(0,400);PsuApp_Snapshot(&s);CHECK(s.control.enabled);
  observed_off=0;inject_usb_off=1;inject_at=now+1200;
  run(GPIO_PIN_8,1100);run(0,160);PsuApp_Snapshot(&s);
  CHECK(observed_off && off_at-inject_at<=10 && !s.control.enabled && !s.control.fault);
  /* No SPI/foreground progress: the original 250 ms watchdog still trips. */
  for(unsigned n=0;n<251;++n)tick_without_main();
  PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_TIMING) && (s.timing_sources&PSU_TIMING_UI));
  CHECK(s.timing_value==251 && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && GPIOA->BRR==SD_Pin);
  return 0;
}
