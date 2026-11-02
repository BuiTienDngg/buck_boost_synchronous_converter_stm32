#include "psu_app.h"
#include <string.h>
SPI_HandleTypeDef hspi1;
static uint32_t preview_ms;
uint32_t preview_bytes;
uint32_t HAL_GetTick(void){return preview_ms;}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *h,const uint8_t *p,uint16_t n,uint32_t t) { (void)h;(void)p;(void)t;preview_bytes+=n;return HAL_OK; }
void HAL_GPIO_WritePin(GPIO_TypeDef *g,uint16_t p,GPIO_PinState s) {(void)g;(void)p;(void)s;}
void HAL_Delay(uint32_t n) {preview_ms+=n;}
void Error_Handler(void) {for(;;){}}
void preview(unsigned scenario) {
  PsuView s;memset(&s,0,sizeof(s));PsuSettings_Defaults(&s.control.settings);
  PsuModes_Init(&s.modes);s.modes.run_seconds=3723;
  s.control.vin=24;s.control.vout=12.30f;s.control.current=3.4f;s.control.voltage_set=12.30f;s.control.current_set=5;
  s.control.temp_c=44;s.control.temp_valid=1;s.control.enabled=1;s.control.state=PSU_CV;
  if(scenario==1) {s.control.fault=PSU_F_OCP;s.control.enabled=0;s.control.current=0;s.control.vout=0;}
  if(scenario==2) {s.control.config_errors=64;s.control.enabled=0;s.control.current=0;s.control.vout=0;}
  if(scenario==3) {s.control.start_block=PSU_B_TEMP;s.control.enabled=0;s.control.current=0;s.control.vout=0;s.control.temp_valid=0;}
  if(scenario==4) {
    s.control.fault=PSU_F_OVP;s.control.enabled=0;s.control.current=0;s.control.vout=0;
    s.control.voltage_set=5;s.control.current_set=1;
    s.control.fault_sample.cause=PSU_F_OVP;s.control.fault_sample.vout_adc=334;s.control.fault_sample.ovp_adc=310;
  }
  if(scenario>=5 && scenario<=7) {
    s.control.enabled=0;s.control.current=0;s.control.vout=3.2f;
    s.discharge_state=scenario==5?PSU_D_ACTIVE:scenario==6?PSU_D_NO_DROP:PSU_D_DONE;
    s.discharge_pulses=347;s.discharge_ticks=1440;
    if(scenario==7)s.control.vout=0.45f;
  }
  if(scenario==8) {
    s.control.state=PSU_RAMP;s.control.voltage_set=5;s.control.current_set=1;
    s.control.vout=2.8f;s.control.current=.14f;s.control.reference_v=3.0f;
  }
  if(scenario==9) {s.edit_digit=3;}
  if(scenario==10) {s.edit_current=1;s.edit_digit=1;}
  if(scenario==12){s.control.enabled=0;s.control.current=0;s.control.vout=0;}
  if(scenario==13){s.control.vin=40;s.control.vout=s.control.voltage_set=36;s.control.current=s.control.current_set=8;s.control.temp_c=105;}
  if(scenario>=14 && scenario<=17){s.control.enabled=0;s.menu.page=scenario==14?PSU_MENU_ROOT:scenario==15?PSU_MENU_RAMP:PSU_MENU_CHARGE;s.menu.selected=scenario==15?2:scenario==17?6:0;s.menu.editing=scenario==16;s.modes.chemistry=PSU_CELL_LIFEPO4;s.modes.cells=6;s.modes.current_ma=2000;}
  if(scenario>=18 && scenario<=20){
    s.modes.mode=PSU_MODE_CHARGE;s.modes.cells=2;s.modes.current_ma=1000;s.control.voltage_set=8.4f;s.control.current_set=1;s.control.vout=8.35f;s.control.current=0.8f;s.edit_current=1;s.edit_digit=1;
    s.modes.phase=scenario==18?PSU_CH_CCCV:scenario==19?PSU_CH_FULL:PSU_CH_ERROR;s.control.enabled=scenario==18;s.control.state=PSU_CC;
    if(scenario==20)s.modes.error=PSU_CH_HIGH;
    if(scenario==19)s.control.current=0;
  }
  if(scenario==21){s.modes.solder.on_ms=18;s.hardware.heater=1;s.modes.mode=PSU_MODE_SOLDER;s.modes.solder_request=s.modes.solder_on=1;s.modes.solder.phase=PSU_SOL_RUN;s.modes.solder.valid=1;s.modes.solder.temperature=299;s.modes.solder.raw=1522;s.modes.solder.active_target=300;s.control.vout=24;s.control.current=2.1f;}
  if(scenario==22){s.modes.mode=PSU_MODE_CHARGE;s.modes.cells=6;s.modes.chemistry=PSU_CELL_LIFEPO4;s.modes.phase=PSU_CH_PRE;s.control.vout=15.5f;s.control.current=0.05f;s.control.current_set=0.05f;s.edit_current=1;}
  if(scenario==23 || scenario==24){s.menu.page=PSU_MENU_ROOT;s.menu.selected=2;s.menu.notice=1;if(scenario==24){s.control.fault=PSU_F_OCP;s.control.enabled=0;}}
  if(scenario==47)s.control.current=0;
  PsuDisplay_Init();PsuDisplay_Update(&s);
  if(scenario>=25 && scenario<=34){
    s.control.initialized=1;s.control.adc_age_ms=1;s.menu.display=PSU_DISPLAY_GRAPH;
    s.control.voltage_set=20;s.control.current_set=8;s.control.vin=15.3f;
    /* Keep gathering history while a menu is visible; final frame draws it.
       180 samples exercise ring wrap. 250ms slots cross SysTick wrap too. */
    if(scenario==30)preview_ms=0xfffff000u;
    s.menu.page=PSU_MENU_DISPLAY;
    for(unsigned n=0;n<180;++n){
      preview_ms+=250;
      s.control.vout=n<90?10.0f+n*.07f:20.0f+(float)(n%13)*.02f;
      s.control.current=n<110?1.5f+(float)(n%7)*.03f:6.9f+(float)(n%9)*.01f;
      s.control.adc_age_ms=scenario==28 && n>=100 && n<=109?5:1;
      PsuDisplay_Update(&s);
    }
    s.menu.page=PSU_MENU_CLOSED;
    if(scenario==26){s.control.state=PSU_CC;s.control.vout=15.19f;s.control.current=6.92f;}
    if(scenario==27){s.control.fault=PSU_F_OCP;s.control.enabled=0;s.control.vout=10;s.control.current=0;}
    if(scenario==29){s.menu.page=PSU_MENU_DISPLAY;s.menu.selected=1;}
    if(scenario==31){s.control.voltage_set=59.9f;s.control.current_set=15;s.control.vout=59.85f;s.control.current=14.8f;}
    if(scenario==34){s.control.enabled=0;s.control.vout=s.control.current=0;}
    preview_ms+=250;PsuDisplay_Update(&s);
    if(scenario==32){s.menu.display=PSU_DISPLAY_NUMBER;PsuDisplay_Update(&s);}
    /* A long missed update becomes gaps, not fabricated intermediate data. */
    if(scenario==33){preview_ms+=40000;PsuDisplay_Update(&s);}
  }
  if((scenario>=35 && scenario<=45) || scenario>=52){
    s.modes.mode=PSU_MODE_SOLDER;s.modes.solder_request=s.modes.solder_on=1;s.modes.solder.phase=PSU_SOL_RUN;
    s.modes.solder.valid=1;s.modes.solder.temperature=298;s.modes.solder.raw=1521;s.modes.solder.active_target=300;s.control.vout=24;s.control.current=2.1f;
    if(scenario==35 || scenario==41){s.menu.page=PSU_MENU_SOLDER;s.menu.selected=scenario==41?1:0;s.menu.editing=scenario==41;}
    if(scenario==36){s.modes.solder.sleeping=1;s.modes.solder.active_target=200;s.modes.solder.temperature=201;}
    if(scenario==37){s.modes.solder.error=PSU_SOL_NO_RISE;s.modes.solder_request=0;s.control.enabled=0;s.modes.solder.phase=PSU_SOL_ERROR;}
    if(scenario==38){s.modes.solder.tip=PSU_TIP_C210;s.control.vout=12;s.control.current=1.2f;}
    if(scenario==40){s.modes.solder.valid=0;s.modes.solder.phase=PSU_SOL_PRECHARGE;}
    if(scenario==42){s.modes.solder.raw=0;s.modes.solder.valid=0;s.modes.solder.error=PSU_SOL_SENSOR;s.modes.solder_request=0;s.control.enabled=0;}
    if(scenario==43){s.modes.solder.raw=4095;s.modes.solder.valid=0;s.modes.solder.error=PSU_SOL_HOT;s.modes.solder_request=0;s.control.enabled=0;}
    if(scenario==44)s.modes.solder.temperature=295;
    if(scenario==45)s.modes.solder.temperature=301;
    PsuDisplay_Update(&s);
    if(scenario==44){s.modes.solder.temperature=301;PsuDisplay_Update(&s);}
    if(scenario==39){s.modes.mode=PSU_MODE_LAB;s.control.enabled=0;s.control.voltage_set=5;s.control.current_set=1;PsuDisplay_Update(&s);}
  }
  if(scenario>=46 && scenario<=51){
    if(scenario==48 || scenario==49){
      s.modes.mode=PSU_MODE_SOLDER;s.modes.solder.valid=1;s.modes.solder.temperature=298;
      s.modes.solder.command=.40f;PsuDisplay_Update(&s);
    }
    if(scenario==50){s.control.vin=10.0f;PsuDisplay_Update(&s);}
    if(scenario==51){s.menu.page=PSU_MENU_ROOT;PsuDisplay_Update(&s);}
    preview_bytes=0;
    if(scenario==47){s.control.current=0;s.control.vout=12.31f;/* isolate voltage cell */}
    if(scenario==48)s.modes.solder.command=.41f;
    if(scenario==49)s.modes.solder.temperature=299;
    if(scenario==50)s.control.vin=9.9f;
    if(scenario==51)s.menu.selected=1;
    PsuDisplay_Update(&s);
  }
  if(scenario>=52){
    s.control.settings.solder_current=3;s.control.current=1.5f;s.modes.solder.command=.4f;PsuDisplay_Update(&s);
    preview_bytes=0;
    if(scenario==53)s.control.current=3.4f;
    if(scenario==54)s.control.current=0;
    if(scenario==55)s.control.settings.solder_current=6;
    if(scenario==56)PsuSolder_SetTarget(&s.modes,350);
    if(scenario==57){s.modes.solder_request=0;s.control.current=0;}
    if(scenario==58){s.control.fault=PSU_F_OVP;s.control.current=0;}
    if(scenario==59)s.modes.run_seconds++;
    PsuDisplay_Update(&s);
  }
  /* The underline must move when only the digit changes. */
  if(scenario==11) {s.edit_digit=2;PsuDisplay_Update(&s);}
}

/* Standalone renderer has no application foreground to service. */
void PsuApp_DisplayProgress(void){}
