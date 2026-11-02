#include "analog_monitor.h"
#include <string.h>

/* ADC1 DMA circular: 2 blocks x 30 full 4-channel scans.
 * Keep the IRQ integer-only. Filtering and calibration run in main loop.
 */
static uint16_t s_adc1_dma[ANALOG_DMA_BUFFER_LENGTH];
static ADC_HandleTypeDef *s_adc1;
static ADC_HandleTypeDef *s_adc2;
static volatile AnalogMonitor_Data s_data;
static volatile uint32_t s_adc1_pending_sum[ANALOG_CHANNELS];
static volatile uint32_t s_adc1_pending_seq;
static uint32_t s_adc1_processed_seq;
static uint32_t s_adc1_last_process_ms;
static uint32_t s_last_fet_read_ms;
static uint32_t s_fet_started_ms;
static uint8_t s_fet_waiting;
static uint8_t s_initialized;

static float iir(float previous, float measurement, float alpha, uint8_t valid)
{
    if (!valid) return measurement;
    return previous + alpha * (measurement - previous);
}

/* Called inside DMA HT/TC IRQ. No HAL_GetTick(), no float, no LCD here. */
static void capture_adc1_block(uint32_t start)
{
    uint32_t ch, sample;
    uint32_t sum[ANALOG_CHANNELS] = {0U, 0U, 0U, 0U};

    if (!s_initialized) return;
    for (sample = 0U; sample < ANALOG_AVERAGE_SAMPLES; ++sample) {
        for (ch = 0U; ch < ANALOG_CHANNELS; ++ch) {
            sum[ch] += s_adc1_dma[start + sample * ANALOG_CHANNELS + ch];
        }
    }
    for (ch = 0U; ch < ANALOG_CHANNELS; ++ch) {
        s_adc1_pending_sum[ch] = sum[ch];
    }
    ++s_adc1_pending_seq;
}

/* Called in main, at most 50 times/second.
 * If LCD rendering delays the main loop, use newest complete 30-scan block.
 */
static void process_adc1_in_main(uint32_t now)
{
    uint32_t sums[ANALOG_CHANNELS];
    uint32_t seq;
    uint32_t i;
    uint32_t irq_state;
    uint8_t already_valid;
    float value;

    if ((uint32_t)(now - s_adc1_last_process_ms) <
        ANALOG_ADC1_PROCESS_PERIOD_MS) return;
    s_adc1_last_process_ms = now;

    irq_state = __get_PRIMASK();
    __disable_irq();
    seq = s_adc1_pending_seq;
    if (seq != s_adc1_processed_seq) {
        for (i = 0U; i < ANALOG_CHANNELS; ++i) {
            sums[i] = s_adc1_pending_sum[i];
        }
    }
    if (irq_state == 0U) __enable_irq();

    if (seq == s_adc1_processed_seq) return;
    s_adc1_processed_seq = seq;
    already_valid = s_data.adc1_valid;
    for (i = 0U; i < ANALOG_CHANNELS; ++i) {
        value = (float)sums[i] / (float)ANALOG_AVERAGE_SAMPLES;
        s_data.adc1_mean[i] = iir(s_data.adc1_mean[i], value,
                                   ANALOG_IIR_ALPHA, already_valid);
    }
    s_data.vout_v = (s_data.adc1_mean[0] * ANALOG_ADC_VREF_VOLTS /
                     ANALOG_ADC_MAX_COUNT) * ANALOG_VOUT_GAIN;
    s_data.current_a = (s_data.adc1_mean[1] * ANALOG_ADC_VREF_VOLTS /
                        ANALOG_ADC_MAX_COUNT) /
                       (ANALOG_SHUNT_OHMS * ANALOG_CURRENT_AMP_GAIN);
    s_data.temp_solder_scaled = (s_data.adc1_mean[2] -
                                 ANALOG_TEMP_SOLDER_ADC_OFFSET) /
                                 ANALOG_TEMP_SOLDER_ADC_DIVISOR;
    s_data.vin_scaled = (s_data.adc1_mean[3] * ANALOG_ADC_VREF_VOLTS /
                         ANALOG_ADC_MAX_COUNT) * ANALOG_VIN_GAIN;
    s_data.adc1_updated_ms = now;
    s_data.adc1_valid = 1U;
}

HAL_StatusTypeDef AnalogMonitor_Init(ADC_HandleTypeDef *adc1,
                                     ADC_HandleTypeDef *adc2)
{
    DMA_HandleTypeDef *dma;
    uint32_t i;
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
    for (i = 0U; i < ANALOG_CHANNELS; ++i)
        s_adc1_pending_sum[i] = 0U;
    s_adc1_pending_seq = 0U;
    s_adc1_processed_seq = 0U;
    s_adc1_last_process_ms = HAL_GetTick() - ANALOG_ADC1_PROCESS_PERIOD_MS;
    s_last_fet_read_ms = HAL_GetTick() - ANALOG_FET_PERIOD_MS;
    s_fet_started_ms = 0U;
    s_fet_waiting = 0U;
    s_adc1 = adc1;
    s_adc2 = adc2;

    if (HAL_ADCEx_Calibration_Start(s_adc1) != HAL_OK) return HAL_ERROR;
    if (HAL_ADCEx_Calibration_Start(s_adc2) != HAL_OK) return HAL_ERROR;

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
    if (hadc == s_adc1) capture_adc1_block(0U);
}

void AnalogMonitor_AdcComplete(ADC_HandleTypeDef *hadc)
{
    if (hadc == s_adc1) capture_adc1_block(ANALOG_DMA_BUFFER_LENGTH / 2U);
}

void AnalogMonitor_Handle(void)
{
    uint32_t now;
    uint32_t raw;
    uint8_t valid;
    HAL_StatusTypeDef status;

    if (!s_initialized) return;
    now = HAL_GetTick();
    process_adc1_in_main(now);

    /* ADC2 is a non-blocking two-phase conversion. No PollForConversion(). */
    if (s_fet_waiting) {
        if (__HAL_ADC_GET_FLAG(s_adc2, ADC_FLAG_EOC) != RESET) {
            raw = HAL_ADC_GetValue(s_adc2);
            if (raw > 4095U) raw = 4095U;
            valid = s_data.temp_fet_valid;
            s_data.temp_fet_adc = iir(s_data.temp_fet_adc, (float)raw,
                                      ANALOG_FET_IIR_ALPHA, valid);
            s_data.temp_fet_v = s_data.temp_fet_adc * ANALOG_ADC_VREF_VOLTS /
                                ANALOG_ADC_MAX_COUNT;
            s_data.temp_fet_updated_ms = now;
            s_data.temp_fet_valid = 1U;
            s_data.temp_fet_error = 0U;
            (void)HAL_ADC_Stop(s_adc2);
            s_fet_waiting = 0U;
        } else if ((uint32_t)(now - s_fet_started_ms) >= ANALOG_FET_TIMEOUT_MS) {
            (void)HAL_ADC_Stop(s_adc2);
            s_data.temp_fet_error = 1U;
            s_fet_waiting = 0U;
        }
        return;
    }

    if ((uint32_t)(now - s_last_fet_read_ms) < ANALOG_FET_PERIOD_MS) return;
    s_last_fet_read_ms = now;
    status = HAL_ADC_Start(s_adc2);
    if (status == HAL_OK) {
        s_fet_started_ms = now;
        s_fet_waiting = 1U;
    } else {
        s_data.temp_fet_error = 1U;
    }
}

void AnalogMonitor_GetSnapshot(AnalogMonitor_Data *out)
{
    uint32_t irq_state;
    uint32_t i;
    if (!out) return;
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
