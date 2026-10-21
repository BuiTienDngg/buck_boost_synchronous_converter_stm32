#ifndef PSU_MODES_H
#define PSU_MODES_H
#include "psu_control.h"
/* 50 ms frame: first 10 ms fully OFF for tip ADC, then 40 ms of
   10 kHz PA6 PWM with duty derived from a fresh quiet temperature. */
#define PSU_SOLDER_FRAME_MS 50u
#define PSU_SOLDER_QUIET_MS 10u
#define PSU_SOLDER_HEAT_MAX_MS (PSU_SOLDER_FRAME_MS-PSU_SOLDER_QUIET_MS)
#define PSU_SOLDER_SAMPLE_MS 5u
#define PSU_SOLDER_STAND_SAMPLES (300u/PSU_SOLDER_SAMPLE_MS+1u)
#define PSU_SOLDER_FULL_ERROR_C 100.0f
/* Initial thermal gains: duty/C, duty/(C*s), duty*s/C; tune against a reference. */
#define PSU_SOLDER_MAX_DUTY 0.95f /* Retain an OFF interval in every 10 kHz period. */
#define PSU_SOLDER_KP 0.01f
#define PSU_SOLDER_KI 0.001f
#define PSU_SOLDER_KD 0.002f
/* Legacy release flag only: rails now come from solder_voltage/current
   in settings, default 18 V / 6 A for either tip. */
#ifndef PSU_SOLDER_TEST18
#define PSU_SOLDER_TEST18 1
#endif
#ifndef PSU_SOLDER_TEMP_PROTECTION
#define PSU_SOLDER_TEMP_PROTECTION 1
#endif
#define PSU_SOLDER_C245_LABEL "C245"
#define PSU_SOLDER_C210_LABEL "C210"
enum { PSU_MODE_LAB, PSU_MODE_CHARGE, PSU_MODE_SOLDER };
enum { PSU_CELL_LIION, PSU_CELL_LIFEPO4 };
enum { PSU_CH_IDLE, PSU_CH_CHECK, PSU_CH_PRE, PSU_CH_CCCV, PSU_CH_FULL, PSU_CH_STOP, PSU_CH_ERROR };
enum { PSU_CH_OK, PSU_CH_LOW, PSU_CH_HIGH, PSU_CH_HEALTH, PSU_CH_TIMEOUT,
       PSU_CH_PRE_TIMEOUT, PSU_CH_NO_CURRENT, PSU_CH_CONFIG, PSU_CH_SWITCH };
enum { PSU_TIP_C245, PSU_TIP_C210 };
enum { PSU_SOL_OFF, PSU_SOL_ROUTE, PSU_SOL_PRECHARGE, PSU_SOL_RUN, PSU_SOL_ERROR };
enum { PSU_SOL_OK, PSU_SOL_SENSOR, PSU_SOL_STALE, PSU_SOL_HOT, PSU_SOL_NO_RISE,
       PSU_SOL_RAIL_TIMEOUT, PSU_SOL_HEALTH, PSU_SOL_CONFIG };
typedef struct {
  float temperature, integral, derivative, previous_temperature, command, duty, baseline;
  uint32_t last_sample_ms, frame_ms, started_ms, watch_ms, stand_ms, route_ms, heat_started_ms;
  uint32_t sum, sample_ms, last_heat_ms;
  uint16_t target, sleep_target, active_target, raw, rail_ms, hot_ms, trip_adc;
  uint16_t ramp_ms,ramp_limit_pm; /* Legacy telemetry: always zero. */
  uint16_t position, on_ms;
  uint8_t tip, phase, error, valid, sleeping, stand_count, pid_ready, count, stand;
  uint8_t cycle_ready;
} PsuSolder;
typedef struct {
  uint32_t started_ms, phase_ms, tail_ms, pre_ready_ms, low_ms;
  uint32_t mah_d10, charge_integral, run_seconds, run_fraction, low_output_ms, changed_ms;
  uint16_t current_ma, lab_mv, lab_ma;
  volatile uint16_t ovp_adc;
  volatile uint8_t mode;
  uint8_t chemistry, cells, hours, phase, error;
  uint8_t solder_request, solder_on, discharge_inhibit, was_active, current_seen, near_full;
  PsuSolder solder;
} PsuModes;
/* Feed only conversions taken with the heater physically off for >=2 ms. */
void PsuSolder_Sample(PsuModes *m,uint16_t raw,uint32_t now);
void PsuSolder_SetTarget(PsuModes *m,int32_t degrees);
void PsuModes_Init(PsuModes *m);
uint16_t PsuModes_FullMillivolts(const PsuModes *m);
uint16_t PsuModes_EndMilliamps(const PsuModes *m);
int PsuModes_Busy(const PsuModes *m,const PsuControl *p);
int PsuModes_Select(PsuModes *m,PsuControl *p,uint8_t mode,uint32_t now);
int PsuModes_Start(PsuModes *m,PsuControl *p,uint32_t now);
void PsuModes_Stop(PsuModes *m,PsuControl *p);
void PsuModes_SetCurrent(PsuModes *m,uint16_t ma);
/* One call per TIM4 tick; no GPIO, display, Flash or blocking operations. */
void PsuModes_Tick(PsuModes *m,PsuControl *p,uint32_t now);
#endif
