/* PA6 hardware PWM, quiet ADC, TIM4 dispatch and independent shutdown. */
#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static PsuModes m;
static uint32_t clock_ms;
static uint16_t raw_tip=1200;
static void step(void){
  ++clock_ms;p.adc_age_ms=p.temp_age_ms=0;
  if(m.solder.duty==0 && clock_ms-m.solder.last_heat_ms>=2)PsuSolder_Sample(&m,raw_tip,clock_ms);
  PsuModes_Tick(&m,&p,clock_ms);
}
static void steps(unsigned n){while(n--)step();}
static void setup(uint16_t raw){
  Psu_Init(&p);PsuModes_Init(&m);p.initialized=p.temp_valid=1;p.ready_ms=1000;
  p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;clock_ms=100;raw_tip=raw;
  CHECK(PsuModes_Select(&m,&p,PSU_MODE_SOLDER,clock_ms));
  steps(300);CHECK(p.enabled && m.solder.phase==PSU_SOL_PRECHARGE);
  p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;steps(100);
  CHECK(m.solder.phase==PSU_SOL_RUN && !m.solder.duty);
}
static void start_application(void){
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;htim3.Instance=TIM3;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  current_code=0;tip_code=1635;PsuApp_Init();
  CHECK(((GPIOA->CRL>>24)&15)==2 && !(GPIOA->ODR&HEATER_Pin) && !TIM3->CCR1);
  run(0,160);run(GPIO_PIN_8,1026);run(0,302);vout_code=971;
  PsuView s;
  for(unsigned n=0;n<2500;++n){run(0,1);PsuApp_Snapshot(&s);if(s.hardware.heater)break;}
  CHECK(s.control.enabled && s.hardware.heater && !s.control.fault);
}
int test_main(void){
  PsuView s;
  setup(1193);CHECK(m.solder.temperature==29 && !m.solder.error);
  setup(1141);CHECK(m.solder.temperature==3 && !m.solder.error);
  setup(1293);CHECK(m.solder.temperature==79 && !m.solder.error);
  setup(1200);steps(9);CHECK(!m.solder.duty);steps(1);CHECK(m.solder.duty==PSU_SOLDER_MAX_DUTY && m.solder.on_ms==40);
  steps(39);CHECK(m.solder.duty==PSU_SOLDER_MAX_DUTY);steps(1);CHECK(!m.solder.duty && m.solder.position==0);
  setup(1635);steps(10);CHECK(m.solder.duty==0.5f && m.solder.on_ms==40);
  steps(39);CHECK(m.solder.duty==0.5f);steps(1);CHECK(!m.solder.duty);
  /* Integral supplies sustained heat at constant error; rising temperature
     reduces duty through D. Neither term may change inside a PWM window. */
  setup(1635);steps(10);float first=m.solder.command;CHECK(first==0.5f);
  steps(50);CHECK(m.solder.integral>0 && m.solder.command>first);
  float held=m.solder.command;steps(20);CHECK(m.solder.command==held);
  raw_tip=1646;steps(30);
  CHECK(m.solder.derivative>0 && m.solder.command<PSU_SOLDER_KP*(m.solder.active_target-m.solder.temperature)+m.solder.integral);
  setup(1200);steps(1010);CHECK(m.solder.integral==0 && m.solder.command==PSU_SOLDER_MAX_DUTY);
  setup(1767);steps(10);CHECK(!m.solder.duty && !m.solder.on_ms);
  setup(1635);steps(11);PsuSolder_SetTarget(&m,200);steps(5);CHECK(!m.solder.duty);
  setup(1635);steps(11);p.fault=PSU_F_OCP;steps(1);CHECK(!p.enabled && !m.solder_request && !m.solder.duty);
  setup(1635);raw_tip=4095;steps(2);CHECK(m.solder.error==PSU_SOL_HOT && !p.enabled);
  setup(1200);steps(22000);CHECK(m.solder.error==PSU_SOL_NO_RISE && !p.enabled);
  start_application();
  CHECK(TIM3->PSC==0 && TIM3->ARR==7199 && (TIM3->CR1&TIM_CR1_CEN));
  CHECK(!(TIM3->DIER&TIM_DIER_UIE) && (TIM3->CCER&TIM_CCER_CC1E));
  CHECK(!(TIM3->CCER&TIM_CCER_CC1P));
  /* Non-inverted PWM HIGH duration is the requested heater duty. */
  unsigned high_ticks=0;for(unsigned cnt=0;cnt<7200;++cnt)if(cnt<TIM3->CCR1)++high_ticks;
  CHECK(high_ticks==3600);

  PsuApp_Snapshot(&s);CHECK(s.hardware.pa6_mode==11 && !(GPIOB->ODR&GPIO_PIN_13));
  TIM3->CR1&=~TIM_CR1_CEN;PsuApp_Snapshot(&s);CHECK(s.hardware.heater_block&PSU_HB_TIMER);TIM3->CR1|=TIM_CR1_CEN;
  unsigned heat=0,quiet=0,bad=0;
  for(unsigned n=0;n<250;++n){
    run(0,1);PsuApp_Snapshot(&s);
    if(s.hardware.heater){++heat;if(TIM3->CCR1!=(uint32_t)(s.modes.solder.duty*7200+0.5f) || s.modes.solder.duty<=0 || s.modes.solder.duty>1)++bad;}
    else{++quiet;if(((GPIOA->CRL>>24)&15)!=2 || (GPIOA->ODR&HEATER_Pin))++bad;if(!(s.hardware.heater_block&PSU_HB_QUIET))++bad;if(TIM3->CCR1 || (TIM3->CCMR1&TIM_CCMR1_OC1M)!=TIM_OCMODE_FORCED_INACTIVE)++bad;}
  }
  CHECK(heat==200 && quiet==50 && bad==0);
  CHECK(s.modes.solder.valid && now-s.modes.solder.last_sample_ms<50);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;
  PsuApp_UsbRx((const uint8_t *)"?",1);
  run(0,101);CHECK(strstr(last_tx,"\"active_low\":0") && strstr(last_tx,"\"pwm_hz\":10000") && strstr(last_tx,"\"pin\":\"PA6\"") && strstr(last_tx,"\"ccr1\":"));
  tip_code=1200;run(0,100);
  for(unsigned n=0;n<50;++n){PsuApp_Snapshot(&s);if(s.hardware.heater)break;run(0,1);}
  CHECK(s.hardware.heater && TIM3->CCR1==6840 && TIM3->CCR1<TIM3->ARR);
  tip_code=1833;run(0,100);PsuApp_Snapshot(&s);
  CHECK(!s.hardware.heater && (s.hardware.heater_block&PSU_HB_PID_ZERO) && !s.control.fault);
  /* Carrier callbacks cannot run PID or consume its ADC snapshot. */
  now+=300;HAL_TIM_PeriodElapsedCallback(&htim3);PsuApp_Snapshot(&s);CHECK(!s.control.fault);
  HAL_TIM_PeriodElapsedCallback(&htim4);PsuApp_Snapshot(&s);CHECK((s.control.fault&PSU_F_TIMING) && !TIM3->CCR1);
  start_application();
  /* SysTick removes an autonomous hardware PWM when TIM4 stalls. */
  now+=3;PsuApp_SysTick();PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_TIMING) && !s.hardware.heater && !TIM3->CCR1);
  CHECK((TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_FORCED_INACTIVE);
  CHECK(((GPIOA->CRL>>24)&15)==2 && !(GPIOA->ODR&HEATER_Pin));
  start_application();
  current_code=4095;for(unsigned n=0;n<8;++n)sample();PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_OCP) && !s.hardware.heater && !TIM3->CCR1);
  CHECK(!(GPIOB->ODR&GPIO_PIN_13) && !(GPIOC->ODR&GPIO_PIN_15));
  return 0;
}
