#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;static PsuModes m;
static void setup(void){
 Psu_Init(&p);PsuModes_Init(&m);p.initialized=p.temp_valid=1;p.ready_ms=1000;
 p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;
 CHECK(PsuModes_Select(&m,&p,PSU_MODE_DUTY,100));
 CHECK(PsuModes_Start(&m,&p,250));
}
static void tick(void){p.adc_age_ms=p.temp_age_ms=0;Psu_Tick1kHz(&p);}
int test_main(void){
 setup();CHECK(p.enabled && p.open_loop && !p.manual_duty_pm);tick();CHECK(!p.duty_buck && !p.duty_boost_ho);
 p.manual_duty_pm=250;tick();CHECK(p.duty_buck==0.25f && p.duty_boost_ho==0.9f);
 p.vout=p.vout_raw=3;tick();CHECK(p.duty_buck==0.25f && !p.cv_i && !p.cc_i);
 PsuModes_Stop(&m,&p);CHECK(!p.enabled && !p.manual_duty_pm && !p.duty_buck);
 CHECK(PsuModes_Start(&m,&p,500));tick();CHECK(!p.duty_buck);
 p.manual_duty_pm=500;p.current_raw=p.current_set+0.1f;tick();CHECK(p.fault && !p.enabled && !p.duty_buck);
 setup();p.manual_duty_pm=100;p.vin_raw=1;for(unsigned n=0;n<6;++n)tick();CHECK((p.fault&PSU_F_VIN_UV) && !p.enabled);
 setup();PsuModes_Stop(&m,&p);CHECK(PsuModes_Select(&m,&p,PSU_MODE_LAB,700) && !p.open_loop);
 PsuView view;
 hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;htim4.Instance=TIM4;
 TIM1->ARR=PSU_PWM_PERIOD;hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
 PsuApp_Init();run(0,160);run(GPIO_PIN_4,1100);run(0,60);
 TIM2->CNT-=5*PSU_ENCODER_COUNTS;run(0,10);tap(GPIO_PIN_4);run(0,160);PsuApp_Snapshot(&view);
 CHECK(view.modes.mode==PSU_MODE_DUTY && !view.control.enabled);
 tap(GPIO_PIN_13);run(0,10);PsuApp_Snapshot(&view);
 CHECK(view.control.enabled && !view.control.manual_duty_pm && !TIM1->CCR1 && !TIM1->CCR2 && GPIOA->BRR==SD_Pin);
 TIM2->CNT-=PSU_ENCODER_COUNTS;run(0,10);PsuApp_Snapshot(&view);
 CHECK(view.control.manual_duty_pm==5 && TIM1->CCR1==14 && TIM1->CCR2==2592);
 tap(GPIO_PIN_13);run(0,10);PsuApp_Snapshot(&view);CHECK(!view.control.enabled && !view.control.manual_duty_pm);
 return 0;
}
