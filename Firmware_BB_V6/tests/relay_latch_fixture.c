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
  CHECK(((GPIOB->CRH>>4)&15)==2 && !(GPIOB->ODR&HEATER_Pin) && !TIM4->CCR4);
  run(0,160);run(GPIO_PIN_8,1026);run(0,302);vout_code=971;
  PsuView s;
  for(unsigned n=0;n<2500;++n){run(0,1);PsuApp_Snapshot(&s);if(s.hardware.heater)break;}
  CHECK(s.control.enabled && s.hardware.heater && !s.control.fault);
}
int test_main(void){
 start_application();CHECK(GPIOC->ODR&GPIO_PIN_15);
 const uint8_t off[]="!\n";PsuApp_UsbRx(off,2);run(0,50);
 PsuView v;PsuApp_Snapshot(&v);CHECK(!v.control.enabled && !v.hardware.heater && !TIM4->CCR4);
 CHECK(GPIOC->ODR&GPIO_PIN_15);
 PsuApp_EmergencyOff();CHECK(GPIOC->ODR&GPIO_PIN_15);
 CHECK(GPIOA->BRR==SD_Pin && !TIM1->CCR1 && !TIM1->CCR2 && !(TIM1->BDTR&TIM_BDTR_MOE));
 current_code=4095;for(unsigned n=0;n<8;++n)sample();run(0,10);PsuApp_Snapshot(&v);
 CHECK(v.control.fault && !v.control.enabled && !v.hardware.heater);
 CHECK(GPIOC->ODR&GPIO_PIN_15);
 run(0,1000);CHECK(GPIOC->ODR&GPIO_PIN_15);
 return 0;
}
