/* PA6 hardware PWM, quiet ADC, TIM4 dispatch and independent shutdown. */
#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static PsuModes m;
static uint32_t clock_ms;
static uint16_t raw_tip=1200;
static void step(void){
  ++clock_ms;p.adc_age_ms=p.temp_age_ms=0;
  if(m.solder.duty==0 && clock_ms-m.solder.last_heat_ms>=2)PsuSolder_Sample(&m,raw_tip,clock_ms);
  PsuModes_Tick(&m,&p,clock_ms);
}
static void steps(unsigned n){while(n--)step();}
static void setup(uint16_t raw){
  Psu_Init(&p);PsuModes_Init(&m);p.initialized=p.temp_valid=1;p.ready_ms=1000;
  p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;clock_ms=100;raw_tip=raw;
  CHECK(PsuModes_Select(&m,&p,PSU_MODE_SOLDER,clock_ms));
  steps(300);CHECK(p.enabled && m.solder.phase==PSU_SOL_PRECHARGE);
  p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;steps(PSU_SOLDER_RAIL_SETTLE_MS);
  CHECK(m.solder.phase==PSU_SOL_RUN && !m.solder.duty);
}
static void precharge(void){
  Psu_Init(&p);PsuModes_Init(&m);p.initialized=p.temp_valid=1;p.ready_ms=1000;
  p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;clock_ms=100;raw_tip=1193;
  CHECK(PsuModes_Select(&m,&p,PSU_MODE_SOLDER,clock_ms));steps(300);
  CHECK(p.enabled && m.solder_request && m.solder.phase==PSU_SOL_PRECHARGE && !m.solder.duty);
  p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;
}
int test_main(void){
  CHECK(PSU_SOLDER_RAIL_SETTLE_MS==500 && PSU_SOLDER_RAMP_MS==0);
  precharge();steps(499);
  CHECK(m.solder.phase==PSU_SOL_PRECHARGE && m.solder.rail_ms==499 && !m.solder.duty && !m.solder.command);
  steps(1);CHECK(m.solder.phase==PSU_SOL_RUN && !m.solder.duty && m.solder.position==0);
  steps(14);CHECK(!m.solder.duty);steps(1);
  CHECK(m.solder.duty>.799f && m.solder.duty<=.80001f && !m.solder.ramp_ms);
  /* The 500 ms timer counts continuously healthy voltage, not wall time. */
  precharge();steps(499);p.vout_raw=p.voltage_set*.94f;steps(1);
  CHECK(m.solder.phase==PSU_SOL_PRECHARGE && !m.solder.rail_ms && !m.solder.duty);
  p.vout_raw=p.voltage_set;steps(499);CHECK(m.solder.phase==PSU_SOL_PRECHARGE && !m.solder.duty);
  steps(1);CHECK(m.solder.phase==PSU_SOL_RUN && !m.solder.duty);
  precharge();steps(400);p.reference_v=p.voltage_set-.2f;steps(100);
  CHECK(m.solder.phase==PSU_SOL_PRECHARGE && !m.solder.rail_ms && !m.solder.duty);
  p.reference_v=p.voltage_set;p.precharge_hold=1;steps(500);
  CHECK(m.solder.phase==PSU_SOL_PRECHARGE && !m.solder.duty);
  p.precharge_hold=0;steps(500);CHECK(m.solder.phase==PSU_SOL_RUN);
  /* Cancellation and existing protections remain effective during settling. */
  precharge();steps(499);PsuModes_Stop(&m,&p);steps(1000);
  CHECK(!p.enabled && !m.solder_request && !m.solder.duty);
  precharge();steps(499);p.fault=PSU_F_OVP;steps(1);
  CHECK(!p.enabled && !m.solder_request && !m.solder.duty && m.solder.phase==PSU_SOL_ERROR);
  precharge();steps(499);p.adc_age_ms=3;PsuModes_Tick(&m,&p,++clock_ms);
  CHECK((p.fault&PSU_F_ADC) && !p.enabled && !m.solder.duty);
  return 0;
}
