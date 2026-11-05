#include "psu_touch.h"
#include <string.h>
SPI_HandleTypeDef hspi1={.Instance=SPI1};
volatile uint32_t test_count,test_failed,test_lines[256];
#define CHECK(x) do{test_lines[test_count++]=(x)?0:__LINE__;if(test_lines[test_count-1])++test_failed;}while(0)
static uint16_t raw_x=2000,raw_y=2000;
static uint8_t down,timeout_io,bad_bus,irq_high,irq_low,penirq_armed,powered;
static uint8_t channel,axis_samples,outlier,noisy,rail,last_command;
static unsigned transfers;
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *g,uint16_t p){(void)g;(void)p;return irq_low || (penirq_armed && down && !irq_high)?GPIO_PIN_RESET:GPIO_PIN_SET;}
void HAL_GPIO_WritePin(GPIO_TypeDef *g,uint16_t p,GPIO_PinState level){if(level)g->ODR|=p;else g->ODR&=~p;}
void HAL_GPIO_Init(GPIO_TypeDef *g,GPIO_InitTypeDef *p){(void)g;(void)p;}
void HAL_NVIC_DisableIRQ(IRQn_Type n){(void)n;}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h,const uint8_t *tx,uint8_t *rx,uint16_t n,uint32_t t){
 (void)h;(void)n;(void)t;
 ++transfers;last_command=tx[0];
 if((SPI1->CR1&SPI_CR1_BR)!=SPI_BAUDRATEPRESCALER_128 || !(GPIOB->ODR&CS_Pin) || (GPIOB->ODR&T_CS_Pin))bad_bus=1;
 if(timeout_io)return HAL_TIMEOUT;
 uint8_t next=tx[0]&0xF0;uint16_t value;
 if(rail)value=rail==1?0:4095;
 else if(!powered || next!=channel){value=4095;axis_samples=0;} /* First acquisition not settled. */
 else if(next==0xB0)value=down?1200:0;
 else if(next==0xC0)value=down?2500:4095;
 else {
   value=next==0xD0?raw_x:raw_y;++axis_samples;
   if(outlier && axis_samples==6)value=4095;
   if(noisy)value=axis_samples&1?1500:2500;
 }
 channel=next;powered=tx[0]&1;penirq_armed=!powered;
 value<<=3;rx[0]=0;rx[1]=(uint8_t)(value>>8);rx[2]=(uint8_t)value;return HAL_OK;
}
int test_main(void){
 PsuTouchEvent e;PsuTouchInfo i;uint8_t digit;uint32_t now=100;
 PsuTouch_Init();
 CHECK(transfers && penirq_armed && last_command==0xD0 && !bad_bus);
 CHECK(PsuTouch_Hit(140,145,&digit)==TOUCH_V_PLUS);
 CHECK(PsuTouch_Hit(228,145,&digit)==TOUCH_A && digit==0);
 CHECK(PsuTouch_Hit(170,15,&digit)==TOUCH_CAL);
 CHECK(PsuTouch_Hit(320,240,&digit)==TOUCH_NONE);
 CHECK(!PsuTouch_Feed(0,1,220,215,0,&e));
 CHECK(!PsuTouch_Feed(1500,1,220,215,0,&e)); /* Held at boot cannot arm. */
 CHECK(!PsuTouch_Feed(1510,0,220,215,0,&e));
 CHECK(!PsuTouch_Feed(1600,1,220,215,0,&e));
 CHECK(!PsuTouch_Feed(1700,0,220,215,0,&e));
 CHECK(!PsuTouch_Feed(1800,1,220,215,0,&e));
 CHECK(!PsuTouch_Feed(2900,1,220,215,0,&e));
 CHECK(PsuTouch_Feed(2910,0,220,215,0,&e) && e.action==TOUCH_OUT && e.long_press);
 CHECK(!PsuTouch_Feed(3000,1,220,215,1,&e));
 CHECK(PsuTouch_Feed(3050,1,220,215,1,&e) && e.action==TOUCH_OUT);
 CHECK(!PsuTouch_Feed(3100,0,220,215,0,&e));
 PsuTouch_Feed(3200,1,220,215,0,&e);PsuTouch_Feed(3300,1,245,215,0,&e);
 CHECK(!PsuTouch_Feed(4500,0,245,215,0,&e));
 PsuTouch_Feed(4600,1,220,215,0,&e);PsuTouch_CancelGesture();
 CHECK(!PsuTouch_Feed(5800,0,220,215,0,&e));
 PsuTouch_Feed(6000,1,220,215,0,&e);
 CHECK(!PsuTouch_Feed(10000,0,220,215,0,&e)); /* Stuck press expires. */
 PsuTouch_Feed(11000,1,32,215,0,&e);
 CHECK(PsuTouch_Feed(12100,1,32,215,0,&e) && e.action==TOUCH_M1 && e.long_press);
 CHECK(!PsuTouch_Feed(12200,0,32,215,0,&e));
 uint32_t lcd=SPI_CR1_MSTR|SPI_CR1_SPE|SPI_BAUDRATEPRESCALER_4;SPI1->CR1=lcd;
 down=1;PsuTouch_Poll(now,0,&e);CHECK(!bad_bus && SPI1->CR1==lcd && (GPIOB->ODR&T_CS_Pin));
 timeout_io=1;now+=20;PsuTouch_Poll(now,0,&e);PsuTouch_Info(now,&i);
 CHECK(i.errors==1 && SPI1->CR1==lcd && (GPIOB->ODR&T_CS_Pin));timeout_io=0;
 raw_x=raw_y=4095;now+=20;CHECK(!PsuTouch_Poll(now,0,&e));
 down=0;now+=20;PsuTouch_Poll(now,0,&e);
 PsuTouch_Calibrate();now+=20;PsuTouch_Poll(now,0,&e);
 static const uint16_t points[4][2]={{400,500},{3600,500},{400,3500},{3600,3500}};
 for(unsigned k=0;k<4;++k){
   down=1;raw_x=points[k][0];raw_y=points[k][1];now+=20;PsuTouch_Poll(now,0,&e);
   now+=100;PsuTouch_Poll(now,0,&e);down=0;now+=20;PsuTouch_Poll(now,0,&e);
 }
 CHECK(!PsuTouch_Calibrating());
 now+=20;PsuTouch_Poll(now,0,&e);down=1;raw_x=raw_y=2000;now+=20;PsuTouch_Poll(now,0,&e);PsuTouch_Info(now,&i);
 CHECK(i.x>=158 && i.x<=160 && i.y>=118 && i.y<=120); /* Swap auto-detected. */
 PsuTouch_Calibrate();PsuTouch_CancelCalibration();CHECK(!PsuTouch_Calibrating());
 /* IRQ must not be a prerequisite for SPI: pressure polling works even if
    the IRQ wire is absent. Do not trust a stuck-low IRQ as a press, either. */
 down=0;now+=40;PsuTouch_Poll(now,0,&e);irq_high=1;down=1;
 now+=40;PsuTouch_Poll(now,0,&e);PsuTouch_Info(now,&i);
 CHECK(i.irq==1 && i.down && i.pressure==2795 && i.status==TOUCH_VALID);
 CHECK(i.z1==1200 && i.z2==2500 && i.valid_samples>0 && i.reads>0);
 CHECK(penirq_armed && !powered && last_command==0xD0 && SPI1->CR1==lcd);
 down=0;irq_high=0;irq_low=1;now+=40;PsuTouch_Poll(now,0,&e);PsuTouch_Info(now,&i);
 CHECK(i.irq==0 && !i.down && i.status==TOUCH_NO_PRESSURE);irq_low=0;
 down=1;raw_x=raw_y=2000;outlier=1;now+=40;PsuTouch_Poll(now,0,&e);PsuTouch_Info(now,&i);
 CHECK(i.down && i.raw_x==2000 && i.raw_y==2000 && i.status==TOUCH_VALID);outlier=0;
 noisy=1;now+=40;CHECK(!PsuTouch_Poll(now,0,&e));PsuTouch_Info(now,&i);
 CHECK(!i.down && i.rejected && i.status==TOUCH_UNSTABLE);noisy=0;
 /* Missing DOUT with either rail cannot become a press. */
 for(rail=1;rail<=2;++rail){now+=40;CHECK(!PsuTouch_Poll(now,0,&e));PsuTouch_Info(now,&i);CHECK(!i.down && i.status==TOUCH_UNSTABLE);}rail=0;
 CHECK(!bad_bus && last_command==0xD0 && SPI1->CR1==lcd);
 /* A short press discovered while IRQ is high still yields a real V+ event. */
 irq_high=1;down=0;now+=40;PsuTouch_Poll(now,0,&e);
 raw_x=1770;raw_y=2401;down=1;now+=40;PsuTouch_Poll(now,0,&e);
 now+=80;PsuTouch_Poll(now,0,&e);down=0;now+=40;
 CHECK(PsuTouch_Poll(now,0,&e) && e.action==TOUCH_V_PLUS);
 /* Disconnecting the bus after a long OUT hold must cancel the pending ON,
    rather than interpreting invalid bus data as the release gesture. */
 for(unsigned fault=1;fault<=2;++fault){
   raw_x=2714;raw_y=3500;down=1;now+=40;CHECK(!PsuTouch_Poll(now,0,&e));
   now+=1100;CHECK(!PsuTouch_Poll(now,0,&e));
   rail=(uint8_t)fault;now+=40;CHECK(!PsuTouch_Poll(now,0,&e) && !e.action);
   rail=0;down=0;now+=40;CHECK(!PsuTouch_Poll(now,0,&e) && !e.action);
 }
 return 0;
}
