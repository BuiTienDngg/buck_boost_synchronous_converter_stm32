#ifndef PSU_APP_H
#define PSU_APP_H
#include "main.h"
#include "psu_control.h"
#include "psu_modes.h"
enum { PSU_MENU_CLOSED, PSU_MENU_ROOT, PSU_MENU_RAMP, PSU_MENU_CHARGE, PSU_MENU_DISPLAY, PSU_MENU_SOLDER };
enum { PSU_DISPLAY_NUMBER, PSU_DISPLAY_GRAPH };
/* Latched diagnostic sources for PSU_F_TIMING; reset with the fault. */
enum { PSU_TIMING_HEATER=1, PSU_TIMING_UI=2, PSU_TIMING_ISR=4,
       PSU_TIMING_DISCHARGE_WAIT=8, PSU_TIMING_DISCHARGE_CONFIG=16 };
/* Reasons PA6 cannot currently carry a thermal PWM. Multiple bits may coexist. */
enum { PSU_HB_MODE=1, PSU_HB_REQUEST=2, PSU_HB_FAULT=4, PSU_HB_RAIL=8,
       PSU_HB_TIP=16, PSU_HB_QUIET=32, PSU_HB_PID_ZERO=64, PSU_HB_TIMER=128 };
typedef struct { volatile uint8_t page; uint8_t selected, editing, notice, display; } PsuMenu;
typedef struct { uint16_t millivolts, milliamps; } PsuPreset;
enum {
  PSU_D_IDLE, PSU_D_WAIT, PSU_D_ACTIVE, PSU_D_DONE, PSU_D_TIMEOUT,
  PSU_D_NO_DROP, PSU_D_RISING, PSU_D_ABORTED
};
typedef struct {
  PsuControl control;
  /* Atomic register snapshot: compare values are timer preloads, not a
     measurement of the waveform or the external gate driver's output. */
  struct {
    uint32_t arr, psc, ccr1, ccr2, ccr3, cr1, ccmr1, ccer, bdtr;
    uint32_t gpio_crh, afio_mapr, sd, change, heater;
    uint32_t heater_ccr,heater_block,heater_cr1,heater_ccmr1,heater_ccer;
    uint32_t pa6_mode,heater_rises;
    uint16_t vout_adc, current_adc, vin_adc;
  } hardware;
  PsuPreset presets[3];
  uint16_t temp_adc;
  /* edit_digit: 0=0.01, 1=0.1, 2=1, 3=10 (voltage only). */
  uint8_t edit_current, edit_digit, selected_preset, saved_notice;
  uint32_t usb_drops, max_control_cycles, max_display_ms;
  uint32_t timing_sources, timing_value;
  uint32_t discharge_pulses;
  uint32_t settings_revision;
  uint16_t discharge_ticks;
  uint8_t discharge_state;
  PsuModes modes;
  PsuMenu menu;
} PsuView;
void PsuApp_Init(void);
void PsuApp_PinTestInit(void);
void PsuApp_Poll(void);
/* Foreground only: service inputs/USB between bounded LCD SPI transfers.
   Inert during display initialization; never renders or writes Flash. */
void PsuApp_DisplayProgress(void);
void PsuApp_EmergencyOff(void);
void PsuApp_SysTick(void);
void PsuApp_UsbRx(const uint8_t *data, uint32_t length);
void PsuApp_Snapshot(PsuView *view);
void PsuDisplay_Init(void);
void PsuDisplay_Update(const PsuView *view);
int PsuStorage_LoadAll(PsuPreset presets[3], PsuSettings *settings);
int PsuStorage_SaveAll(const PsuPreset presets[3], const PsuSettings *settings);
int PsuStorage_Load(PsuPreset presets[3]);
int PsuStorage_Save(const PsuPreset presets[3]);
#endif
