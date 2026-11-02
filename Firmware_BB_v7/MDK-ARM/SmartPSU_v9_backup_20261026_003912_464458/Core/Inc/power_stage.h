#ifndef POWER_STAGE_H
#define POWER_STAGE_H
#include "stm32f1xx_hal.h"
#include <stdint.h>

/* TIM1_CH1 PA8: buck IR2184 IN; TIM1_CH2 PA9: boost IR2184 IN.
 * PA10: common /SD (HIGH continuously after initialized, including OFF/FAULT).
 * IMPORTANT: /SD HIGH + PWM=0 leaves BOTH low-side MOSFETs ON. This is
 * NOT a galvanic or gate-driver disable. Use /SD LOW/hardware interlock
 * for a real protective shutdown in a future hardware revision.
 */
#define PSU_PWM_HZ               20000U
#define PSU_CONTROL_HZ            2000U
#define PSU_VSET_MIN_CV            300  /* 3.00 V */
#define PSU_VSET_MAX_CV           2800  /* 28.00 V, under 30V OVP */
#define PSU_ISET_MIN_CA             10  /* 0.10 A */
#define PSU_ISET_MAX_CA            500  /* 5.00 A */
#define PSU_OVP_V                 30.0f
#define PSU_OCP_A                  6.0f  /* independent latched hard limit */
#define PSU_VIN_MIN_V              7.0f
#define PSU_VIN_MAX_V             40.0f
#define PSU_ADC_STALE_TICKS        400U  /* 200 ms @ 2kHz */
#define PSU_DUTY_MIN               3.0f
#define PSU_DUTY_MAX              97.0f
#define PSU_DUTY_RAMP_PCT_PER_S   30.0f  /* output duty slew limit */

/* Bench starting PI gains ONLY; tune with a current-limited supply and load.
 * Outer CV PI at 200Hz: duty-per-volt; fast CC clamp PI at 2kHz.
 * Because ADC measures Iout, NOT inductor current, this is dual CV/CC
 * arbitration, not a genuine inductor-current cascade.
 */
#define PSU_CV_KP                  1.0f
#define PSU_CV_KI                  6.0f
#define PSU_CC_KP                  5.0f
#define PSU_CC_KI                120.0f

typedef enum { PSU_OFF=0, PSU_ON=1, PSU_FAULT=2 } PSU_RunState;
typedef enum { PSU_BUCK=0, PSU_BOOST=1 } PSU_Mode;
typedef enum { PSU_EDIT_V=0, PSU_EDIT_I=1 } PSU_Edit;
typedef enum {
  PSU_ERR_NONE=0, PSU_ERR_ADC, PSU_ERR_VIN, PSU_ERR_OVP,
  PSU_ERR_OCP, PSU_ERR_TIMERS
} PSU_Error;
typedef struct {
    PSU_RunState state;
    PSU_Mode mode;
    PSU_Edit editing;
    PSU_Error error;
    uint16_t vset_cv;
    uint16_t iset_ca;
    uint8_t digit;              /* V digit 0..3; I digit 0..2 */
    float duty_buck_pct;
    float duty_boost_pct;
    float duty_applied_pct;
    float vout_v;
    float iout_a;
    float vin_v;
    uint32_t running_seconds;
} PSU_Status;

HAL_StatusTypeDef PowerStage_Init(TIM_HandleTypeDef *pwm, TIM_HandleTypeDef *control_timer);
void PowerStage_Handle(void);       /* main loop after input/button/AnalogMonitor */
void PowerStage_TIM3_Callback(TIM_HandleTypeDef *htim); /* from HAL period callback */
void PowerStage_OffNow(void);       /* PWM = 0, SD stays HIGH */
PSU_Status PowerStage_GetStatus(void);
#endif
