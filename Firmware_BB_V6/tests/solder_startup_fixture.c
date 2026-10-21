#include "psu_app.h"
#define TEST_FACTORY_CONFIG 1
#define HAL_GPIO_WritePin model_write_pin
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
#undef HAL_GPIO_WritePin
static uint32_t routed_at;
static unsigned route_count;
static PsuControl delayed_control;
static PsuModes delayed_modes;
volatile uint32_t startup_route_wait_ms,startup_supply_wait_ms,startup_ramp_ms,startup_first_heat_ms;
void HAL_GPIO_WritePin(GPIO_TypeDef *g,uint16_t pin,GPIO_PinState state){
  if(g==GPIOC && (pin&GPIO_PIN_15) && ((g->ODR&GPIO_PIN_15)!=0)!=(state==GPIO_PIN_SET))CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  if(g==GPIOC && (pin&GPIO_PIN_15) && state==GPIO_PIN_SET && !(g->ODR&GPIO_PIN_15)){
    CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
    routed_at=now;++route_count;
  }
  model_write_pin(g,pin,state);
}
int test_main(void){
  PsuView s;
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  tip_code=4095;PsuApp_Init();run(0,160);
  run(GPIO_PIN_8,1026);PsuApp_Snapshot(&s);uint32_t entered=now;
  CHECK(s.modes.mode==PSU_MODE_SOLDER && !s.control.enabled && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !(GPIOC->ODR&GPIO_PIN_15));
  for(unsigned n=0;n<99;++n){run(0,1);CHECK(route_count==0);}
  run(0,2);CHECK(route_count==1 && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  startup_route_wait_ms=routed_at-entered;CHECK(startup_route_wait_ms>=100);
  unsigned early_heat=0;
  for(unsigned n=0;n<201;++n){run(0,1);if(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1)++early_heat;PsuApp_Snapshot(&s);if(s.control.enabled)break;}
  CHECK(s.control.enabled && !early_heat && s.modes.solder.phase==PSU_SOL_PRECHARGE);
  startup_supply_wait_ms=now-routed_at;CHECK(startup_supply_wait_ms>=200);
  run(0,500);PsuApp_Snapshot(&s);CHECK(s.modes.solder.phase==PSU_SOL_PRECHARGE && !s.modes.solder.duty && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  vout_code=971;
  for(unsigned n=0;n<2000;++n){run(0,1);PsuApp_Snapshot(&s);if(s.modes.solder.phase==PSU_SOL_RUN)break;}
  CHECK(s.modes.solder.phase==PSU_SOL_RUN && !s.modes.solder.duty && !s.modes.solder.ramp_limit_pm && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  uint32_t heat_start=now,first_heat=0;uint16_t previous=0;unsigned monotonic=1,bounded=1;
  for(unsigned n=0;n<2100;++n){
    run(0,1);PsuApp_Snapshot(&s);
    if(s.modes.solder.ramp_limit_pm<previous)monotonic=0;
    if(s.modes.solder.ramp_limit_pm>500 || s.modes.solder.duty>0.5001f)bounded=0;
    if(s.modes.solder.ramp_limit_pm==500 && !startup_ramp_ms)startup_ramp_ms=now-heat_start;
    if((TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !first_heat)first_heat=now;
    previous=s.modes.solder.ramp_limit_pm;
  }
  startup_first_heat_ms=first_heat-heat_start;
  CHECK(monotonic && bounded && startup_ramp_ms==2000 && startup_first_heat_ms>=100);
  CHECK(s.modes.solder.duty==0.50f && s.modes.solder.ramp_ms==2000 && !s.control.fault);
  tap(GPIO_PIN_9);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled && !s.modes.solder_request && !s.modes.solder_on && !s.modes.solder.ramp_limit_pm && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !(GPIOC->ODR&GPIO_PIN_15));
  tap(GPIO_PIN_9);run(0,600);PsuApp_Snapshot(&s);
  CHECK(route_count==2 && s.modes.solder.phase==PSU_SOL_RUN && s.modes.solder.ramp_limit_pm<500 && s.modes.solder.ramp_ms<2000);
  current_code=3000;run(0,1);PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_OCP) && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && !(GPIOC->ODR&GPIO_PIN_15) && !s.modes.solder.ramp_limit_pm);
  /* A late first tick cannot skip the switch-settling interval. */
  Psu_Init(&delayed_control);PsuModes_Init(&delayed_modes);
  delayed_control.initialized=delayed_control.temp_valid=1;delayed_control.ready_ms=1000;
  delayed_control.adc_age_ms=delayed_control.temp_age_ms=0;delayed_control.vin=delayed_control.vin_raw=24;delayed_control.temp_c=30;
  PsuModes_Select(&delayed_modes,&delayed_control,PSU_MODE_SOLDER,1000);
  PsuSolder_Sample(&delayed_modes,1200,1500);PsuSolder_Sample(&delayed_modes,1200,1500);PsuModes_Tick(&delayed_modes,&delayed_control,1500);
  CHECK(delayed_modes.solder_on && !delayed_control.enabled);
  PsuSolder_Sample(&delayed_modes,1200,1699);PsuSolder_Sample(&delayed_modes,1200,1699);PsuModes_Tick(&delayed_modes,&delayed_control,1699);
  CHECK(!delayed_control.enabled);
  PsuModes_Tick(&delayed_modes,&delayed_control,1700);CHECK(delayed_control.enabled && !delayed_modes.solder.duty);
  return 0;
}
