#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
int test_main(void){
  PsuView s;
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  PsuApp_Init();run(0,160);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;
  static const uint8_t config[]="@99 CFG 0 solder_current=3000\n";
  PsuApp_UsbRx(config,sizeof(config)-1);run(0,180);
  vout_code=971;run(0,160);
  static const uint8_t heat[]="@1 HEAT 700 200\n";
  PsuApp_UsbRx(heat,sizeof(heat)-1);run(0,10);PsuApp_Snapshot(&s);
  CHECK(s.modes.mode==PSU_MODE_SOLDER && s.modes.solder_request);
  CHECK(s.modes.solder.target==200 && !s.control.enabled);
  run(0,450);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && s.modes.solder.phase==PSU_SOL_RUN && s.hardware.heater_rises>0);
  /* No more host commands: the on-device lease must expire. */
  run(0,350);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled && !s.modes.solder_request && !s.hardware.heater);
  CHECK(s.modes.solder.target==300 && !s.control.fault);
  static const uint8_t heat2[]="@2 HEAT 700 200\n";
  PsuApp_UsbRx(heat2,sizeof(heat2)-1);run(0,450);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && s.modes.solder_request);
  hUsbDeviceFS.dev_state=0;run(0,1);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled && !s.modes.solder_request && !s.hardware.heater);
  CHECK(!(GPIOC->ODR&GPIO_PIN_15) && s.modes.solder.target==300);
  return 0;
}
