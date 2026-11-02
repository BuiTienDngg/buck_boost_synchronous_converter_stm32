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
static void recover(void){p.vout_raw=p.voltage_set;steps(20);CHECK(!m.solder.rail_recover && !m.solder.duty);steps(15);}
int test_main(void){
  setup(1193);steps(15);CHECK(m.solder.duty>.799f && !m.solder.rail_events && !m.solder.zero_cause);
  p.vout_raw=12;steps(1);
  CHECK(m.solder.rail_recover && m.solder.zero_cause==5 && !m.solder.duty && !m.solder.command && p.enabled);
  CHECK(m.solder.load_limit_pm==400 && m.solder.rail_events==1);
  /* 70% recovery alone must not re-enable PWM or repeatedly halve the cap. */
  p.vout_raw=13;steps(150);CHECK(m.solder.rail_recover && !m.solder.duty && m.solder.load_limit_pm==400 && m.solder.rail_events==1);
  p.vout_raw=18;steps(19);CHECK(m.solder.rail_recover && !m.solder.duty);
  steps(1);CHECK(!m.solder.rail_recover && !m.solder.duty);steps(14);CHECK(!m.solder.duty);steps(1);
  CHECK(m.solder.duty>.399f && m.solder.duty<=.40001f && m.solder.ramp_limit_pm==400);
  /* The ceiling remains part of PID saturation even with ramp disabled. */
  steps(300);CHECK(m.solder.command<=.40001f && m.solder.integral<=.40001f);
  p.vout_raw=12;steps(1);CHECK(m.solder.load_limit_pm==200 && m.solder.rail_events==2);recover();
  CHECK(m.solder.command>.199f && m.solder.command<=.20001f);
  p.state=PSU_CC;steps(2000);CHECK(m.solder.load_limit_pm==200 && m.solder.rail_events==2);
  p.state=PSU_CV;steps(1550);CHECK(m.solder.load_limit_pm==210 && m.solder.command<=.21001f);
  /* Unloaded time does not prove the supply can carry more duty. */
  raw_tip=1735;steps(200);unsigned limit=m.solder.load_limit_pm;steps(3000);CHECK(m.solder.load_limit_pm==limit);
  /* The configured cap and hard 80% ceiling still dominate. */
  setup(1193);p.settings.solder_duty_max=20;steps(15);CHECK(m.solder.duty<=.20001f && m.solder.load_limit_pm==200);
  p.vout_raw=12;steps(1);CHECK(m.solder.load_limit_pm==100);recover();CHECK(m.solder.command<=.10001f);
  setup(1193);steps(15);p.vout_raw=12;steps(1999);CHECK(p.enabled && m.solder.rail_recover);steps(1);
  CHECK(!p.enabled && !m.solder_request && m.solder.error==PSU_SOL_RAIL_TIMEOUT && m.solder.zero_cause==7);
  setup(1193);steps(15);
  for(unsigned n=0;n<4;++n){p.vout_raw=12;steps(1);CHECK(p.enabled);recover();}
  CHECK(m.solder.load_limit_pm==50 && m.solder.rail_events==4);
  p.vout_raw=12;steps(1);CHECK(!p.enabled && m.solder.error==PSU_SOL_RAIL_TIMEOUT && m.solder.rail_events==5);
  setup(1193);steps(15);p.vout_raw=12;steps(1);PsuModes_Stop(&m,&p);p.vout_raw=18;steps(1000);
  CHECK(!p.enabled && !m.solder_request && !m.solder.duty && m.solder.zero_cause==1);
  setup(1193);steps(15);p.vout_raw=12;steps(1);p.fault=PSU_F_OCP;steps(1);
  CHECK(!p.enabled && !m.solder.duty && m.solder.zero_cause==7);
  setup(1700);p.settings.solder_ki=p.settings.solder_kd=0;steps(15);
  CHECK(m.solder.command>.174f && m.solder.command<.176f);
  p.vout_raw=12;steps(1);CHECK(m.solder.load_limit_pm>=86 && m.solder.load_limit_pm<=88);
  recover();CHECK(m.solder.command<.089f);
  /* Real application confirms a rail dip removes PB9 PWM and recovery
     drives CCR4 at the reduced cap, not the previous full-power command. */
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  tip_code=1193;PsuApp_Init();run(0,160);run(GPIO_PIN_13,1026);run(0,60);vout_code=971;
  PsuView view;
  for(unsigned n=0;n<1800;++n){run(0,1);PsuApp_Snapshot(&view);if(view.hardware.heater)break;}
  CHECK(view.hardware.heater && view.hardware.heater_ccr==5760 && !view.control.fault);
  vout_code=647;run(0,2);PsuApp_Snapshot(&view);
  CHECK(view.modes.solder.rail_recover && view.modes.solder.load_limit_pm==400 && !view.hardware.heater && !TIM4->CCR4);
  CHECK(view.hardware.heater_block&PSU_HB_RAIL);
  CHECK(view.control.enabled && (GPIOC->ODR&GPIO_PIN_15));
  vout_code=971;
  for(unsigned n=0;n<100;++n){run(0,1);PsuApp_Snapshot(&view);if(view.hardware.heater)break;}
  CHECK(view.hardware.heater && view.hardware.heater_ccr==2880 && view.modes.solder.rail_events==1);
  CHECK(!sleep_pin_reads_heating);
  return 0;
}
