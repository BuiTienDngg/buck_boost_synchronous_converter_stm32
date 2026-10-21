#include "psu_modes.h"
volatile uint32_t test_count,test_failed,test_lines[256];
#define CHECK(x) do{test_lines[test_count++]=(x)?0:__LINE__;if(test_lines[test_count-1])++test_failed;}while(0)
static PsuControl p;static PsuModes m;
static void setup(float v){Psu_Init(&p);PsuModes_Init(&m);p.initialized=p.temp_valid=1;p.ready_ms=1000;p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.vout=p.vout_raw=v;p.temp_c=30;}
static void charging(void){CHECK(PsuModes_Select(&m,&p,PSU_MODE_CHARGE,100));CHECK(PsuModes_Start(&m,&p,250));PsuModes_Tick(&m,&p,1250);CHECK(p.enabled);}
int test_main(void){
  uint32_t now;
  for(unsigned chem=0;chem<2;++chem)for(unsigned cells=1;cells<=6;++cells){
    setup((chem?3.2f:3.7f)*cells);m.chemistry=chem;m.cells=cells;
    CHECK(PsuModes_FullMillivolts(&m)==(chem?3650:4200)*cells);
    CHECK(PsuModes_Select(&m,&p,PSU_MODE_CHARGE,100));CHECK(PsuModes_Start(&m,&p,250));CHECK(!p.enabled);
    PsuModes_Tick(&m,&p,1249);CHECK(!p.enabled && m.phase==PSU_CH_CHECK);
    PsuModes_Tick(&m,&p,1250);CHECK(p.enabled && m.phase==PSU_CH_CCCV);
    CHECK(!PsuModes_Select(&m,&p,PSU_MODE_SOLDER,1300));
    PsuModes_Stop(&m,&p);CHECK(!p.enabled && m.phase==PSU_CH_STOP && m.discharge_inhibit);
  }
  setup(2.8f);charging();CHECK(m.phase==PSU_CH_PRE && p.voltage_set>2.99f && p.voltage_set<3.01f && p.current_set<0.051f);
  p.vout=p.vout_raw=3.0f;now=1250;
  for(unsigned n=0;n<5000;++n)PsuModes_Tick(&m,&p,++now);
  CHECK(m.phase==PSU_CH_CCCV && p.voltage_set>4.19f && p.current_set==0.5f);
  p.vout=p.vout_raw=4.195f;p.current=p.current_raw=0.04f;p.state=PSU_CV;p.current_limit=0.5f;m.current_seen=1;
  for(unsigned n=0;n<1000;++n)PsuModes_Tick(&m,&p,++now);
  CHECK(m.tail_ms==1000);p.state=PSU_CC;PsuModes_Tick(&m,&p,++now);CHECK(!m.tail_ms);p.state=PSU_CV;
  for(unsigned n=0;n<59999;++n)PsuModes_Tick(&m,&p,++now);
  CHECK(m.phase==PSU_CH_CCCV && p.enabled);PsuModes_Tick(&m,&p,++now);
  CHECK(m.phase==PSU_CH_FULL && !p.enabled && m.run_seconds>=60);
  uint32_t elapsed=m.run_seconds;
  for(unsigned n=0;n<1000;++n)PsuModes_Tick(&m,&p,++now);
  CHECK(!p.enabled && m.phase==PSU_CH_FULL && m.run_seconds==elapsed);
  CHECK(m.ovp_adc>=228 && m.ovp_adc<=230);
  setup(2.0f);CHECK(PsuModes_Select(&m,&p,PSU_MODE_CHARGE,100));CHECK(!PsuModes_Start(&m,&p,250) && m.error==PSU_CH_LOW && !p.enabled);
  setup(4.4f);CHECK(PsuModes_Select(&m,&p,PSU_MODE_CHARGE,100));CHECK(!PsuModes_Start(&m,&p,250) && m.error==PSU_CH_HIGH && !p.enabled);
  setup(3.7f);m.cells=7;CHECK(PsuModes_Select(&m,&p,PSU_MODE_CHARGE,100));CHECK(!PsuModes_Start(&m,&p,250) && m.error==PSU_CH_CONFIG);
  setup(3.7f);charging();m.hours=1;PsuModes_Tick(&m,&p,m.started_ms+3600000u);CHECK(!p.enabled && m.error==PSU_CH_TIMEOUT);
  setup(2.8f);charging();m.current_seen=1;PsuModes_Tick(&m,&p,m.phase_ms+1800000u);CHECK(!p.enabled && m.error==PSU_CH_PRE_TIMEOUT);
  setup(3.7f);charging();PsuModes_Tick(&m,&p,m.phase_ms+30000u);CHECK(!p.enabled && m.error==PSU_CH_NO_CURRENT);
  setup(3.7f);charging();p.fault=PSU_F_OVP;Psu_Disable(&p);PsuModes_Tick(&m,&p,1300);CHECK(!p.enabled && m.phase==PSU_CH_ERROR);
  setup(3.7f);charging();PsuModes_Stop(&m,&p);CHECK(PsuModes_Select(&m,&p,PSU_MODE_LAB,1400));
  CHECK(m.discharge_inhibit);p.vout_raw=3.7f;for(now=1400;now<3500;++now)PsuModes_Tick(&m,&p,now);CHECK(m.discharge_inhibit);
  p.vout_raw=0.2f;for(now=3500;now<5502;++now)PsuModes_Tick(&m,&p,now);CHECK(!m.discharge_inhibit);
  setup(0);CHECK(PsuModes_Select(&m,&p,PSU_MODE_SOLDER,100));PsuModes_Tick(&m,&p,199);CHECK(!m.solder_on);
  PsuModes_Tick(&m,&p,200);CHECK(m.solder_on && !p.enabled && !m.solder.duty);
  p.fault=PSU_F_ADC;PsuModes_Tick(&m,&p,201);CHECK(!m.solder_on && !m.solder_request && m.solder.error==PSU_SOL_HEALTH);
  p.fault=0;PsuModes_Tick(&m,&p,300);CHECK(!m.solder_request);CHECK(PsuModes_Start(&m,&p,301));
  PsuModes_Tick(&m,&p,302);CHECK(!p.enabled && !m.solder.duty);
  p.vin_raw=5;PsuModes_Tick(&m,&p,304);CHECK(!m.solder_request && !p.enabled);
  setup(0);CHECK(PsuModes_Start(&m,&p,200));for(now=200;now<1200;++now)PsuModes_Tick(&m,&p,now);CHECK(m.run_seconds==1);
  PsuModes_Stop(&m,&p);for(now=1200;now<3200;++now)PsuModes_Tick(&m,&p,now);CHECK(m.run_seconds==1);
  CHECK(PsuModes_Start(&m,&p,3200));PsuModes_Tick(&m,&p,3201);CHECK(m.run_seconds==0);
  return 0;
}
