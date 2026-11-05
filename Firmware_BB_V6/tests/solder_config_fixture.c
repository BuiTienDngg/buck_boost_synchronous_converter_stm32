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
static void start_application(void){
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;htim3.Instance=TIM3;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  current_code=0;tip_code=1635;PsuApp_Init();
  CHECK(((GPIOB->CRH>>4)&15)==2 && !(GPIOB->ODR&HEATER_Pin) && !TIM4->CCR4);
  run(0,160);run(GPIO_PIN_8,1026);run(0,302);vout_code=971;
  PsuView s;
  for(unsigned n=0;n<2500;++n){run(0,1);PsuApp_Snapshot(&s);if(s.hardware.heater)break;}
  CHECK(s.control.enabled && s.hardware.heater && !s.control.fault);
}
int test_main(void){
 CHECK(PSU_SOLDER_QUIET_MS==15 && PSU_SOLDER_HEAT_MAX_MS==35);
 setup(1193);CHECK(p.settings.solder_duty_max==80);steps(14);CHECK(!m.solder.duty);steps(1);CHECK(m.solder.duty>0.799f && m.solder.duty<=0.80001f && m.solder.on_ms==35);
 steps(34);CHECK(m.solder.duty>0);steps(1);CHECK(!m.solder.duty);
 steps(5015);CHECK(m.solder.duty<=0.80001f && m.solder.ramp_limit_pm==800);
 setup(1700);p.settings.solder_ki=p.settings.solder_kd=0;steps(5015);float first=m.solder.command;
 CHECK(first>0.174f && first<0.176f);
 p.settings.solder_kp=0.02f;steps(50);CHECK(m.solder.command>0.349f && m.solder.command<0.351f);
 p.settings.solder_duty_max=20;steps(50);CHECK(m.solder.command>0.199f && m.solder.command<0.201f);
 p.settings.solder_duty_max=0;steps(50);CHECK(!m.solder.command && !m.solder.duty);
 setup(1193);p.settings.solder_duty_max=95;steps(5015);
 CHECK(m.solder.command<=0.80001f && m.solder.ramp_limit_pm==800);
 p.settings.solder_duty_max=40;steps(50);CHECK(m.solder.command<=0.40001f);
 return 0;
}
