#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static void turn(int steps){TIM2->CNT=(uint16_t)(TIM2->CNT-steps*PSU_ENCODER_COUNTS);run(0,5);}
static void menu(void){run(GPIO_PIN_4,1100);run(0,60);}
int test_main(void){
  PsuView s;
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  PsuApp_Init();run(0,160);tap(GPIO_PIN_8);PsuApp_Snapshot(&s);CHECK(s.edit_current && s.modes.mode==PSU_MODE_LAB);
  tap(GPIO_PIN_8);PsuApp_Snapshot(&s);CHECK(!s.edit_current);
  tap(GPIO_PIN_9);run(0,160);PsuApp_Snapshot(&s);CHECK(s.control.enabled);
  run(GPIO_PIN_8,1024);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB && s.control.enabled);
  run(GPIO_PIN_8,2);PsuApp_Snapshot(&s);
  CHECK(s.modes.mode==PSU_MODE_SOLDER && !s.control.enabled && s.modes.solder_request && !s.menu.page);
  CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && GPIOA->BRR==SD_Pin && !(GPIOC->ODR&GPIO_PIN_15));
  CHECK(s.modes.lab_mv==5000 && s.modes.lab_ma==1000 && s.discharge_state==PSU_D_IDLE && !s.discharge_pulses);
  run(GPIO_PIN_8,99);CHECK(!(GPIOC->ODR&GPIO_PIN_15));
  run(GPIO_PIN_8,2);CHECK(GPIOC->ODR&GPIO_PIN_15);
  run(0,250);PsuApp_Snapshot(&s);CHECK(s.control.enabled && s.modes.solder.phase==PSU_SOL_PRECHARGE);
  vout_code=971;run(0,3000);PsuApp_Snapshot(&s);CHECK(s.modes.solder.phase==PSU_SOL_RUN);
  tap(GPIO_PIN_9);run(0,160);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && !s.modes.solder_request);
  /* Holding again in SOLDER must not restart heating. */
  run(GPIO_PIN_8,1100);run(0,60);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && !s.modes.solder_request);
  menu();turn(3);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB && !s.control.enabled);
  /* Faulted LAB stays off; the shortcut never clears faults. */
  HAL_ADC_ErrorCallback(&hadc1);PsuApp_Snapshot(&s);CHECK(s.control.fault&PSU_F_ADC);
  run(GPIO_PIN_8,1100);run(0,60);PsuApp_Snapshot(&s);
  CHECK(s.modes.mode==PSU_MODE_LAB && !s.control.enabled && (s.control.fault&PSU_F_ADC));
  CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1) && GPIOA->BRR==SD_Pin);
  return 0;
}
