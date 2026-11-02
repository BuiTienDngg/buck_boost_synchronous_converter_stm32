#include "analog_monitor.h"
#include <string.h>

/* Two blocks of 30 complete ADC1 scans. STM32F1 ADC1_DMA1_Channel1
 * config: circular, memory/peripheral data width 16 bits, memory increment.
 */
static uint16_t s_adc1_dma[ANALOG_DMA_BUFFER_LENGTH];
static ADC_HandleTypeDef *s_adc1;
static ADC_HandleTypeDef *s_adc2;
static volatile AnalogMonitor_Data s_data;
static uint32_t s_last_fet_read_ms;
static uint8_t s_initialized;

static float iir(float previous, float measurement, float alpha, uint8_t valid)
{
    if (!valid) return measurement;
    return previous + alpha * (measurement - previous);
}

static void process_adc1_block(uint32_t start)
{
    uint32_t ch, sample;
    uint32_t sum[ANALOG_CHANNELS] = {0U, 0U, 0U, 0U};
    float average[ANALOG_CHANNELS];
    uint8_t already_valid;

    if (!s_initialized) return;
    /* Caller runs in DMA half/full-complete IRQ: DMA is writing to opposite half. */
    for (sample = 0U; sample < ANALOG_AVERAGE_SAMPLES; ++sample) {
        for (ch = 0U; ch < ANALOG_CHANNELS; ++ch) {
            sum[ch] += (uint32_t)s_adc1_dma[start + sample * ANALOG_CHANNELS + ch];
        }
    }
    already_valid = s_data.adc1_valid;
    for (ch = 0U; ch < ANALOG_CHANNELS; ++ch) {
        average[ch] = (float)sum[ch] / (float)ANALOG_AVERAGE_SAMPLES;
        s_data.adc1_mean[ch] = iir(s_data.adc1_mean[ch], average[ch],
                                   ANALOG_IIR_ALPHA, already_valid);
    }
    /* Deliberately match the four supplied calibration expressions. */
    s_data.vout_v = (s_data.adc1_mean[0] * ANALOG_ADC_VREF_VOLTS /
                     ANALOG_ADC_MAX_COUNT) * ANALOG_VOUT_GAIN;
    s_data.current_a = (s_data.adc1_mean[1] * ANALOG_ADC_VREF_VOLTS /
                        ANALOG_ADC_MAX_COUNT) /
                       (ANALOG_SHUNT_OHMS * ANALOG_CURRENT_AMP_GAIN);
    s_data.temp_solder_scaled = (s_data.adc1_mean[2] * ANALOG_ADC_VREF_VOLTS /
                                 ANALOG_ADC_MAX_COUNT) * ANALOG_TEMP_SOLDER_GAIN;
    s_data.vin_scaled = (s_data.adc1_mean[3] - ANALOG_VIN_ADC_OFFSET) /
                         ANALOG_VIN_ADC_DIVISOR;
    s_data.adc1_updated_ms = HAL_GetTick();
    s_data.adc1_valid = 1U;
}

HAL_StatusTypeDef AnalogMonitor_Init(ADC_HandleTypeDef *adc1,
                                     ADC_HandleTypeDef *adc2)
{
    DMA_HandleTypeDef *dma;
    if (!adc1 || !adc2 || adc1->Instance != ADC1 || adc2->Instance != ADC2)
        return HAL_ERROR;
    if (!adc1->DMA_Handle) return HAL_ERROR;
    dma = adc1->DMA_Handle;
    if (dma->Instance != DMA1_Channel1 || dma->Init.Mode != DMA_CIRCULAR ||
        dma->Init.PeriphDataAlignment != DMA_PDATAALIGN_HALFWORD ||
        dma->Init.MemDataAlignment != DMA_MDATAALIGN_HALFWORD ||
        dma->Init.MemInc != DMA_MINC_ENABLE)
        return HAL_ERROR;
    if (adc1->Init.ScanConvMode != ADC_SCAN_ENABLE ||
        adc1->Init.ContinuousConvMode != ENABLE ||
        adc1->Init.NbrOfConversion != ANALOG_CHANNELS ||
        adc1->Init.DataAlign != ADC_DATAALIGN_RIGHT ||
        adc2->Init.ContinuousConvMode != DISABLE)
        return HAL_ERROR;

    s_initialized = 0U;
    memset((void *)s_adc1_dma, 0, sizeof(s_adc1_dma));
    memset((void *)&s_data, 0, sizeof(s_data));
    s_adc1 = adc1;
    s_adc2 = adc2;

    if (HAL_ADCEx_Calibration_Start(s_adc1) != HAL_OK) return HAL_ERROR;
    if (HAL_ADCEx_Calibration_Start(s_adc2) != HAL_OK) return HAL_ERROR;

    s_last_fet_read_ms = HAL_GetTick() - ANALOG_FET_PERIOD_MS;
    s_initialized = 1U;
    if (NVIC_GetEnableIRQ(DMA1_Channel1_IRQn) == 0U) {
        HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 5U, 0U);
        HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    }
    if (HAL_ADC_Start_DMA(s_adc1, (uint32_t *)s_adc1_dma,
                          ANALOG_DMA_BUFFER_LENGTH) != HAL_OK) {
        s_initialized = 0U;
        return HAL_ERROR;
    }
    return HAL_OK;
}

void AnalogMonitor_AdcHalfComplete(ADC_HandleTypeDef *hadc)
{
    if (hadc == s_adc1) process_adc1_block(0U);
}

void AnalogMonitor_AdcComplete(ADC_HandleTypeDef *hadc)
{
    if (hadc == s_adc1) process_adc1_block(ANALOG_DMA_BUFFER_LENGTH / 2U);
}

void AnalogMonitor_Handle(void)
{
    uint32_t now;
    uint32_t raw;
    uint8_t valid;
    HAL_StatusTypeDef status;
    if (!s_initialized) return;
    now = HAL_GetTick();
    if ((uint32_t)(now - s_last_fet_read_ms) < ANALOG_FET_PERIOD_MS) return;
    s_last_fet_read_ms = now;

    status = HAL_ADC_Start(s_adc2);
    if (status == HAL_OK) {
        status = HAL_ADC_PollForConversion(s_adc2, 5U);
        if (status == HAL_OK) {
            raw = HAL_ADC_GetValue(s_adc2);
            if (raw > 4095U) raw = 4095U;
            valid = s_data.temp_fet_valid;
            s_data.temp_fet_adc = iir(s_data.temp_fet_adc, (float)raw,
                                      ANALOG_FET_IIR_ALPHA, valid);
            s_data.temp_fet_v = s_data.temp_fet_adc * ANALOG_ADC_VREF_VOLTS /
                                ANALOG_ADC_MAX_COUNT;
            s_data.temp_fet_updated_ms = HAL_GetTick();
            s_data.temp_fet_valid = 1U;
            s_data.temp_fet_error = 0U;
        } else {
            s_data.temp_fet_error = 1U;
        }
        (void)HAL_ADC_Stop(s_adc2);
    } else {
        s_data.temp_fet_error = 1U;
    }
}

void AnalogMonitor_GetSnapshot(AnalogMonitor_Data *out)
{
    uint32_t irq_state;
    uint32_t i;
    if (!out) return;
    /* Short critical section: consistent snapshot of ISR-updated ADC1 fields. */
    irq_state = __get_PRIMASK();
    __disable_irq();
    for (i = 0; i < ANALOG_CHANNELS; ++i) out->adc1_mean[i] = s_data.adc1_mean[i];
    out->vout_v = s_data.vout_v;
    out->current_a = s_data.current_a;
    out->temp_solder_scaled = s_data.temp_solder_scaled;
    out->vin_scaled = s_data.vin_scaled;
    out->temp_fet_adc = s_data.temp_fet_adc;
    out->temp_fet_v = s_data.temp_fet_v;
    out->adc1_updated_ms = s_data.adc1_updated_ms;
    out->temp_fet_updated_ms = s_data.temp_fet_updated_ms;
    out->adc1_valid = s_data.adc1_valid;
    out->temp_fet_valid = s_data.temp_fet_valid;
    out->temp_fet_error = s_data.temp_fet_error;
    if (irq_state == 0U) __enable_irq();
}

void AnalogMonitor_DMA1_IRQHandler(void)
{
    if (s_adc1 != 0 && s_adc1->DMA_Handle != 0)
        HAL_DMA_IRQHandler(s_adc1->DMA_Handle);
}
