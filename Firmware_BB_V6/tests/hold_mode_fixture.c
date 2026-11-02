#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
int test_main(void){
 PsuView s;
 hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;htim4.Instance=TIM4;
 TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
 hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
 PsuApp_Init();run(0,160);
 run(GPIO_PIN_13,1024);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB && !s.control.enabled);
 run(GPIO_PIN_13,2);run(0,60);PsuApp_Snapshot(&s);
 CHECK(s.modes.mode==PSU_MODE_SOLDER && !s.control.enabled && s.modes.solder_request && (GPIOC->ODR&GPIO_PIN_15));
 run(GPIO_PIN_13,1100);run(0,60);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB && !s.control.enabled);
 tap(GPIO_PIN_13);run(0,160);PsuApp_Snapshot(&s);CHECK(s.control.enabled);
 run(GPIO_PIN_13,30);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
 run(GPIO_PIN_13,1000);run(0,60);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_SOLDER && !s.control.enabled && s.modes.solder_request);
 run(0,400);PsuApp_Snapshot(&s);CHECK(s.control.enabled && s.modes.solder_request && (GPIOC->ODR&GPIO_PIN_15));
 HAL_ADC_ErrorCallback(&hadc1);PsuApp_Snapshot(&s);CHECK(s.control.fault && !s.control.enabled && (GPIOC->ODR&GPIO_PIN_15));
 CHECK(!TIM1->CCR1 && !TIM1->CCR2 && !TIM4->CCR4 && !(TIM1->BDTR&TIM_BDTR_MOE));
 run(GPIO_PIN_13,30);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && (GPIOC->ODR&GPIO_PIN_15));
 run(GPIO_PIN_13,1500);run(0,60);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB && !s.control.enabled && !(GPIOC->ODR&GPIO_PIN_15));
 run(GPIO_PIN_8,1100);run(0,60);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB);
 HAL_ADC_ErrorCallback(&hadc1);run(GPIO_PIN_13,1100);run(0,60);PsuApp_Snapshot(&s);CHECK(s.control.fault && !s.control.enabled && s.modes.mode==PSU_MODE_SOLDER && (GPIOC->ODR&GPIO_PIN_15));
 run(GPIO_PIN_13,1100);run(0,60);PsuApp_Snapshot(&s);CHECK(s.control.fault && !s.control.enabled && s.modes.mode==PSU_MODE_LAB && !(GPIOC->ODR&GPIO_PIN_15));
 return 0;
}
