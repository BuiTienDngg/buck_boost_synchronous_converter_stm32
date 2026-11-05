#ifndef PSU_TOUCH_H
#define PSU_TOUCH_H
#include "main.h"
#include "psu_config.h"
enum { TOUCH_NONE, TOUCH_V, TOUCH_A, TOUCH_V_MINUS, TOUCH_V_PLUS,
       TOUCH_A_MINUS, TOUCH_A_PLUS, TOUCH_M1, TOUCH_M2, TOUCH_M3, TOUCH_OUT, TOUCH_CAL };
enum { TOUCH_IDLE, TOUCH_VALID, TOUCH_NO_PRESSURE, TOUCH_UNSTABLE, TOUCH_SPI_ERROR };
typedef struct { uint8_t action, digit, long_press; } PsuTouchEvent;
typedef struct {
  uint16_t raw_x, raw_y, x, y, hold_pm;
  uint32_t errors, reads, valid_samples, rejected;
  uint16_t pressure, z1, z2;
  uint8_t down, calibrated, irq, status;
  volatile uint8_t calibration; /* Foreground publishes the ON interlock to TIM3. */
} PsuTouchInfo;
void PsuTouch_Init(void);
int PsuTouch_Poll(uint32_t now,uint8_t running,PsuTouchEvent *event);
void PsuTouch_Info(uint32_t now,PsuTouchInfo *info);
void PsuTouch_Calibrate(void);
void PsuTouch_CancelCalibration(void);
void PsuTouch_CancelGesture(void);
uint8_t PsuTouch_Calibrating(void);
uint8_t PsuTouch_Calibrated(void);
/* Shared hit geometry and gesture engine, also exercised without SPI. */
uint8_t PsuTouch_Hit(uint16_t x,uint16_t y,uint8_t *digit);
int PsuTouch_Feed(uint32_t now,uint8_t down,uint16_t x,uint16_t y,uint8_t running,PsuTouchEvent *event);
#endif
