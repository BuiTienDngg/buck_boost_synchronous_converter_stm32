#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static void turn(int steps){TIM2->CNT=(uint16_t)(TIM2->CNT-steps*PSU_ENCODER_COUNTS);run(0,5);}
static void open_menu(void){run(GPIO_PIN_4,1100);run(0,60);}
static void command(const char *s){PsuApp_UsbRx((const uint8_t *)s,(uint32_t)strlen(s));run(0,5);}
int test_main(void){
  PsuView s;hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  PsuApp_Init();run(0,160);open_menu();PsuApp_Snapshot(&s);
  CHECK(s.menu.page==PSU_MENU_ROOT && s.edit_digit==0);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;
  command("@1 ON\n");CHECK(strstr(last_tx,"\"error\":\"local_mode_required\"")!=0);
  PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
  tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.menu.page==PSU_MENU_RAMP);
  tap(GPIO_PIN_4);run(0,160);PsuApp_Snapshot(&s);CHECK(!s.menu.page && s.control.settings.ramp_v_per_s==30 && !s.control.enabled);
  open_menu();turn(1);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.menu.page==PSU_MENU_CHARGE);
  tap(GPIO_PIN_4);turn(1);tap(GPIO_PIN_4);turn(1);tap(GPIO_PIN_4);turn(5);tap(GPIO_PIN_4);
  PsuApp_Snapshot(&s);CHECK(s.modes.chemistry==PSU_CELL_LIFEPO4 && s.modes.cells==6);
  turn(4);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(!s.menu.page && s.modes.mode==PSU_MODE_CHARGE && s.edit_current);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE) && !(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));
  command("@2 SET 5000 500\n");CHECK(strstr(last_tx,"\"error\":\"setpoint_range\"")!=0);
  command("@3 DEFAULTS 1\n");CHECK(strstr(last_tx,"\"error\":\"local_mode_required\"")!=0);
  run(0,110);CHECK(strstr(last_tx,"\"mode\":1,")!=0);
  CHECK(strstr(last_tx,"\"charge\":{\"chem\":1,\"cells\":6,")!=0);
  CHECK(strstr(last_tx,"\"time_s\":0,")!=0 && strstr(last_tx,"\"mAh10\":0}")!=0);
  tap(GPIO_PIN_9);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && s.modes.phase==PSU_CH_ERROR && s.modes.error==PSU_CH_LOW);
  vout_code=1000;run(0,200);tap(GPIO_PIN_9);PsuApp_Snapshot(&s);CHECK(s.modes.phase==PSU_CH_CHECK && !s.control.enabled);
  static const uint8_t stop[]="!\n";PsuApp_UsbRx(stop,2);run(0,1100);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && s.modes.phase==PSU_CH_STOP);
  tap(GPIO_PIN_9);run(0,1100);PsuApp_Snapshot(&s);CHECK(s.control.enabled && s.modes.phase==PSU_CH_CCCV);
  turn(1);PsuApp_Snapshot(&s);CHECK(s.modes.current_ma==510 && s.control.voltage_set>21.89f && s.control.voltage_set<21.91f);
  /* Tighter charger OVP uses the existing raw ADC trip and app log. */
  vout_code=1192;run(0,2);PsuApp_Snapshot(&s);CHECK(s.control.fault&PSU_F_OVP);CHECK(!s.control.enabled && !(TIM1->BDTR&TIM_BDTR_MOE));
  CHECK(s.control.fault_sample.ovp_adc<=1192);
  vout_code=1000;run(0,200);run(GPIO_PIN_9,1100);run(0,60);PsuApp_Snapshot(&s);CHECK(!s.control.fault);
  tap(GPIO_PIN_9);run(0,1100);tap(GPIO_PIN_9);run(0,200);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled && s.discharge_state==PSU_D_IDLE && !s.discharge_pulses);
  CHECK(!(TIM1->BDTR&TIM_BDTR_MOE));
  open_menu();turn(3);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB && s.modes.discharge_inhibit);
  open_menu();turn(2);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.menu.page==PSU_MENU_SOLDER);turn(3);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_SOLDER && !(GPIOC->ODR&GPIO_PIN_15));
  /* Configurable common rail defaults to 18 V: let the old charger
     capacitor fall below that profile before routing to the solder tip. */
  vout_code=970;run(0,150);PsuApp_Snapshot(&s);CHECK(GPIOC->ODR&GPIO_PIN_15);CHECK(!s.control.enabled && !(TIM1->BDTR&TIM_BDTR_MOE));CHECK(!TIM1->CCR1 && !TIM1->CCR2);
  command("@4 ON\n");CHECK(strstr(last_tx,"\"error\":\"local_mode_required\"")!=0);
  open_menu();turn(3);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_SOLDER && s.menu.notice==1);
  tap(GPIO_PIN_9);CHECK(!(TIM3->CCR1 && (TIM3->CCMR1&TIM_CCMR1_OC1M)==TIM_OCMODE_PWM1));tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.modes.mode==PSU_MODE_LAB && !s.menu.page);
  vout_code=0;run(0,2200);tap(GPIO_PIN_9);run(0,1200);PsuApp_Snapshot(&s);CHECK(s.control.enabled && s.modes.run_seconds>=1);
  tap(GPIO_PIN_9);PsuApp_Snapshot(&s);uint32_t elapsed=s.modes.run_seconds;run(0,1100);PsuApp_Snapshot(&s);CHECK(!s.control.enabled && s.modes.run_seconds==elapsed);
  open_menu();turn(4);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);
  CHECK(s.menu.page==PSU_MENU_DISPLAY && s.menu.display==PSU_DISPLAY_NUMBER);
  turn(1);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(!s.menu.page && s.menu.display==PSU_DISPLAY_GRAPH && !s.control.enabled);
  run(0,160);tap(GPIO_PIN_9);PsuApp_Snapshot(&s);CHECK(s.control.enabled);
  uint32_t revision=s.settings_revision;float voltage=s.control.voltage_set,current=s.control.current_set;
  open_menu();turn(4);tap(GPIO_PIN_4);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);
  CHECK(!s.menu.page && s.menu.display==PSU_DISPLAY_NUMBER && s.control.enabled);
  CHECK(s.control.voltage_set==voltage && s.control.current_set==current && s.settings_revision==revision);
  open_menu();turn(4);tap(GPIO_PIN_4);turn(2);tap(GPIO_PIN_4);PsuApp_Snapshot(&s);CHECK(s.menu.page==PSU_MENU_ROOT && s.menu.display==PSU_DISPLAY_NUMBER);
  tap(GPIO_PIN_9);PsuApp_Snapshot(&s);CHECK(!s.control.enabled);
  return 0;
}
