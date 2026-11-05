#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static PsuModes m;
static uint32_t clock;
static void feed(uint16_t raw,unsigned ticks){
  while(ticks--){PsuSolder_Sample(&m,raw,clock);PsuModes_Tick(&m,&p,clock++);}
}
static void ready(uint16_t raw){
  Psu_Init(&p);PsuModes_Init(&m);
  p.initialized=p.temp_valid=1;p.ready_ms=1000;p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;
  PsuModes_Select(&m,&p,PSU_MODE_SOLDER,100);
  for(clock=100;clock<700;++clock){
    if(p.enabled){p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;}
    PsuSolder_Sample(&m,raw,clock);PsuModes_Tick(&m,&p,clock);
  }
}
static void turn(int steps){TIM2->CNT=(uint16_t)(TIM2->CNT-steps*PSU_ENCODER_COUNTS);run(0,5);}
int test_main(void){
  PsuView s;
  CHECK(PSU_SOLDER_TEMP_PROTECTION==0);
  ready(0);CHECK(p.enabled && m.solder.valid && !m.solder.error && m.solder.raw==0);
  CHECK(m.solder.phase==PSU_SOL_RUN && m.solder.temperature==25);
  ready(4095);CHECK(p.enabled && m.solder.valid && !m.solder.error && m.solder.raw==4095);
  CHECK(!m.solder.duty); /* Existing PID still regulates with V5 calibration. */
  ready(1273);feed(1273,16000);CHECK(p.enabled && m.solder_request && !m.solder.error);
  ready(1273);feed(1600,2000);CHECK(p.enabled && !m.solder.error && !m.solder.duty);
  ready(1273);for(unsigned n=0;n<251;++n)PsuModes_Tick(&m,&p,clock++);
  CHECK(!p.enabled && m.solder.error==PSU_SOL_STALE);
  ready(1273);p.fault=PSU_F_OCP;PsuModes_Tick(&m,&p,clock++);CHECK(!p.enabled && !m.solder.duty);
  ready(1273);p.temp_c=p.settings.temp_trip;PsuModes_Tick(&m,&p,clock++);CHECK(!p.enabled && !m.solder_request);
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  PsuApp_Init();tip_code=0;run(0,160);run(GPIO_PIN_4,1100);run(0,60);turn(2);tap(GPIO_PIN_4);turn(3);tap(GPIO_PIN_4);
  run(0,500);vout_code=1295;run(0,3000);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && s.modes.solder.phase==PSU_SOL_RUN && !s.modes.solder.error && s.modes.solder.raw==0 && s.modes.solder.valid);
  tip_code=4095;run(0,150);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && !s.modes.solder.error && s.modes.solder.raw==4095 && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;run(0,110);
  CHECK(strstr(last_tx,"\"protect\":0") && strstr(last_tx,"\"adc\":4095"));
  PsuApp_UsbRx((const uint8_t *)"!",1);CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !(TIM1->BDTR&TIM_BDTR_MOE) && GPIOA->BRR==SD_Pin);
  run(0,200);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && !s.modes.solder_request);
  tip_code=1273;tap(GPIO_PIN_9);run(0,1000);current_code=3000;run(0,1);PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_OCP) && !s.control.enabled && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && GPIOA->BRR==SD_Pin);
  return 0;
}
