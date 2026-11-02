#include <math.h>
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static void setup(float vin,float out){
 Psu_Init(&p);p.initialized=p.temp_valid=1;p.ready_ms=1000;p.temp_c=30;
 p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=vin;p.vout=p.vout_raw=out;
 Psu_Setpoint(&p,out,3);CHECK(Psu_Enable(&p));
 p.settings.cv_kp=p.settings.cv_ki=p.settings.cv_kd=0;
 p.settings.cc_kp=p.settings.cc_ki=p.settings.cc_kd=0;
}
static void tick(void){p.adc_age_ms=p.temp_age_ms=0;Psu_Tick1kHz(&p);}
int test_main(void){
 setup(30,18);tick();CHECK(p.duty_buck==0.375f && p.duty_boost_ho==0.625f);
 setup(12,18);tick();CHECK(fabsf(p.duty_buck-0.6f)<0.00001f && fabsf(p.duty_boost_ho-0.4f)<0.00001f);
 setup(18,18);tick();CHECK(p.duty_buck==0.5f && p.duty_boost_ho==0.5f);
 p.cv_i=10;tick();CHECK(p.duty_buck<=0.90f && p.duty_boost_ho>=0.09999f);
 setup(30,1.5f);tick();CHECK(!p.duty_buck && !p.duty_boost_ho);
 setup(24,5);p.settings.cv_kp=0.03f;p.vout=6;tick();CHECK(p.duty_buck<5.0f/29.0f);
 setup(24,5);p.settings.cc_kp=0.1f;p.current=4;tick();CHECK(p.duty_buck<5.0f/29.0f);
 setup(24,5);p.open_loop=1;p.manual_duty_pm=250;tick();CHECK(p.duty_buck==0.25f && p.duty_boost_ho==0.75f);
 p.manual_duty_pm=0;tick();CHECK(!p.duty_buck && !p.duty_boost_ho);
 setup(24,5);Psu_FastSample(&p,0,p.ocp_raw,1300);Psu_FastSample(&p,0,p.ocp_raw,1300);CHECK(p.fault && !p.enabled && !p.duty_buck);
 hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;htim4.Instance=TIM4;
 TIM1->ARR=PSU_PWM_PERIOD;hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
 vout_code=270;PsuApp_Init();run(0,160);tap(GPIO_PIN_13);run(0,160);
 PsuView v;PsuApp_Snapshot(&v);CHECK(v.control.enabled && !v.control.fault);
 CHECK(TIM1->CCR1>0 && TIM1->CCR2>0 && TIM1->CCR1+TIM1->CCR2>=3599 && TIM1->CCR1+TIM1->CCR2<=3600);
 CHECK(TIM1->CCR1<=3240 && TIM1->CCR2<=3240);
 tap(GPIO_PIN_13);run(0,160);PsuApp_Snapshot(&v);CHECK(!v.control.enabled);
 return 0;
}
