#ifndef PSU_CONFIG_H
#define PSU_CONFIG_H

/* Temporary PA6 waveform-only image. Set 0 and rebuild to restore thermal PID. */
#ifndef PSU_PA6_PWM_PIN_TEST
#define PSU_PA6_PWM_PIN_TEST 0
#endif

/* Keep SPI1 clock disabled so PA6 can output TIM3_CH1. LCD uses existing PA5/PA7. */
#ifndef PSU_LCD_SOFT_SPI
#define PSU_LCD_SOFT_SPI 0
#endif

/* Actual, unmodified hardware values supplied by the board owner. */
#define PSU_VDDA_V                 3.300f
#define PSU_VIN_DIVIDER            23.0f /* 220k high-side / 10k to GND */
#define PSU_VOUT_DIVIDER           23.0f /* corrected by board owner: 220k / 10k */
#define PSU_SHUNT_OHM              0.001f
#define PSU_CURRENT_GAIN           100.0f
#define PSU_CURRENT_ATTENUATION    1.0f
#define PSU_CURRENT_OFFSET_V       0.0f /* INA199 REF confirmed at GND */
#define PSU_VIN_CAL                1.0f
#define PSU_VOUT_CAL               1.0f
#define PSU_CURRENT_CAL            1.0f

/* Set only after the corresponding hardware and bench checks in README. */
#define PSU_GATE_WIRING_VERIFIED   1
#define PSU_CURRENT_PATH_VERIFIED  0
#define PSU_POWER_STAGE_VERIFIED   0
/* User-authorized temporary operation; does not certify the hardware above.
   Only the unverified current-path/power-stage flags are waived in this mode.
   ADC range, gate wiring, sensor and all runtime protection checks remain. */
#define PSU_BENCH_MODE             1
/* Normal firmware now uses SD=1, both IN=0 for healthy standby as well.
   Optional diagnostic: set to 1 to inhibit CC/CV RUN and stay at zero PWM. */
#ifndef PSU_LOW_SIDE_TEST
#define PSU_LOW_SIDE_TEST          0
#endif
#define PSU_NTC_CONFIGURED         1
/* Provisional NTC configuration explicitly requested by the board owner. */
#define PSU_NTC_R25_OHM            50000.0f
#define PSU_NTC_PULL_OHM           10000.0f
#define PSU_NTC_BETA_K             3950.0f
#define PSU_NTC_TO_GND             1

#define PSU_VIN_MIN_V              7.0f
#define PSU_VIN_MAX_V              60.0f
#define PSU_VIN_WARN_V             7.8f
#define PSU_VOUT_MIN_V             1.5f
#define PSU_VOUT_MAX_V             59.9f /* Setpoints stay below the 60 V trip ceiling. */
#define PSU_OUTPUT_OVP_ABS_V        60.0f
#define PSU_CURRENT_MAX_A          15.0f
#define PSU_CURRENT_PEAK_A         20.0f
/* Provisional bring-up budgets, not measured hardware ratings.
   Zero/invalid budgets still lock RUN even in bench mode. */
#define PSU_POWER_MAX_W            600.0f
#define PSU_INPUT_CURRENT_MAX_A    10.0f
#define PSU_EFFICIENCY_ESTIMATE    0.85f
#define PSU_TEMP_WARN_C            70.0f
#define PSU_TEMP_TRIP_C            85.0f
#define PSU_TEMP_RESET_C           60.0f
#define PSU_HEATER_PWM_PERIOD     7199u /* Heater TIM4 stays at 10 kHz. */
#define PSU_PWM_HZ                 25000u
#define PSU_PWM_PERIOD             2879u
/* User-authorized gentle, open-loop capacitor discharge through Q2/L4/Q3.
   Estimated delta-I only: the output shunt does NOT measure this current.
   Each pulse is ended by TIM1 hardware OPM; never repeated at 10 kHz. */
#ifndef PSU_DISCHARGE_ENABLED
#define PSU_DISCHARGE_ENABLED       1
#endif
#define PSU_INDUCTANCE_H            0.000097f
#define PSU_DISCHARGE_EST_PEAK_A     1.0f
#define PSU_DISCHARGE_INTERVAL_MS    20u
#define PSU_DISCHARGE_MAX_ON_US      20u
#define PSU_DISCHARGE_STOP_V         0.5f
#define PSU_DISCHARGE_MAX_MS         360000u
#define PSU_DISCHARGE_PROGRESS_MS    5000u
#define PSU_DISCHARGE_MAX_PULSES     18000u
/* ADC1 clock 12MHz, three channels at 239.5+12.5 cycles = 63us/scan. */
#define PSU_ADC_MEAN_FRAMES        4u
#define PSU_ADC_BLOCK_SECONDS      (PSU_ADC_MEAN_FRAMES * 0.000063f)
#define PSU_CONTROL_HZ             1000u
#define PSU_RAMP_V_PER_S           10.0f
#define PSU_ENCODER_COUNTS         4
#define PSU_ENCODER_DIRECTION      (-1)
#define PSU_LED_ACTIVE_LOW         0
/* Active buzzer. External transistor driver required for PC14 loading. */
#define PSU_BUZZER_ENABLED         1
#define PSU_BUZZER_ACTIVE_HIGH     1
#define PSU_LCD_BGR                1
/* This panel needs normal display polarity. INVON complements black/white
   and green/magenta; RGB/BGR only exchanges the red and blue channels. */
#ifndef PSU_LCD_INVERT
#define PSU_LCD_INVERT             0
#endif
#define PSU_LCD_X_OFFSET           0
#define PSU_LCD_Y_OFFSET           0

/* Measured on BBV6.5 at 5V/light fan (CV) and 1.5V/2A tweezers (CC).
   Other operating points still require validation. Flash settings override. */
#define PSU_CV_KP                  0.015f
#define PSU_CV_KI                  0.20f
#define PSU_CC_KP                  0.010f
#define PSU_CC_KI                  1.0f
#endif
