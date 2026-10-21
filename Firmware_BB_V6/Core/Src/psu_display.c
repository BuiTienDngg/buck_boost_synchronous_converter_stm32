#include "psu_app.h"
#include <stdio.h>
#include <string.h>
extern SPI_HandleTypeDef hspi1;
#define BG 0x0863u
#define PANEL 0x10A5u
#define WHITE 0xEF7Du
#define MUTED 0x7C51u
#define GREEN 0x3F37u
#define CYAN 0x2E7Fu
#define AMBER 0xFDC0u
#define RED 0xF9A6u
/* Original compact 5x7 uppercase glyphs, MSB left. */
static const char alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ.-:/!?+ >";
static const uint8_t glyphs[][7] = {
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
 {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{15,16,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},{0,0,0,0,0,12,12},{0,0,0,31,0,0,0},
 {0,12,12,0,12,12,0},{1,2,2,4,8,8,16},{4,4,4,4,4,0,4},{14,17,1,2,4,0,4},
 {0,4,4,31,4,4,0},{0,0,0,0,0,0,0},{16,8,4,2,4,8,16}
};
static uint8_t line[640], fill_buffer[256];
static void cs(int high) { HAL_GPIO_WritePin(GPIOB, CS_Pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET); }
static void send(const uint8_t *p, unsigned n) {
#if PSU_LCD_SOFT_SPI
  /* Mode 0, MSB first. Only PA5 SCK and PA7 MOSI are written;
     PA6 remains owned by TIM3. Interrupts stay enabled for CC/CV and OFF. */
  for(unsigned i=0;i<n;++i){
    uint8_t value=p[i];
    for(unsigned bit=0;bit<8;++bit){
      GPIOA->BSRR=value&0x80u?GPIO_PIN_7:((uint32_t)GPIO_PIN_7<<16);
      __NOP();__NOP();
      GPIOA->BSRR=GPIO_PIN_5;
      __NOP();__NOP();__NOP();__NOP();
      GPIOA->BSRR=(uint32_t)GPIO_PIN_5<<16;
      value<<=1;
    }
    /* Bound foreground service latency even during a full-screen redraw. */
    if((i&31u)==31u)PsuApp_DisplayProgress();
  }
#else
  if (HAL_SPI_Transmit(&hspi1, (uint8_t *)p, (uint16_t)n, 50) != HAL_OK) Error_Handler();
#endif
  PsuApp_DisplayProgress();
}
static void command(uint8_t c, const uint8_t *p, unsigned n) {
  cs(0); HAL_GPIO_WritePin(GPIOB, DC_Pin, GPIO_PIN_RESET); send(&c,1);
  HAL_GPIO_WritePin(GPIOB, DC_Pin, GPIO_PIN_SET); if (n) send(p,n); cs(1);
}
static void window(unsigned x, unsigned y, unsigned w, unsigned h) {
  uint8_t a[4]; unsigned end;
  x += PSU_LCD_X_OFFSET; y += PSU_LCD_Y_OFFSET;
  end = x+w-1; a[0]=(uint8_t)(x>>8); a[1]=(uint8_t)x; a[2]=(uint8_t)(end>>8); a[3]=(uint8_t)end; command(0x2A,a,4);
  end = y+h-1; a[0]=(uint8_t)(y>>8); a[1]=(uint8_t)y; a[2]=(uint8_t)(end>>8); a[3]=(uint8_t)end; command(0x2B,a,4);
  command(0x2C,0,0); cs(0);
}
static void rect(unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color) {
  if(!w || !h || x+w>320 || y+h>240)return;
  unsigned k, bytes=w*h*2; for(k=0;k<sizeof(fill_buffer);k+=2) {fill_buffer[k]=(uint8_t)(color>>8);fill_buffer[k+1]=(uint8_t)color;}
  window(x,y,w,h); while(bytes) {unsigned n=bytes>sizeof(fill_buffer)?sizeof(fill_buffer):bytes;send(fill_buffer,n);bytes-=n;} cs(1);
}
static void text_xy(unsigned x, unsigned y, const char *s, unsigned sx,unsigned sy, uint16_t color, uint16_t bg) {
  unsigned n=(unsigned)strlen(s), row, col, k, repeat, width=n*6*sx;
  if (!width || x+width>320 || y+7*sy>240) return;
  window(x,y,width,7*sy);
  for(row=0;row<7;++row) {
    for(col=0;col<n;++col) {
      const char *at=strchr(alphabet,s[col]); unsigned index=at?(unsigned)(at-alphabet):43;
      for(k=0;k<6*sx;++k) {
        unsigned bit=k/sx; uint16_t c=(bit<5 && (glyphs[index][row] & (16u>>bit)))?color:bg;
        unsigned offset=2*(col*6*sx+k); line[offset]=(uint8_t)(c>>8);line[offset+1]=(uint8_t)c;
      }
    }
    for(repeat=0;repeat<sy;++repeat) send(line,width*2);
  } cs(1);
}
static void text(unsigned x,unsigned y,const char *s,unsigned scale,uint16_t color,uint16_t bg){text_xy(x,y,s,scale,scale,color,bg);}
static char previous[10][48],menu_previous[128];
static uint8_t menu_visible,last_mode=255,last_chem=255,last_cells=255;
/* 30 seconds at 4Hz. No framebuffer and no work in the control ISR. */
#define GRAPH_POINTS 121u
#define GRAPH_PERIOD_MS 250u
#define GRAPH_GAP 65535u
#define GRAPH_X 40u
#define GRAPH_Y 76u
#define GRAPH_W 241u
#define GRAPH_H 74u
#define GRID 0x2189u
static uint16_t graph_history[GRAPH_POINTS][2],graph_head,graph_count;
static uint8_t graph_y[2][GRAPH_POINTS],graph_started,graph_dirty,shown_display;
static uint32_t graph_last_ms,graph_vscale,graph_iscale;
static uint16_t graph_value(float value){
  if(!(value>=0))return GRAPH_GAP;
  return value>=65.534f?65534u:(uint16_t)(value*1000.0f+0.5f);
}
static void graph_push(uint16_t volts,uint16_t amps){
  graph_history[graph_head][0]=volts;graph_history[graph_head][1]=amps;
  graph_head=(graph_head+1u)%GRAPH_POINTS;if(graph_count<GRAPH_POINTS)++graph_count;
  graph_dirty=1;
}
static void graph_sample(const PsuView *v,uint32_t now){
  unsigned steps;
  if(!graph_started){graph_started=1;graph_last_ms=now;steps=1;}
  else {steps=(now-graph_last_ms)/GRAPH_PERIOD_MS;if(!steps)return;graph_last_ms+=steps*GRAPH_PERIOD_MS;}
  if(steps>GRAPH_POINTS)steps=GRAPH_POINTS;
  while(--steps)graph_push(GRAPH_GAP,GRAPH_GAP);
  if(v->control.initialized && v->control.adc_age_ms<3)graph_push(graph_value(v->control.vout),graph_value(v->control.current));
  else graph_push(GRAPH_GAP,GRAPH_GAP);
}
static int changed(unsigned slot,const char *s) {
  if(!strcmp(previous[slot],s))return 0;
  strcpy(previous[slot],s);return 1;
}
static void number(char *s,size_t n,float value) {
  unsigned v=(unsigned)(value>0?value*100+0.5f:0);if(v>9999)v=9999;
  snprintf(s,n,"%02u.%02u",v/100,v%100);
}
static void layout(uint8_t display) {
  memset(previous,0,sizeof(previous));
  last_mode=last_chem=last_cells=255;menu_previous[0]=0;
  rect(0,0,320,240,BG);text(10,10,"LAB SUPPLY",2,WHITE,BG);
  if(display==PSU_DISPLAY_GRAPH){
    rect(8,34,148,30,PANEL);rect(164,34,148,30,PANEL);
    text(14,38,"VOUT",1,GREEN,PANEL);text(170,38,"IOUT",1,CYAN,PANEL);
    text(40,151,"-30S",1,MUTED,BG);text(146,151,"-15S",1,MUTED,BG);text(262,151,"NOW",1,MUTED,BG);
    graph_dirty=1;
  } else {
  rect(8,36,148,76,PANEL);rect(164,36,148,76,PANEL);
  rect(8,34,148,2,GREEN);rect(164,34,148,2,CYAN);
  text(16,42,"VOLTAGE",2,MUTED,PANEL);text(172,42,"CURRENT",2,MUTED,PANEL);
  text(140,79,"V",2,GREEN,PANEL);text(296,79,"A",2,CYAN,PANEL);
  }
  text(12,174,"VIN",1,MUTED,BG);text(90,174,"FET TEMP",1,MUTED,BG);text(166,174,"POWER",1,MUTED,BG);text(246,174,"TIME",1,MUTED,BG);
  rect(84,174,1,26,PANEL);rect(160,174,1,26,PANEL);rect(240,174,1,26,PANEL);
}
static void lcd_bus_init(void) {
#if PSU_LCD_SOFT_SPI
  GPIO_InitTypeDef bus={0};
  __HAL_RCC_GPIOA_CLK_ENABLE();
  /* SPI1 must not claim PA6 even though its unused MISO was omitted. */
  __HAL_RCC_SPI1_CLK_DISABLE();
  HAL_GPIO_WritePin(GPIOA,GPIO_PIN_5|GPIO_PIN_7,GPIO_PIN_RESET);
  bus.Pin=GPIO_PIN_5|GPIO_PIN_7;bus.Mode=GPIO_MODE_OUTPUT_PP;
  bus.Speed=GPIO_SPEED_FREQ_HIGH;HAL_GPIO_Init(GPIOA,&bus);
#endif
}
void PsuDisplay_Init(void) {
  lcd_bus_init();
  uint8_t v;
  cs(1);HAL_GPIO_WritePin(GPIOB,T_CS_Pin,GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOB,RESET_Pin,GPIO_PIN_RESET);HAL_Delay(20);
  HAL_GPIO_WritePin(GPIOB,RESET_Pin,GPIO_PIN_SET);HAL_Delay(120);
  command(0x01,0,0);HAL_Delay(120);command(0x11,0,0);HAL_Delay(120);
  v=0x55;command(0x3A,&v,1);v=(uint8_t)(0x60u|(PSU_LCD_BGR?8u:0u));command(0x36,&v,1);
  command(0x21,0,0);command(0x13,0,0);command(0x29,0,0);HAL_Delay(20);
  graph_head=graph_count=graph_started=graph_dirty=0;graph_last_ms=graph_vscale=graph_iscale=0;
  shown_display=PSU_DISPLAY_NUMBER;menu_visible=0;layout(shown_display);
}
static void set_card(const PsuView *v,unsigned current) {
  const PsuControl *p=&v->control;char value[12],key[48];unsigned x=current?164:8;
  uint16_t accent=current?CYAN:GREEN;
  float setting=current?p->current_set:p->voltage_set;
  if(v->modes.mode==PSU_MODE_CHARGE)setting=(current?v->modes.current_ma:PsuModes_FullMillivolts(&v->modes))*0.001f;
  number(value,sizeof(value),setting);
  snprintf(key,sizeof(key),"%s/%u/%u/%u",value,v->edit_current,v->edit_digit,v->modes.mode);
  if(!changed(current?4:3,key))return;
  if(v->menu.display==PSU_DISPLAY_GRAPH){
    rect(x,159,148,12,PANEL);text(x+8,160,current?"SETA":"SETV",1,MUTED,PANEL);
    text(x+44,160,value,1,WHITE,PANEL);text(x+76,160,current?"A":"V",1,accent,PANEL);
    if(v->edit_current==current && v->modes.mode!=PSU_MODE_SOLDER && (v->modes.mode!=PSU_MODE_CHARGE || current)){
      static const uint8_t columns[4]={4,3,1,0};unsigned at=columns[v->edit_digit%(current?3u:4u)];char d[2]={value[at],0};
      text(x+44+6*at,160,d,1,RED,PANEL);rect(x+44+6*at,168,5,1,RED);
    }
    return;
  }
  rect(x,118,148,50,PANEL);text(x+8,121,current?"SETA":"SETV",1,MUTED,PANEL);
  text(x+8,131,value,4,v->modes.mode==PSU_MODE_SOLDER?MUTED:WHITE,PANEL);
  text(x+132,145,current?"A":"V",1,accent,PANEL);
  if(v->edit_current==current && v->modes.mode!=PSU_MODE_SOLDER && (v->modes.mode!=PSU_MODE_CHARGE || current)) {
    static const uint8_t columns[4]={4,3,1,0};
    unsigned column=columns[v->edit_digit%(current?3u:4u)];char digit[2]={value[column],0};
    text(x+8+24*column,131,digit,4,RED,PANEL);
    rect(x+8+24*column,161,20,2,RED);
    rect(x,166,148,2,accent);
  }
}
static void menu_draw(const PsuView *v){
  char key[128],label[40];const PsuModes *m=&v->modes;const PsuMenu *u=&v->menu;
  snprintf(key,sizeof(key),"%u/%u/%u/%u/%u/%u/%u/%u/%u/%lu/%u/%lu/%u/%u/%u/%u",u->page,u->selected,u->editing,u->notice,m->mode,m->chemistry,m->cells,m->current_ma,m->hours,(unsigned long)v->control.fault,PsuModes_Busy(m,&v->control),(unsigned long)(v->control.settings.ramp_v_per_s*1000),u->display,m->solder.tip,m->solder.target,m->solder.sleep_target);
  if(!strcmp(key,menu_previous))return;
  strcpy(menu_previous,key);
  rect(0,0,320,240,BG);
  text(12,10,u->page==PSU_MENU_ROOT?"MENU":u->page==PSU_MENU_RAMP?"RAMP SPEED":u->page==PSU_MENU_DISPLAY?"DISPLAY MODE":u->page==PSU_MENU_SOLDER?"SOLDER SETUP":"BATTERY CHARGER",2,WHITE,BG);
  const char *notice=v->control.fault?"PROTECT - DETAILS IN APP":u->notice==1?"STOP OUTPUT BEFORE CHANGING":u->notice==2?"END: 10% CURRENT / HOLD 60S":PsuModes_Busy(m,&v->control)?"OUTPUT ACTIVE":"OUTPUT OFF";
  text(12,36,notice,1,v->control.fault?RED:AMBER,BG);
  unsigned count=u->page==PSU_MENU_ROOT?6:u->page==PSU_MENU_RAMP?4:u->page==PSU_MENU_DISPLAY?3:u->page==PSU_MENU_SOLDER?5:7,start=u->selected>=6?u->selected-5:0;
  for(unsigned row=0;row<6 && start+row<count;++row){
    unsigned at=start+row,y=57+25*row;uint16_t bg=at==u->selected?PANEL:BG;
    rect(8,y,304,23,bg);
    if(u->page==PSU_MENU_ROOT){static const char *items[]={"RAMP SPEED","BATTERY CHARGER","SOLDER","LAB SUPPLY","DISPLAY","EXIT"};snprintf(label,sizeof(label),"%s",items[at]);}
    else if(u->page==PSU_MENU_RAMP){static const char *items[]={"FAST     30 V/S","NORMAL   10 V/S","SLOW      2 V/S","BACK"};snprintf(label,sizeof(label),"%s",items[at]);}
    else if(u->page==PSU_MENU_DISPLAY)snprintf(label,sizeof(label),"%s%s",at==0?"NUMBER":at==1?"GRAPH":"BACK",at<2 && at==u->display?"   ACTIVE":"");
    else if(u->page==PSU_MENU_SOLDER){
      if(at==0)snprintf(label,sizeof(label),"TIP: %s",m->solder.tip==PSU_TIP_C210?PSU_SOLDER_C210_LABEL:PSU_SOLDER_C245_LABEL);
      else if(at==1)snprintf(label,sizeof(label),"TARGET: %uC",m->solder.target);
      else if(at==2)snprintf(label,sizeof(label),"STAND: %uC",m->solder.sleep_target);
      else snprintf(label,sizeof(label),"%s",at==3?"START SOLDER":"BACK");
    }
    else switch(at){
      case 0:snprintf(label,sizeof(label),"TYPE: %s",m->chemistry==PSU_CELL_LIFEPO4?"LIFEPO4 3.65V":"LI-ION 4.20V");break;
      case 1:snprintf(label,sizeof(label),"CELLS: %uS",m->cells);break;
      case 2:snprintf(label,sizeof(label),"CHARGE: %u.%02uA",m->current_ma/1000,(m->current_ma%1000)/10);break;
      case 3:{unsigned ma=PsuModes_EndMilliamps(m);snprintf(label,sizeof(label),"END: %u.%02uA / 60S",ma/1000,(ma%1000)/10);break;}
      case 4:snprintf(label,sizeof(label),"TIME LIMIT: %uH",m->hours);break;
      case 5:snprintf(label,sizeof(label),"USE CHARGER");break;
      default:snprintf(label,sizeof(label),"BACK");break;
    }
    if(at==u->selected)text(10,y+5,">",1,u->editing?RED:GREEN,bg);
    text(24,y+4,label,2,at==u->selected?(u->editing?RED:GREEN):WHITE,bg);
  }
  text(12,214,u->editing?"TURN: EDIT   PRESS: DONE":"TURN: SELECT   PRESS: ENTER",1,MUTED,BG);
  text(12,229,"ENC HOLD: BACK / EXIT",1,MUTED,BG);
}
static uint32_t graph_scale(uint32_t value,unsigned current){
  static const uint32_t volts[]={1000,2000,5000,10000,20000,25000,30000,40000,50000,60000,80000};
  static const uint32_t amps[]={100,200,500,1000,2000,5000,10000,15000,20000,30000,80000};
  const uint32_t *scales=current?amps:volts;unsigned count=current?sizeof(amps)/sizeof(amps[0]):sizeof(volts)/sizeof(volts[0]);
  value+=value/10u;for(unsigned n=0;n<count;++n)if(scales[n]>=value)return scales[n];return 80000;
}
static int graph_ink(const uint8_t *points,unsigned x,unsigned y){
  unsigned at=x/2u,a=points[at],b=a,prev=a;
  if(x&1u){if(at+1u>=GRAPH_POINTS || a==255 || points[at+1u]==255)return 0;b=(a+points[at+1u])/2u;}
  else if(at && a!=255 && points[at-1u]!=255)prev=(a+points[at-1u])/2u;
  if(b==255)return 0;
  if(prev==255)prev=b;
  unsigned lo=b<prev?b:prev,hi=b>prev?b:prev;return y>=lo && y<=hi+1u;
}
static void graph_draw(const PsuView *v){
  uint32_t vmax=graph_value(v->control.voltage_set),imax=graph_value(v->control.current_set);
  for(unsigned n=0;n<graph_count;++n){
    if(graph_history[n][0]!=GRAPH_GAP && graph_history[n][0]>vmax)vmax=graph_history[n][0];
    if(graph_history[n][1]!=GRAPH_GAP && graph_history[n][1]>imax)imax=graph_history[n][1];
  }
  vmax=graph_scale(vmax,0);imax=graph_scale(imax,1);
  if(!graph_dirty && vmax==graph_vscale && imax==graph_iscale)return;
  graph_vscale=vmax;graph_iscale=imax;graph_dirty=0;
  unsigned first=GRAPH_POINTS-graph_count;
  for(unsigned n=0;n<GRAPH_POINTS;++n)for(unsigned axis=0;axis<2;++axis){
    unsigned at=(graph_head+GRAPH_POINTS-graph_count+(n>=first?n-first:0))%GRAPH_POINTS;
    uint16_t sample=n<first?GRAPH_GAP:graph_history[at][axis];uint32_t scale=axis?imax:vmax;
    graph_y[axis][n]=sample==GRAPH_GAP?255u:(uint8_t)(GRAPH_H-1u-(sample>scale?scale:sample)*(GRAPH_H-1u)/scale);
  }
  char label[12];rect(8,70,30,80,BG);rect(282,70,30,80,BG);
  snprintf(label,sizeof(label),"%luV",(unsigned long)(vmax/1000));text(8,73,label,1,GREEN,BG);
  if(imax<1000)snprintf(label,sizeof(label),"0.%luA",(unsigned long)(imax/100));else snprintf(label,sizeof(label),"%luA",(unsigned long)(imax/1000));
  text(284,73,label,1,CYAN,BG);text(8,143,"0",1,MUTED,BG);text(284,143,"0",1,MUTED,BG);
  window(GRAPH_X,GRAPH_Y,GRAPH_W,GRAPH_H);
  for(unsigned y=0;y<GRAPH_H;++y){
    for(unsigned x=0;x<GRAPH_W;++x){
      uint16_t color=(x%60u==0 || y==0 || y==GRAPH_H-1u || y==GRAPH_H/2u)?GRID:BG;
      if(graph_ink(graph_y[0],x,y))color=GREEN;
      if(graph_ink(graph_y[1],x,y))color=CYAN;
      line[2*x]=(uint8_t)(color>>8);line[2*x+1]=(uint8_t)color;
    }
    send(line,GRAPH_W*2u);
  }cs(1);
}
static void solder_draw(const PsuView *v){
  const PsuSolder *s=&v->modes.solder;const PsuControl *p=&v->control;char b[48];
  snprintf(b,sizeof(b),"%u/%u/%u/%u/%u",s->tip,s->target,s->active_target,s->sleeping,(unsigned)(s->command*100+0.5f));
  if(changed(0,b)){
    rect(0,0,320,34,BG);text(10,10,s->tip==PSU_TIP_C210?PSU_SOLDER_C210_LABEL:PSU_SOLDER_C245_LABEL,2,WHITE,BG);
    rect(8,118,304,48,PANEL);
    snprintf(b,sizeof(b),"SET %uC",s->target);text(16,130,b,3,RED,PANEL);
    text(194,127,"10KHZ PWM",1,MUTED,PANEL);
    snprintf(b,sizeof(b),"PWM %u%%",(unsigned)(s->command*100+0.5f));
    text(194,145,b,1,MUTED,PANEL);
  }
#if PSU_SOLDER_TEMP_PROTECTION
  snprintf(b,sizeof(b),"%u/%d",s->valid,(int)(s->temperature+0.5f));
#else
  snprintf(b,sizeof(b),"%u/%u",s->valid,s->raw);
#endif
  if(changed(1,b)){
    rect(8,36,304,76,PANEL);
#if PSU_SOLDER_TEMP_PROTECTION
    text(16,43,"TIP TEMPERATURE",1,MUTED,PANEL);
    if(s->valid)snprintf(b,sizeof(b),"%03dC",(int)(s->temperature+0.5f));else strcpy(b,"---C");
#else
    text(16,43,"TIP ADC / PA3",1,MUTED,PANEL);
    if(s->valid)snprintf(b,sizeof(b),"%04u",s->raw);else strcpy(b,"----");
#endif
    text(90,65,b,5,s->error?RED:WHITE,PANEL);
  }
  const char *state=p->fault || s->error?"ERROR":!v->modes.solder_request?"OFF":s->phase==PSU_SOL_ROUTE?(s->valid?"ROUTE":"NO TIP"):s->phase==PSU_SOL_PRECHARGE?"RAIL":(v->hardware.heater_block&PSU_HB_TIMER)?"TIM ERR":s->sleeping?"STAND":v->hardware.heater?"HEAT":s->command<=0?"PID 0":"READ";
  if(changed(2,state)){rect(248,4,64,25,PANEL);text(252,11,state,1,s->error || p->fault?RED:GREEN,PANEL);}
  snprintf(b,sizeof(b),"%ld/%ld/%lu",(long)(p->vout*10),(long)(p->current*100),(unsigned long)v->modes.run_seconds);
  if(changed(3,b)){
    rect(8,174,304,27,BG);
    snprintf(b,sizeof(b),"%ld.%ldV  %ld.%02ldA  %lu:%02lu",(long)(p->vout*10)/10,(long)(p->vout*10)%10,(long)(p->current*100)/100,(long)(p->current*100)%100,(unsigned long)v->modes.run_seconds/60,(unsigned long)v->modes.run_seconds%60);
    text(12,181,b,2,WHITE,BG);
  }
  static const char *errors[]={PSU_SOLDER_TEMP_PROTECTION?"ENC HOLD: MENU / TIP / STAND":"TIP PROTECT OFF / ADC DEBUG","TIP SENSOR / CHECK PA3","TIP ADC TIMEOUT","TIP OVER TEMPERATURE","NO TEMPERATURE RISE","SUPPLY NOT READY","CHECK INPUT / FET / ADC","PROFILE OUTSIDE LIMITS"};
  snprintf(b,sizeof(b),"%u/%lu",s->error,(unsigned long)p->fault);
  if(changed(4,b)){rect(8,202,304,9,BG);text(12,203,p->fault?"PROTECTED - DETAILS IN APP":errors[s->error<=PSU_SOL_CONFIG?s->error:PSU_SOL_HEALTH],1,s->error || p->fault?RED:PSU_SOLDER_TEMP_PROTECTION?MUTED:AMBER,BG);}
  snprintf(b,sizeof(b),"%u/%u/%lu",v->modes.solder_request,s->error,(unsigned long)p->fault);
  if(changed(5,b)){
    uint16_t c=s->error || p->fault?RED:v->modes.solder_request?GREEN:PANEL;
    rect(8,215,132,23,c);text(46,223,v->modes.solder_request?"HEAT ON":"HEAT OFF",1,WHITE,c);
    for(unsigned n=0;n<3;++n){unsigned x=148+n*56;rect(x,215,52,23,PANEL);snprintf(b,sizeof(b),"%uC",250+n*50);text(x+12,223,b,1,WHITE,PANEL);}
  }
}
void PsuDisplay_Update(const PsuView *v) {
  const PsuControl *p=&v->control;char s[48];
  graph_sample(v,HAL_GetTick());
  if(v->menu.page){menu_draw(v);menu_visible=1;return;}
  uint8_t display=v->modes.mode==PSU_MODE_SOLDER?2:v->menu.display;
  if(menu_visible || shown_display!=display){shown_display=display;layout(shown_display);menu_visible=0;}
  if(v->modes.mode==PSU_MODE_SOLDER){solder_draw(v);return;}
  const PsuModes *m=&v->modes;
  if(last_mode!=m->mode || last_chem!=m->chemistry || last_cells!=m->cells){
    last_mode=m->mode;last_chem=m->chemistry;last_cells=m->cells;rect(8,4,194,26,BG);
    if(m->mode==PSU_MODE_CHARGE)snprintf(s,sizeof(s),"%s %uS",m->chemistry==PSU_CELL_LIFEPO4?"LIFEPO4":"LI-ION",m->cells);
    else snprintf(s,sizeof(s),"%s",m->mode==PSU_MODE_SOLDER?"SOLDER":"LAB SUPPLY");
    text(10,10,s,2,WHITE,BG);
  }
  if(v->menu.display==PSU_DISPLAY_GRAPH){
    number(s,sizeof(s),p->vout);if(changed(0,s)){text(50,42,s,2,GREEN,PANEL);text(112,42,"V",2,GREEN,PANEL);}
    number(s,sizeof(s),p->current);if(changed(1,s)){text(206,42,s,2,CYAN,PANEL);text(268,42,"A",2,CYAN,PANEL);}
    graph_draw(v);
  }else{
    number(s,sizeof(s),p->vout);if(changed(0,s))text(16,65,s,4,WHITE,PANEL);
    number(s,sizeof(s),p->current);if(changed(1,s))text(172,65,s,4,WHITE,PANEL);
  }
  const char *mode=p->fault?"PROTECT":p->config_errors?"LOCKED":PSU_LOW_SIDE_TEST?"TEST":p->enabled?(p->state==PSU_CC?"CC":p->state==PSU_RAMP?"RAMP":"CV"):v->discharge_state==PSU_D_ACTIVE?"DRAIN":p->start_block?"WAIT":"OFF";
  if(!p->fault && !p->config_errors){
    if(m->mode==PSU_MODE_SOLDER)mode=m->solder_on?"ROUTE":"OFF";
    else if(m->mode==PSU_MODE_CHARGE){if(m->phase==PSU_CH_CHECK)mode="CHECK";else if(m->phase==PSU_CH_PRE)mode="PRE";else if(m->phase==PSU_CH_FULL)mode="FULL";else if(m->phase==PSU_CH_ERROR)mode="CHG ERR";}
  }
  if(changed(2,mode)){rect(210,4,102,24,PANEL);text(218,9,mode,2,p->fault?RED:(p->config_errors||p->start_block)?AMBER:GREEN,PANEL);}
  set_card(v,0);set_card(v,1);
  unsigned vin=(unsigned)(p->vin>0?p->vin*10+0.5f:0);
  unsigned power=(unsigned)(p->vout>0 && p->current>0?p->vout*p->current*10+0.5f:0);
  int temp=(int)p->temp_c;
  snprintf(s,sizeof(s),"%u/%d/%u/%u",vin,temp,p->temp_valid,power);
  if(changed(5,s)) {
    rect(8,185,74,15,BG);rect(88,185,70,15,BG);rect(164,185,74,15,BG);
    snprintf(s,sizeof(s),"%u.%uV",vin/10,vin%10);text(12,185,s,2,WHITE,BG);
    if(p->temp_valid)snprintf(s,sizeof(s),"%dC",temp);else snprintf(s,sizeof(s),"--C");
    text(90,185,s,2,p->temp_valid?WHITE:AMBER,BG);
    if(power>=1000)snprintf(s,sizeof(s),"%uW",power/10);else snprintf(s,sizeof(s),"%u.%uW",power/10,power%10);text(166,185,s,2,WHITE,BG);
  }
  unsigned long hours=m->run_seconds/3600u;
  if(hours>99)snprintf(s,sizeof(s),"%luH",hours);else snprintf(s,sizeof(s),"%02lu:%02lu:%02lu",hours,(unsigned long)(m->run_seconds/60u)%60u,(unsigned long)m->run_seconds%60u);
  if(changed(9,s)){rect(244,185,68,15,BG);text_xy(246,185,s,1,2,WHITE,BG);}
  if(p->fault)snprintf(s,sizeof(s),"PROTECTED - DETAILS IN APP");
  else if(p->config_errors || p->start_block)snprintf(s,sizeof(s),"OUTPUT OFF - CHECK APP");
  else if(v->discharge_state==PSU_D_ACTIVE)snprintf(s,sizeof(s),"OUTPUT OFF / DISCHARGING");
  else if(v->discharge_state>=PSU_D_TIMEOUT)snprintf(s,sizeof(s),"OUTPUT OFF - CHECK APP");
  else if(m->mode==PSU_MODE_CHARGE && m->error){static const char *errors[]={"","NO BATTERY / VOLTAGE LOW","BATTERY VOLTAGE TOO HIGH","CHECK SENSORS / INPUT / BMS","CHARGE TIMEOUT","PRECHARGE TIMEOUT","NO CHARGE CURRENT","PROFILE OUTSIDE LIMITS","WAIT FOR OUTPUT SWITCH"};snprintf(s,sizeof(s),"%s",errors[m->error<=PSU_CH_SWITCH?m->error:PSU_CH_HEALTH]);}
  else if(m->mode==PSU_MODE_CHARGE){snprintf(s,sizeof(s),"%s",m->phase==PSU_CH_FULL?"CHARGE COMPLETE":m->phase==PSU_CH_CHECK?"CHECKING BATTERY":m->phase==PSU_CH_PRE?"PRECHARGE / LIMITED CURRENT":m->phase==PSU_CH_CCCV?"CC/CV / AUTO STOP WHEN FULL":"BMS PACK - SELECT TYPE / CELLS");}
  else if(m->mode==PSU_MODE_SOLDER)snprintf(s,sizeof(s),"DC-DC OFF / PC15 %s",m->solder_on?"ON":"OFF");
  else if(v->saved_notice)snprintf(s,sizeof(s),v->saved_notice==1?"SAVED":"SAVE FAILED - CHECK APP");
  else if(p->vin_warning)snprintf(s,sizeof(s),"INPUT LOW");
  else if(p->temp_c>p->settings.temp_warn)snprintf(s,sizeof(s),"FET WARM");
  else s[0]=0;
  if(changed(6,s)){rect(8,202,304,9,BG);text(12,203,s,1,p->fault?RED:AMBER,BG);}
  snprintf(s,sizeof(s),"%u/%u/%u",v->selected_preset,p->enabled,m->mode);
  if(changed(7,s))for(unsigned k=0;k<3;++k){
    unsigned x=148+k*56;rect(x,215,52,23,PANEL);if(k==v->selected_preset)rect(x,215,52,2,CYAN);
    char label[4];snprintf(label,sizeof(label),"M%u",k+1);text(x+20,223,label,1,p->enabled || m->mode!=PSU_MODE_LAB?MUTED:WHITE,PANEL);
  }
  uint8_t active=p->enabled || m->solder_on || m->phase==PSU_CH_CHECK;
  snprintf(s,sizeof(s),"%u/%lu/%u",active,(unsigned long)p->fault,m->phase);
  if(changed(8,s)) {
    uint16_t fill=p->fault?RED:active?GREEN:PANEL,ink=active || p->fault?BG:WHITE;
    rect(8,215,132,23,fill);
    text(p->fault?59:active?56:53,223,p->fault?"RESET":active?"OUT ON":"OUT OFF",1,ink,fill);
  }
}
