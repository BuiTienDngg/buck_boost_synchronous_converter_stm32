#ifndef POWER_STAGE_H
#define POWER_STAGE_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* IR2184S shared active-low shutdown: PA10 low = BOTH gate drivers OFF.
 * PA10 is TIM1_CH3's default alternate pin: configure it as GPIO OUTPUT,
 * and DO NOT enable TIM1_CH3 or USART1 RX on PA10.
 * TIM1_CH1 (PA8) -> buck IN; TIM1_CH2 (PA9) -> boost IN.
 * TIM2 remains dedicated to the encoder.
 */
#define BB_SD_GPIO_PORT GPIOA
#define BB_SD_GPIO_PIN  GPIO_PIN_10

#define BB_PWM_HZ                 20000UL
#define BB_DUTY_MIN_PCT          3U
#define BB_DUTY_MAX_PCT          97U
#define BB_DUTY_START_PCT        3U
#define BB_DUTY_DEFAULT_PCT      30U
#define BB_RAMP_INTERVAL_MS      50U
#define BB_ADC_STALE_MS          200U

/* Initial conservative bench limits; verify these for the real power stage.
 * Current output shunt: 0.001R, instrumentation amplifier gain 200.
 * No inductor-current sensor is available, so output-current OCP alone
 * cannot protect all switching transients or current reversals.
 */
#define BB_VIN_MIN_V             7.0f
#define BB_VIN_MAX_V            40.0f
#define BB_VOUT_MAX_V           30.0f
#define BB_IOUT_MAX_A            5.0f
#define BB_BOOST_INPUT_MARGIN_V  2.0f

typedef enum { BB_MODE_BUCK = 0, BB_MODE_BOOST = 1 } BB_Mode;
typedef enum { BB_STATE_OFF = 0, BB_STATE_RUNNING = 1, BB_STATE_FAULT = 2 } BB_RunState;
typedef enum {
    BB_FAULT_NONE = 0,
    BB_FAULT_ADC_STALE,
    BB_FAULT_INPUT_RANGE,
    BB_FAULT_OUTPUT_OVERVOLT,
    BB_FAULT_OUTPUT_OVERCURRENT,
    BB_FAULT_BOOST_INPUT_TOO_HIGH,
    BB_FAULT_PWM_INIT
} BB_Fault;

typedef struct {
    BB_RunState state;
    BB_Mode mode;
    BB_Fault fault;
    uint8_t duty_request_pct; /* encoder command: 3..97 */
    uint8_t duty_applied_pct; /* ramped: 3..97 */
    uint8_t buck_input_pct; /* high-side PWM at PA8 */
    uint8_t boost_input_pct;/* high-side PWM at PA9 */
} BB_Status;

HAL_StatusTypeDef PowerStage_Init(TIM_HandleTypeDef *htim1);
void PowerStage_Handle(void); /* call AFTER button_handle / encoder_hander / AnalogMonitor_Handle */
void PowerStage_OffNow(void); /* force common SD PA10 low */
BB_Status PowerStage_GetStatus(void);

#endif
