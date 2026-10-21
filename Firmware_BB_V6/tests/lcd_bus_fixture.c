/* Exercise the real software LCD bus with TIM3 already active on PA6. */
#define PsuDisplay_Init unused_display_init
#define PsuDisplay_Update unused_display_update
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
#undef PsuDisplay_Init
#undef PsuDisplay_Update
#include "../Core/Src/psu_display.c"
int test_main(void){
  CHECK(PSU_LCD_SOFT_SPI && !PSU_PA6_PWM_PIN_TEST);
  RCC->APB2ENR|=RCC_APB2ENR_SPI1EN;
  GPIOA->CRL=11u<<24;
  TIM3->CCR1=3600;TIM3->CCMR1=0x68;TIM3->CCER=1;
  lcd_bus_init();
  CHECK(!(RCC->APB2ENR&RCC_APB2ENR_SPI1EN));
  CHECK(((GPIOA->CRL>>20)&15)==3 && ((GPIOA->CRL>>28)&15)==3);
  const uint8_t payload[]={0x00,0x28,0x01,0x18};
  command(0x2A,payload,sizeof(payload));
  const uint8_t pattern[]={0x00,0xFF,0x80,0x01,0x55,0xAA};
  command(0x36,pattern,sizeof(pattern));
  CHECK(((GPIOA->CRL>>24)&15)==11 && TIM3->CCR1==3600 && TIM3->CCMR1==0x68 && TIM3->CCER==1);
  CHECK(GPIOB->ODR&CS_Pin);
  return 0;
}
