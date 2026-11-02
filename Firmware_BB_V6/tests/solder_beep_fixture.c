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
  p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;steps(PSU_SOLDER_RAIL_SETTLE_MS);
  CHECK(m.solder.phase==PSU_SOL_RUN && !m.solder.duty);
}
int test_main(void){
  CHECK(PSU_SOLDER_RAMP_MS==0);
  setup(1193);steps(15);CHECK(m.solder.duty>.799f && m.solder.ramp_limit_pm==800 && !m.solder.ramp_ms);
  setup(1735);steps(764);CHECK(!m.solder.target_event && m.solder.target_hold_ms==750);
  steps(1);CHECK(m.solder.target_event==1 && m.solder.target_announced);
  steps(1000);CHECK(m.solder.target_event==1);
  raw_tip=1635;steps(300);raw_tip=1735;steps(1000);CHECK(m.solder.target_event==1);
  PsuSolder_SetTarget(&m,350);raw_tip=1835;steps(1000);
  CHECK(m.solder.target_event==2 && m.solder.target_announced);
  /* Leaving the band before 800 ms restarts the stability interval. */
  setup(1735);steps(415);CHECK(!m.solder.target_event);
  raw_tip=1635;steps(50);CHECK(!m.solder.target_hold_ms);
  raw_tip=1735;steps(700);CHECK(!m.solder.target_event);steps(100);CHECK(m.solder.target_event==1);
  /* Sleep is silent; waking re-arms the normal target notification. */
  setup(1735);m.solder.sleep_target=300;m.solder.stand=1;steps(115);steps(1000);
  CHECK(m.solder.sleeping && !m.solder.target_event && !m.solder.target_hold_ms);
  m.solder.stand=0;steps(1000);CHECK(!m.solder.sleeping && m.solder.target_event==1);
  /* Bad sensor cannot announce a target. */
  setup(1735);steps(400);raw_tip=4095;steps(1000);CHECK(!m.solder.target_event && !p.enabled);
  /* Physical button auto-entry plus actual PC14 pulse duration. */
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  tip_code=1735;PsuApp_Init();run(0,160);
  run(GPIO_PIN_13,1026);run(0,60);PsuView view;PsuApp_Snapshot(&view);
  CHECK(view.modes.mode==PSU_MODE_SOLDER && view.modes.solder_request && !view.control.enabled);
  CHECK(!buzzer_rises);vout_code=971;
  for(unsigned n=0;n<2500 && !(GPIOC->ODR&GPIO_PIN_14);++n)run(0,1);
  PsuApp_Snapshot(&view);
  CHECK(view.control.enabled && view.modes.solder.target_announced && !view.control.fault);
  CHECK(buzzer_rises==1 && (GPIOC->ODR&GPIO_PIN_14));
  run(0,149);CHECK(GPIOC->ODR&GPIO_PIN_14);run(0,1);
  CHECK(!(GPIOC->ODR&GPIO_PIN_14) && buzzer_falls==1);
  tip_code=1635;run(0,300);tip_code=1735;run(0,1300);CHECK(buzzer_rises==1);
  /* New preset announces exactly once, and STOP cancels an active pulse. */
  tap(GPIO_PIN_5);tip_code=1835;
  for(unsigned n=0;n<1500 && buzzer_rises<2;++n)run(0,1);
  CHECK(buzzer_rises==2 && (GPIOC->ODR&GPIO_PIN_14));
  run(GPIO_PIN_13,30);PsuApp_Snapshot(&view);
  CHECK(!view.control.enabled && !view.modes.solder_request && !(GPIOC->ODR&GPIO_PIN_14));
  run(0,200);CHECK(buzzer_rises==2);
  return 0;
}
