#include "psu_app.h"
#include "psu_remote.h"
#include "usbd_cdc_if.h"
#include "usb_device.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

extern ADC_HandleTypeDef hadc1, hadc2;
extern TIM_HandleTypeDef htim1, htim2, htim3, htim4;
extern USBD_HandleTypeDef hUsbDeviceFS;
static PsuControl control;
static PsuModes modes;
static PsuMenu menu;
static uint16_t adc_dma[2u * PSU_ADC_MEAN_FRAMES * 3u];
/* DMA producer publishes one mean of four scans. TIM4 consumes the newest
   complete mean; it never polls ADC or rebuilds averages from DMA memory. */
static volatile struct { uint16_t vout, current, vin; uint32_t sequence; } adc_mean;
static uint32_t adc_consumed;
static volatile uint32_t request, request_v = 5000, request_i = 1000;
static volatile uint32_t heartbeat, rx_head, rx_tail;
static uint8_t rx_ring[2048];
static volatile uint8_t rx_overflow, rx_reset, query_log, cancel_remote_on;
static char rx_line[1024];
static unsigned rx_length;
static uint8_t rx_discard;
static PsuRemoteCommand remote;
static volatile uint8_t remote_phase; /* 0 free, 1 queued, 2 reply, 3 Flash save */
static const char *remote_error;
static uint32_t settings_revision, rx_drops;
static volatile uint32_t rx_epoch;
static PsuView view;
/* LCD can service USB/buttons while drawing: keep its snapshot out of the
   nested foreground/USB call stack, with room for TIM4 and DMA preemption. */
static PsuView display_view;
static volatile uint32_t last_ui;
static uint32_t last_log, max_cycles, usb_drops;
static uint32_t max_display_ms, last_heartbeat;
static uint8_t display_active;
static uint32_t timing_sources, timing_value;
static volatile uint16_t temp_adc;
static uint16_t encoder_old;
static int32_t encoder_remainder;
static uint8_t edit_current, voltage_digit, current_digit, selected_preset, save_pending, saved_notice;
static uint8_t tx_buffer[2304];
static uint32_t tx_length;
static struct {
  uint32_t started, last_pulse, window, pulses;
  float initial_v, window_v;
  uint16_t ticks;
  volatile uint8_t state;
  uint8_t fired;
} discharge;
static volatile uint8_t discharge_timer;
static volatile uint8_t heater_on;
static uint32_t heater_rises;
static volatile uint32_t heater_until;
/* Bounded commissioning command only; normal remote ON remains locked in
   SOLDER. The independent heater/IWDG watchdogs remain fully active. */
static uint32_t remote_heat_until;
static uint16_t remote_heat_target;
static uint8_t remote_heat_active;
#define OC_MASK (TIM_CCMR1_OC1M | TIM_CCMR1_OC2M)
#define OC_LOW (TIM_OCMODE_FORCED_INACTIVE | (TIM_OCMODE_FORCED_INACTIVE << 8))

/* Must be called with interrupts masked, or from a higher-priority emergency
   ISR. Forced low precedes clearing OPM: no stopped PWM2 may start repeating. */
static void discharge_restore_timer(void) {
  if (!discharge_timer) return;
  TIM1->CCMR1 = (TIM1->CCMR1 & ~OC_MASK) | OC_LOW;
  TIM1->CR1 &= ~(TIM_CR1_CEN | TIM_CR1_OPM | TIM_CR1_UDIS);
  TIM1->CCR1 = TIM1->CCR2 = 0;
  TIM1->EGR = TIM_EGR_UG;
  TIM1->CR1 |= TIM_CR1_CEN;
  discharge_timer = 0;
}
static void discharge_stop(uint8_t state) {
  discharge.state = state;
  discharge_restore_timer();
}

static uint32_t lock(void) { uint32_t p = __get_PRIMASK(); __disable_irq(); return p; }
static void unlock(uint32_t p) { __set_PRIMASK(p); }
static void timing_record(uint32_t source,uint32_t value){
  uint32_t key=lock();
  if(!timing_sources)timing_value=value;
  timing_sources|=source;
  unlock(key);
}
static void heater_off(void){
  heater_on=0;
  /* Active HIGH heater: GPIO LOW is OFF, even if SPI1 owns the AF mux.
     Latch LOW first; then detach PA6 from alternate-function peripherals. */
  HAL_GPIO_WritePin(HEATER_GPIO_Port,HEATER_Pin,GPIO_PIN_RESET);
  GPIOA->CRL=(GPIOA->CRL&~(15u<<24))|(2u<<24);
  TIM3->CCER&=~TIM_CCER_CC1P;
  TIM3->CCMR1=(TIM3->CCMR1&~TIM_CCMR1_OC1M)|TIM_OCMODE_FORCED_INACTIVE;
  TIM3->CCR1=0;TIM3->EGR=TIM_EGR_UG;
}
static void gate_shutdown(void) {
  /* Original wiring: PA10 drives the two active-LOW IR2184 shutdown pins. */
  GPIOA->BRR=SD_Pin;
  heater_off();
  TIM1->CCMR1=(TIM1->CCMR1&~OC_MASK)|OC_LOW;
  TIM1->CCR1=TIM1->CCR2=0;
  TIM1->BDTR |= TIM_BDTR_OSSI | TIM_BDTR_OSSR;
  TIM1->BDTR &= ~TIM_BDTR_MOE;
}
void PsuApp_EmergencyOff(void) {
  gate_shutdown();
  HAL_GPIO_WritePin(GPIOC,GPIO_PIN_15,GPIO_PIN_RESET);
  if (discharge.state == PSU_D_WAIT || discharge.state == PSU_D_ACTIVE) discharge_stop(PSU_D_ABORTED);
}
/* Hardware heater PWM must stop even if the TIM4 interrupt stops arriving.
   SysTick runs independently; the IWDG covers a fully stalled CPU/IRQ mask. */
void PsuApp_SysTick(void){
  if(!heater_on)return;
  uint32_t key=lock();
  if(heater_on && (int32_t)(HAL_GetTick()-heater_until)>=0){
    timing_record(PSU_TIMING_HEATER,HAL_GetTick()-heater_until);
    control.fault|=PSU_F_TIMING;control.enabled=0;PsuApp_EmergencyOff();
  }
  unlock(key);
}
static void discharge_arm(uint32_t now) {
  if(modes.mode!=PSU_MODE_LAB || modes.discharge_inhibit)return;
  if ((!PSU_DISCHARGE_ENABLED || !control.settings.discharge_enabled) || PSU_LOW_SIDE_TEST) return;
  discharge_stop(PSU_D_WAIT);
  discharge.started = discharge.last_pulse = discharge.window = now;
  discharge.initial_v = discharge.window_v = control.vout_raw;
  discharge.pulses = 0; discharge.ticks = 0; discharge.fired = 0;
}
/* Called inside the locked actuator only. No software wait controls gate ON
   time. PWM2 ends LOW at the OPM update, even if TIM4 stops running. */
static int discharge_actuator(uint32_t now) {
  uint32_t ticks;
  if (discharge.state != PSU_D_ACTIVE) return 0;
  if (control.vout_raw <= control.settings.discharge_stop) { discharge_stop(PSU_D_DONE); return 0; }
  if (control.vout_raw >= PSU_OUTPUT_OVP_ABS_V || control.vout_raw > discharge.initial_v + 0.3f) {
    discharge_stop(PSU_D_RISING); return 0;
  }
  if (now - discharge.started >= (uint32_t)control.settings.discharge_timeout || discharge.pulses >= PSU_DISCHARGE_MAX_PULSES) {
    discharge_stop(PSU_D_TIMEOUT); return 0;
  }
  if (now - discharge.window >= PSU_DISCHARGE_PROGRESS_MS) {
    if (control.vout_raw > discharge.window_v - 0.05f) { discharge_stop(PSU_D_NO_DROP); return 0; }
    discharge.window_v = control.vout_raw; discharge.window = now;
  }
  if (now - discharge.last_pulse < (uint32_t)control.settings.discharge_interval) return discharge_timer;
  if (discharge_timer && (TIM1->CR1 & TIM_CR1_CEN)) {
    /* One 100 us timer period must complete before the next 20 ms slot. */
    timing_record(PSU_TIMING_DISCHARGE_WAIT,now-discharge.last_pulse);
    control.fault |= PSU_F_TIMING; Psu_Disable(&control); PsuApp_EmergencyOff(); return 1;
  }
  if (TIM1->PSC != 0 || TIM1->ARR != PSU_PWM_PERIOD ||
      (TIM1->CR1 & (TIM_CR1_DIR | TIM_CR1_CMS))) {
    timing_record(PSU_TIMING_DISCHARGE_CONFIG,TIM1->CR1);
    control.fault |= PSU_F_TIMING; Psu_Disable(&control); PsuApp_EmergencyOff(); return 1;
  }
  /* delta-I = Vout*t/L; nominal 1 A, truncated down, hard cap 20 us.
     2 us minimum is adequate for the driver's propagation delay. */
  ticks = (uint32_t)(PSU_INDUCTANCE_H * control.settings.discharge_peak * 72000000.0f / control.vout_raw);
  if (ticks > (uint32_t)control.settings.discharge_on_us * 72u) ticks = (uint32_t)control.settings.discharge_on_us * 72u;
  if (ticks < 144u) ticks = 144u;
  TIM1->CCMR1 = (TIM1->CCMR1 & ~OC_MASK) | OC_LOW;
  TIM1->CR1 &= ~(TIM_CR1_CEN | TIM_CR1_UDIS);
  TIM1->CR1 |= TIM_CR1_OPM;
  TIM1->RCR = 0; /* EXACTLY one pulse, no repetition counter extension. */
  TIM1->CCR1 = 0; TIM1->CCR2 = PSU_PWM_PERIOD + 1u - ticks;
  TIM1->EGR = TIM_EGR_UG;
  TIM1->CCMR1 = (TIM1->CCMR1 & ~OC_MASK) | TIM_OCMODE_FORCED_INACTIVE | (TIM_OCMODE_PWM2 << 8);
  discharge_timer = 1; discharge.fired = 1;
  discharge.last_pulse = now; discharge.ticks = (uint16_t)ticks; ++discharge.pulses;
  TIM1->BDTR |= TIM_BDTR_MOE;
  GPIOA->BSRR=SD_Pin;
  TIM1->CR1 |= TIM_CR1_CEN;
  return 1;
}
static void actuator(void) {
  uint32_t key = lock();
  const uint32_t mode_mask = TIM_CCMR1_OC1M | TIM_CCMR1_OC2M;
  const uint32_t inactive = TIM_OCMODE_FORCED_INACTIVE | (TIM_OCMODE_FORCED_INACTIVE << 8);
  const uint32_t pwm = TIM_OCMODE_PWM1 | (TIM_OCMODE_PWM1 << 8);
  uint8_t heat_ok=modes.mode==PSU_MODE_SOLDER && control.enabled && modes.solder.phase==PSU_SOL_RUN &&
    modes.solder_request && !modes.solder.error && modes.solder.valid;
  if(!heat_ok || modes.solder.duty<=0)heater_off();
  if (control.fault || control.config_errors || !control.initialized ||
      control.adc_age_ms >= 3 || control.temp_age_ms >= 200 || !control.temp_valid ||
      (!(TIM1->CR1 & TIM_CR1_CEN) && !discharge_timer)) PsuApp_EmergencyOff();
  else if((modes.mode==PSU_MODE_SOLDER || modes.mode==PSU_MODE_CHARGE) && !control.enabled) {
    gate_shutdown();
  }
  else if ((!control.enabled || PSU_LOW_SIDE_TEST) && control.ready_ms < 100) {
    /* A normal OFF may need 100 ms for load current to settle before the
       first shot. Preserve only WAIT; loss of health during ACTIVE aborts. */
    if (discharge.state == PSU_D_ACTIVE) PsuApp_EmergencyOff();
    else gate_shutdown();
    if (discharge.state == PSU_D_WAIT && HAL_GetTick() - discharge.started >= (uint32_t)control.settings.discharge_timeout)
      discharge_stop(PSU_D_TIMEOUT);
  }
  else if (discharge_actuator(HAL_GetTick())) { /* Hardware one-shot or its low-side decay interval. */ }
  else if (PSU_LOW_SIDE_TEST || !control.enabled || control.duty_buck <= 0.0f) {
    /* Force BOTH IN pins low immediately, even if a nonzero compare is still
       active in the PWM preload registers. Keep low sides ON in normal OFF
       and zero-command RUN. The emergency path above disables /SD and heater. */
    if ((TIM1->CCMR1 & mode_mask) != inactive || TIM1->CCR1 || TIM1->CCR2) {
      TIM1->CCMR1 = (TIM1->CCMR1 & ~mode_mask) | inactive;
      TIM1->CCR1 = TIM1->CCR2 = 0;
      TIM1->EGR = TIM_EGR_UG;
    }
    TIM1->BDTR |= TIM_BDTR_MOE;
    GPIOA->BSRR=SD_Pin;
  }
  else {
    uint32_t buck = (uint32_t)(control.duty_buck * (PSU_PWM_PERIOD + 1));
    uint32_t boost = (uint32_t)(control.duty_boost_ho * (PSU_PWM_PERIOD + 1));
    if ((TIM1->CCMR1 & mode_mask) != pwm || !(TIM1->BDTR & TIM_BDTR_MOE)) {
      /* Restart from driven-low inputs. Load both compares before restoring
         PWM1, so the first pulse cannot use a stale preloaded command. */
      TIM1->CCMR1 = (TIM1->CCMR1 & ~mode_mask) | inactive;
      TIM1->CCR1 = buck; TIM1->CCR2 = boost;
      TIM1->EGR = TIM_EGR_UG;
      TIM1->CCMR1 = (TIM1->CCMR1 & ~mode_mask) | pwm;
    } else {
      /* Prevent a timer update between the two preload writes. Both new
         duties take effect together at the next natural PWM update. */
      uint32_t cr1 = TIM1->CR1;
      TIM1->CR1 = cr1 | TIM_CR1_UDIS;
      TIM1->CCR1 = buck; TIM1->CCR2 = boost;
      TIM1->CR1 = cr1;
    }
    TIM1->BDTR |= TIM_BDTR_MOE;
    GPIOA->BSRR=SD_Pin;
  }
  if(modes.mode==PSU_MODE_SOLDER && modes.solder_on && !control.fault && !control.config_errors && control.initialized && control.adc_age_ms<3 && control.temp_valid && control.temp_age_ms<200 && (TIM1->CR1&TIM_CR1_CEN)){
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_15,GPIO_PIN_SET);
    if(control.enabled && modes.solder.phase==PSU_SOL_RUN && modes.solder.valid &&
       !modes.solder.error && modes.solder_request && modes.solder.duty>0 && (TIM1->BDTR&TIM_BDTR_MOE)){
      /* PA6 is TIM3_CH1. Renew the independent PWM watchdog every PID tick. */
      if(!heater_on)++heater_rises;
      heater_until=HAL_GetTick()+2u;heater_on=1;
      uint32_t compare=(uint32_t)(modes.solder.duty*(PSU_PWM_PERIOD+1u)+0.5f);
      if(compare>(PSU_PWM_PERIOD+1u)*95u/100u)compare=(PSU_PWM_PERIOD+1u)*95u/100u;
      TIM3->CCER&=~TIM_CCER_CC1P; /* PWM1 non-inverted: CCR counts heater-ON HIGH time. */
      TIM3->CCR1=compare;
      if((TIM3->CCMR1&TIM_CCMR1_OC1M)!=TIM_OCMODE_PWM1){
        TIM3->EGR=TIM_EGR_UG;
        TIM3->CCMR1=(TIM3->CCMR1&~TIM_CCMR1_OC1M)|TIM_OCMODE_PWM1;
      }
      GPIOA->CRL=(GPIOA->CRL&~(15u<<24))|(11u<<24);
    }
  }else HAL_GPIO_WritePin(GPIOC,GPIO_PIN_15,GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, ((control.enabled || modes.solder_request) != PSU_LED_ACTIVE_LOW) ? GPIO_PIN_SET : GPIO_PIN_RESET);
  unlock(key);
}
/* ADC2 injected PA3 leaves the continuous PA0 NTC conversion and ADC1 DMA
   untouched. One asynchronous conversion per ms, only after 2 ms heater OFF.
   Poll JEOC; never wait for ADC in the 1 kHz control ISR. */
static void solder_adc(uint32_t now){
  static uint32_t quiet_ms;
  static uint8_t was_quiet,pending;
  /* Keep PA3 observable in LAB, SOLDER OFF and latched fault states too. */
  uint8_t quiet=!heater_on;
  if(!quiet){was_quiet=pending=0;ADC2->SR &= ~ADC_SR_JEOC;return;}
  if(!was_quiet){quiet_ms=now;was_quiet=1;pending=0;ADC2->SR &= ~ADC_SR_JEOC;}
  if(now-quiet_ms<2)return;
  if(ADC2->SR&ADC_SR_JEOC){
    uint16_t raw=(uint16_t)ADC2->JDR1;ADC2->SR &= ~ADC_SR_JEOC;
    if(pending)PsuSolder_Sample(&modes,raw,now);
    pending=0;
    if(modes.mode==PSU_MODE_SOLDER && modes.solder.error)heater_off();
  }
  if(!pending){ADC2->CR2|=ADC_CR2_JEXTTRIG|ADC_CR2_JSWSTART;pending=1;}
}
static void adc_block(unsigned start) {
  unsigned k; uint32_t a = 0, b = 0, c = 0, sq = 0;
  float offset_counts = control.settings.i_offset * 4095.0f / control.settings.vdda;
  float current_scale = control.settings.vdda * control.settings.i_cal / (4095.0f * PSU_SHUNT_OHM * PSU_CURRENT_GAIN * PSU_CURRENT_ATTENUATION);
  for (k = start; k < start + PSU_ADC_MEAN_FRAMES * 3u; k += 3) {
    uint16_t v = adc_dma[k], i = adc_dma[k + 1], in = adc_dma[k + 2];
    if(modes.mode==PSU_MODE_CHARGE && control.enabled && modes.ovp_adc && control.ovp_raw>modes.ovp_adc)control.ovp_raw=modes.ovp_adc;
    if (Psu_FastSample(&control, v, i, in)) PsuApp_EmergencyOff();
    a += v; b += i; c += in; sq += (uint32_t)i * i;
  }
  Psu_CurrentMeanSquare(&control,
    ((float)sq / PSU_ADC_MEAN_FRAMES - 2.0f*offset_counts*((float)b / PSU_ADC_MEAN_FRAMES) + offset_counts*offset_counts) * current_scale*current_scale,
    PSU_ADC_BLOCK_SECONDS);
  adc_mean.vout = (uint16_t)(a / PSU_ADC_MEAN_FRAMES);
  adc_mean.current = (uint16_t)(b / PSU_ADC_MEAN_FRAMES);
  adc_mean.vin = (uint16_t)(c / PSU_ADC_MEAN_FRAMES);
  __DMB();
  ++adc_mean.sequence;
}
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *adc) { if (adc->Instance == ADC1) adc_block(0); }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc) { if (adc->Instance == ADC1) adc_block(PSU_ADC_MEAN_FRAMES * 3u); }
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *adc) { (void)adc; PsuApp_EmergencyOff(); control.fault |= PSU_F_ADC; control.enabled = 0; }
static int remote_idle_required(void) {
  return !control.enabled && discharge.state!=PSU_D_WAIT && discharge.state!=PSU_D_ACTIVE &&
    (!discharge.fired || HAL_GetTick()-discharge.last_pulse>=(uint32_t)control.settings.discharge_interval);
}
static void clamp_presets(void) {
  for(unsigned k=0;k<3;++k) {
    uint16_t mv=(uint16_t)(control.settings.vout_max*1000.0f+0.5f),ma=(uint16_t)(control.settings.i_max*1000.0f+0.5f);
    if(view.presets[k].millivolts>mv)view.presets[k].millivolts=mv;
    if(view.presets[k].milliamps>ma)view.presets[k].milliamps=ma;
  }
}
static void remote_execute(uint32_t off_requested) {
  if(remote_phase!=1)return;
  uint32_t key=lock();remote_error=0;
  if(remote.op!=REM_OFF && hUsbDeviceFS.dev_state!=USBD_STATE_CONFIGURED)remote_error="disconnected";
  else switch(remote.op) {
  case REM_GET:break;
  case REM_SET:
    if(modes.mode==PSU_MODE_SOLDER || menu.page)remote_error="local_mode_required";
    else if(modes.mode==PSU_MODE_CHARGE && (remote.arg[0]!=PsuModes_FullMillivolts(&modes) || remote.arg[1]<100))remote_error="setpoint_range";
    else if(remote.arg[0]<1500 || remote.arg[0]>control.settings.vout_max*1000.0f+0.5f ||
       remote.arg[1]<50 || remote.arg[1]>control.settings.i_max*1000.0f+0.5f)remote_error="setpoint_range";
    else {request_v=(uint32_t)remote.arg[0];request_i=(uint32_t)remote.arg[1];if(modes.mode==PSU_MODE_CHARGE)PsuModes_SetCurrent(&modes,(uint16_t)request_i);else Psu_Setpoint(&control,request_v/1000.0f,request_i/1000.0f);}
    break;
  case REM_ON:
    if(off_requested || cancel_remote_on)remote_error="cancelled";
    else if(menu.page || modes.mode==PSU_MODE_SOLDER)remote_error="local_mode_required";
    else if(PSU_LOW_SIDE_TEST)remote_error="test_mode";
    else {
      discharge_stop(PSU_D_IDLE);
      if(discharge.fired && HAL_GetTick()-discharge.last_pulse<(uint32_t)control.settings.discharge_interval){unlock(key);return;}
      if(!PsuModes_Start(&modes,&control,HAL_GetTick()))remote_error="blocked";
    }
    break;
  case REM_HEAT:
    if(off_requested || cancel_remote_on)remote_error="cancelled";
    else if(menu.page || modes.mode==PSU_MODE_CHARGE)remote_error="local_mode_required";
    else if(!PSU_SOLDER_TEMP_PROTECTION || remote.arg[0]<100 || remote.arg[0]>10000 ||
      remote.arg[1]<150 || remote.arg[1]>250 || control.settings.solder_voltage>18 ||
      control.settings.solder_current>3)remote_error="bench_range";
    else if(PsuModes_Busy(&modes,&control) || !remote_idle_required())remote_error="output_off_required";
    else if(control.fault || modes.solder.error)remote_error="blocked";
    else {
      remote_heat_target=modes.solder.target;
      PsuSolder_SetTarget(&modes,remote.arg[1]);
      int started=modes.mode==PSU_MODE_LAB?
        PsuModes_Select(&modes,&control,PSU_MODE_SOLDER,HAL_GetTick()):PsuModes_Start(&modes,&control,HAL_GetTick());
      if(!started){
        PsuSolder_SetTarget(&modes,remote_heat_target);remote_error="blocked";
      }else {remote_heat_until=HAL_GetTick()+(uint32_t)remote.arg[0];remote_heat_active=1;}
    }
    break;
  case REM_OFF: {
    uint8_t was_enabled=control.enabled;request&=~8u;PsuModes_Stop(&modes,&control);
    if(was_enabled && !control.fault)discharge_arm(HAL_GetTick());
    break;
  }
  case REM_CLEAR:if(!Psu_ClearFault(&control))remote_error="clear_conditions";break;
  case REM_CONFIG:case REM_DEFAULTS:
    if(modes.mode==PSU_MODE_CHARGE || menu.page)remote_error="local_mode_required";
    else if(PsuModes_Busy(&modes,&control) || !remote_idle_required())remote_error="output_off_required";
    else if(remote.revision!=settings_revision)remote_error="revision_changed";
    else {
      gate_shutdown();
      if(!Psu_ApplySettings(&control,&remote.settings))remote_error="invalid_config";
      else {++settings_revision;clamp_presets();request_v=(uint32_t)(control.voltage_set*1000+0.5f);request_i=(uint32_t)(control.current_set*1000+0.5f);}
    }
    break;
  case REM_SAVE:
    if(PsuModes_Busy(&modes,&control) || !remote_idle_required())remote_error="output_off_required";
    else {save_pending=1;remote_phase=3;unlock(key);return;}
    break;
  case REM_PRESET:
    if(modes.mode!=PSU_MODE_LAB)remote_error="local_mode_required";
    else if(!remote_idle_required())remote_error="output_off_required";
    else if(remote.arg[0]<0 || remote.arg[0]>2 || remote.arg[1]<1500 || remote.arg[1]>control.settings.vout_max*1000.0f+0.5f ||
            remote.arg[2]<50 || remote.arg[2]>control.settings.i_max*1000.0f+0.5f)remote_error="setpoint_range";
    else view.presets[remote.arg[0]]=(PsuPreset){(uint16_t)remote.arg[1],(uint16_t)remote.arg[2]};
    break;
  default:remote_error="unknown_command";break;
  }
  __DMB();remote_phase=2;unlock(key);
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *tim) {
  uint32_t begin, key, sequence, command; uint16_t a, b, c; float t = -99.0f; uint8_t valid_temp = 0;
  if (tim->Instance != TIM4) return;
  begin = DWT->CYCCNT;
  key = lock(); sequence = adc_mean.sequence; a = adc_mean.vout; b = adc_mean.current; c = adc_mean.vin;
  command = request; request = 0; unlock(key);
  if (sequence != adc_consumed) {
    adc_consumed = sequence;
    Psu_Measurement(&control, a, b, c);
  }
  if (ADC2->SR & ADC_SR_EOC) {
    temp_adc = (uint16_t)ADC2->DR;
    if ((heartbeat % 10u) == 0) {
      if (PSU_NTC_CONFIGURED && temp_adc > 8 && temp_adc < 4087) {
        float r = control.settings.ntc_pull * (control.settings.ntc_to_gnd ? (float)temp_adc / (4095 - temp_adc) : (4095.0f - temp_adc) / temp_adc);
        t = 1.0f / (1.0f / 298.15f + logf(r / control.settings.ntc_r25) / control.settings.ntc_beta) - 273.15f;
        valid_temp = 1;
      }
      Psu_Temperature(&control, t, valid_temp);
      if(modes.mode==PSU_MODE_SOLDER && valid_temp && t>=control.settings.temp_trip){control.fault|=PSU_F_OTP;PsuApp_EmergencyOff();}
    }
  }
  if (command & 1u) {
    uint8_t was_enabled = control.enabled;
    PsuModes_Stop(&modes,&control); command &= ~8u;
    if (was_enabled && !control.fault && !(command & 16u)) discharge_arm(HAL_GetTick());
  }
  if (command & 2u) {
    if(modes.mode==PSU_MODE_CHARGE)PsuModes_SetCurrent(&modes,(uint16_t)request_i);
    else if(modes.mode==PSU_MODE_LAB)Psu_Setpoint(&control, request_v / 1000.0f, request_i / 1000.0f);
  }
  if (command & 4u) {
    if(Psu_ClearFault(&control)){timing_sources=timing_value=0;}
  }
  if ((command & 8u) && !menu.page && !PSU_LOW_SIDE_TEST && remote_phase!=3) {
    key = lock(); discharge_stop(PSU_D_IDLE);
    /* Allow reverse inductor current to decay with both low sides ON before
       accepting ON, including an ON request arriving just after a shot. */
    if (discharge.fired && HAL_GetTick() - discharge.last_pulse < (uint32_t)control.settings.discharge_interval) request |= 8u;
    else {
      if(modes.mode==PSU_MODE_SOLDER && !PsuModes_Busy(&modes,&control)){
        heater_off();HAL_GPIO_WritePin(GPIOC,GPIO_PIN_15,GPIO_PIN_RESET);
      }
      (void)PsuModes_Start(&modes,&control,HAL_GetTick());
    }
    unlock(key);
  }
  /* Healthy OFF actively drives the low sides, so monitor it like RUN. */
  if (HAL_GetTick() - last_ui > 250u) {
    timing_record(PSU_TIMING_UI,HAL_GetTick()-last_ui);
    control.fault |= PSU_F_TIMING; Psu_Disable(&control);
  }
  Psu_Tick1kHz(&control);
  if (discharge.state == PSU_D_WAIT && control.ready_ms >= 100 && !control.fault && !control.config_errors) {
    discharge.last_pulse = discharge.window = HAL_GetTick(); discharge.window_v = control.vout_raw;
    discharge.state = PSU_D_ACTIVE;
  }
  remote_execute(command&1u);
  modes.solder.stand=HAL_GPIO_ReadPin(GPIOB,GPIO_PIN_11)==GPIO_PIN_RESET;
  PsuModes_Tick(&modes,&control,HAL_GetTick());
  if(remote_heat_active && ((int32_t)(HAL_GetTick()-remote_heat_until)>=0 || !modes.solder_request || hUsbDeviceFS.dev_state!=USBD_STATE_CONFIGURED)){
    if(modes.solder_request)PsuModes_Stop(&modes,&control);
    heater_off();
    PsuSolder_SetTarget(&modes,remote_heat_target);remote_heat_active=0;
  }
  if (DWT->CYCCNT - begin > 36000u) {
    timing_record(PSU_TIMING_ISR,DWT->CYCCNT-begin);
    control.fault |= PSU_F_TIMING; Psu_Disable(&control);
  }
  actuator();solder_adc(HAL_GetTick()); ++heartbeat;
  if (DWT->CYCCNT - begin > max_cycles) max_cycles = DWT->CYCCNT - begin;
}
void HAL_GPIO_EXTI_Callback(uint16_t pin) { (void)pin; /* Debounce and actions run in foreground. */ }
void PsuApp_Snapshot(PsuView *v) {
  uint32_t key = lock(); v->control = control;
  v->modes=modes;v->menu=menu;
  v->discharge_state = discharge.state; v->discharge_pulses = discharge.pulses;
  v->discharge_ticks = discharge.ticks; v->settings_revision=settings_revision;
  v->hardware.arr=TIM1->ARR;v->hardware.psc=TIM1->PSC;
  v->hardware.ccr1=TIM1->CCR1;v->hardware.ccr2=TIM1->CCR2;v->hardware.ccr3=TIM1->CCR3;
  v->hardware.cr1=TIM1->CR1;v->hardware.ccmr1=TIM1->CCMR1;
  v->hardware.ccer=TIM1->CCER;v->hardware.bdtr=TIM1->BDTR;
  v->hardware.gpio_crh=GPIOA->CRH;v->hardware.afio_mapr=AFIO->MAPR;
  v->hardware.sd=(GPIOA->ODR&SD_Pin)?1u:0u;v->hardware.change=(GPIOC->ODR&GPIO_PIN_15)?1u:0u;
  v->hardware.heater=heater_on;
  v->hardware.pa6_mode=(GPIOA->CRL>>24)&15u;
  v->hardware.heater_ccr=TIM3->CCR1;
  v->hardware.heater_cr1=TIM3->CR1;v->hardware.heater_ccmr1=TIM3->CCMR1;v->hardware.heater_ccer=TIM3->CCER;
  uint32_t block=0;
  if(modes.mode!=PSU_MODE_SOLDER)block|=PSU_HB_MODE;
  if(!modes.solder_request)block|=PSU_HB_REQUEST;
  if(control.fault || control.config_errors || modes.solder.error)block|=PSU_HB_FAULT;
  if(!control.enabled || !modes.solder_on || modes.solder.phase!=PSU_SOL_RUN)block|=PSU_HB_RAIL;
  if(!modes.solder.valid)block|=PSU_HB_TIP;
  if(modes.solder.phase==PSU_SOL_RUN && modes.solder.position<PSU_SOLDER_QUIET_MS)block|=PSU_HB_QUIET;
  if(modes.solder.command<=0)block|=PSU_HB_PID_ZERO;
  if(!(TIM3->CR1&TIM_CR1_CEN) || !(TIM3->CCER&TIM_CCER_CC1E) ||
     !(RCC->APB1ENR&RCC_APB1ENR_TIM3EN) || (AFIO->MAPR&AFIO_MAPR_TIM3_REMAP) ||
     TIM3->PSC!=0 || TIM3->ARR!=PSU_PWM_PERIOD || (v->hardware.pa6_mode!=11 && !(v->hardware.pa6_mode==2 && !heater_on)))block|=PSU_HB_TIMER;
  v->hardware.heater_block=block;

  v->hardware.heater_rises=heater_rises;
  v->hardware.vout_adc=adc_mean.vout;v->hardware.current_adc=adc_mean.current;
  v->hardware.vin_adc=adc_mean.vin;
  v->timing_sources=timing_sources;v->timing_value=timing_value;unlock(key);
  v->max_display_ms=max_display_ms;
  memcpy(v->presets, view.presets, sizeof(v->presets)); v->temp_adc = temp_adc;
  v->edit_current = edit_current; v->edit_digit = edit_current ? current_digit : voltage_digit; v->selected_preset = selected_preset;
  v->saved_notice = saved_notice; v->usb_drops = usb_drops; v->max_control_cycles = max_cycles;
}
static void post(uint32_t bits) { uint32_t key = lock(); request |= bits; unlock(key); }
static void set_values(uint32_t mv, uint32_t ma) {
  uint32_t max_ma = (uint32_t)(control.settings.i_max * 1000.0f + 0.5f);
  if (mv < (uint32_t)(PSU_VOUT_MIN_V * 1000.0f)) mv = (uint32_t)(PSU_VOUT_MIN_V * 1000.0f);
  if (mv > (uint32_t)(control.settings.vout_max * 1000.0f)) mv = (uint32_t)(control.settings.vout_max * 1000.0f);
  if (ma < 50) ma = 50;
  if (ma > max_ma) ma = max_ma;
  if(modes.mode==PSU_MODE_CHARGE){mv=PsuModes_FullMillivolts(&modes);if(ma<100)ma=100;}
  uint32_t key = lock(); request_v = mv; request_i = ma; request |= 2u; unlock(key);
}
static void menu_page(uint8_t page){menu.page=page;menu.selected=menu.editing=menu.notice=0;encoder_remainder=0;}
static int select_mode(uint8_t mode){
  uint32_t key=lock();
  if(PsuModes_Busy(&modes,&control) || (PSU_LOW_SIDE_TEST && mode!=PSU_MODE_LAB)){menu.notice=1;unlock(key);return 0;}
  discharge_stop(PSU_D_IDLE);PsuApp_EmergencyOff();
  TIM1->CCMR1=(TIM1->CCMR1&~OC_MASK)|OC_LOW;TIM1->CCR1=TIM1->CCR2=0;TIM1->EGR=TIM_EGR_UG;
  int ok=PsuModes_Select(&modes,&control,mode,HAL_GetTick());
  if(ok){
    request&=~(2u|8u);cancel_remote_on=1;rx_tail=rx_head;rx_reset=1;++rx_epoch;
    request_v=mode==PSU_MODE_CHARGE?PsuModes_FullMillivolts(&modes):(uint32_t)(control.voltage_set*1000+0.5f);
    request_i=mode==PSU_MODE_CHARGE?modes.current_ma:(uint32_t)(control.current_set*1000+0.5f);
    if(mode==PSU_MODE_CHARGE)edit_current=1;
    menu_page(PSU_MENU_CLOSED);
  }
  unlock(key);return ok;
}
static void menu_press(uint8_t long_press){
  if(long_press){menu_page(menu.page==PSU_MENU_ROOT?PSU_MENU_CLOSED:PSU_MENU_ROOT);return;}
  if(menu.editing){menu.editing=0;return;}
  if(menu.page==PSU_MENU_ROOT){
    if(menu.selected==0)menu_page(PSU_MENU_RAMP);
    else if(menu.selected==1)menu_page(PSU_MENU_CHARGE);
    else if(menu.selected==2)menu_page(PSU_MENU_SOLDER);
    else if(menu.selected==3)(void)select_mode(PSU_MODE_LAB);
    else if(menu.selected==4)menu_page(PSU_MENU_DISPLAY);
    else menu_page(PSU_MENU_CLOSED);
  } else if(menu.page==PSU_MENU_SOLDER){
    if(menu.selected==4){menu_page(PSU_MENU_ROOT);return;}
    if(PsuModes_Busy(&modes,&control)){menu.notice=1;return;}
    if(menu.selected==3){(void)select_mode(PSU_MODE_SOLDER);return;}
    menu.editing=1;
  } else if(menu.page==PSU_MENU_DISPLAY){
    if(menu.selected==2){menu_page(PSU_MENU_ROOT);return;}
    menu.display=menu.selected;menu_page(PSU_MENU_CLOSED);
  } else if(menu.page==PSU_MENU_RAMP){
    if(menu.selected==3){menu_page(PSU_MENU_ROOT);return;}
    uint32_t key=lock();
    if(PsuModes_Busy(&modes,&control) || modes.mode==PSU_MODE_SOLDER){menu.notice=1;unlock(key);return;}
    static const float speed[3]={30.0f,10.0f,2.0f};
    control.settings.ramp_v_per_s=speed[menu.selected];++settings_revision;save_pending=1;menu_page(PSU_MENU_CLOSED);unlock(key);
  } else if(menu.page==PSU_MENU_CHARGE){
    if(menu.selected==6){menu_page(PSU_MENU_ROOT);return;}
    if(PsuModes_Busy(&modes,&control)){menu.notice=1;return;}
    if(menu.selected==5){(void)select_mode(PSU_MODE_CHARGE);return;}
    if(menu.selected==3){menu.notice=2;return;}
    menu.editing=1;
  }
}
static void menu_rotate(int32_t steps){
  menu.notice=0;
  if(!menu.editing){int count=menu.page==PSU_MENU_ROOT?6:menu.page==PSU_MENU_RAMP?4:menu.page==PSU_MENU_DISPLAY?3:menu.page==PSU_MENU_SOLDER?5:7;int at=((int)menu.selected+steps)%count;if(at<0)at+=count;menu.selected=(uint8_t)at;return;}
  uint32_t key=lock();
  if(PsuModes_Busy(&modes,&control)){menu.editing=0;menu.notice=1;unlock(key);return;}
  int32_t value;
  if(menu.page==PSU_MENU_SOLDER){
    if(menu.selected==0)modes.solder.tip^=(uint8_t)(steps&1);
    else if(menu.selected==1)PsuSolder_SetTarget(&modes,modes.solder.target+steps*5);
    else {value=modes.solder.sleep_target+steps*5;modes.solder.sleep_target=(uint16_t)(value<150?150:value>250?250:value);}
    unlock(key);return;
  }
  if(menu.selected==0)modes.chemistry=(uint8_t)((modes.chemistry+(steps&1))&1);
  else if(menu.selected==1){value=modes.cells+steps;modes.cells=(uint8_t)(value<1?1:value>6?6:value);}
  else if(menu.selected==2){value=modes.current_ma+steps*50;int32_t max=(int32_t)(control.settings.i_max*1000);modes.current_ma=(uint16_t)(value<100?100:value>max?max:value);}
  else if(menu.selected==4){value=modes.hours+steps;modes.hours=(uint8_t)(value<1?1:value>12?12:value);}
  if(modes.mode==PSU_MODE_CHARGE){(void)PsuModes_Select(&modes,&control,PSU_MODE_CHARGE,HAL_GetTick());request_v=PsuModes_FullMillivolts(&modes);request_i=modes.current_ma;}
  unlock(key);
}
static void button(unsigned button, uint8_t long_press) {
  /* Inputs can run inside an LCD transfer. Only snapshot the two flags used
     here, instead of stacking a complete telemetry/display view. */
  uint32_t snapshot_key=lock();
  uint8_t enabled=control.enabled,busy=(uint8_t)PsuModes_Busy(&modes,&control);
  unlock(snapshot_key);
  if (button == 0) {
    if(long_press || menu.page){menu_press(long_press);return;}
    if(modes.mode==PSU_MODE_SOLDER)return;
    uint8_t *digit = edit_current ? &current_digit : &voltage_digit;
    *digit = (uint8_t)((*digit + 1u) % (edit_current ? 3u : 4u));
    encoder_remainder = 0;
  }
  else if (button == 1) { /* BTN1 PB9 */
    if (long_press) post(1u | 4u);
    else post(menu.page || busy ? 1u : 8u);
  } else if(button==2 && long_press){ /* Hold V/A: direct SOLDER shortcut. */
    uint32_t key=lock();
    if(modes.mode==PSU_MODE_SOLDER)menu_page(PSU_MENU_CLOSED);
    else if(PSU_LOW_SIDE_TEST || remote_phase==3 || control.fault || control.config_errors){
      menu_page(PSU_MENU_ROOT);menu.selected=2;menu.notice=1;
    }else{
      /* select_mode shuts SD/heater off, cancels queued ON and preserves LAB
         settings. Its normal 100/300 ms route/precharge sequence still runs. */
      PsuModes_Stop(&modes,&control);
      (void)select_mode(PSU_MODE_SOLDER);
    }
    unlock(key);
  } else if(menu.page){if(button==2){if(menu.editing)menu.editing=0;else menu_page(menu.page==PSU_MENU_ROOT?PSU_MENU_CLOSED:PSU_MENU_ROOT);}return;}
  else if (button == 2) { if(modes.mode==PSU_MODE_CHARGE)edit_current=1;else edit_current ^= 1u; encoder_remainder = 0; }
  else {
    if(modes.mode==PSU_MODE_SOLDER){uint32_t key=lock();PsuSolder_SetTarget(&modes,250+(int32_t)(button-3)*50);unlock(key);return;}
    if(enabled || modes.mode!=PSU_MODE_LAB)return;
    unsigned slot = button - 3; selected_preset = (uint8_t)slot;
    if (long_press && !enabled) {
      view.presets[slot].millivolts = (uint16_t)request_v; view.presets[slot].milliamps = (uint16_t)request_i; save_pending = 1;
    } else if (!long_press && !enabled) set_values(view.presets[slot].millivolts, view.presets[slot].milliamps);
  }
}
static void inputs(uint32_t now) {
  static uint8_t raw[6], stable[6], fired[6];
  static uint32_t changed[6], pressed[6];
  static const uint16_t pins[6] = {GPIO_PIN_4, GPIO_PIN_9, GPIO_PIN_8, GPIO_PIN_7, GPIO_PIN_6, GPIO_PIN_5};
  static const int32_t increments[4] = {10, 100, 1000, 10000};
  int16_t delta; unsigned k;
  for (k = 0; k < 6; ++k) {
    uint8_t down = HAL_GPIO_ReadPin(GPIOB, pins[k]) == GPIO_PIN_RESET;
    if (down != raw[k]) { raw[k] = down; changed[k] = now; }
    if (now - changed[k] >= 25u && stable[k] != down) {
      stable[k] = down;
      if (down) { pressed[k] = now; fired[k] = 0;
        /* Output OFF is immediate after debounce; ON requires release. */
        if (k == 1 && PsuModes_Busy(&modes,&control)) { post(1u); fired[k] = 1; }
      } else if (!fired[k]) button(k, 0);
    }
    if (stable[k] && !fired[k] && now - pressed[k] >= 1000u) { button(k, 1); fired[k] = 1; }
  }
  { uint16_t value = (uint16_t)TIM2->CNT; delta = (int16_t)(value - encoder_old); encoder_old = value; }
  encoder_remainder += delta;
  if (encoder_remainder >= PSU_ENCODER_COUNTS || encoder_remainder <= -PSU_ENCODER_COUNTS) {
    int32_t steps = PSU_ENCODER_DIRECTION * (encoder_remainder / PSU_ENCODER_COUNTS); int32_t value;
    encoder_remainder %= PSU_ENCODER_COUNTS;
    if(menu.page){menu_rotate(steps);return;}
    if(modes.mode==PSU_MODE_SOLDER){uint32_t key=lock();PsuSolder_SetTarget(&modes,modes.solder.target+steps*5);unlock(key);return;}
    if(modes.mode==PSU_MODE_CHARGE)edit_current=1;
    value = (int32_t)(edit_current ? request_i : request_v) + steps * increments[edit_current ? current_digit : voltage_digit];
    if (edit_current) { int32_t max_ma = (int32_t)(control.settings.i_max * 1000.0f + 0.5f); if (value < 50) value = 50; if (value > max_ma) value = max_ma; set_values(request_v, (uint32_t)value); }
    else { if (value < 1500) value = 1500; if (value > (int32_t)(control.settings.vout_max*1000)) value = (int32_t)(control.settings.vout_max*1000); set_values((uint32_t)value, request_i); }
  }
}
void PsuApp_UsbRx(const uint8_t *data, uint32_t length) {
  while(length--) {
    uint8_t c=*data++;
    if(c=='!') {
        uint32_t key=lock();cancel_remote_on=1;PsuApp_EmergencyOff();
      /* Cancel both queued bytes and a partially parsed ON line. Otherwise
         the newline after '!' could complete and later execute that ON. */
      rx_tail=rx_head;rx_overflow=0;rx_reset=1;
      ++rx_epoch;
      request=(request&~8u)|1u|16u;unlock(key);continue;
    }
    if(c=='?'){query_log=1;continue;}
    uint32_t next=(rx_head+1u)&2047u;
    if(next==rx_tail){rx_overflow=1;++rx_drops;continue;}
    rx_ring[rx_head]=c;__DMB();rx_head=next;
  }
}
static void usb_receive_line(void) {
  if(remote_phase)return;
  /* A continuous malformed USB stream must yield back to buttons/display. */
  unsigned budget=128;
  while(budget--) {
    /* '!' may flush the ring from the USB ISR. Dequeue and generation checks
       keep that flush atomic with this foreground parser. */
    uint32_t key=lock();
    if(rx_reset){rx_length=rx_discard=0;rx_reset=0;}
    if(rx_overflow){rx_tail=rx_head;rx_overflow=0;rx_length=0;rx_discard=1;}
    if(rx_tail==rx_head){unlock(key);return;}
    uint32_t epoch=rx_epoch;
    uint8_t c=rx_ring[rx_tail];rx_tail=(rx_tail+1u)&2047u;unlock(key);
    if(c=='\r')continue;
    if(c=='\n') {
      if(rx_discard){rx_discard=0;rx_length=0;continue;}
      if(!rx_length)continue;
      rx_line[rx_length]=0;rx_length=0;
      PsuSettings base;key=lock();base=control.settings;unlock(key);
      remote_error=PsuRemote_Parse(rx_line,&base,&remote);
      key=lock();
      if(epoch!=rx_epoch){rx_length=rx_discard=0;unlock(key);return;}
      cancel_remote_on=0;__DMB();remote_phase=remote_error?2:1;unlock(key);return;
    }
    if(c<32 || c>126 || rx_length>=sizeof(rx_line)-1){rx_discard=1;rx_length=0;continue;}
    if(!rx_discard)rx_line[rx_length++]=(char)c;
  }
}
static uint32_t usb_reply(const PsuView *s) {
  static const char *ops[]={"GET","SET","ON","OFF","CLEAR","CFG","DEFAULTS","SAVE","PRESET","HEAT"};
  const char *error=remote_error?remote_error:"ok";
  int n=snprintf((char *)tx_buffer,sizeof(tx_buffer),
    "{\"type\":\"reply\",\"protocol\":1,\"fw\":\"BBV6.40-PA6-SOFTLCD\",\"id\":%lu,\"op\":\"%s\",\"ok\":%s,\"error\":\"%s\",\"rev\":%lu,\"enabled\":%u,\"fault\":%lu,\"start_block\":%lu,\"set_mV\":%ld,\"set_mA\":%ld,\"softstart\":true,\"settings\":",
    (unsigned long)remote.id,remote.op<10?ops[remote.op]:"?",remote_error?"false":"true",error,
    (unsigned long)s->settings_revision,s->control.enabled,(unsigned long)s->control.fault,(unsigned long)s->control.start_block,
    (long)(s->control.voltage_set*1000+0.5f),(long)(s->control.current_set*1000+0.5f));
  if(n<0 || (unsigned)n>=sizeof(tx_buffer))return 0;
  size_t used=(size_t)n,added=PsuSettings_Json(&s->control.settings,(char *)tx_buffer+used,sizeof(tx_buffer)-used);
  if(!added)return 0;
  used+=added;
  n=snprintf((char *)tx_buffer+used,sizeof(tx_buffer)-used,
    ",\"presets\":[[%u,%u],[%u,%u],[%u,%u]],\"caps\":{\"vout_mV\":%lu,\"i_mA\":%lu,\"ipeak_mA\":%lu,\"vin_mV\":%lu,\"power_mW\":%lu,\"input_mA\":%lu,\"ovp_mV\":%lu},\"mode\":%u,\"time_s\":%lu,\"change\":%lu}\r\n",
    s->presets[0].millivolts,s->presets[0].milliamps,s->presets[1].millivolts,s->presets[1].milliamps,s->presets[2].millivolts,s->presets[2].milliamps,
    (unsigned long)(PSU_VOUT_MAX_V*1000+0.5f),(unsigned long)(PSU_CURRENT_MAX_A*1000+0.5f),(unsigned long)(PSU_CURRENT_PEAK_A*1000+0.5f),
    (unsigned long)(PSU_VIN_MAX_V*1000+0.5f),(unsigned long)(PSU_POWER_MAX_W*1000+0.5f),(unsigned long)(PSU_INPUT_CURRENT_MAX_A*1000+0.5f),(unsigned long)(PSU_OUTPUT_OVP_ABS_V*1000+0.5f),
    s->modes.mode,(unsigned long)s->modes.run_seconds,(unsigned long)s->hardware.change);
  if(n<0 || (size_t)n>=sizeof(tx_buffer)-used)return 0;
  return (uint32_t)(used+(size_t)n);
}
static void usb_poll(uint32_t now) {
  PsuView s; uint32_t key; int configured, busy;
  key = lock();
  configured = hUsbDeviceFS.dev_state == USBD_STATE_CONFIGURED && hUsbDeviceFS.pClassData != 0;
  busy = configured ? ((USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData)->TxState != 0 : 0;
  unlock(key);
  if (!configured) {
    tx_length=0;key=lock();rx_tail=rx_head;rx_length=rx_discard=0;
    if(remote_phase!=3)remote_phase=0;
    unlock(key);return;
  }
  usb_receive_line();
  if(query_log){query_log=0;last_log=now-100u;}
  /* Buffer stays immutable until the USB class has finished its transfer. */
  if (busy) { if (now - last_log >= 100u) { ++usb_drops; last_log = now; } return; }
  if(remote_phase==2) {
    PsuApp_Snapshot(&s);tx_length=usb_reply(&s);
    if(tx_length){key=lock();if(CDC_Transmit_FS(tx_buffer,(uint16_t)tx_length)==USBD_OK)remote_phase=0;else ++usb_drops;unlock(key);}
    return;
  }
  if (now - last_log < 100u) return;
  last_log = now; PsuApp_Snapshot(&s);
  tx_length = (uint32_t)snprintf((char *)tx_buffer, sizeof(tx_buffer),
    "{\"type\":\"telemetry\",\"protocol\":1,\"enabled\":%u,\"rev\":%lu,\"softstart\":true,\"rx_drops\":%lu,\"ms\":%lu,\"vin_mV\":%ld,\"vout_mV\":%ld,\"iout_mA\":%ld,\"temp_dC\":%ld,\"temp_adc\":%u,\"set_mV\":%ld,\"set_mA\":%ld,\"state\":%u,\"fault\":%lu,\"config\":%lu,\"buck_pm\":%ld,\"boost_ho_pm\":%ld,\"cycles\":%lu,\"drops\":%lu,\"vin_warn\":%u,\"temp_valid\":%u,\"irms_mA\":%ld,\"samples\":%lu,\"bench\":%u,\"imax_mA\":%u,\"ipeak_mA\":%u,\"pmax_W\":%u,\"start_block\":%lu,\"ready_ms\":%u,\"adc_age\":%lu,\"dma_left\":%u,\"adc_src\":\"cont\",\"ilim_mA\":%ld,\"ratio_pm\":%ld,\"cv_pm\":%ld,\"cc_pm\":%ld,\"ref_mV\":%ld,\"drain\":{\"state\":%u,\"pulses\":%lu,\"ticks\":%u},\"trip\":{\"cause\":%lu,\"sample\":%lu,\"vo_adc\":%u,\"io_adc\":%u,\"vi_adc\":%u,\"ovp_adc\":%u}",
    s.control.enabled,(unsigned long)s.settings_revision,(unsigned long)rx_drops,
    (unsigned long)now, (long)(s.control.vin*1000), (long)(s.control.vout*1000), (long)(s.control.current*1000), (long)(s.control.temp_c*10), s.temp_adc,
    (long)(s.control.voltage_set*1000), (long)(s.control.current_set*1000), s.control.state, (unsigned long)s.control.fault, (unsigned long)s.control.config_errors,
    (long)(s.control.duty_buck*1000), (long)(s.control.duty_boost_ho*1000), (unsigned long)s.max_control_cycles, (unsigned long)s.usb_drops,
    s.control.vin_warning, s.control.temp_valid, (long)(sqrtf(s.control.continuous_i2)*1000), (unsigned long)s.control.samples,
    (unsigned)PSU_BENCH_MODE, (unsigned)(s.control.settings.i_max*1000), (unsigned)(s.control.settings.i_peak*1000), (unsigned)s.control.settings.power_max, (unsigned long)s.control.start_block,s.control.ready_ms,
    (unsigned long)s.control.adc_age_ms, (unsigned)DMA1_Channel1->CNDTR,
    (long)(s.control.current_limit*1000), (long)(s.control.ratio*1000),
    (long)(s.control.cv_command*1000), (long)(s.control.cc_command*1000), (long)(s.control.reference_v*1000),
    (unsigned)s.discharge_state, (unsigned long)s.discharge_pulses, (unsigned)s.discharge_ticks,
    (unsigned long)s.control.fault_sample.cause, (unsigned long)s.control.fault_sample.sample_index,
    (unsigned)s.control.fault_sample.vout_adc, (unsigned)s.control.fault_sample.current_adc,
    (unsigned)s.control.fault_sample.vin_adc, (unsigned)s.control.fault_sample.ovp_adc);
  if(tx_length>=sizeof(tx_buffer))return;
  int tail=snprintf((char *)tx_buffer+tx_length,sizeof(tx_buffer)-tx_length,
    ",\"ui_draw_ms\":%lu,\"timing_src\":%lu,\"timing_value\":%lu,\"precharge_hold\":%u,\"ramp_active\":%u,\"ramp_mV_s\":%lu,\"cv_sat\":%u,\"vout_div_pm\":%lu,\"adc\":{\"vo\":%u,\"io\":%u,\"vi\":%u},\"pwm\":{\"arr\":%lu,\"psc\":%lu,\"ccr1\":%lu,\"ccr2\":%lu,\"cr1\":%lu,\"ccmr1\":%lu,\"ccer\":%lu,\"bdtr\":%lu,\"gpio_crh\":%lu,\"afio_mapr\":%lu,\"sd\":%lu},\"mode\":%u,\"time_s\":%lu,\"change\":%lu,\"charge\":{\"chem\":%u,\"cells\":%u,\"phase\":%u,\"reason\":%u,\"set_mA\":%u,\"end_mA\":%u,\"limit_h\":%u,\"tail_ms\":%lu,\"mAh10\":%lu},\"solder\":{\"tip\":%u,\"phase\":%u,\"reason\":%u,\"temp_dC\":%ld,\"target_C\":%u,\"active_C\":%u,\"sleep_C\":%u,\"stand\":%u,\"valid\":%u,\"adc\":%u,\"trip_adc\":%u,\"duty_pm\":%ld,\"ccr1\":%lu,\"heater\":%lu,\"protect\":%u,\"active_low\":0,\"pwm_hz\":10000,\"pin\":\"PA6\",\"timer\":\"TIM3_CH1\",\"block\":%lu,\"cr1\":%lu,\"ccmr1\":%lu,\"ccer\":%lu,\"cycle_ms\":50,\"quiet_ms\":10,\"on_ms\":%u,\"cycle_pos_ms\":%u,\"sample_age_ms\":%lu,\"pin_mode\":%lu,\"rises\":%lu}}\r\n",
    (unsigned long)s.max_display_ms,
    (unsigned long)s.timing_sources,(unsigned long)s.timing_value,
    (unsigned)s.control.precharge_hold,
    (unsigned)(s.control.enabled && s.control.reference_v<s.control.voltage_set),
    (unsigned long)(s.control.settings.ramp_v_per_s*1000.0f+0.5f),
    (unsigned)(s.control.enabled && s.control.ratio>=11.24f), (unsigned long)(PSU_VOUT_DIVIDER*1000.0f),
    s.hardware.vout_adc,s.hardware.current_adc,s.hardware.vin_adc,
    (unsigned long)s.hardware.arr,(unsigned long)s.hardware.psc,
    (unsigned long)s.hardware.ccr1,(unsigned long)s.hardware.ccr2,
    (unsigned long)s.hardware.cr1,(unsigned long)s.hardware.ccmr1,
    (unsigned long)s.hardware.ccer,(unsigned long)s.hardware.bdtr,
    (unsigned long)s.hardware.gpio_crh,(unsigned long)s.hardware.afio_mapr,(unsigned long)s.hardware.sd,s.modes.mode,(unsigned long)s.modes.run_seconds,(unsigned long)s.hardware.change,
    s.modes.chemistry,s.modes.cells,s.modes.phase,s.modes.error,s.modes.current_ma,PsuModes_EndMilliamps(&s.modes),s.modes.hours,(unsigned long)s.modes.tail_ms,(unsigned long)s.modes.mah_d10,
    s.modes.solder.tip,s.modes.solder.phase,s.modes.solder.error,(long)(s.modes.solder.temperature*10),
    s.modes.solder.target,s.modes.solder.active_target,s.modes.solder.sleep_target,s.modes.solder.sleeping,s.modes.solder.valid,s.modes.solder.raw,s.modes.solder.trip_adc,
    (long)(s.modes.solder.duty*1000),(unsigned long)s.hardware.heater_ccr,(unsigned long)s.hardware.heater,(unsigned)PSU_SOLDER_TEMP_PROTECTION,(unsigned long)s.hardware.heater_block,(unsigned long)s.hardware.heater_cr1,(unsigned long)s.hardware.heater_ccmr1,(unsigned long)s.hardware.heater_ccer,s.modes.solder.on_ms,s.modes.solder.position,
    (unsigned long)(HAL_GetTick()-s.modes.solder.last_sample_ms),
    (unsigned long)s.hardware.pa6_mode,(unsigned long)s.hardware.heater_rises);

  if(tail<0 || (uint32_t)tail>=sizeof(tx_buffer)-tx_length)return;
  tx_length+=(uint32_t)tail;
  if (tx_length < sizeof(tx_buffer)) {
    key = lock(); if (CDC_Transmit_FS(tx_buffer, (uint16_t)tx_length) != USBD_OK) ++usb_drops; unlock(key);
  }
}
/* Standalone pin waveform: no ADC/PID/USB or output-start commands run.
   Called only by the boot-time diagnostic branch, never by the normal app. */
void PsuApp_PinTestInit(void) {
  GPIO_InitTypeDef gpio={0};
  __HAL_RCC_GPIOA_CLK_ENABLE();__HAL_RCC_GPIOB_CLK_ENABLE();__HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_TIM3_CLK_ENABLE();
  PsuApp_EmergencyOff();
  TIM1->CR1&=~TIM_CR1_CEN;TIM1->CCER=0;
  gpio.Mode=GPIO_MODE_OUTPUT_PP;gpio.Speed=GPIO_SPEED_FREQ_LOW;
  gpio.Pin=SD_Pin;HAL_GPIO_WritePin(SD_GPIO_Port,SD_Pin,GPIO_PIN_RESET);HAL_GPIO_Init(SD_GPIO_Port,&gpio);
  gpio.Pin=GPIO_PIN_15;HAL_GPIO_WritePin(GPIOC,gpio.Pin,GPIO_PIN_RESET);HAL_GPIO_Init(GPIOC,&gpio);
  gpio.Pin=GPIO_PIN_13;HAL_GPIO_WritePin(GPIOB,gpio.Pin,GPIO_PIN_RESET);HAL_GPIO_Init(GPIOB,&gpio);
  MODIFY_REG(AFIO->MAPR,AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG,AFIO_MAPR_SWJ_CFG_JTAGDISABLE);
  __HAL_RCC_TIM3_CLK_ENABLE();MODIFY_REG(AFIO->MAPR,AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG,AFIO_MAPR_SWJ_CFG_JTAGDISABLE);
  gpio.Pin=HEATER_Pin;gpio.Mode=GPIO_MODE_AF_PP;gpio.Speed=GPIO_SPEED_FREQ_HIGH;
  HAL_GPIO_WritePin(HEATER_GPIO_Port,HEATER_Pin,GPIO_PIN_RESET);
  TIM3->CR1=0;TIM3->DIER=0;TIM3->PSC=0;TIM3->ARR=7199;TIM3->CCR1=3600;
  TIM3->CCMR1=TIM_CCMR1_OC1PE|TIM_OCMODE_PWM1;TIM3->CCER=TIM_CCER_CC1E;
  TIM3->EGR=TIM_EGR_UG;TIM3->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
  HAL_GPIO_Init(HEATER_GPIO_Port,&gpio);
}
void PsuApp_Init(void) {
  GPIO_InitTypeDef gpio = {0};
  PsuApp_EmergencyOff(); Psu_Init(&control);
  PsuModes_Init(&modes);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15|GPIO_PIN_14, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, PSU_LED_ACTIVE_LOW ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, CS_Pin|T_CS_Pin, GPIO_PIN_SET);
  gpio.Pin=GPIO_PIN_11;gpio.Mode=GPIO_MODE_INPUT;gpio.Pull=GPIO_PULLUP;HAL_GPIO_Init(GPIOB,&gpio);
  gpio.Pin = GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  gpio.Mode = GPIO_MODE_IT_RISING_FALLING; gpio.Pull = GPIO_PULLUP; HAL_GPIO_Init(GPIOB, &gpio);
  HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);HAL_NVIC_DisableIRQ(TIM2_IRQn);
  /* PB13 is retired; PA6 belongs exclusively to TIM3_CH1. */
  HAL_GPIO_WritePin(GPIOB,GPIO_PIN_13,GPIO_PIN_RESET);
  gpio.Pin=GPIO_PIN_13;gpio.Mode=GPIO_MODE_OUTPUT_PP;gpio.Pull=GPIO_NOPULL;gpio.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB,&gpio);
  __HAL_RCC_TIM3_CLK_ENABLE();MODIFY_REG(AFIO->MAPR,AFIO_MAPR_TIM3_REMAP|AFIO_MAPR_SWJ_CFG,AFIO_MAPR_SWJ_CFG_JTAGDISABLE);
  HAL_GPIO_WritePin(HEATER_GPIO_Port,HEATER_Pin,GPIO_PIN_RESET);
  gpio.Pin=HEATER_Pin;gpio.Mode=GPIO_MODE_OUTPUT_PP;gpio.Speed=GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(HEATER_GPIO_Port,&gpio);
  TIM3->CR1=0;TIM3->PSC=0;TIM3->ARR=PSU_PWM_PERIOD;TIM3->CCR1=0;
  TIM3->CCMR1=TIM_CCMR1_OC1PE|TIM_OCMODE_FORCED_INACTIVE;
  TIM3->CCER=TIM_CCER_CC1E;TIM3->EGR=TIM_EGR_UG;
  TIM3->CR1=TIM_CR1_ARPE|TIM_CR1_CEN;
  view.presets[0] = (PsuPreset){5000,1000}; view.presets[1] = (PsuPreset){12000,3000}; view.presets[2] = (PsuPreset){24000,2000};
  {PsuSettings loaded; if(PsuStorage_LoadAll(view.presets,&loaded)) (void)Psu_ApplySettings(&control,&loaded);}
  /* Keep old CRC-valid presets, but clamp legacy 8..15 A values in RAM.
     Loading never enables output or silently rewrites Flash. */
  clamp_presets();
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; DWT->CYCCNT = 0; DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  /* LCD reset delays finish before starting ADC DMA and its timing monitor. */
  PsuDisplay_Init(); last_ui = HAL_GetTick();
  if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK || HAL_ADCEx_Calibration_Start(&hadc2) != HAL_OK) Error_Handler();
  if (HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL) != HAL_OK) Error_Handler();
  encoder_old = (uint16_t)TIM2->CNT;
  if (HAL_ADC_Start(&hadc2) != HAL_OK || HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma, sizeof(adc_dma)/sizeof(adc_dma[0])) != HAL_OK) Error_Handler();
  /* Do not HAL_TIM_PWM_Start: that enables MOE before interlocks are checked.
     Latch zero into the preloaded compares before MOE can ever be set. */
  TIM1->CCR1 = TIM1->CCR2 = 0;
  TIM1->EGR = TIM_EGR_UG;
  TIM1->CCER &= ~(TIM_CCER_CC1NE | TIM_CCER_CC2NE | TIM_CCER_CC3E | TIM_CCER_CC3NE);
  TIM1->CCER |= TIM_CCER_CC1E | TIM_CCER_CC2E;
  TIM1->CR1 |= TIM_CR1_CEN;
  if (HAL_TIM_Base_Start_IT(&htim4) != HAL_OK) Error_Handler();
  /* Independent watchdog ~0.8 s at typical 40kHz LSI; main must see TIM4 alive. */
  IWDG->KR = 0xCCCCu; IWDG->KR = 0x5555u; IWDG->PR = 4; IWDG->RLR = 499;
  while (IWDG->SR != 0) { /* Clocked register update; watchdog also bounds a fault here. */ }
  IWDG->KR = 0xAAAAu;
}
static void foreground_service(void){
  uint32_t now=HAL_GetTick();
  inputs(now);usb_poll(now);
  last_ui=HAL_GetTick();
  if(heartbeat!=last_heartbeat){last_heartbeat=heartbeat;IWDG->KR=0xAAAAu;}
}
void PsuApp_DisplayProgress(void){
  if(display_active && HAL_GetTick()-last_ui>=5u)foreground_service();
}
void PsuApp_Poll(void) {
  static uint32_t last_draw, notice_until;
  foreground_service();
  uint32_t now = HAL_GetTick();
  if (save_pending && !control.enabled && !(request & 8u) &&
      discharge.state != PSU_D_WAIT && discharge.state != PSU_D_ACTIVE &&
      (!discharge.fired || now - discharge.last_pulse >= (uint32_t)control.settings.discharge_interval)) {
    /* Stop the actuator ISR before shutdown, so it cannot re-enable healthy
       standby between EmergencyOff and the Flash write. */
    uint32_t key = lock(); HAL_TIM_Base_Stop_IT(&htim4); PsuApp_EmergencyOff(); unlock(key);
    HAL_ADC_Stop_DMA(&hadc1);
    saved_notice = PsuStorage_SaveAll(view.presets,&control.settings) ? 1 : 2;
    key = lock(); adc_mean.sequence = adc_consumed = 0; control.adc_age_ms = 10000; control.ready_ms = 0; request &= ~8u; unlock(key);
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)adc_dma, sizeof(adc_dma)/sizeof(adc_dma[0])) != HAL_OK || HAL_TIM_Base_Start_IT(&htim4) != HAL_OK) Error_Handler();
    if(remote_phase==3){remote_error=saved_notice==1?0:"flash_failed";remote_phase=2;}
    save_pending = 0; notice_until = HAL_GetTick() + 2000; now = HAL_GetTick();
  }
  if ((int32_t)(now - notice_until) >= 0) saved_notice = 0;
  if (now - last_draw >= 100u) {
    uint32_t started=HAL_GetTick();PsuApp_Snapshot(&display_view);
    display_active=1;PsuDisplay_Update(&display_view);display_active=0;
    uint32_t elapsed=HAL_GetTick()-started;
    if(elapsed>max_display_ms)max_display_ms=elapsed;
    last_draw=now=HAL_GetTick(); /* Avoid back-to-back redraws after a slow frame. */
  }
  if (PSU_BUZZER_ENABLED) HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, ((control.fault || (modes.mode==PSU_MODE_SOLDER && modes.solder.error)) && (now % 1000u < 100u)) == PSU_BUZZER_ACTIVE_HIGH ? GPIO_PIN_SET : GPIO_PIN_RESET);
  if (heartbeat != last_heartbeat) { last_heartbeat = heartbeat; IWDG->KR = 0xAAAAu; }
}
