#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static void turn(int steps){TIM2->CNT=(uint16_t)(TIM2->CNT-steps*PSU_ENCODER_COUNTS);run(0,5);}
static void menu(void){run(GPIO_PIN_4,1100);run(0,60);}
static void enter(void){menu();turn(2);tap(GPIO_PIN_4);turn(3);tap(GPIO_PIN_4);}
static PsuControl p;
static PsuModes m;
static uint32_t clock;
static void ready(uint8_t tip){
  Psu_Init(&p);PsuModes_Init(&m);m.solder.tip=tip;
  p.initialized=p.temp_valid=1;p.ready_ms=1000;p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;
  PsuModes_Select(&m,&p,PSU_MODE_SOLDER,100);
  for(clock=100;clock<700;++clock){
    if(p.enabled){p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;}
    PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock);
  }
}
int test_main(void){
  PsuView s;
  ready(PSU_TIP_C245);CHECK(p.enabled && m.solder.phase==PSU_SOL_RUN);
  CHECK(p.voltage_set==24 && p.current_set==6 && m.discharge_inhibit);
  CHECK(m.solder.temperature>110 && m.solder.temperature<111 && m.solder.command<=0.95f);
  /* The reported cold reading must not latch SENSOR on either cartridge. */
  for(unsigned tip=0;tip<2;++tip){
    ready((uint8_t)tip);
    for(unsigned n=0;n<2000;++n){PsuSolder_Sample(&m,1200,clock);PsuModes_Tick(&m,&p,clock++);}
    CHECK(p.enabled && m.solder.valid && !m.solder.error && m.solder.temperature>29.9f && m.solder.temperature<30.1f);
  }
  ready(PSU_TIP_C210);CHECK(p.enabled && p.voltage_set==12 && p.current_set==4);
  PsuSolder_SetTarget(&m,100);CHECK(m.solder.target==150);PsuSolder_SetTarget(&m,800);CHECK(m.solder.target==450);
  m.solder.stand=1;for(unsigned n=0;n<331;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(m.solder.sleeping && m.solder.active_target==200);
  m.solder.stand=0;for(unsigned n=0;n<111;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!m.solder.sleeping && m.solder.active_target==450);
  PsuSolder_Sample(&m,1600,clock);PsuModes_Tick(&m,&p,clock++);CHECK(m.solder.error==PSU_SOL_HOT && !m.solder_request && !p.enabled && !m.solder.duty);
  for(unsigned n=0;n<300;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!p.enabled && !m.solder_request);
  ready(0);PsuSolder_Sample(&m,0,clock);PsuModes_Tick(&m,&p,clock++);CHECK(m.solder.error==PSU_SOL_SENSOR && !p.enabled && !m.solder.trip_adc);
  PsuSolder_Sample(&m,1373,clock);PsuModes_Tick(&m,&p,clock++);
  CHECK(m.solder.raw==1373 && m.solder.error==PSU_SOL_SENSOR && !m.solder.trip_adc && !p.enabled);
  ready(0);PsuSolder_Sample(&m,1100,clock);PsuModes_Tick(&m,&p,clock++);
  CHECK(m.solder.error==PSU_SOL_SENSOR && m.solder.trip_adc==1100 && !p.enabled);
  PsuSolder_Sample(&m,4095,clock);CHECK(m.solder.trip_adc==1100 && m.solder.error==PSU_SOL_SENSOR);
  ready(0);PsuSolder_Sample(&m,4095,clock);PsuModes_Tick(&m,&p,clock++);
  CHECK(m.solder.error==PSU_SOL_HOT && m.solder.trip_adc==4095 && !p.enabled);
  ready(0);for(unsigned n=0;n<251;++n)PsuModes_Tick(&m,&p,clock++);
  CHECK(!p.enabled && m.solder.error==PSU_SOL_STALE && !m.solder.valid);
  ready(0);m.solder.sum=m.solder.count=0;m.solder.frame_ms=clock-100;
  PsuModes_Tick(&m,&p,clock++);CHECK(p.enabled && !m.solder.valid && !m.solder.duty && !m.solder.error);
  PsuSolder_Sample(&m,1273,clock);PsuSolder_Sample(&m,1273,clock);clock+=100;
  PsuModes_Tick(&m,&p,clock++);CHECK(p.enabled && m.solder.valid && !m.solder.error);
  /* One sample per frame must not refresh the validated-mean timestamp. */
  ready(0);for(unsigned n=0;n<300;++n){if(n%100==0)PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!p.enabled && m.solder.error==PSU_SOL_STALE);
  ready(0);for(unsigned n=0;n<18000;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!p.enabled && m.solder.error==PSU_SOL_NO_RISE);
  ready(0);PsuModes_Stop(&m,&p);CHECK(!p.enabled && !m.solder.duty && !m.solder_request);
  CHECK(PsuModes_Select(&m,&p,PSU_MODE_LAB,clock));CHECK(!p.enabled && p.voltage_set==5 && p.current_set==1);
  ready(0);p.settings.vout_max=20;PsuModes_Stop(&m,&p);CHECK(PsuModes_Start(&m,&p,clock));
  for(unsigned n=0;n<400;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!p.enabled && m.solder.error==PSU_SOL_CONFIG);
  ready(0);for(unsigned n=0;n<2000;++n){PsuSolder_Sample(&m,1600,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!p.enabled && m.solder.error==PSU_SOL_HOT);
  ready(0);m.solder.stand=1;m.solder.sleeping=1;m.solder.stand_count=3;m.solder.stand_ms=clock-600000u;
  PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);CHECK(!p.enabled && !m.solder_request && !m.solder.error);
  ready(0);p.vout_raw=0;for(unsigned n=0;n<2001;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!p.enabled && m.solder.error==PSU_SOL_RAIL_TIMEOUT);
  ready(0);PsuModes_Stop(&m,&p);m.solder.tip=1;CHECK(PsuModes_Start(&m,&p,clock));
  for(unsigned n=0;n<400;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(!p.enabled && !m.solder.duty && m.solder.phase==PSU_SOL_ROUTE);
  p.vout_raw=12;for(unsigned n=0;n<202;++n){PsuSolder_Sample(&m,1273,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(p.enabled && p.voltage_set==12 && p.current_set==4);
  /* Full application: real input/menu, 1 kHz callback, asynchronous ADC2,
     timer registers and USB emergency OFF. No simulated thermal plant. */
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;TIM1->CCMR2=TIM_CCMR2_OC3PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  PsuApp_Init();tip_code=1200;run(0,160);CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));enter();run(0,200);PsuApp_Snapshot(&s);
  CHECK(s.modes.mode==PSU_MODE_SOLDER && (GPIOC->ODR&GPIO_PIN_15) && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  CHECK(s.modes.solder.valid && s.modes.solder.raw==1200 && !s.modes.solder.error && s.modes.solder.temperature==30);
  run(0,200);PsuApp_Snapshot(&s);CHECK(s.control.enabled && s.modes.solder.phase==PSU_SOL_PRECHARGE && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  vout_code=1295;run(0,5500);PsuApp_Snapshot(&s);
  CHECK(s.modes.solder.phase==PSU_SOL_RUN && !s.modes.solder.error && s.control.voltage_set==24);
  unsigned heated=0,quiet=0;
  for(unsigned n=0;n<200;++n){run(0,1);if(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1)++heated;else ++quiet;}
  CHECK(heated>=160 && quiet>=10 && TIM1->ARR==7199 && !(TIM1->CCER&TIM_CCER_CC3E));
  run(GPIO_PIN_11,330);PsuApp_Snapshot(&s);CHECK(s.modes.solder.sleeping && s.modes.solder.active_target==200);
  run(0,120);PsuApp_Snapshot(&s);CHECK(!s.modes.solder.sleeping && s.modes.solder.active_target==300);
  turn(1);PsuApp_Snapshot(&s);CHECK(s.modes.solder.target==305);
  tap(GPIO_PIN_7);PsuApp_Snapshot(&s);CHECK(s.modes.solder.target==250);
  tip_code=1800;run(0,150);PsuApp_Snapshot(&s);
  CHECK(s.modes.solder.error==PSU_SOL_HOT && s.modes.solder.trip_adc==1800 && !s.control.enabled && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  tip_code=1273;run(0,200);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && !s.modes.solder_request);
  tap(GPIO_PIN_9);run(0,600);PsuApp_Snapshot(&s);CHECK(s.modes.solder.phase==PSU_SOL_RUN && !s.modes.solder.error);
  PsuApp_UsbRx((const uint8_t *)"!",1);CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !(TIM1->BDTR&TIM_BDTR_MOE));
  run(0,200);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && !s.modes.solder_request && !s.discharge_pulses);
  tap(GPIO_PIN_9);run(0,600);for(unsigned n=0;n<120 && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1);++n)run(0,1);
  CHECK(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1);now+=2;PsuApp_SysTick();PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_TIMING) && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && GPIOA->BRR==SD_Pin && !(TIM1->BDTR&TIM_BDTR_MOE));
  CHECK(s.timing_sources==PSU_TIMING_HEATER && s.timing_value==0);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;run(0,110);
  CHECK(strstr(last_tx,"\"timing_src\":1") && strstr(last_tx,"\"trip_adc\":0") && strstr(last_tx,"\"protect\":1"));
  run(0,200);run(GPIO_PIN_9,1100);run(0,60);PsuApp_Snapshot(&s);CHECK(!s.control.fault && !s.control.enabled && !s.timing_sources && !s.timing_value);
  tap(GPIO_PIN_9);run(0,600);current_code=3000;run(0,1);PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_OCP) && !s.control.enabled && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !(TIM1->BDTR&TIM_BDTR_MOE) && !(GPIOC->ODR&GPIO_PIN_15));
  return 0;
}
