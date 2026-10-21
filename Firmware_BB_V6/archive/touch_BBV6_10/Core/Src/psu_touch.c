#include "psu_touch.h"
#include <string.h>
extern SPI_HandleTypeDef hspi1;
/* XPT2046 resistive panel, landscape.
   Reference: https://files.waveshare.com/wiki/common/XPT2046_Datasheet.pdf
   Runtime calibration is RAM-only; defaults return after reset. */
static PsuTouchInfo info;
static struct { int32_t left,right,top,bottom;uint8_t swap; } map;
static struct { uint32_t start;uint16_t x,y;uint8_t blocked,active,fired,action,digit; } gesture;
static uint32_t last_poll,last_read,cal_started;
static uint16_t cal_points[4][2];
static uint8_t cal_wait_release,cal_pressed;
static int distance(uint16_t a,uint16_t b){return a>b?a-b:b-a;}
static uint16_t median(uint16_t a[5]) {
  for(unsigned i=1;i<5;++i){uint16_t v=a[i];unsigned j=i;while(j && a[j-1]>v){a[j]=a[j-1];--j;}a[j]=v;}
  return a[2];
}
static int conversion(uint8_t command,uint16_t *value) {
  uint8_t tx[3]={command,0,0},rx[3];
  if(HAL_SPI_TransmitReceive(&hspi1,tx,rx,3,2)!=HAL_OK)return 0;
  *value=(uint16_t)((((uint16_t)rx[1]<<8)|rx[2])>>3)&4095u;return 1;
}
static uint32_t bus_begin(void) {
  uint32_t previous=hspi1.Instance->CR1;
  HAL_GPIO_WritePin(GPIOB,CS_Pin|T_CS_Pin,GPIO_PIN_SET);
  /* Foreground owns SPI. LCD transfers have finished. Keep ADC/control IRQs
     enabled; 72MHz / 128 = 562.5kHz, mode 0. */
  hspi1.Instance->CR1=previous&~SPI_CR1_SPE;
  hspi1.Instance->CR1=(previous&~(SPI_CR1_BR|SPI_CR1_CPOL|SPI_CR1_CPHA))|SPI_BAUDRATEPRESCALER_128|SPI_CR1_SPE;
  HAL_GPIO_WritePin(GPIOB,T_CS_Pin,GPIO_PIN_RESET);
  return previous;
}
static int bus_end(uint32_t previous) {
  uint16_t discard;
  /* Complete a PD1:PD0=00 conversion even on an earlier failure. This turns
     off the reference/ADC and re-enables PENIRQ before releasing CS. */
  int ok=conversion(0xD0,&discard);
  HAL_GPIO_WritePin(GPIOB,T_CS_Pin,GPIO_PIN_SET);
  hspi1.Instance->CR1&=~SPI_CR1_SPE;hspi1.Instance->CR1=previous;
  return ok;
}
static int settled(uint8_t command,uint16_t *value) {
  uint16_t discard;
  return conversion(command,&discard) && conversion(command,value);
}
static int read_raw(uint16_t *x,uint16_t *y) {
  uint16_t a[5],b[5],discard;uint32_t previous=bus_begin();
  ++info.reads;
  /* Keep PD0=1 while sampling. Discard settling conversions after each axis
     switch, instead of powering down and swapping axes at every sample. */
  int ok=settled(0xB1,&info.z1) && settled(0xC1,&info.z2);
  info.pressure=ok && info.z2>=info.z1 ? (uint16_t)(info.z1+4095u-info.z2) : 0;
  int pressed=ok && info.z1>=10 && info.z2>=info.z1 && info.pressure>=300;
  if(pressed) {
    ok=conversion(0xD1,&discard) && conversion(0xD1,&discard);
    for(unsigned k=0;k<5 && ok;++k)ok=conversion(0xD1,&a[k]);
    if(ok)ok=conversion(0x91,&discard) && conversion(0x91,&discard);
    for(unsigned k=0;k<5 && ok;++k)ok=conversion(0x91,&b[k]);
  }
  int ended=bus_end(previous);
  if(!ok || !ended){++info.errors;info.status=TOUCH_SPI_ERROR;return -1;}
  if((info.z1==0 && info.z2==0) || (info.z1==4095 && info.z2==4095)) {
    ++info.rejected;info.status=TOUCH_UNSTABLE;return -1;
  }
  if(!pressed){info.status=TOUCH_NO_PRESSURE;return 0;}
  *x=median(a);*y=median(b);
  /* A single switching-noise outlier must not discard an otherwise stable
     press. The middle three samples must agree; rail medians remain invalid. */
  if(*x<=64 || *x>=4031 || *y<=64 || *y>=4031 || a[3]-a[1]>160 || b[3]-b[1]>160) {
    ++info.rejected;info.status=TOUCH_UNSTABLE;return -1;
  }
  ++info.valid_samples;info.status=TOUCH_VALID;return 1;
}
static uint16_t coordinate(int32_t raw,int32_t lo,int32_t hi,int32_t inset,int32_t span,int32_t maximum) {
  int32_t v=(raw-lo)*span/(hi-lo)+inset;
  return (uint16_t)(v<0?0:v>maximum?maximum:v);
}
uint8_t PsuTouch_Hit(uint16_t x,uint16_t y,uint8_t *digit) {
  *digit=255;
  if(x>=154 && x<202 && y<29)return TOUCH_CAL;
  if(y>=118 && y<164) {
    if(x>=92 && x<124)return TOUCH_V_MINUS;
    if(x>=124 && x<156)return TOUCH_V_PLUS;
    if(x>=248 && x<280)return TOUCH_A_MINUS;
    if(x>=280 && x<312)return TOUCH_A_PLUS;
    if(x>=8 && x<92){unsigned c=x<18?0:(x-18)/12;*digit=c==0?3:c==1?2:c==3?1:c==4?0:255;return TOUCH_V;}
    if(x>=164 && x<248){unsigned c=x<174?0:(x-174)/12;*digit=c==1?2:c==3?1:c==4?0:255;return TOUCH_A;}
  }
  if(y>=198 && y<238) {
    if(x>=8 && x<60)return TOUCH_M1;
    if(x>=64 && x<116)return TOUCH_M2;
    if(x>=120 && x<172)return TOUCH_M3;
    if(x>=180 && x<312)return TOUCH_OUT;
  }
  return TOUCH_NONE;
}
int PsuTouch_Feed(uint32_t now,uint8_t down,uint16_t x,uint16_t y,uint8_t running,PsuTouchEvent *event) {
  memset(event,0,sizeof(*event));
  if(!down) {
    uint32_t duration=now-gesture.start;
    if(!gesture.blocked && gesture.active && !gesture.fired && duration>=50 && duration<=3000) {
      event->action=gesture.action;event->digit=gesture.digit;event->long_press=duration>=1000;
      if(gesture.action==TOUCH_OUT && !event->long_press)event->action=TOUCH_NONE;
    }
    gesture.blocked=gesture.active=gesture.fired=0;return event->action!=0;
  }
  if(gesture.blocked)return 0;
  if(!gesture.active) {
    gesture.action=PsuTouch_Hit(x,y,&gesture.digit);gesture.x=x;gesture.y=y;
    gesture.start=now;gesture.active=1;gesture.fired=0;
  }
  if(distance(x,gesture.x)>12 || distance(y,gesture.y)>12 || !gesture.action) {gesture.blocked=1;return 0;}
  if(!gesture.fired && gesture.action==TOUCH_OUT && running && now-gesture.start>=50) {
    gesture.fired=1;event->action=TOUCH_OUT;return 1;
  }
  if(!gesture.fired && gesture.action>=TOUCH_M1 && gesture.action<=TOUCH_M3 && now-gesture.start>=1000) {
    gesture.fired=1;event->action=gesture.action;event->long_press=1;return 1;
  }
  return 0;
}
void PsuTouch_Calibrate(void) {info.calibration=1;cal_wait_release=1;cal_pressed=0;gesture.blocked=1;}
void PsuTouch_CancelCalibration(void){info.calibration=0;gesture.blocked=1;gesture.active=0;}
void PsuTouch_CancelGesture(void){gesture.blocked=1;gesture.active=0;gesture.fired=0;}
uint8_t PsuTouch_Calibrating(void){return info.calibration;}
uint8_t PsuTouch_Calibrated(void){return info.calibrated;}
static void calibrate(uint32_t now,uint8_t down) {
  if(cal_wait_release){if(!down)cal_wait_release=0;return;}
  if(down) {
    if(!cal_pressed){cal_started=now;cal_pressed=1;}
    unsigned at=info.calibration-1;cal_points[at][0]=info.raw_x;cal_points[at][1]=info.raw_y;
  } else if(cal_pressed) {
    cal_pressed=0;if(now-cal_started<80)return;
    if(++info.calibration<=4)return;
    int32_t dx=(int32_t)cal_points[1][0]-cal_points[0][0],dy=(int32_t)cal_points[1][1]-cal_points[0][1];
    uint8_t swap=(dy<0?-dy:dy)>(dx<0?-dx:dx);
    int32_t left=(cal_points[0][swap]+cal_points[2][swap])/2,right=(cal_points[1][swap]+cal_points[3][swap])/2;
    int32_t top=(cal_points[0][!swap]+cal_points[1][!swap])/2,bottom=(cal_points[2][!swap]+cal_points[3][!swap])/2;
    if(distance((uint16_t)left,(uint16_t)right)<1000 || distance((uint16_t)top,(uint16_t)bottom)<1000){info.calibration=1;return;}
    map.left=left;map.right=right;map.top=top;map.bottom=bottom;map.swap=swap;
    info.calibrated=1;gesture.blocked=1;__DMB();info.calibration=0;
  }
}
void PsuTouch_Init(void) {
  GPIO_InitTypeDef gpio={0};gpio.Pin=T_IRQ_Pin;gpio.Mode=GPIO_MODE_INPUT;gpio.Pull=GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB,&gpio);HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
  memset(&info,0,sizeof(info));memset(&gesture,0,sizeof(gesture));gesture.blocked=1;
  map.left=PSU_TOUCH_LEFT;map.right=PSU_TOUCH_RIGHT;map.top=PSU_TOUCH_TOP;map.bottom=PSU_TOUCH_BOTTOM;map.swap=PSU_TOUCH_SWAP;
  /* A disconnected DOUT should read as a rail, never random coordinates. */
  gpio.Pin=GPIO_PIN_6;gpio.Mode=GPIO_MODE_INPUT;gpio.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOA,&gpio);
  last_poll=last_read=0;HAL_GPIO_WritePin(GPIOB,T_CS_Pin,GPIO_PIN_SET);
  uint32_t previous=bus_begin();
  if(!bus_end(previous)){++info.errors;info.status=TOUCH_SPI_ERROR;}
}
int PsuTouch_Poll(uint32_t now,uint8_t running,PsuTouchEvent *event) {
  memset(event,0,sizeof(*event));
  if(now-last_poll<10)return 0;
  last_poll=now;
  info.irq=HAL_GPIO_ReadPin(GPIOB,T_IRQ_Pin)==GPIO_PIN_SET;
  /* IRQ is a latency hint, not the only way to discover a press. Pressure
     polling also recovers from an unwired/stuck-high PENIRQ. */
  if(info.irq && !info.down && !info.calibration && now-last_read<30)return 0;
  last_read=now;
  int sample=read_raw(&info.raw_x,&info.raw_y);
  if(sample<0){info.down=0;gesture.blocked=1;cal_pressed=0;return 0;}
  uint8_t down=sample>0;
  if(down) {
    uint16_t raw[2]={info.raw_x,info.raw_y};
    info.x=coordinate(raw[map.swap],map.left,map.right,24,271,319);
    info.y=coordinate(raw[!map.swap],map.top,map.bottom,24,191,239);
  }
  info.down=down;
  if(info.calibration){calibrate(now,down);return 0;}
  return PsuTouch_Feed(now,down,info.x,info.y,running,event);
}
void PsuTouch_Info(uint32_t now,PsuTouchInfo *out) {
  *out=info;out->hold_pm=0;
  if(gesture.active && !gesture.blocked && !gesture.fired && gesture.action==TOUCH_OUT && info.down) {
    uint32_t ms=now-gesture.start;out->hold_pm=(uint16_t)(ms>1000?1000:ms);
  }
}
