#ifndef PSU_CONTROL_H
#define PSU_CONTROL_H
#include <stdint.h>
#include "psu_settings.h"
enum {
  PSU_F_OCP = 1u, PSU_F_OVP = 2u, PSU_F_VIN_OV = 4u,
  PSU_F_VIN_UV = 8u, PSU_F_OTP = 16u, PSU_F_SENSOR = 32u,
  PSU_F_ADC = 64u, PSU_F_CONT_CURRENT = 128u, PSU_F_TIMING = 256u,
  PSU_F_SHORT = 512u
};
enum { PSU_OFF, PSU_RAMP, PSU_CV, PSU_CC, PSU_FAULT };
enum {
  PSU_B_CONFIG = 1u, PSU_B_FAULT = 2u, PSU_B_ADC = 4u,
  PSU_B_TEMP = 8u, PSU_B_VIN_LOW = 16u, PSU_B_VIN_HIGH = 32u,
  PSU_B_HOT = 64u, PSU_B_CURRENT = 128u, /* 256 reserved: former VOUT interlock */
  PSU_B_SETTLING = 512u
};
/* First raw ADC sample causing a latched protection event; retained until
   explicit fault clear, so foreground logging cannot miss the transient. */
typedef struct {
  uint32_t cause, sample_index;
  uint16_t vout_adc, current_adc, vin_adc, ovp_adc;
  float vout_volts_per_count;
} PsuFaultSample;

typedef struct {
  float vin, vout, current, temp_c;
  float vin_raw, vout_raw, current_raw;
  float voltage_set, current_set, reference_v, cv_i, cc_i, ratio, ovp_reference;
  PsuSettings settings;
  float previous_v, previous_i, dv_rate, di_rate;
  float duty_buck, duty_boost_ho;
  /* Latest regulator decisions for bench diagnostics (ratios, amperes). */
  float cv_command, cc_command, current_limit;
  volatile float continuous_i2;
  volatile uint32_t fault;
  uint32_t config_errors, samples, adc_age_ms, temp_age_ms, start_block;
  uint16_t ocp_raw, ovp_raw, vin_ov_raw, ocp_count, rail_count;
  uint16_t continuous_ms, uv_ms, ready_ms, short_ms;
  uint8_t state;
  volatile uint8_t enabled;
  uint8_t temp_valid, initialized, vin_warning, precharge_hold;
  PsuFaultSample fault_sample;
} PsuControl;
uint32_t Psu_ConfigErrors(void);
void Psu_Init(PsuControl *p);
void Psu_Setpoint(PsuControl *p, float voltage, float current);
int Psu_ApplySettings(PsuControl *p, const PsuSettings *settings);
/* Called for EVERY consecutive ADC frame, before averaging or IIR. */
uint32_t Psu_FastSample(PsuControl *p, uint16_t vout, uint16_t current, uint16_t vin);
void Psu_Measurement(PsuControl *p, uint16_t vout, uint16_t current, uint16_t vin);
/* Called by the continuous ADC producer; TIM4 only consumes the result. */
void Psu_CurrentMeanSquare(PsuControl *p, float mean_square, float seconds);
void Psu_Temperature(PsuControl *p, float celsius, uint8_t valid);
void Psu_Tick1kHz(PsuControl *p);
int Psu_Enable(PsuControl *p);
void Psu_Disable(PsuControl *p);
int Psu_ClearFault(PsuControl *p);
#endif
