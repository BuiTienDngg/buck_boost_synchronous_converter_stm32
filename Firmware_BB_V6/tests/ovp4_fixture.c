#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static void setup(void){
 Psu_Init(&p);p.initialized=p.temp_valid=1;p.ready_ms=1000;
 p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;
 CHECK(Psu_Enable(&p));
}
static void high(void){Psu_FastSample(&p,p.ovp_raw,0,1300);}
int test_main(void){
 setup();uint16_t threshold=p.ovp_raw;
 high();high();high();CHECK(!p.fault && p.enabled && p.ovp_count==3);
 Psu_FastSample(&p,threshold-1,0,1300);CHECK(!p.ovp_count);
 high();high();high();CHECK(!p.fault);high();
 CHECK((p.fault&PSU_F_OVP) && !p.enabled && !p.duty_buck && !p.duty_boost_ho);
 CHECK(p.fault_sample.vout_adc==threshold && p.fault_sample.ovp_adc==threshold && p.fault_sample.sample_index==8);
 setup();high();high();high();Psu_Disable(&p);CHECK(!p.ovp_count);
 CHECK(Psu_Enable(&p));high();CHECK(!p.fault && p.ovp_count==1);
 setup();Psu_Disable(&p);high();high();high();high();CHECK(!p.fault && !p.ovp_count);
 setup();Psu_FastSample(&p,0,p.ocp_raw,1300);Psu_FastSample(&p,0,p.ocp_raw,1300);CHECK(p.fault&PSU_F_OCP);
 return 0;
}
