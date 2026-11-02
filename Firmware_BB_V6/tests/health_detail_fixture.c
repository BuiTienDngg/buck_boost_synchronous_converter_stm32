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
 setup(1193);p.vin_raw=p.settings.vin_min-0.1f;steps(1);
 CHECK(m.solder_request && p.enabled && !m.solder.error && (m.solder.health_seen&128));
 p.fault=PSU_F_VIN_UV;steps(1);CHECK(!m.solder_request && !p.enabled && m.solder.stop_fault==PSU_F_VIN_UV && (m.solder.stop_flags&128));
 uint32_t stop=m.solder.stop_ms;steps(20);CHECK(m.solder.stop_ms==stop && m.solder.stop_fault==PSU_F_VIN_UV);
 setup(1193);p.adc_age_ms=3;PsuModes_Tick(&m,&p,++clock_ms);
 CHECK((p.fault&PSU_F_ADC) && !p.enabled && (m.solder.stop_flags&8) && m.solder.stop_ms==clock_ms);
 setup(1193);p.temp_valid=0;steps(1);CHECK((p.fault&PSU_F_SENSOR) && !p.enabled && (m.solder.stop_flags&16));
 setup(1193);p.temp_c=p.settings.temp_trip;steps(1);CHECK((p.fault&PSU_F_OTP) && !p.enabled && (m.solder.stop_flags&64));
 setup(1193);p.enabled=0;steps(1);CHECK(!m.solder_request && (m.solder.stop_flags&512));
 setup(1193);CHECK(!m.solder.stop_ms && !m.solder.stop_flags && !m.solder.stop_fault);
 return 0;
}
