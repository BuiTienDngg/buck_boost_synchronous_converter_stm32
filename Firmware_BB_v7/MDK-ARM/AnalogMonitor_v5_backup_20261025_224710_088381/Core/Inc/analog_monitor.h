#ifndef ANALOG_MONITOR_H
#define ANALOG_MONITOR_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ADC1 regular ranks 1-4: ADC1_IN1/2/3/4 (PA1/PA2/PA3/PA4).
 * ADC1 + DMA circular, HALFWORD data, length = 2 * 30 * 4.
 * ADC2: one regular channel (default ADC2_IN0 on PA0), polled once per second.
 * This module uses ALREADY INITIALIZED HAL ADC/DMA handles, owned by CubeMX.
 */
#define ANALOG_CHANNELS                4U
#define ANALOG_AVERAGE_SAMPLES         30U
#define ANALOG_DMA_BUFFER_LENGTH       (ANALOG_CHANNELS * ANALOG_AVERAGE_SAMPLES * 2U)
#define ANALOG_ADC_VREF_VOLTS          3.30f
#define ANALOG_ADC_MAX_COUNT           4095.0f
#define ANALOG_ADC1_PROCESS_PERIOD_MS  20U   /* ADC1 filter at max 50 Hz */
#define ANALOG_FET_TIMEOUT_MS          10U   /* ADC2 no-response deadline */
#define ANALOG_IIR_ALPHA               0.15f  /* gentle filtering, 0 < alpha <= 1 */
#define ANALOG_FET_IIR_ALPHA           0.15f
#define ANALOG_FET_PERIOD_MS           1000U

#define ANALOG_VOUT_GAIN               22.0f
#define ANALOG_SHUNT_OHMS              0.01f
#define ANALOG_CURRENT_AMP_GAIN        200.0f
#define ANALOG_TEMP_SOLDER_GAIN        22.0f
#define ANALOG_VIN_ADC_OFFSET          1200.0f /* raw ADC counts, as requested */
#define ANALOG_VIN_ADC_DIVISOR         2.0f

/* No NTC resistance / Beta / divider orientation supplied. Raw and filtered
 * ADC2 readings are available; no unverified degrees-C conversion is applied.
 */
typedef struct {
    float adc1_mean[ANALOG_CHANNELS];  /* averaged raw ADC counts per channel */
    float vout_v;                      /* ADC1_CH1: (raw/4095 * Vref) * 22 */
    float current_a;                   /* ADC1_CH2: (raw/4095 * Vref) / (0.01*200) */
    float temp_solder_scaled;          /* ADC1_CH3: (raw/4095 * Vref) * 22; units TBD */
    float vin_scaled;                  /* ADC1_CH4: (raw - 1200) / 2; units TBD */
    float temp_fet_adc;                /* filtered ADC2 raw counts */
    float temp_fet_v;                  /* filtered ADC2 voltage */
    uint32_t adc1_updated_ms;
    uint32_t temp_fet_updated_ms;
    uint8_t adc1_valid;
    uint8_t temp_fet_valid;
    uint8_t temp_fet_error;
} AnalogMonitor_Data;

/* Call ONCE after MX_DMA_Init(), MX_ADC1_Init(), MX_ADC2_Init(). */
HAL_StatusTypeDef AnalogMonitor_Init(ADC_HandleTypeDef *adc1,
                                     ADC_HandleTypeDef *adc2);
/* Call on every while(1) iteration. Filters ADC1 in main, starts/checks ADC2
 * in two non-blocking steps every 1000 ms. Never call inside an ISR. */
void AnalogMonitor_Handle(void);
/* Forward from existing HAL ADC callbacks. Callbacks do only integer sums. */
void AnalogMonitor_AdcHalfComplete(ADC_HandleTypeDef *hadc);
void AnalogMonitor_AdcComplete(ADC_HandleTypeDef *hadc);
void AnalogMonitor_GetSnapshot(AnalogMonitor_Data *out);
/* Only for projects lacking their own DMA1_Channel1_IRQHandler. */
void AnalogMonitor_DMA1_IRQHandler(void);

#ifdef __cplusplus
}
#endif
#endif
