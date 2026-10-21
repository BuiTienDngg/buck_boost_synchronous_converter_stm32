/* Reuse the application fixture's hardware bus and continuous ADC model. */
#define TEST_FACTORY_CONFIG
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
void remote_fixture_init(void) {
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  PsuApp_Init();hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;
  run(0,160);
}
void remote_fixture_tick(uint32_t ms){run(0,ms);}
void remote_fixture_buttons(uint32_t pins,uint32_t ms){run((uint16_t)pins,ms);}
void remote_fixture_input(const uint8_t *p,uint32_t n){PsuApp_UsbRx(p,n);}
void remote_fixture_sense(uint32_t v,uint32_t i,uint32_t vin,uint32_t t){vout_code=v;current_code=i;vin_code=vin;ntc=t;}
void remote_fixture_tip(uint32_t raw){tip_code=(uint16_t)raw;}
