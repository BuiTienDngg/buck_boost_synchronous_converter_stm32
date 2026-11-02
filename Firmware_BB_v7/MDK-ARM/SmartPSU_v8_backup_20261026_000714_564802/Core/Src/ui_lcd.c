#include "ui_lcd.h"
#include "st7789_min.h"
#include "power_stage.h"
#include "analog_monitor.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

#define BLACK 0x0000U
#define RED   0xF800U
#define BLUE  0x001FU
#define PINK  0xF81FU
#define WHITE 0xFFFFU
#define GRAY  0x8410U
#define LCD_PERIOD_MS 200U

static uint8_t s_lcd_ready;
static uint32_t s_last_draw;
static void text_line(uint16_t y, const char *txt, uint16_t color, uint8_t scale) {
    ST7789_FillRect(0U,y,240U,(uint16_t)(scale*8U+2U),BLACK);
    ST7789_DrawText(8U,y,txt,color,BLACK,scale);
}
static void fixed_2(char *dst,unsigned width, float v, const char *unit) {
    unsigned long whole,fract; long hundredths;
    if (!(v>=-999.0f && v<=999.0f)) { (void)snprintf(dst,width,"--.--%s",unit);return; }
    hundredths=(long)(v*100.0f+(v>=0.0f?0.5f:-0.5f));
    whole=(unsigned long)((hundredths<0?-hundredths:hundredths)/100L);
    fract=(unsigned long)((hundredths<0?-hundredths:hundredths)%100L);
    (void)snprintf(dst,width,"%s%lu.%02lu%s",hundredths<0?"-":"",whole,fract,unit);
}
void LCD_Init(void) {
    ST7789_FillScreen(BLACK);
    s_lcd_ready=1U;
    s_last_draw=HAL_GetTick()-LCD_PERIOD_MS;
    LCD_run();
}
void LCD_run(void) {
    PSU_Status ps;
    AnalogMonitor_Data adc;
    char t[40];
    float p;
    uint32_t now=HAL_GetTick(), runtime;
    uint8_t idx;
    static const uint8_t vcols[4]={5,6,8,9};
    static const uint8_t icols[3]={6,8,9};
    if (!s_lcd_ready || (uint32_t)(now-s_last_draw)<LCD_PERIOD_MS) return;
    s_last_draw=now;
    ps=PowerStage_GetStatus();
    AnalogMonitor_GetSnapshot(&adc);
    p=ps.vout_v*ps.iout_a;
    if (p<0.0f)p=0.0f;
    text_line(2U,"SMART PSU  CV/CC",PINK,2U);
    ST7789_FillRect(0U,23U,240U,2U,BLUE);
    text_line(28U,"VOUT",RED,2U);
    fixed_2(t,sizeof(t),ps.vout_v,"V");
    text_line(46U,t,RED,4U);
    text_line(98U,"IOUT",BLUE,2U);
    fixed_2(t,sizeof(t),ps.iout_a,"A");
    text_line(116U,t,BLUE,4U);
    text_line(169U,"POWER",PINK,2U);
    fixed_2(t,sizeof(t),p,"W");
    text_line(187U,t,PINK,4U);
    (void)snprintf(t,sizeof(t),"VSET %02u.%02uV",
       (unsigned)(ps.vset_cv/100U),(unsigned)(ps.vset_cv%100U));
    text_line(231U,t,ps.editing==PSU_EDIT_V?RED:WHITE,2U);
    if (ps.editing==PSU_EDIT_V) {
        idx=vcols[ps.digit<4U?ps.digit:0U];
        ST7789_FillRect((uint16_t)(8U+idx*12U),247U,11U,2U,PINK);
    }
    (void)snprintf(t,sizeof(t),"ISET %02u.%02uA",
       (unsigned)(ps.iset_ca/100U),(unsigned)(ps.iset_ca%100U));
    text_line(254U,t,ps.editing==PSU_EDIT_I?BLUE:WHITE,2U);
    if (ps.editing==PSU_EDIT_I) {
        idx=icols[ps.digit<3U?ps.digit:0U];
        ST7789_FillRect((uint16_t)(8U+idx*12U),270U,11U,2U,PINK);
    }
    if (adc.temp_fet_valid) {
        (void)snprintf(t,sizeof(t),"VIN %02u.%01uV  FET %01u.%02uV",
            (unsigned)((ps.vin_v>0?ps.vin_v:0)),
            (unsigned)(((uint32_t)((ps.vin_v>0?ps.vin_v:0)*10.0f))%10U),
            (unsigned)adc.temp_fet_v,
            (unsigned)(((uint32_t)(adc.temp_fet_v*100.0f))%100U));
    } else {
        (void)snprintf(t,sizeof(t),"VIN %02uV  FET --V",(unsigned)(ps.vin_v>0?ps.vin_v:0));
    }
    text_line(279U,t,WHITE,1U);
    runtime=ps.running_seconds;
    (void)snprintf(t,sizeof(t),"%s %s  %02lu:%02lu:%02lu",
          ps.state==PSU_ON?"ON":(ps.state==PSU_FAULT?"FAULT":"OFF"),
          ps.mode==PSU_BUCK?"BUCK":"BOOST",
          (unsigned long)(runtime/3600UL),(unsigned long)((runtime/60UL)%60UL),
          (unsigned long)(runtime%60UL));
    text_line(292U,t,ps.state==PSU_FAULT?RED:PINK,1U);
    if (ps.state==PSU_FAULT) (void)snprintf(t,sizeof(t),"ERR %u  BTN3 RESET",(unsigned)ps.error);
    else (void)snprintf(t,sizeof(t),"B1 ON/OFF  B2 V/A  ENC DIGIT");
    text_line(305U,t,GRAY,1U);
}
