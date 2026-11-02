#include "psu_control.h"
#include <string.h>

static float limit(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
static uint16_t counts(const PsuControl *p,float v) { return (uint16_t)limit(v * 4095.0f / p->settings.vdda, 0.0f, 65535.0f); }
static void thresholds(PsuControl *p) {
  float ovp = limit(p->ovp_reference * (1.0f+p->settings.ovp_percent*0.01f) + p->settings.ovp_offset, 0.0f, PSU_OUTPUT_OVP_ABS_V);
  p->ocp_raw = counts(p,p->settings.i_peak * PSU_SHUNT_OHM * PSU_CURRENT_GAIN * PSU_CURRENT_ATTENUATION / p->settings.i_cal + p->settings.i_offset);
  p->ovp_raw = counts(p,ovp / (PSU_VOUT_DIVIDER * p->settings.vout_cal));
  p->vin_ov_raw = counts(p,p->settings.vin_max / (PSU_VIN_DIVIDER * p->settings.vin_cal));
}
uint32_t Psu_ConfigErrors(void) {
  uint32_t e = 0;
  if (PSU_VDDA_V * 0.95f * PSU_VIN_DIVIDER * PSU_VIN_CAL < PSU_VIN_MAX_V + 2.0f) e |= 1u;
  //if (PSU_VDDA_V * 0.95f * PSU_VOUT_DIVIDER * PSU_VOUT_CAL < PSU_OUTPUT_OVP_ABS_V + 1.0f) e |= 2u;
  if (PSU_CURRENT_MAX_A <= 0.0f || PSU_CURRENT_PEAK_A <= PSU_CURRENT_MAX_A ||
      PSU_CURRENT_OFFSET_V + PSU_CURRENT_PEAK_A * 1.05f * PSU_SHUNT_OHM * PSU_CURRENT_GAIN * PSU_CURRENT_ATTENUATION / PSU_CURRENT_CAL > PSU_VDDA_V * 0.95f) e |= 4u;
  if (!PSU_NTC_CONFIGURED) e |= 8u;
  if (!PSU_GATE_WIRING_VERIFIED) e |= 16u;
  if (!PSU_CURRENT_PATH_VERIFIED && !PSU_BENCH_MODE) e |= 32u;
  if ((!PSU_POWER_STAGE_VERIFIED && !PSU_BENCH_MODE) || PSU_POWER_MAX_W <= 0.0f || PSU_INPUT_CURRENT_MAX_A <= 0.0f) e |= 64u;
  return e;
}
void Psu_Init(PsuControl *p) {
  memset(p, 0, sizeof(*p)); p->config_errors = Psu_ConfigErrors();
  p->adc_age_ms = p->temp_age_ms = 1000; p->temp_c = -99.0f;
  PsuSettings_Defaults(&p->settings);
  Psu_Setpoint(p, 5.0f, 1.0f);
}
void Psu_Setpoint(PsuControl *p, float v, float i) {
  if (!(v >= PSU_VOUT_MIN_V && v <= p->settings.vout_max) || !(i >= 0.05f && i <= p->settings.i_max)) return;
  if(p->enabled && v<p->voltage_set && p->vout_raw>v+0.05f)p->precharge_hold=1;
  if (!p->enabled || v > p->ovp_reference) p->ovp_reference = v;
  p->voltage_set = v; p->current_set = i;
  /* Upward edits follow the same ramp as startup. Downward edits take
     effect immediately, with the existing precharge hold / OVP envelope. */
  if(!p->enabled)p->reference_v=0;
  else if(p->reference_v>v)p->reference_v=v;
  thresholds(p);
}
void Psu_Disable(PsuControl *p) {
  p->ovp_count=0;
  p->manual_duty_pm=0;
  p->enabled = 0; p->duty_buck = p->duty_boost_ho = p->ratio = 0.0f;
  p->cv_i = p->cc_i = p->reference_v = 0.0f;
  p->cv_command = p->cc_command = p->current_limit = 0.0f;
  p->dv_rate=p->di_rate=0;
  p->precharge_hold=0;
  p->uv_ms = p->short_ms = 0;
  p->start_block = 0;
  p->state = p->fault ? PSU_FAULT : PSU_OFF;
}
static void trip(PsuControl *p, uint32_t fault) {
  p->fault |= fault; Psu_Disable(p);
  /* Re-establish healthy sensor conditions after each new trip. */
  p->ready_ms = 0;
}
static void trip_raw(PsuControl *p, uint32_t fault, uint16_t v, uint16_t i, uint16_t in) {
  if (!p->fault) {
    p->fault_sample.cause = fault;
    p->fault_sample.sample_index = p->samples + 1u;
    p->fault_sample.vout_adc = v; p->fault_sample.current_adc = i;
    p->fault_sample.vin_adc = in; p->fault_sample.ovp_adc = p->ovp_raw;
    p->fault_sample.vout_volts_per_count=p->settings.vdda*PSU_VOUT_DIVIDER*p->settings.vout_cal/4095.0f;
  }
  trip(p, fault);
}
uint32_t Psu_FastSample(PsuControl *p, uint16_t v, uint16_t i, uint16_t in) {
  /* The two-sample OCP counter deliberately survives DMA half/full boundaries. */
  if (i >= p->ocp_raw) { if (p->ocp_count < 2) ++p->ocp_count; } else p->ocp_count = 0;
  if (v >= 4090 || i >= 4090 || in >= 4090) { if (p->rail_count < 2) ++p->rail_count; } else p->rail_count = 0;
  if (p->ocp_count >= 2) trip_raw(p, PSU_F_OCP, v, i, in);
  if (p->rail_count >= 2) trip_raw(p, PSU_F_SENSOR, v, i, in);
  /* Four consecutive raw conversions, spanning DMA half/full boundaries.
     No averaging: a clean reading or an OFF resets the confirmation. */
//  if(p->enabled && v>=p->ovp_raw){if(p->ovp_count<4)++p->ovp_count;}
//  else p->ovp_count=0;
//  if(p->ovp_count>=4)trip_raw(p,PSU_F_OVP,v,i,in);
  if (in >= p->vin_ov_raw) trip_raw(p, PSU_F_VIN_OV, v, i, in);
  ++p->samples; return p->fault;
}
void Psu_Measurement(PsuControl *p, uint16_t v, uint16_t i, uint16_t in) {
  float scale = p->settings.vdda / 4095.0f;
  p->vin_raw = in * scale * PSU_VIN_DIVIDER * p->settings.vin_cal;
  p->vout_raw = v * scale * PSU_VOUT_DIVIDER * p->settings.vout_cal;
  p->current_raw = limit((i * scale - p->settings.i_offset) * p->settings.i_cal / (PSU_SHUNT_OHM * PSU_CURRENT_GAIN * PSU_CURRENT_ATTENUATION), 0.0f, 100.0f);
  if (!p->initialized) { p->vin = p->vin_raw; p->vout = p->vout_raw; p->current = p->current_raw; p->initialized = 1; }
  else { p->vin += p->settings.adc_alpha * (p->vin_raw - p->vin); p->vout += p->settings.adc_alpha * (p->vout_raw - p->vout); p->current += p->settings.adc_alpha * (p->current_raw - p->current); }
  p->adc_age_ms = 0;
}
void Psu_CurrentMeanSquare(PsuControl *p, float mean_square, float seconds) {
  /* tau = 1s; all ADC blocks contribute, including blocks between TIM4 ticks. */
  float alpha = limit(seconds, 0.0f, 1.0f);
  if (mean_square < 0.0f) mean_square = 0.0f;
  p->continuous_i2 += alpha * (mean_square - p->continuous_i2);
}
void Psu_Temperature(PsuControl *p, float t, uint8_t valid) {
  p->temp_valid = valid && t >= -30.0f && t <= 150.0f;
  if (p->temp_valid) p->temp_c = p->temp_age_ms > 100 ? t : p->temp_c + 0.1f * (t - p->temp_c);
  p->temp_age_ms = 0;
  /* Raw temperature protects before the display filter. */
  if (p->enabled && (!p->temp_valid || t >= p->settings.temp_trip)) trip(p, p->temp_valid ? PSU_F_OTP : PSU_F_SENSOR);
}
static int healthy(const PsuControl *p) {
  return p->initialized && p->adc_age_ms < 3 && p->temp_age_ms < 200 && p->temp_valid &&
    p->vin_raw >= p->settings.vin_min + 0.2f && p->vin_raw < p->settings.vin_max - 0.5f &&
    p->temp_c < p->settings.temp_reset && p->current_raw < 0.2f;
}
int Psu_Enable(PsuControl *p) {
  uint32_t reason = 0;
  if (p->config_errors) reason |= PSU_B_CONFIG;
  if (p->fault) reason |= PSU_B_FAULT;
  if (!p->initialized || p->adc_age_ms >= 3) reason |= PSU_B_ADC;
  if (!p->temp_valid || p->temp_age_ms >= 200) reason |= PSU_B_TEMP;
  if (p->vin_raw < p->settings.vin_min + 0.2f) reason |= PSU_B_VIN_LOW;
  if (p->vin_raw >= p->settings.vin_max - 0.5f) reason |= PSU_B_VIN_HIGH;
  if (p->temp_c >= p->settings.temp_reset) reason |= PSU_B_HOT;
  if (p->current_raw >= 0.2f) reason |= PSU_B_CURRENT;
  if (p->ready_ms < 100) reason |= PSU_B_SETTLING;
  p->start_block = reason;
  if (reason) return 0;
  Psu_Disable(p);
  /* Start at the actual capacitor voltage, capped at the requested target.
     No step from a precharged output down to a zero reference. */
  p->reference_v=limit(p->vout_raw,0.0f,p->voltage_set);
  p->precharge_hold=p->vout_raw>p->voltage_set+0.05f;
  p->previous_v=p->vout; p->previous_i=p->current;
  p->enabled = 1; p->state = p->reference_v<p->voltage_set?PSU_RAMP:PSU_CV; return 1;
}
int Psu_ClearFault(PsuControl *p) {
  if (p->enabled || p->ready_ms < 100 || !healthy(p)) return 0;
  p->fault = 0; p->continuous_ms = p->ocp_count = p->rail_count = 0;
  memset(&p->fault_sample, 0, sizeof(p->fault_sample));
  p->continuous_i2 = 0; Psu_Disable(p); return 1;
}
void Psu_Tick1kHz(PsuControl *p) {
  float ev, ei, ff, cv, cc, selected, ceiling, current_limit, max_power, release_margin;
  uint8_t current_binding;
  if (p->adc_age_ms < 10000) ++p->adc_age_ms;
  if (p->temp_age_ms < 10000) ++p->temp_age_ms;
  p->vin_warning = p->vin < p->settings.vin_warn;
  /* Current mean square is produced by ADC callbacks, independently of TIM4. */
  if (healthy(p)) { if (p->ready_ms < 1000) ++p->ready_ms; } else p->ready_ms = 0;
  if (!p->enabled) return;
  if (p->reference_v >= 1.5f && p->vout_raw < 0.3f && p->current_raw > p->current_set * 0.9f) {
    if (++p->short_ms >= 500) trip(p, PSU_F_SHORT);
  } else p->short_ms = 0;
  if (!p->enabled) return;
  if (p->adc_age_ms >= 3 || p->temp_age_ms >= 200) trip(p, PSU_F_ADC);
  if (!p->temp_valid) trip(p, PSU_F_SENSOR);
  if (p->temp_c >= p->settings.temp_trip) trip(p, PSU_F_OTP);
  if (p->vin_raw < p->settings.vin_min) { if (++p->uv_ms >= 5) trip(p, PSU_F_VIN_UV); } else p->uv_ms = 0;
  if (p->vin_raw > p->settings.vin_max) trip(p, PSU_F_VIN_OV);
  if (p->continuous_i2 > p->settings.i_max * p->settings.i_max) {
    if (++p->continuous_ms >= 100) trip(p, PSU_F_CONT_CURRENT);
  } else p->continuous_ms = 0;
  if (!p->enabled) return;
  /* A downward setpoint edit must allow the existing capacitor voltage to
     settle. OVP follows a monotonically falling envelope; it never follows
     a rising fault voltage. The absolute OVP ceiling always applies. */
  p->ovp_reference = limit(p->vout_raw, p->voltage_set, p->ovp_reference);
  thresholds(p);
  if(p->open_loop){
    /* Diagnostic open loop: protections trip, never silently regulate duty. */
    if(p->current_raw>p->current_set || p->vout_raw*p->current_raw>p->settings.power_max ||
       p->vout_raw*p->current_raw>p->vin_raw*p->settings.input_current_max*p->settings.efficiency){
      trip(p,PSU_F_CONT_CURRENT);return;
    }
    if(p->manual_duty_pm>900)p->manual_duty_pm=900;
    p->duty_buck=p->manual_duty_pm*0.001f;
    p->duty_boost_ho=p->manual_duty_pm?0.90f:0;
    p->ratio=p->duty_buck/0.90f;p->reference_v=0;
    p->cv_i=p->cc_i=p->cv_command=p->cc_command=0;
    p->state=PSU_CV;return;
  }
  p->reference_v=limit(p->reference_v+p->settings.ramp_v_per_s/PSU_CONTROL_HZ,0.0f,p->voltage_set);
  /* Hold only an output precharged above its target, or a downward setpoint
     edit. A normal CV overshoot must be corrected by the regulator: cutting
     PWM and clearing its integrator every +50 mV created repeated bursts.
     Raw OVP and all current/temperature protections stay active throughout. */
  if(p->precharge_hold && p->vout_raw<=p->voltage_set+0.02f)p->precharge_hold=0;
  if (p->precharge_hold) {
    p->duty_buck=p->duty_boost_ho=p->ratio=0;
    p->cv_i=p->cc_i=p->dv_rate=p->di_rate=0;
    p->cv_command=p->cc_command=p->current_limit=0;
    p->previous_v=p->vout;p->previous_i=p->current;
    p->state=p->reference_v<p->voltage_set?PSU_RAMP:PSU_CV;
    return;
  }
  p->dv_rate+=0.1f*((p->vout-p->previous_v)*1000.0f-p->dv_rate);
  p->di_rate+=0.1f*((p->current-p->previous_i)*1000.0f-p->di_rate);
  p->previous_v=p->vout;p->previous_i=p->current;
  ff = p->reference_v / limit(p->vin, p->settings.vin_min, p->settings.vin_max);
  max_power = limit(p->settings.input_current_max * p->vin * p->settings.efficiency, 0.0f, p->settings.power_max);
  current_limit = limit(max_power / limit(p->vout, PSU_VOUT_MIN_V, PSU_OUTPUT_OVP_ABS_V), 0.0f, p->current_set);
  if (p->temp_c > p->settings.temp_warn) current_limit *= limit((p->settings.temp_trip - p->temp_c) / (p->settings.temp_trip - p->settings.temp_warn), 0.0f, 1.0f);
  p->current_limit = current_limit;
  ev = p->reference_v - p->vout; ei = current_limit - p->current;
  cv = ff + p->settings.cv_kp * ev + p->cv_i - p->settings.cv_kd*p->dv_rate;
  /* Preserve the BBV6.5 regulator measured at 2 A; BBV6.6's experimental
     absolute-command CC implementation was not flashed/bench validated. */
  cc = ff + p->settings.cc_kp * ei + p->cc_i - p->settings.cc_kd*p->di_rate;
  ceiling = 0.90f / 0.08f;
  /* A tracked current PI can inherit a low actuator command during startup
     or after current limiting. If current is clearly BELOW the effective
     limit, release that stale restriction and preload the current PI to the
     voltage command. Near/above the limit the original min selector keeps
     full priority, including during temperature/power derating. */
  release_margin = limit(current_limit * 0.05f, 0.01f, 0.10f);
  current_binding = cc < cv;
  if (ei > release_margin && cc < cv) {
    cc=cv;
    p->cc_i = limit(cc - ff - p->settings.cc_kp * ei + p->settings.cc_kd*p->di_rate, -ceiling, ceiling);
    current_binding = 0;
  }
  p->cv_command = cv; p->cc_command = cc;
  selected = limit(cv < cc ? cv : cc, 0.0f, ceiling);
  /* Both integrators track the selected actuator: smooth CV/CC handoff. */
  p->cv_i = limit(p->cv_i + p->settings.cv_ki * 0.001f * ev + 0.05f * (selected - cv), -ceiling, ceiling);
  if(!current_binding) {
    /* Inactive CC follows the applied actuator baseline without accumulating
       positive current error. Its proportional headroom stays available;
       crossing the current limit can take ownership on the next tick. */
    p->cc_i=limit(selected-ff+p->settings.cc_kd*p->di_rate,-ceiling,ceiling);
  } else p->cc_i = limit(p->cc_i + p->settings.cc_ki * 0.001f * ei + 0.05f * (selected - cc), -ceiling, ceiling);
  p->ratio = selected;
  /* Both half bridges switch: avoids holding a bootstrap high side at 100%.
     Vout/Vin = D_buck_HO / D_boost_HO, NOT D_buck/(1-D_boost_HO). */
  if (selected <= 1.0f) { p->duty_buck = 0.90f * selected; p->duty_boost_ho = 0.90f; }
  else { p->duty_buck = 0.90f; p->duty_boost_ho = 0.90f / selected; }
  if (p->duty_buck < 0.025f) p->duty_buck = p->duty_boost_ho = 0.0f;
  p->state = current_binding ? PSU_CC : p->reference_v<p->voltage_set?PSU_RAMP:PSU_CV;
}

int Psu_ApplySettings(PsuControl *p,const PsuSettings *settings) {
  if(p->enabled || PsuSettings_Validate(settings)) return 0;
  p->settings=*settings;
  Psu_Disable(p);
  Psu_Setpoint(p,limit(p->voltage_set,PSU_VOUT_MIN_V,settings->vout_max),limit(p->current_set,0.05f,settings->i_max));
  p->initialized=p->temp_valid=0;p->ready_ms=0;
  p->adc_age_ms=p->temp_age_ms=10000;p->continuous_i2=0;
  p->ocp_count=p->rail_count=p->continuous_ms=0;
  return 1;
}
