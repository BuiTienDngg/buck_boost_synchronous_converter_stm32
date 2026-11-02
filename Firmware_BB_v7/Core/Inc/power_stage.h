#ifndef POWER_STAGE_H
#define POWER_STAGE_H
#include "stm32f1xx_hal.h"
#include <stdint.h>

/* TIM1_CH1 PA8: buck IR2184 IN; TIM1_CH2 PA9: boost IR2184 IN.
 * PA10: common /SD. LOW in OFF/FAULT and on all initialization failures.
 * Both TIM1 channels are PWM1 at 20kHz: buck HS=D, boost HS=1-D.
 * No 500Hz refresh, no pass leg held continuously HIGH, no refresh DMA.
 * HIGH and LOW intervals are >=3us (6..94% at 20kHz).
 * DUTY_MAX_PM limits D. TIM3 PI/control remains 2kHz.
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
#define PSU_ADC_STALE_TICKS         10U  /* 5 ms; a new 30-scan block is ~0.84 ms */
#define PSU_DUTY_MIN               3.0f
#define PSU_DUTY_MAX              97.0f
#define PSU_DUTY_RAMP_PCT_PER_S   30.0f  /* output duty slew limit */
#define PSU_PRECHARGE_MS             2U /* initial bootstrap charge only */
#define PSU_MIN_PULSE_US             3U /* IR2184 delay + IRF3710 gate settling */

/* Bench starting PI gains ONLY; tune with a current-limited supply and load.
 * Outer CV PI at 200Hz: duty-per-volt; fast CC clamp PI at 2kHz.
 * Because ADC measures Iout, NOT inductor current, this is dual CV/CC
 * arbitration, not a genuine inductor-current cascade.
 */
#define PSU_CV_KP                  1.0f
#define PSU_CV_KI                  6.0f
#define PSU_CC_KP                  5.0f
#define PSU_CC_KI                120.0f

/* Parameters transferred as signed decimal integers; PI gains x1000, limits
 * in millivolts/milliamps, duty in per-mille; ramps in millivolts/sec.
 * Configuration is restored from redundant 1KB Flash pages with CRC. */
typedef struct {
    uint32_t cv_kp_milli, cv_ki_milli, cc_kp_milli, cc_ki_milli;
    uint32_t ovp_mv, ocp_ma, vin_min_mv, vin_max_mv;
    uint32_t vset_max_mv, iset_max_ma, duty_max_permille;
    uint32_t v_rise_mv_s, v_fall_mv_s;
} PSU_Tuning;

typedef enum { PSU_OFF=0, PSU_ON=1, PSU_FAULT=2 } PSU_RunState;
typedef enum { PSU_BUCK=0, PSU_BOOST=1, PSU_BB=2 } PSU_Mode;
typedef enum { PSU_EDIT_V=0, PSU_EDIT_I=1 } PSU_Edit;
typedef enum {
  PSU_ERR_NONE=0, PSU_ERR_ADC, PSU_ERR_VIN, PSU_ERR_OVP,
  PSU_ERR_OCP, PSU_ERR_TIMERS, PSU_ERR_DMA
} PSU_Error;

typedef enum {
    PSU_INIT_OK=0, PSU_INIT_HANDLES, PSU_INIT_FREQUENCY,
    PSU_INIT_PWM_MODE, PSU_INIT_TIMER_CONFIG, PSU_INIT_PWM_CH1,
    PSU_INIT_PWM_CH2, PSU_INIT_DMA, PSU_INIT_CONTROL_START
} PSU_InitResult;
typedef struct {
    PSU_InitResult result;
    uint32_t pwm_hz, control_hz, tim1_ccmr1, tim1_cr1;
    uint32_t tim1_psc, tim1_arr, tim3_psc, tim3_arr;
    uint32_t pwm_ch1_state, pwm_ch2_state;
} PSU_Diagnostics;
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
    float vref_ramped_v; /* output of voltage slew limiter, measured in volts */
    uint32_t running_seconds;
} PSU_Status;

HAL_StatusTypeDef PowerStage_Init(TIM_HandleTypeDef *pwm, TIM_HandleTypeDef *control_timer);
void PowerStage_Handle(void);       /* main loop after input/button/AnalogMonitor */
void PowerStage_TIM3_Callback(TIM_HandleTypeDef *htim); /* from HAL period callback */
void PowerStage_OffNow(void);       /* /SD LOW immediately */
void PowerStage_EmergencyDisable(void); /* also safe before Init */
void PowerStage_ControlStartFailed(void);
void PowerStage_GetDiagnostics(PSU_Diagnostics *out);
PSU_Status PowerStage_GetStatus(void);
void PowerStage_GetTuning(PSU_Tuning *cfg);
/* Returns 1 on success. On failure, no parameters or setpoints change. */
uint8_t PowerStage_SetParam(const char *name, uint32_t value);
void PowerStage_MarkDirty(void);     /* called after local setpoint changes */
void PowerStage_RequestSave(void);   /* explicit SAVE command, defer while ON */
void PowerStage_StoragePoll(void);   /* from PowerStage_Handle in main loop */
uint8_t PowerStage_TakeSaveEvent(void); /* 0:none, 1:saved, 2:flash error */

#endif
