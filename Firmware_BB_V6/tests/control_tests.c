#include "psu_control.h"
#include <math.h>
volatile uint32_t test_count, test_failed, test_lines[256];
#define CHECK(x) do { uint32_t passed=(x)?1:0; test_lines[test_count++]= passed?0:__LINE__; if(!passed) ++test_failed; } while(0)
static uint16_t voltage(float v, float divider) { return (uint16_t)(v*4095.0f/(PSU_VDDA_V*divider)+0.5f); }
static uint16_t amps(float a) { return (uint16_t)((a*PSU_SHUNT_OHM*PSU_CURRENT_GAIN*PSU_CURRENT_ATTENUATION+PSU_CURRENT_OFFSET_V)*4095.0f/PSU_VDDA_V+0.5f); }
static void tick(PsuControl *p,float vin,float vout,float current) {
  Psu_Measurement(p,voltage(vout,PSU_VOUT_DIVIDER),amps(current),voltage(vin,PSU_VIN_DIVIDER));
  Psu_CurrentMeanSquare(p,p->current_raw*p->current_raw,0.001f);
  Psu_Temperature(p,25,1);Psu_Tick1kHz(p);
}
static void ready(PsuControl *p) { unsigned k;Psu_Init(p);for(k=0;k<150;++k)tick(p,24,0,0); }
/* First-order averaged voltage plant, used only to check regulator ownership
   and recovery. It does not model gate timing, inductor ripple or a real fan. */
static void plant(PsuControl *p,float vin,float resistance,float *vout,unsigned ms) {
  while(ms--) {
    float target;
    Psu_FastSample(p,voltage(*vout,PSU_VOUT_DIVIDER),amps(*vout/resistance),voltage(vin,PSU_VIN_DIVIDER));
    tick(p,vin,*vout,*vout/resistance);
    target=p->enabled && p->duty_buck>0?vin*p->ratio:0;
    *vout+=0.05f*(target-*vout);
  }
}
int test_main(void) {
  PsuControl p; unsigned k; float before;
  Psu_Init(&p);CHECK(!p.enabled);CHECK(p.state==PSU_OFF);CHECK(!Psu_Enable(&p));
#ifdef TEST_LOCKED_CONFIG
  CHECK((p.config_errors & 7)==0);CHECK((p.config_errors & 32)!=0);CHECK((p.config_errors & 64)!=0);
  CHECK(p.config_errors==0x60u);
  ready(&p);CHECK(!Psu_Enable(&p));CHECK(!p.enabled);
  /* Independent raw-code fixtures for 220k/10k VIN, 220k/10k VOUT,
     VDDA=3.3 V and shunt 1mOhm/gain 100/REF=0 V. */
  Psu_Measurement(&p,1942,993,2158);
  CHECK(p.vin_raw>39.97f && p.vin_raw<40.03f);
  CHECK(p.vout_raw>35.97f && p.vout_raw<36.03f);
  CHECK(p.current_raw>7.98f && p.current_raw<8.02f);
  CHECK(PSU_CURRENT_PEAK_A*PSU_SHUNT_OHM*PSU_CURRENT_GAIN<PSU_VDDA_V);
  Psu_FastSample(&p,0,4095,voltage(24,PSU_VIN_DIVIDER));CHECK(!p.fault);
  Psu_FastSample(&p,0,4095,voltage(24,PSU_VIN_DIVIDER));CHECK(p.fault & PSU_F_SENSOR);
  return 0;
#else
  CHECK(Psu_ConfigErrors()==0);
  CHECK(PSU_CURRENT_MAX_A==15.0f && PSU_CURRENT_PEAK_A==20.0f);
  CHECK(p.ocp_raw>=2481 && p.ocp_raw<=2482);
  Psu_Measurement(&p,1942,993,2158);
  CHECK(p.vin_raw>39.97f && p.vin_raw<40.03f);
  CHECK(p.vout_raw>35.97f && p.vout_raw<36.03f);
  CHECK(p.current_raw>7.98f && p.current_raw<8.02f);
  Psu_Init(&p);CHECK(!Psu_Enable(&p));CHECK(p.start_block&PSU_B_ADC);CHECK(p.start_block&PSU_B_TEMP);
  Psu_Measurement(&p,0,0,voltage(24,PSU_VIN_DIVIDER));Psu_Temperature(&p,25,1);
  CHECK(!Psu_Enable(&p));CHECK(p.start_block==PSU_B_SETTLING);
  ready(&p);Psu_Measurement(&p,0,0,voltage(6,PSU_VIN_DIVIDER));CHECK(!Psu_Enable(&p));CHECK(p.start_block&PSU_B_VIN_LOW);
  ready(&p);Psu_Measurement(&p,0,0,voltage(60,PSU_VIN_DIVIDER));CHECK(!Psu_Enable(&p));CHECK(p.start_block&PSU_B_VIN_HIGH);
  ready(&p);Psu_Measurement(&p,voltage(1,PSU_VOUT_DIVIDER),0,voltage(24,PSU_VIN_DIVIDER));CHECK(Psu_Enable(&p));CHECK(!p.start_block);
  /* User board: 15V input, about 5V output already present while OFF. */
  Psu_Init(&p);for(k=0;k<150;++k)tick(&p,15,4.97f,0);
  CHECK(!p.enabled && !p.fault);CHECK(p.ready_ms>=100);CHECK(Psu_Enable(&p));CHECK(!p.start_block);
  CHECK(p.reference_v>4.96f && p.reference_v<4.99f);CHECK(p.state==PSU_RAMP);
  Psu_FastSample(&p,voltage(4.97f,PSU_VOUT_DIVIDER),0,voltage(15,PSU_VIN_DIVIDER));
  tick(&p,15,4.97f,0);CHECK(p.enabled && !p.fault);CHECK(p.reference_v<=5.0f);CHECK(p.duty_buck>0.0f);
  Psu_Disable(&p);CHECK(!p.enabled && p.duty_buck==0.0f);
  for(k=0;k<150;++k)tick(&p,15,4.97f,0);CHECK(Psu_Enable(&p));CHECK(!p.start_block);
  /* A precharged output above the setpoint waits with gates off. Existing
     OVP must still trip if output exceeds the unchanged threshold. */
  Psu_Init(&p);for(k=0;k<150;++k)tick(&p,15,5.2f,0);CHECK(Psu_Enable(&p));
  tick(&p,15,5.2f,0);CHECK(p.enabled && !p.fault);CHECK(p.state==PSU_CV);
  CHECK(p.duty_buck==0.0f && p.duty_boost_ho==0.0f);
  CHECK(p.precharge_hold);
  tick(&p,15,5.0f,0);CHECK(!p.precharge_hold && p.duty_buck>0);
  /* A later small overshoot is handled by CV, without repeatedly clearing
     the PI state and reapplying feed-forward as a fresh burst. */
  p.cv_i=0.01f;tick(&p,15,5.1f,0);
  CHECK(!p.precharge_hold && p.duty_buck>0 && p.cv_i!=0 && !p.fault);
  Psu_FastSample(&p,voltage(6.0f,PSU_VOUT_DIVIDER),0,voltage(15,PSU_VIN_DIVIDER));
  CHECK(p.fault&PSU_F_OVP);CHECK(!p.enabled);CHECK(p.ready_ms==0);
  CHECK(p.fault_sample.cause==PSU_F_OVP);
  CHECK(p.fault_sample.vout_adc==voltage(6.0f,PSU_VOUT_DIVIDER) && p.fault_sample.ovp_adc==310);
  CHECK(p.fault_sample.current_adc==0 && p.fault_sample.vin_adc==voltage(15,PSU_VIN_DIVIDER));
  CHECK(p.fault_sample.sample_index==p.samples);
  Psu_FastSample(&p,0,0,voltage(15,PSU_VIN_DIVIDER));
  CHECK(p.fault_sample.vout_adc==voltage(6.0f,PSU_VOUT_DIVIDER) && p.fault_sample.sample_index==p.samples-1);
  for(k=0;k<50;++k)tick(&p,15,4.97f,0);CHECK(!Psu_ClearFault(&p));
  for(k=0;k<100;++k)tick(&p,15,4.97f,0);CHECK(Psu_ClearFault(&p));CHECK(!p.fault_sample.cause);CHECK(!p.enabled);CHECK(Psu_Enable(&p));
  ready(&p);Psu_Measurement(&p,0,amps(0.3f),voltage(24,PSU_VIN_DIVIDER));CHECK(!Psu_Enable(&p));CHECK(p.start_block&PSU_B_CURRENT);
  ready(&p);for(k=0;k<100;++k)Psu_Temperature(&p,65,1);CHECK(!Psu_Enable(&p));CHECK(p.start_block&PSU_B_HOT);
  ready(&p);CHECK(p.ready_ms>=100);CHECK(Psu_Enable(&p));CHECK(p.state==PSU_RAMP);
  CHECK(!p.start_block);
  Psu_FastSample(&p,0,p.ocp_raw,voltage(24,PSU_VIN_DIVIDER));CHECK(!p.fault);CHECK(p.enabled);
  Psu_FastSample(&p,0,p.ocp_raw-1,voltage(24,PSU_VIN_DIVIDER));CHECK(p.ocp_count==0);
  Psu_FastSample(&p,0,p.ocp_raw,voltage(24,PSU_VIN_DIVIDER));CHECK(!p.fault);
  Psu_FastSample(&p,0,p.ocp_raw,voltage(24,PSU_VIN_DIVIDER));CHECK(p.fault & PSU_F_OCP);CHECK(!p.enabled);CHECK(p.duty_buck==0 && p.duty_boost_ho==0);
  for(k=0;k<50;++k)tick(&p,24,2,0);CHECK(!Psu_ClearFault(&p));CHECK(!Psu_Enable(&p));
  for(k=0;k<200;++k)tick(&p,24,0,0);CHECK(p.fault & PSU_F_OCP);CHECK(Psu_ClearFault(&p));CHECK(!p.enabled);CHECK(Psu_Enable(&p));
  Psu_FastSample(&p,p.ovp_raw,0,voltage(24,PSU_VIN_DIVIDER));CHECK(p.fault & PSU_F_OVP);CHECK(!p.enabled);
  ready(&p);CHECK(Psu_Enable(&p));Psu_FastSample(&p,0,0,p.vin_ov_raw);CHECK(p.fault & PSU_F_VIN_OV);
  ready(&p);CHECK(Psu_Enable(&p));for(k=0;k<4;++k)tick(&p,6,0,0);CHECK(p.enabled);tick(&p,6,0,0);CHECK(p.fault & PSU_F_VIN_UV);
  ready(&p);CHECK(Psu_Enable(&p));Psu_Temperature(&p,86,1);CHECK(p.fault & PSU_F_OTP);CHECK(!p.enabled);
  ready(&p);CHECK(Psu_Enable(&p));Psu_Temperature(&p,25,0);CHECK(p.fault & PSU_F_SENSOR);
  ready(&p);CHECK(Psu_Enable(&p));for(k=0;k<3;++k)Psu_Tick1kHz(&p);CHECK(p.fault & PSU_F_ADC);
  ready(&p);CHECK(Psu_Enable(&p));for(k=0;k<205;++k){Psu_Measurement(&p,0,0,voltage(24,PSU_VIN_DIVIDER));Psu_Tick1kHz(&p);}CHECK(p.fault & PSU_F_ADC);
  ready(&p);Psu_Setpoint(&p,60,1);CHECK(p.voltage_set==5.0f);Psu_Setpoint(&p,5,16);CHECK(p.current_set==1.0f);
  Psu_Setpoint(&p,5,8);CHECK(p.current_set==8.0f);Psu_Setpoint(&p,5,1);
  Psu_Setpoint(&p,NAN,1);CHECK(p.voltage_set==5.0f);Psu_Setpoint(&p,5,NAN);CHECK(p.current_set==1.0f);
  CHECK(Psu_Enable(&p));tick(&p,24,0,0);CHECK(p.reference_v>0.009f && p.reference_v<0.011f);CHECK(p.duty_buck==0);
  for(k=0;k<1000;++k)tick(&p,24,0,0);CHECK(p.reference_v<=5);CHECK(p.duty_buck<=0.901f);CHECK(p.duty_boost_ho<=0.901f);
  CHECK(isfinite(p.cv_i) && isfinite(p.cc_i));Psu_Disable(&p);CHECK(p.duty_buck==0 && !p.enabled);
  ready(&p);Psu_Setpoint(&p,36,8);CHECK(Psu_Enable(&p));
  for(k=0;k<3000;++k)tick(&p,24,1,16.0f);CHECK(p.fault & PSU_F_CONT_CURRENT);CHECK(!p.enabled);
  ready(&p);CHECK(Psu_Enable(&p));Psu_FastSample(&p,0,4095,voltage(24,PSU_VIN_DIVIDER));CHECK(!p.fault);Psu_FastSample(&p,0,4095,voltage(24,PSU_VIN_DIVIDER));CHECK(p.fault & PSU_F_SENSOR);
  ready(&p);before=p.vin;Psu_Measurement(&p,0,0,voltage(12,PSU_VIN_DIVIDER));CHECK(p.vin<before && p.vin>12.1f);CHECK(p.vin_raw<12.1f);
  ready(&p);Psu_Setpoint(&p,24,1);CHECK(Psu_Enable(&p));tick(&p,24,24,0);Psu_Setpoint(&p,5,1);
  CHECK(p.precharge_hold);
  Psu_FastSample(&p,voltage(24,PSU_VOUT_DIVIDER),0,voltage(24,PSU_VIN_DIVIDER));CHECK(!p.fault);
  tick(&p,24,10,0);CHECK(p.ovp_reference<10.1f);Psu_FastSample(&p,voltage(12,PSU_VOUT_DIVIDER),0,voltage(24,PSU_VIN_DIVIDER));CHECK(p.fault & PSU_F_OVP);
  ready(&p);CHECK(Psu_Enable(&p));for(k=0;k<2000;++k)tick(&p,24,0,0);CHECK(!p.fault);CHECK(p.ratio>=0 && p.ratio<=11.25f);CHECK(p.duty_buck<=0.90f);
  ready(&p);CHECK(Psu_Enable(&p));for(k=0;k<1000;++k)tick(&p,24,0,1.0f);CHECK(p.fault & PSU_F_SHORT);CHECK(!p.enabled);
  ready(&p);Psu_Setpoint(&p,36,8);CHECK(Psu_Enable(&p));for(k=0;k<6000;++k)tick(&p,8,10,0);CHECK(p.duty_buck<=0.90f);CHECK(p.duty_boost_ho>=0.08f);CHECK(isfinite(p.ratio));
  /* Reported board condition: no thermal/power derating, far below 1 A.
     The current PI must not retain ownership merely because its integrator
     command is below the voltage PI command. */
  Psu_Init(&p);for(k=0;k<150;++k)tick(&p,17.8f,2.53f,0.03f);
  CHECK(Psu_Enable(&p));for(k=0;k<1000;++k)tick(&p,17.8f,2.53f,0.03f);
  CHECK(p.state==PSU_CV);CHECK(p.enabled && !p.fault);
  CHECK(p.current_limit>0.999f && p.current_limit<1.001f);
  /* Latest log: 17.811 V in, 2.613 V out, 28 mA, CV at its ceiling.
     Confirm that the command asks for more conversion, not current limiting.
     This fixture cannot establish whether the gate driver follows that command. */
  p.cv_i=11.25f;p.cc_i=11.25f;
  tick(&p,17.811f,2.613f,0.028f);
  CHECK(p.state==PSU_CV && !p.fault);CHECK(p.ratio==11.25f);
  CHECK(p.duty_buck==0.90f && p.duty_boost_ho==0.08f);
  /* A stale CC integral at the observed low command must recover with a
     direct response without a rise ramp; actual overcurrent must still immediately own CC. */
  p.cc_i=-0.20f;p.cv_i=0;p.ratio=2.53f/17.8f;before=p.ratio;
  tick(&p,17.8f,2.53f,0.03f);CHECK(p.state==PSU_CV);
  CHECK(p.ratio>before+0.05f);
  CHECK(p.duty_buck>0 && p.duty_boost_ho==0.90f);
  for(k=0;k<20;++k)tick(&p,17.8f,2.53f,1.20f);
  CHECK(p.state==PSU_CC && !p.fault);CHECK(p.current>p.current_limit);
  /* Effective derating takes priority over the knob's 1 A setting. */
  Psu_Init(&p);for(k=0;k<150;++k)tick(&p,17.8f,2.53f,0.03f);CHECK(Psu_Enable(&p));
  for(k=0;k<600;++k){
    Psu_Measurement(&p,voltage(2.53f,PSU_VOUT_DIVIDER),amps(0.5f),voltage(17.8f,PSU_VIN_DIVIDER));
    Psu_Temperature(&p,80,1);Psu_Tick1kHz(&p);
  }
  CHECK(p.current_limit<0.4f);CHECK(p.state==PSU_CC);
  ready(&p);Psu_Setpoint(&p,36,8);Psu_Measurement(&p,voltage(36,PSU_VOUT_DIVIDER),0,voltage(8,PSU_VIN_DIVIDER));
  CHECK(Psu_Enable(&p));for(k=0;k<100;++k)tick(&p,8,36,2.5f);
  CHECK(p.current_limit<1.90f && p.current_limit>1.85f);CHECK(p.state==PSU_CC);
  /* Closed-loop proxy: cold start at 5 V/1 A under a light load. */
  {float vo=0;
   ready(&p);CHECK(Psu_Enable(&p));plant(&p,17.8f,84.333f,&vo,2500);
   CHECK(!p.fault && p.enabled && p.state==PSU_CV);CHECK(vo>4.90f && vo<5.10f);
   /* A load requiring CC, then release back to the light fan-equivalent load. */
   plant(&p,17.8f,3.0f,&vo,5000);
   CHECK(!p.fault && p.enabled);CHECK(p.state==PSU_CC);CHECK(vo>2.85f && vo<3.15f);
   plant(&p,17.8f,84.333f,&vo,3000);
   CHECK(!p.fault && p.enabled && p.state==PSU_CV);CHECK(vo>4.90f && vo<5.10f);
  }
  {PsuSettings s,invalid;
   PsuSettings_Defaults(&s);CHECK(!PsuSettings_Validate(&s));
   invalid=s;invalid.i_peak=21;CHECK(PsuSettings_Validate(&invalid)!=0);
   invalid=s;invalid.i_max=8;invalid.i_peak=5;CHECK(PsuSettings_Validate(&invalid)!=0);
   invalid=s;invalid.temp_reset=80;CHECK(PsuSettings_Validate(&invalid)!=0);
   invalid=s;invalid.cv_kp=NAN;CHECK(PsuSettings_Validate(&invalid)!=0);
   invalid=s;invalid.i_cal=0.49f;CHECK(PsuSettings_Validate(&invalid)!=0);
   invalid=s;invalid.i_cal=0.5f;CHECK(PsuSettings_Validate(&invalid)!=0);
   CHECK(!PsuSettings_SetScaled(&s,"unknown",5));CHECK(!PsuSettings_SetScaled(&s,"i_peak",21000));
   CHECK(PsuSettings_SetScaled(&s,"cv_kp",20000)>0);CHECK(s.cv_kp>0.01999f && s.cv_kp<0.02001f);
   ready(&p);CHECK(Psu_Enable(&p));CHECK(!Psu_ApplySettings(&p,&s));
   Psu_Disable(&p);Psu_Setpoint(&p,24,8);s.i_max=4;s.i_peak=6;s.vout_max=12;
   CHECK(Psu_ApplySettings(&p,&s));CHECK(!p.initialized && !p.ready_ms && p.adc_age_ms>=3);
   CHECK(p.voltage_set==12 && p.current_set==4);CHECK(p.ocp_raw>=744 && p.ocp_raw<=745);
   CHECK(!Psu_Enable(&p));
   for(k=0;k<150;++k)tick(&p,17.8f,0,0);CHECK(Psu_Enable(&p));
   CHECK(p.reference_v==0 && p.state==PSU_RAMP);tick(&p,17.8f,0,0);
   CHECK(p.reference_v>0.009f && p.reference_v<0.011f && p.duty_buck==0); /* Soft-start begins from zero. */
   for(k=0;k<1250;++k)tick(&p,17.8f,0,0);CHECK(p.reference_v==12 && p.state==PSU_CV);
   Psu_Setpoint(&p,5,1);tick(&p,17.8f,0,0);CHECK(p.reference_v==5 && p.state==PSU_CV);
   Psu_Disable(&p);s.cv_kd=.001f;s.cc_kd=.001f;CHECK(Psu_ApplySettings(&p,&s));
   for(k=0;k<150;++k)tick(&p,17.8f,0,0);CHECK(Psu_Enable(&p));
   for(k=0;k<150;++k)tick(&p,17.8f,0,0);tick(&p,17.8f,1,0.1f);
   CHECK(isfinite(p.cv_command) && isfinite(p.cc_command) && p.dv_rate>0);
  }
  /* Ramp timing is in the 1 kHz control loop, independent of the 10 kHz PWM.
     Check startup, live edits, CC priority and immediate fault/OFF cancellation. */
  ready(&p);CHECK(Psu_Enable(&p));CHECK(p.reference_v==0 && p.state==PSU_RAMP);
  for(k=0;k<100;++k)tick(&p,24,0,0);
  CHECK(p.reference_v>0.99f && p.reference_v<1.01f && p.state==PSU_RAMP);
  for(k=0;k<450;++k)tick(&p,24,0,0);
  CHECK(p.reference_v==5 && p.state==PSU_CV);
  Psu_Setpoint(&p,12,1);CHECK(p.reference_v==5);
  for(k=0;k<100;++k)tick(&p,24,0,0);
  CHECK(p.reference_v>5.99f && p.reference_v<6.01f && p.state==PSU_RAMP);
  Psu_Setpoint(&p,3,1);CHECK(p.reference_v==3);tick(&p,24,0,0);
  CHECK(p.reference_v==3 && p.state==PSU_CV);
  Psu_Disable(&p);CHECK(!p.enabled && p.reference_v==0 && p.duty_buck==0);
  for(k=0;k<150;++k)tick(&p,24,2.8f,0);CHECK(Psu_Enable(&p));
  CHECK(p.reference_v>2.79f && p.reference_v<2.81f && p.state==PSU_RAMP);
  for(k=0;k<30;++k)tick(&p,24,2.8f,0);CHECK(p.reference_v==3 && p.state==PSU_CV);
  Psu_Disable(&p);Psu_Setpoint(&p,12,1);
  for(k=0;k<150;++k)tick(&p,24,0,0);CHECK(Psu_Enable(&p));
  for(k=0;k<50;++k)tick(&p,24,0,0);
  for(k=0;k<20;++k)tick(&p,24,0.1f,1.2f);
  CHECK(p.state==PSU_CC && p.reference_v<12 && !p.fault);
  Psu_Disable(&p);for(k=0;k<150;++k)tick(&p,24,0,0);CHECK(Psu_Enable(&p));
  CHECK(p.state==PSU_RAMP);
  Psu_FastSample(&p,0,p.ocp_raw,voltage(24,PSU_VIN_DIVIDER));CHECK(!p.fault);
  Psu_FastSample(&p,0,p.ocp_raw,voltage(24,PSU_VIN_DIVIDER));
  CHECK((p.fault&PSU_F_OCP) && !p.enabled && p.reference_v==0 && p.duty_buck==0);
  ready(&p);CHECK(Psu_Enable(&p));CHECK(p.state==PSU_RAMP);
  Psu_FastSample(&p,voltage(6,PSU_VOUT_DIVIDER),0,voltage(24,PSU_VIN_DIVIDER));
  CHECK((p.fault&PSU_F_OVP) && !p.enabled && p.reference_v==0);
  ready(&p);CHECK(Psu_Enable(&p));for(k=0;k<550;++k)tick(&p,24,5.0f,0.07f);
  CHECK(!p.precharge_hold && p.state==PSU_CV && !p.fault);
  p.cv_i=0.02f;tick(&p,24,5.12f,0.07f);
  CHECK(p.duty_buck>0 && p.cv_i!=0 && !p.precharge_hold);
  Psu_Setpoint(&p,4.9f,1);CHECK(p.precharge_hold);
  tick(&p,24,5.05f,0.07f);CHECK(p.duty_buck==0 && p.precharge_hold);
  tick(&p,24,4.90f,0.07f);CHECK(p.duty_buck>0 && !p.precharge_hold);
  tick(&p,24,5.01f,0.07f);CHECK(p.duty_buck>0 && !p.precharge_hold);
  /* BBV6.7 UI release retains the bench-tested BBV6.5 CC implementation
     and uses the selected gain when Flash contains no settings. */
  Psu_Init(&p);CHECK(p.settings.cc_kp==0.010f && p.settings.cc_ki==1.0f && p.settings.cc_kd==0);
  /* User-selected ramp: rate applies at startup and on upward edits. */
  ready(&p);p.settings.ramp_v_per_s=2.5f;CHECK(Psu_Enable(&p));
  for(k=0;k<100;++k)tick(&p,24,0,0);
  CHECK(p.reference_v>0.249f && p.reference_v<0.251f);
  Psu_Setpoint(&p,12,1);for(k=0;k<100;++k)tick(&p,24,0,0);
  CHECK(p.reference_v>0.499f && p.reference_v<0.501f);
  Psu_Disable(&p);p.settings.ramp_v_per_s=100;CHECK(Psu_Enable(&p));
  for(k=0;k<130;++k)tick(&p,24,0,0);CHECK(p.reference_v==12);
  Psu_Setpoint(&p,3,1);CHECK(p.reference_v==3);
  {PsuSettings bad=p.settings;bad.ramp_v_per_s=0;CHECK(PsuSettings_Validate(&bad)!=0);
   bad.ramp_v_per_s=NAN;CHECK(PsuSettings_Validate(&bad)!=0);}
  /* Full-range calibration, raw OVP and power limiting above the old 40V cap. */
  ready(&p);Psu_Setpoint(&p,59.9f,15);CHECK(p.voltage_set==59.9f && p.current_set==15);
  Psu_Measurement(&p,voltage(50,PSU_VOUT_DIVIDER),0,voltage(58,PSU_VIN_DIVIDER));CHECK(Psu_Enable(&p));
  for(k=0;k<100;++k)tick(&p,58,50,0);
  CHECK(p.current_limit>9.85f && p.current_limit<9.87f); /* 58V * 10A * .85 / 50V */
  p.settings.power_max=100;tick(&p,58,50,0);CHECK(p.current_limit>1.98f && p.current_limit<2.02f);
  CHECK(p.ovp_raw>3200 && p.ovp_raw<3250);Psu_FastSample(&p,p.ovp_raw-1,0,voltage(58,PSU_VIN_DIVIDER));CHECK(!p.fault);
  Psu_FastSample(&p,p.ovp_raw,0,voltage(58,PSU_VIN_DIVIDER));CHECK(p.fault&PSU_F_OVP);
  ready(&p);Psu_Setpoint(&p,50,15);CHECK(Psu_Enable(&p));
  for(k=0;k<100;++k)tick(&p,13.2f,10,0);CHECK(p.current_limit>11.18f && p.current_limit<11.25f);
  PsuSettings low=p.settings;low.vin_max=40;low.vout_max=36;low.i_max=8;low.i_peak=10;low.power_max=60;low.input_current_max=5;low.i_cal=0.5f;
  CHECK(!PsuSettings_Validate(&low));low.vdda=2.7f;low.vin_cal=low.vout_cal=0.8f;CHECK(!PsuSettings_Validate(&low));
  low.vin_max=60;low.vout_max=59.9f;CHECK(PsuSettings_Validate(&low)!=0);
  return 0;
#endif
}
