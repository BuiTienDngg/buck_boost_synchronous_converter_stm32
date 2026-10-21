#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
int test_main(void){
  CHECK(PSU_PA6_PWM_PIN_TEST==1);
  TIM1->CR1=TIM_CR1_CEN;TIM1->CCER=0x555;TIM1->CCR1=123;TIM1->CCR2=456;
  GPIOA->ODR=SD_Pin;GPIOB->ODR=GPIO_PIN_13;GPIOC->ODR=GPIO_PIN_15;
  SPI1->CR1=0xC34C;SPI1->CR2=0;
  PsuApp_PinTestInit();
  CHECK(SPI1->CR1==0xC34C && SPI1->CR2==0);
  CHECK(TIM3->PSC==0 && TIM3->ARR==7199 && TIM3->CCR1==3600);
  CHECK((TIM3->CR1&TIM_CR1_CEN) && TIM3->DIER==0);
  CHECK((TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1 && (TIM3->CCER&TIM_CCER_CC1E));
  CHECK(((GPIOA->CRL>>24)&15)==11 && !(TIM3->CCER&TIM_CCER_CC1P));
  CHECK(!(GPIOA->ODR&SD_Pin) && !(GPIOB->ODR&GPIO_PIN_13) && !(GPIOC->ODR&GPIO_PIN_15));
  CHECK(!(TIM1->CR1&TIM_CR1_CEN) && TIM1->CCER==0 && !TIM1->CCR1 && !TIM1->CCR2);
  /* Independent SysTick must not impose the former 50 ms thermal frame. */
  for(unsigned n=0;n<1000;++n){++now;PsuApp_SysTick();}
  CHECK(TIM3->CCR1==3600 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1);
  CHECK(!(GPIOA->ODR&SD_Pin) && !(TIM1->BDTR&TIM_BDTR_MOE));
  return 0;
}
