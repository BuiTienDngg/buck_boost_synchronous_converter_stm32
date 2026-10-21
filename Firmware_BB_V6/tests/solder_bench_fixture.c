#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static PsuModes m;
static uint32_t clock;
volatile uint32_t waveform_rises,waveform_min_period=1000,waveform_max_period;
volatile uint32_t waveform_high_ms,waveform_low_ms,waveform_min_width=1000,waveform_max_width;
static void ready(uint8_t tip,uint16_t raw){
  Psu_Init(&p);PsuModes_Init(&m);m.solder.tip=tip;
  p.initialized=p.temp_valid=1;p.ready_ms=1000;p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;
  PsuModes_Select(&m,&p,PSU_MODE_SOLDER,100);
  for(clock=100;clock<700;++clock){
    if(p.enabled){p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;}
    PsuSolder_Sample(&m,raw,clock);PsuModes_Tick(&m,&p,clock);
  }
}
int test_main(void){
  PsuView s;
  CHECK(PSU_SOLDER_TEST18 && !PSU_SOLDER_TEMP_PROTECTION);
  ready(PSU_TIP_C245,0);CHECK(p.enabled && p.voltage_set==18 && p.current_set==6 && !m.solder.error);
  CHECK(m.solder.valid && m.solder.phase==PSU_SOL_RUN);
  ready(PSU_TIP_C210,4095);CHECK(p.enabled && p.voltage_set==18 && p.current_set==6 && !m.solder.error);
  CHECK(m.solder.valid && m.solder.duty<0.50f && m.solder.ramp_ms<2000);
  ready(PSU_TIP_C245,1200);
  for(unsigned n=0;n<16000;++n){PsuSolder_Sample(&m,1200,clock);PsuModes_Tick(&m,&p,clock++);}
  CHECK(p.enabled && !m.solder.error); /* NO_RISE disabled for the test. */
  p.fault=PSU_F_OCP;PsuModes_Tick(&m,&p,clock++);
  CHECK(!p.enabled && !m.solder.duty && !m.solder_request);
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  tip_code=1200;PsuApp_Init();run(0,160);PsuApp_Snapshot(&s);
  CHECK(s.modes.mode==PSU_MODE_LAB && s.modes.solder.raw==1200 && !s.control.enabled);
  CHECK(s.hardware.pb13_mode==2 && !s.hardware.pb13_odr && !s.hardware.pb13_idr);
  run(GPIO_PIN_8,1100);run(0,60);vout_code=971;run(0,5500);PsuApp_Snapshot(&s);
  CHECK(s.modes.mode==PSU_MODE_SOLDER && s.control.voltage_set==18 && s.control.current_set==6);
  CHECK(s.control.enabled && s.modes.solder.phase==PSU_SOL_RUN && !s.modes.solder.error);
  /* Measure physical GPIO edges at fixed 50% test duty, not just the duty field. */
  uint32_t rise=0;uint8_t previous=(GPIOB->ODR&HEATER_Pin)!=0;
  for(unsigned n=0;n<1000;++n){
    run(0,1);uint8_t level=(GPIOB->ODR&HEATER_Pin)!=0;
    if(level)++waveform_high_ms;else ++waveform_low_ms;
    if(level && !previous){
      ++waveform_rises;
      if(rise){uint32_t period=now-rise;if(period<waveform_min_period)waveform_min_period=period;if(period>waveform_max_period)waveform_max_period=period;}
      rise=now;
    }
    if(!level && previous && rise){
      uint32_t width=now-rise;if(width<waveform_min_width)waveform_min_width=width;if(width>waveform_max_width)waveform_max_width=width;
    }
    previous=level;
  }
  CHECK(waveform_rises==10 && waveform_min_period==100 && waveform_max_period==100);
  CHECK(waveform_min_width==50 && waveform_max_width==50 && waveform_high_ms==500 && waveform_low_ms==500);
  PsuApp_Snapshot(&s);CHECK(!s.control.fault && s.modes.solder.valid && !s.modes.solder.error);
  tip_code=0;run(0,150);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && !s.modes.solder.error && s.modes.solder.raw==0);
  tip_code=4095;run(0,150);PsuApp_Snapshot(&s);
  CHECK(s.control.enabled && !s.modes.solder.error && s.modes.solder.raw==4095 && s.modes.solder.duty==0.50f);
  uint32_t edges=s.hardware.heater_rises;
  run(0,1000);PsuApp_Snapshot(&s);
  CHECK(s.hardware.heater_rises-edges==10 && s.hardware.pb13_mode==2);
  CHECK(s.hardware.pb13_odr==s.hardware.heater && s.hardware.pb13_idr==s.hardware.pb13_odr && s.modes.solder.duty==0.50f);
  hUsbDeviceFS.dev_state=USBD_STATE_CONFIGURED;hUsbDeviceFS.pClassData=&cdc;run(0,110);
  CHECK(strstr(last_tx,"\"protect\":0") && strstr(last_tx,"\"fixed_pwm\":1") && strstr(last_tx,"\"pin_mode\":2") && strstr(last_tx,"\"pwm_hz\":10") && strstr(last_tx,"\"set_mV\":18000"));
  tap(GPIO_PIN_9);tip_code=1500;run(0,200);PsuApp_Snapshot(&s);
  CHECK(!s.control.enabled && !s.modes.solder_request && s.modes.solder.raw==1500 && !(GPIOB->ODR&HEATER_Pin));
  HAL_ADC_ErrorCallback(&hadc1);tip_code=1700;run(0,200);PsuApp_Snapshot(&s);
  CHECK((s.control.fault&PSU_F_ADC) && !s.control.enabled && s.modes.solder.raw==1700);
  CHECK(!(GPIOB->ODR&HEATER_Pin) && GPIOA->BRR==SD_Pin && !(TIM1->BDTR&TIM_BDTR_MOE));
  return 0;
}
