/* Real 50-millisecond mode plus real GPIO/ADC application; no thermal-plant model. */
#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static PsuModes m;
static uint32_t clock_ms;
static uint16_t raw_tip=1200;
static uint8_t feed_tip=1;
volatile uint32_t onoff_full_ms,onoff_partial_ms,onoff_first_heat_ms;
volatile uint32_t onoff_gpio_period_ms,onoff_gpio_width_ms,onoff_sample_age_max_ms;
static void mode_step(void){
  ++clock_ms;
  p.adc_age_ms=p.temp_age_ms=0;
  /* Same quiet-only sample contract as the hardware path. */
  if(feed_tip && m.solder.duty==0 && clock_ms-m.solder.last_heat_ms>=2)
    PsuSolder_Sample(&m,raw_tip,clock_ms);
  PsuModes_Tick(&m,&p,clock_ms);
}
static void steps(unsigned n){while(n--)mode_step();}
static void setup_mode(uint16_t raw){
  Psu_Init(&p);PsuModes_Init(&m);
  p.initialized=p.temp_valid=1;p.ready_ms=1000;p.adc_age_ms=p.temp_age_ms=0;
  p.vin=p.vin_raw=24;p.temp_c=30;clock_ms=100;raw_tip=raw;feed_tip=1;
  CHECK(PsuModes_Select(&m,&p,PSU_MODE_SOLDER,clock_ms));
  steps(99);CHECK(!m.solder_on && !p.enabled);
  steps(1);CHECK(m.solder_on && !p.enabled);
  steps(199);CHECK(!p.enabled);
  steps(1);CHECK(p.enabled && m.solder.phase==PSU_SOL_PRECHARGE);
  p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;
  steps(100);CHECK(m.solder.phase==PSU_SOL_RUN && !m.solder.duty);
  CHECK(p.voltage_set==p.settings.solder_voltage && p.current_set==p.settings.solder_current);
}
int test_main(void){
  PsuView s;
  CHECK(PSU_SOLDER_FRAME_MS==50 && PSU_SOLDER_QUIET_MS==15);
  CHECK(PSU_SOLDER_SAMPLE_MS==5 && PSU_SOLDER_TEMP_PROTECTION);
  setup_mode(1200);
  uint32_t frame=m.solder.frame_ms;
  steps(14);CHECK(!m.solder.duty && !m.solder.on_ms);
  steps(1);CHECK(m.solder.duty==1 && m.solder.on_ms==35);
  onoff_first_heat_ms=clock_ms-frame;
  unsigned high=1;
  for(unsigned n=0;n<34;++n){mode_step();if(m.solder.duty>0)++high;}
  onoff_full_ms=high;
  CHECK(high==35 && m.solder.position==49 && !p.fault && p.enabled);
  CHECK(clock_ms-m.solder.last_sample_ms>=34 && m.solder.valid);
  raw_tip=1445; /* about 299.5 C; fresh samples in the next OFF window. */
  steps(1);CHECK(!m.solder.duty && m.solder.position==0 && !m.solder.on_ms);
  steps(15);CHECK(p.enabled && !m.solder.error && !m.solder.on_ms);
  CHECK(m.solder.temperature>299 && clock_ms-m.solder.last_sample_ms<6);
  CHECK(!m.solder.ramp_ms && !m.solder.ramp_limit_pm);
  /* 50% of 35 ms = 17.5 ms, rounded to 18 at the 1 kHz GPIO tick. */
  setup_mode(1400);
  steps(15);CHECK(m.solder.on_ms==18 && m.solder.duty==1);
  high=1;for(unsigned n=0;n<17;++n){mode_step();if(m.solder.duty>0)++high;}
  onoff_partial_ms=high;CHECK(high==18);
  steps(1);CHECK(!m.solder.duty && m.solder.position==33);
  raw_tip=1446;steps(17);CHECK(!m.solder.error && m.solder.position==0);
  steps(15);CHECK(!m.solder.duty && !m.solder.on_ms);
  PsuSolder_SetTarget(&m,450);steps(10);CHECK(!m.solder.duty);
  setup_mode(1400);steps(16);CHECK(m.solder.duty==1);
  PsuSolder_SetTarget(&m,200);steps(5);CHECK(!m.solder.duty && !m.solder.on_ms);
  PsuSolder_SetTarget(&m,450);steps(5);CHECK(!m.solder.duty);
  setup_mode(1400);steps(16);m.solder.stand=1;steps(299);
  CHECK(!m.solder.sleeping);steps(5);
  CHECK(m.solder.sleeping && m.solder.active_target==200 && !m.solder.duty);
  /* Missing samples must never authorize an ON in a new 50 ms frame. */
  setup_mode(1400);feed_tip=0;steps(15);
  CHECK(m.solder.error==PSU_SOL_STALE && !p.enabled && !m.solder.duty);
  setup_mode(1400);steps(33);CHECK(!m.solder.duty && p.enabled);
  feed_tip=0;steps(31);CHECK(p.enabled);
  steps(1);CHECK(m.solder.error==PSU_SOL_STALE && !p.enabled);
  setup_mode(1400);steps(16);clock_ms+=50;steps(1);
  CHECK(!m.solder.duty && !m.solder.position && !m.solder.on_ms);
  setup_mode(1400);raw_tip=4095;steps(2);CHECK(m.solder.error==PSU_SOL_HOT);
  steps(1);CHECK(!p.enabled && !m.solder_request);
  setup_mode(1400);raw_tip=0;steps(3);
  CHECK(m.solder.error==PSU_SOL_SENSOR && !p.enabled);
  setup_mode(1200);steps(22000);
  CHECK(m.solder.error==PSU_SOL_NO_RISE && !p.enabled && !m.solder.duty);
  setup_mode(1400);steps(16);p.fault=PSU_F_OCP;steps(1);
  CHECK(!p.enabled && !m.solder.duty && !m.solder.on_ms && !m.solder_request);
  setup_mode(1400);steps(16);p.vout_raw=0;steps(1);
  CHECK(!m.solder.duty && !m.solder.on_ms);
  p.vout_raw=p.voltage_set;steps(14);CHECK(!m.solder.duty);
  setup_mode(1400);steps(16);PsuModes_Stop(&m,&p);
  CHECK(!m.solder.on_ms && !m.solder.duty && !m.solder.cycle_ready);
  CHECK(PsuModes_Start(&m,&p,clock_ms));
  for(unsigned n=0;n<500 && m.solder.phase!=PSU_SOL_RUN && !m.solder.error;++n)steps(1);
  CHECK(m.solder.phase==PSU_SOL_RUN);
  steps(14);CHECK(!m.solder.duty);steps(1);CHECK(m.solder.duty==1);
  setup_mode(1400);clock_ms=0xfffffff8u;
  m.solder.frame_ms=m.solder.last_sample_ms=m.solder.sample_ms=m.solder.last_heat_ms=clock_ms;
  steps(14);CHECK(!m.solder.duty);steps(1);
  CHECK(m.solder.position==15 && m.solder.duty==1);
  /* Actual application, not just the mode: check five GPIO periods, quiet
     ADC acquisition, temperature updates and immediate OCP shutdown. */
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  TIM1->CCER=TIM_CCER_CC1NE|TIM_CCER_CC2NE|TIM_CCER_CC3NE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  tip_code=1400;PsuApp_Init();run(0,160);run(GPIO_PIN_8,1026);run(0,302);
  vout_code=971;
  for(unsigned n=0;n<2500;++n){run(0,1);PsuApp_Snapshot(&s);if(s.modes.solder.phase==PSU_SOL_RUN)break;}
  CHECK(s.modes.solder.phase==PSU_SOL_RUN && !s.control.fault);
  CHECK(s.hardware.pb13_mode==2 && !(TIM1->CCER&(TIM_CCER_CC1NE|TIM_CCER_CC2NE|TIM_CCER_CC3NE)));
  unsigned rises=0,falls=0,high_ms=0,period_ok=1,width_ok=1,quiet_ok=1;
  uint32_t rise_at=0,fall_at=now;uint8_t was_high=0;
  for(unsigned n=0;n<250;++n){
    run(0,1);PsuApp_Snapshot(&s);
    uint8_t active=(GPIOB->ODR&HEATER_Pin)!=0;
    if(active)++high_ms;
    if(active && !was_high){
      if(rises){onoff_gpio_period_ms=now-rise_at;if(onoff_gpio_period_ms!=50)period_ok=0;}
      if(now-fall_at<15)quiet_ok=0;
      rise_at=now;++rises;
    }
    if(!active && was_high){
      onoff_gpio_width_ms=now-rise_at;if(onoff_gpio_width_ms!=18)width_ok=0;
      fall_at=now;++falls;
    }
    uint32_t age=now-s.modes.solder.last_sample_ms;
    if(age>onoff_sample_age_max_ms)onoff_sample_age_max_ms=age;
    was_high=active;
  }
  CHECK(rises==5 && falls==5 && high_ms==90 && period_ok && width_ok && quiet_ok);
  CHECK(!s.control.fault && s.modes.solder.on_ms==0 && s.modes.solder.position==0);
  CHECK(onoff_sample_age_max_ms<=50 && s.modes.solder.valid);
  tip_code=1200;run(0,50);high_ms=0;
  for(unsigned n=0;n<100;++n){
    run(0,1);PsuApp_Snapshot(&s);if(GPIOB->ODR&HEATER_Pin)++high_ms;
    uint32_t age=now-s.modes.solder.last_sample_ms;
    if(age>onoff_sample_age_max_ms)onoff_sample_age_max_ms=age;
  }
  CHECK(high_ms==70 && !s.control.fault && s.modes.solder.valid);
  CHECK(onoff_sample_age_max_ms<=45 && s.modes.solder.raw==1200);
  /* A rising temperature must reach the app model within one quiet frame,
     and the next frame must request no heat at/above the target. */
  tip_code=1446;run(0,50);PsuApp_Snapshot(&s);
  CHECK(s.modes.solder.temperature>300 && s.modes.solder.raw==1446 && !s.control.fault);
  uint32_t hot_rises=s.hardware.heater_rises;
  run(0,50);PsuApp_Snapshot(&s);
  CHECK(s.hardware.heater_rises==hot_rises && !s.hardware.heater && !s.modes.solder.on_ms);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;
  run(0,101);CHECK(strstr(last_tx,"\"cycle_ms\":50,") && strstr(last_tx,"\"quiet_ms\":15,"));
  CHECK(strstr(last_tx,"\"pwm_hz\":0") && strstr(last_tx,"\"fixed_pwm\":0"));
  tip_code=1400;run(0,100);
  for(unsigned n=0;n<50 && !(GPIOB->ODR&HEATER_Pin);++n)run(0,1);
  CHECK(GPIOB->ODR&HEATER_Pin);
  current_code=3000;run(0,1);PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_OCP) && !(GPIOB->ODR&HEATER_Pin) && !(GPIOC->ODR&GPIO_PIN_15));
  CHECK(!s.modes.solder_request && !s.modes.solder.on_ms);
  return 0;
}
