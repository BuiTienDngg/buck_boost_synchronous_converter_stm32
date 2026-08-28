/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "st7789.h"
#include <stdio.h>
#include <math.h>
#include "UI.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc1;

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;

TIM_HandleTypeDef htim1;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_SPI1_Init(void);
static void MX_ADC1_Init(void);
static void MX_ADC2_Init(void);
static void MX_TIM1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* =========================================================
 * ADC / POWER STAGE CONSTANTS
 * ========================================================= */

#define ADC_MAX                     4095.0f
#define VREF                        3.3f

#define VIN_DIV_GAIN                11.131f
#define VOUT_DIV_GAIN               10.959f

#define VIN_MIN_PROTECT             2.0f
#define TEMP_MAX_PROTECT            80.0f

#define CURRENT_MAX                 10.0f
#define I_HARD_LIMIT_A              8.5f
#define POWER_START_BLANK_MS        200U

#define CURRENT_GAIN                200.0f
#define SHUNT_R                     0.0011f


/* =========================================================
 * SETPOINT / RATIO LIMITS
 * ========================================================= */

#define VSET_MIN                    1.0f
#define VSET_MAX                    30.0f
#define VSET_STEP                   0.1f

#define ISET_MIN                    0.1f
#define ISET_MAX                    10.0f
#define ISET_STEP                   0.1f

#define POWERSTAGE_DUTY_MIN         0.05f
#define POWERSTAGE_DUTY_MAX         0.95f

#define RATIO_MIN                   POWERSTAGE_DUTY_MIN
#define RATIO_MAX                   POWERSTAGE_DUTY_MAX


/* =========================================================
 * CONTROL FREQUENCIES
 *
 * TIM1 clock = 72 MHz
 *
 * PSC = 5
 * ARR = 599
 *
 * PWM:
 *   72 MHz / (5 + 1) / (599 + 1)
 *   = 20 kHz
 *
 * RCR = 3:
 *   Update IRQ = 20 kHz / (3 + 1)
 *              = 5 kHz
 *
 * CC:
 *   5 kHz
 *   Ts = 200 us
 *
 * CV:
 *   5 kHz / 5
 *   = 1 kHz
 *   Ts = 1 ms
 * ========================================================= */

#define CC_CTRL_HZ                  5000.0f
#define CV_CTRL_HZ                  1000.0f

#define CC_CTRL_TS                  0.0002f
#define CV_CTRL_TS                  0.0010f

#define CV_LOOP_DIV                 5U


/* =========================================================
 * PI PARAMETERS
 * ========================================================= */

#define CV_KP                       0.01f
#define CV_KI                       1.9f

#define CC_KP                       0.01f
#define CC_KI                       1.5f

#define CV_I_MIN                   -0.30f
#define CV_I_MAX                    0.30f

#define CC_I_MIN                   -0.30f
#define CC_I_MAX                    0.30f

#define SOFTSTART_RATE_VS           20.0f


/* =========================================================
 * RESPONSE PROFILE
 *
 * TIMING OF CONTROL LOOPS DOES NOT CHANGE:
 *   CC = 5 kHz
 *   CV = 1 kHz
 *
 * FAST / NORMAL / SLOW change:
 *   - PI aggressiveness
 *   - Vout/Iout measurement filtering
 *   - CC current filtering
 *
 * UI refresh/filter speed is changed in UI.c.
 * ========================================================= */

/*
 * Forward declaration.
 * PowerStage is defined later in this file.
 */
extern BBUI_Data_t PowerStage;

typedef struct
{
    float cv_kp_scale;
    float cv_ki_scale;

    float cc_kp_scale;
    float cc_ki_scale;

    float vout_alpha;
    float current_alpha;
    float current_ctrl_alpha;

} PowerResponseProfile_t;


static PowerResponseProfile_t
PowerStage_GetResponseProfile(void)
{
    PowerResponseProfile_t p;

    switch(PowerStage.response_mode)
    {
        case BB_RESPONSE_FAST:
        {
            /*
             * Faster response, lighter filtering.
             * Keep gain increase modest because BOOST is more sensitive.
             */
            p.cv_kp_scale = 1.15f;
            p.cv_ki_scale = 1.10f;

            p.cc_kp_scale = 1.15f;
            p.cc_ki_scale = 1.10f;

            p.vout_alpha = 0.65f;
            p.current_alpha = 0.65f;
            p.current_ctrl_alpha = 0.65f;
        }
        break;

        case BB_RESPONSE_SLOW:
        {
            /*
             * Smooth response, strong filtering.
             */
            p.cv_kp_scale = 0.75f;
            p.cv_ki_scale = 0.60f;

            p.cc_kp_scale = 0.75f;
            p.cc_ki_scale = 0.60f;

            p.vout_alpha = 0.20f;
            p.current_alpha = 0.20f;
            p.current_ctrl_alpha = 0.20f;
        }
        break;

        case BB_RESPONSE_NORMAL:
        default:
        {
            p.cv_kp_scale = 1.00f;
            p.cv_ki_scale = 1.00f;

            p.cc_kp_scale = 1.00f;
            p.cc_ki_scale = 1.00f;

            p.vout_alpha = 0.40f;
            p.current_alpha = 0.40f;
            p.current_ctrl_alpha = 0.40f;
        }
        break;
    }

    return p;
}


/* =========================================================
 * INPUT VOLTAGE DROOP LIMITER
 * ========================================================= */

#define VIN_FILTER_ALPHA            0.20f
#define VIN_REF_UPDATE_ALPHA        0.05f

#define VIN_DROOP_ENTER_RATIO       0.90f
#define VIN_DROOP_EXIT_RATIO        0.93f

#define VIN_LIMIT_DOWN_RATE_AS      100.0f
#define VIN_LIMIT_UP_RATE_AS        5.0f

#define VIN_LIMIT_MIN_CURRENT       0.20f


/* =========================================================
 * VIN FIR FILTER
 *
 * Sampled at CC frequency = 5 kHz
 * N = 8
 *
 * Group delay:
 *   (N - 1) / 2 * Ts
 *   = 3.5 * 200 us
 *   = 0.7 ms
 * ========================================================= */

#define VIN_FIR_N                   8U

static float vin_fir_buf[VIN_FIR_N] = {0.0f};
static float vin_fir_sum = 0.0f;
static uint8_t vin_fir_index = 0U;
static uint8_t vin_fir_count = 0U;


/* =========================================================
 * GLOBAL POWER-STAGE STATE
 * ========================================================= */

BBUI_Data_t PowerStage =
{
    .vin = 0.0f,
    .vout = 0.0f,
    .current = 0.0f,
    .temp = 0.0f,

    .vset = 12.0f,
    .iset = 2.0f,

    .enable = 0U,
    .state = BBUI_STATE_OFF,

    .batt_cells = 3,
    .active_preset = 0,
    .mqtt_enable = 0,

    .max_input_power = 200.0f,
    .start_mode = BB_START_SOFT,
    .response_mode = BB_RESPONSE_NORMAL,

    .preset =
    {
        {12.0f, 5.0f},
        {16.8f, 3.0f},
        {20.0f, 2.5f}
    }
};


/* =========================================================
 * ADC DMA
 * ========================================================= */

#define ADC1_DMA_LEN                4U
#define ADC_AVG_SAMPLES             30U

uint16_t adc1_dma_buf[ADC1_DMA_LEN];
uint8_t adc1_dma_ready = 0U;

static uint32_t adc_sum[ADC1_DMA_LEN] = {0U};
static uint16_t adc_avg[ADC1_DMA_LEN] = {0U};
static uint16_t adc_sample_cnt = 0U;

int adc_calib_offset = 0;


/* =========================================================
 * CONTROL STATE
 * ========================================================= */

static uint8_t PowerStage_pwm_running = 0U;
static uint8_t power_prev_enable = 0U;

static uint32_t power_start_tick = 0U;

static float cv_i = 0.0f;
static float cc_i = 0.0f;

static float vref_soft = 0.0f;

static float ratio_cv = RATIO_MIN;
static float ratio_cc = RATIO_MIN;
static float ratio_out = RATIO_MIN;

static float effective_iset = 0.0f;

/*
 * current_fast:
 *   raw / latest sample, used for HARD over-current protection.
 *
 * current_ctrl:
 *   filtered current used by the 5 kHz CC controller.
 */
static float current_fast = 0.0f;
static float current_ctrl = 0.0f;
static uint8_t current_ctrl_filter_init = 0U;

static float vin_raw_fast = 0.0f;

static float vin_filtered_droop = 0.0f;
static float vin_reference = 0.0f;
static float input_limited_iset = 0.0f;

static uint8_t vin_filter_init = 0U;
static uint8_t input_limit_active = 0U;


/* Debug counters */
volatile uint32_t tim1_irq_count = 0U;
volatile uint32_t cc_loop_count = 0U;
volatile uint32_t cv_loop_count = 0U;


/* =========================================================
 * BASIC HELPERS
 * ========================================================= */

static float clampf(float x,
                    float min,
                    float max)
{
    if(x < min)
        return min;

    if(x > max)
        return max;

    return x;
}


static float ADC_To_Voltage(uint16_t adc)
{
    return ((float)adc * VREF) / ADC_MAX;
}


static float Read_Vout(uint16_t adc_vout)
{
    return ADC_To_Voltage(adc_vout) *
           VOUT_DIV_GAIN;
}


static float Read_Vin(uint16_t adc_vin)
{
    return ADC_To_Voltage(adc_vin) *
           VIN_DIV_GAIN;
}


static float Read_Current(uint16_t adc_current)
{
    float v_adc =
        ((float)adc_current * VREF) /
        ADC_MAX;

    float current =
        v_adc /
        CURRENT_GAIN /
        SHUNT_R;

    if(current < 0.0f)
        current = 0.0f;

    return current;
}


/* =========================================================
 * VIN FIR
 * ========================================================= */

static float Vin_FIR_Filter(float vin_new)
{
    vin_fir_sum -=
        vin_fir_buf[vin_fir_index];

    vin_fir_buf[vin_fir_index] =
        vin_new;

    vin_fir_sum +=
        vin_new;

    vin_fir_index++;

    if(vin_fir_index >= VIN_FIR_N)
        vin_fir_index = 0U;

    if(vin_fir_count < VIN_FIR_N)
        vin_fir_count++;

    return vin_fir_sum /
           (float)vin_fir_count;
}


/* =========================================================
 * NTC
 * ========================================================= */

static uint16_t ADC_Read_Channel(
    ADC_HandleTypeDef *hadc,
    uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime =
        ADC_SAMPLETIME_239CYCLES_5;

    HAL_ADC_ConfigChannel(
        hadc,
        &sConfig
    );

    HAL_ADC_Start(hadc);
    HAL_ADC_PollForConversion(
        hadc,
        10
    );

    uint32_t adc_value =
        HAL_ADC_GetValue(hadc);

    HAL_ADC_Stop(hadc);

    return (uint16_t)adc_value;
}


uint16_t raw = 0U;

static float Read_NTC_Temp(void)
{
    raw =
        ADC_Read_Channel(
            &hadc2,
            ADC_CHANNEL_2
        );

    const float RFIX = 50000.0f;
    const float R0 = 50000.0f;
    const float T0 = 298.15f;
    const float BETA = 3950.0f;

    float v =
        ((float)raw / ADC_MAX) *
        VREF;

    if(v <= 0.01f ||
       v >= (VREF - 0.01f))
    {
        return -100.0f;
    }

    float r_ntc =
        RFIX *
        v /
        (VREF - v);

    float temp_k =
        1.0f /
        (
            (1.0f / T0) +
            (1.0f / BETA) *
            logf(r_ntc / R0)
        );

    return temp_k -
           273.15f +
           30.0f;
}


/* =========================================================
 * PWM OUTPUT
 *
 * TIM1 counter must keep running continuously because its
 * Update interrupt is the CC/CV scheduler.
 *
 * OFF:
 *      MOE = 0
 *
 * ON:
 *      MOE = 1
 *
 * Do NOT HAL_TIM_PWM_Stop() while TIM1 is used for control IRQ.
 * ========================================================= */

static void PowerStage_Start(void)
{
    __HAL_TIM_MOE_ENABLE(&htim1);

    PowerStage_pwm_running = 1U;
}


static void PowerStage_Stop(void)
{
    __HAL_TIM_MOE_DISABLE(&htim1);
		TIM1 -> CCR1 = 0;
		TIM1 -> CCR2 = 599;
    PowerStage_pwm_running = 0U;
}


uint16_t duty_buck = 0U;
uint16_t duty_boost = 0U;


void PowerStage_SetRatio(float ratio)
{
    ratio =
        clampf(
            ratio,
            RATIO_MIN,
            RATIO_MAX
        );

    uint32_t arr =
        TIM1->ARR;

    uint32_t ccr =
        (uint32_t)(
            ratio *
            (float)arr
        );

    duty_buck =
        (uint16_t)ccr;

    duty_boost =
        (uint16_t)(
            arr - ccr
        );

    /*
     * Old ratio convention:
     *
     * CH1 = ratio
     * CH2 = 1 - ratio
     */
    TIM1->CCR1 = duty_buck;
    TIM1->CCR2 = duty_boost;
}


/* =========================================================
 * INPUT VOLTAGE REFERENCE / DROOP LIMIT
 * ========================================================= */

static void PowerStage_UpdateIdleVinReference(void)
{
    if((PowerStage.enable == 0U) &&
       (PowerStage.vin >
        VIN_MIN_PROTECT))
    {
        if(vin_reference <
           VIN_MIN_PROTECT)
        {
            vin_reference =
                PowerStage.vin;
        }
        else
        {
            vin_reference +=
                VIN_REF_UPDATE_ALPHA *
                (
                    PowerStage.vin -
                    vin_reference
                );
        }
    }
}


static void PowerStage_InputDroopRuntimeReset(void)
{
    vin_filtered_droop =
        PowerStage.vin;

    input_limited_iset =
        PowerStage.iset;

    vin_filter_init = 1U;
    input_limit_active = 0U;
}


static float PowerStage_GetInputDroopCurrentLimit(void)
{
    if(vin_filter_init == 0U)
    {
        PowerStage_InputDroopRuntimeReset();
    }

    vin_filtered_droop +=
        VIN_FILTER_ALPHA *
        (
            PowerStage.vin -
            vin_filtered_droop
        );

    if(vin_reference <
       VIN_MIN_PROTECT)
    {
        vin_reference =
            PowerStage.vin;
    }

    float vin_enter =
        vin_reference *
        VIN_DROOP_ENTER_RATIO;

    float vin_exit =
        vin_reference *
        VIN_DROOP_EXIT_RATIO;

    if(input_limit_active == 0U)
    {
        if(vin_filtered_droop <=
           vin_enter)
        {
            input_limit_active = 1U;
        }
    }
    else
    {
        if(vin_filtered_droop >=
           vin_exit)
        {
            input_limit_active = 0U;
        }
    }

    if(input_limited_iset >
       PowerStage.iset)
    {
        input_limited_iset =
            PowerStage.iset;
    }

    /*
     * This function runs at CV frequency = 1 kHz.
     */
    if(input_limit_active != 0U)
    {
        input_limited_iset -=
            VIN_LIMIT_DOWN_RATE_AS *
            CV_CTRL_TS;
    }
    else
    {
        input_limited_iset +=
            VIN_LIMIT_UP_RATE_AS *
            CV_CTRL_TS;
    }

    input_limited_iset =
        clampf(
            input_limited_iset,
            VIN_LIMIT_MIN_CURRENT,
            PowerStage.iset
        );

    return input_limited_iset;
}


/* =========================================================
 * FEED FORWARD
 * ========================================================= */

static float PowerStage_FeedForward_Ratio(
    float vin,
    float vref)
{
    if(vin < 0.5f)
        return RATIO_MIN;

    if(vref < 0.5f)
        return RATIO_MIN;

    float ratio =
        vref /
        (vin + vref);

    return clampf(
        ratio,
        RATIO_MIN,
        RATIO_MAX
    );
}


/* =========================================================
 * SOFT START REFERENCE
 *
 * Runs only at CV frequency = 1 kHz.
 * ========================================================= */

static void PowerStage_StartReference_1kHz(void)
{
    if(PowerStage.enable == 0U)
    {
        vref_soft = 0.0f;
        return;
    }

    if(PowerStage.start_mode ==
       BB_START_HARD)
    {
        vref_soft =
            PowerStage.vset;

        return;
    }

    if(vref_soft <
       PowerStage.vset)
    {
        vref_soft +=
            SOFTSTART_RATE_VS *
            CV_CTRL_TS;

        if(vref_soft >
           PowerStage.vset)
        {
            vref_soft =
                PowerStage.vset;
        }
    }
    else
    {
        vref_soft =
            PowerStage.vset;
    }
}


/* =========================================================
 * CONTROL RESET
 * ========================================================= */

static void PowerStage_CVCC_Reset(void)
{
    cv_i = 0.0f;
    cc_i = 0.0f;

    vref_soft = 0.0f;

    ratio_cv = RATIO_MIN;
    ratio_cc = RATIO_MIN;
    ratio_out = RATIO_MIN;

    effective_iset =
        PowerStage.iset;

    /*
     * Re-seed the CC current filter on the next ADC sample.
     */
    current_ctrl_filter_init = 0U;
}


/* =========================================================
 * MEASUREMENTS
 *
 * FAST @ 5 kHz:
 *      raw current for CC
 *      raw Vin -> FIR8
 *
 * SLOW @ 1 kHz:
 *      Vout filtered
 *      Current display filtered
 *      idle Vin reference
 * ========================================================= */

static void PowerStage_UpdateMeasurements_5kHz(void)
{
    PowerResponseProfile_t p =
        PowerStage_GetResponseProfile();

    vin_raw_fast =
        Read_Vin(
            adc1_dma_buf[3]
        );

    PowerStage.vin =
        Vin_FIR_Filter(
            vin_raw_fast
        );

    /*
     * Raw current is retained for hard protection.
     */
    current_fast =
        Read_Current(
            adc1_dma_buf[1]
        );

    /*
     * Filtered current is used by CC loop.
     *
     * FAST   alpha = 0.65
     * NORMAL alpha = 0.40
     * SLOW   alpha = 0.20
     */
    if(current_ctrl_filter_init == 0U)
    {
        current_ctrl =
            current_fast;

        current_ctrl_filter_init = 1U;
    }
    else
    {
        current_ctrl +=
            p.current_ctrl_alpha *
            (
                current_fast -
                current_ctrl
            );
    }
}


static void PowerStage_UpdateMeasurements_1kHz(void)
{
    if(adc1_dma_ready == 0U)
    {
        PowerStage_UpdateIdleVinReference();
        return;
    }

    float vout_new =
        Read_Vout(
            adc_avg[0]
        );

    float current_new =
        Read_Current(
            adc_avg[1]
        );

    PowerResponseProfile_t p =
        PowerStage_GetResponseProfile();

    PowerStage.vout +=
        p.vout_alpha *
        (
            vout_new -
            PowerStage.vout
        );

    PowerStage.current +=
        p.current_alpha *
        (
            current_new -
            PowerStage.current
        );

    PowerStage_UpdateIdleVinReference();
}


/* =========================================================
 * ENABLE / PROTECTION
 *
 * Called at 5 kHz.
 * ========================================================= */

static uint8_t PowerStage_Service_5kHz(void)
{
    uint32_t now =
        HAL_GetTick();

    /*
     * Output OFF:
     * TIM1 keeps counting.
     * Only PWM outputs are disabled with MOE.
     */
    if(PowerStage.enable == 0U)
    {
        if(power_prev_enable != 0U ||
           PowerStage_pwm_running != 0U)
        {
            PowerStage_Stop();
        }

        power_prev_enable = 0U;

        PowerStage.state =
            BBUI_STATE_OFF;

        PowerStage_CVCC_Reset();

        return 0U;
    }

    /*
     * Do not enable MOSFETs until VIN is valid.
     */
    if(PowerStage.vin <
       VIN_MIN_PROTECT)
    {
        PowerStage.state =
            BBUI_STATE_OFF;

        PowerStage_Stop();

        power_prev_enable = 0U;

        return 0U;
    }

    /*
     * OFF -> ON edge.
     */
    if(power_prev_enable == 0U)
    {
        power_start_tick =
            now;

        PowerStage_CVCC_Reset();

        PowerStage_InputDroopRuntimeReset();

        /*
         * Prepare initial duty BEFORE enabling MOE.
         */
        PowerStage_SetRatio(
            RATIO_MIN
        );

        PowerStage_Start();

        power_prev_enable = 1U;
    }

    /*
     * Temperature fault.
     */
    if(PowerStage.temp >
       TEMP_MAX_PROTECT)
    {
        PowerStage_CVCC_Reset();

        PowerStage.enable = 0U;
        PowerStage.state =
            BBUI_STATE_FAULT;

        PowerStage_Stop();

        power_prev_enable = 0U;

        return 0U;
    }

    /*
     * Hard-current fault.
     *
     * Use current_fast, not the slow display current.
     */
    if((uint32_t)(
           now -
           power_start_tick
       ) >= POWER_START_BLANK_MS)
    {
        if(current_fast >
           I_HARD_LIMIT_A)
        {
            PowerStage_CVCC_Reset();

            PowerStage.enable = 0U;
            PowerStage.state =
                BBUI_STATE_FAULT;

            PowerStage_Stop();

            power_prev_enable = 0U;

            return 0U;
        }
    }

    return 1U;
}


/* =========================================================
 * CV LOOP = 1 kHz
 *
 * Ts = 1 ms
 *
 * Only calculates ratio_cv.
 * Final PWM is selected by CC loop using:
 *
 *      ratio_out = min(ratio_cv, ratio_cc)
 * ========================================================= */

static void PowerStage_CV_Loop_1kHz(void)
{
    cv_loop_count++;

    PowerResponseProfile_t p =
        PowerStage_GetResponseProfile();

    float cv_kp =
        CV_KP * p.cv_kp_scale;

    float cv_ki =
        CV_KI * p.cv_ki_scale;

    PowerStage_StartReference_1kHz();

    effective_iset =
        PowerStage_GetInputDroopCurrentLimit();

    float ratio_ff =
        PowerStage_FeedForward_Ratio(
            PowerStage.vin,
            vref_soft
        );

    float err_v =
        vref_soft -
        PowerStage.vout;

    float cv_i_new =
        cv_i +
        cv_ki *
        err_v *
        CV_CTRL_TS;

    cv_i_new =
        clampf(
            cv_i_new,
            CV_I_MIN,
            CV_I_MAX
        );

    float ratio_cv_unsat =
        ratio_ff +
        cv_kp * err_v +
        cv_i_new;

    /*
     * Anti-windup.
     */
    if(!(
        (
            ratio_cv_unsat >
            RATIO_MAX &&
            err_v > 0.0f
        ) ||
        (
            ratio_cv_unsat <
            RATIO_MIN &&
            err_v < 0.0f
        )
    ))
    {
        cv_i = cv_i_new;
    }

    ratio_cv =
        ratio_ff +
        cv_kp * err_v +
        cv_i;

    ratio_cv =
        clampf(
            ratio_cv,
            RATIO_MIN,
            RATIO_MAX
        );
}


/* =========================================================
 * CC LOOP = 5 kHz
 *
 * Ts = 200 us
 *
 * Uses current_fast from latest ADC DMA sample.
 * ========================================================= */

static void PowerStage_CC_Loop_5kHz(void)
{
    cc_loop_count++;

    PowerResponseProfile_t p =
        PowerStage_GetResponseProfile();

    float cc_kp =
        CC_KP * p.cc_kp_scale;

    float cc_ki =
        CC_KI * p.cc_ki_scale;

    float ratio_ff =
        PowerStage_FeedForward_Ratio(
            PowerStage.vin,
            vref_soft
        );

    float err_i =
        effective_iset -
        current_ctrl;

    float cc_i_new =
        cc_i +
        cc_ki *
        err_i *
        CC_CTRL_TS;

    cc_i_new =
        clampf(
            cc_i_new,
            CC_I_MIN,
            CC_I_MAX
        );

    float ratio_cc_unsat =
        ratio_ff +
        cc_kp * err_i +
        cc_i_new;

    /*
     * Anti-windup.
     */
    if(!(
        (
            ratio_cc_unsat >
            RATIO_MAX &&
            err_i > 0.0f
        ) ||
        (
            ratio_cc_unsat <
            RATIO_MIN &&
            err_i < 0.0f
        )
    ))
    {
        cc_i = cc_i_new;
    }

    ratio_cc =
        ratio_ff +
        cc_kp * err_i +
        cc_i;

    ratio_cc =
        clampf(
            ratio_cc,
            RATIO_MIN,
            RATIO_MAX
        );

    /*
     * Tightest limiter wins.
     */
    if(ratio_cc <
       ratio_cv)
    {
        ratio_out =
            ratio_cc;

        PowerStage.state =
            BBUI_STATE_CC;
    }
    else
    {
        ratio_out =
            ratio_cv;

        PowerStage.state =
            BBUI_STATE_CV;
    }

    ratio_out =
        clampf(
            ratio_out,
            RATIO_MIN,
            RATIO_MAX
        );

    PowerStage_SetRatio(
        ratio_out
    );
}


/* =========================================================
 * TIM1 UPDATE ISR CALLBACK
 *
 * TIM1 Update = 5 kHz.
 *
 * Every interrupt:
 *      measurement fast
 *      protection
 *      CC loop
 *
 * Every 5 interrupts:
 *      measurement slow
 *      CV loop
 * ========================================================= */

void HAL_TIM_PeriodElapsedCallback(
    TIM_HandleTypeDef *htim)
{
    if(htim->Instance != TIM1)
        return;

    static uint8_t cv_div = 0U;

    tim1_irq_count++;

    /*
     * Measurement always runs,
     * even when BB output is OFF.
     */
    PowerStage_UpdateMeasurements_5kHz();

    cv_div++;

    uint8_t run_cv = 0U;

    if(cv_div >= CV_LOOP_DIV)
    {
        cv_div = 0U;
        run_cv = 1U;

        PowerStage_UpdateMeasurements_1kHz();
    }

    /*
     * If output is OFF or protection has tripped,
     * do not run PI loops.
     */
    if(PowerStage_Service_5kHz() == 0U)
        return;

    /*
     * Run CV first when both loops are due.
     */
    if(run_cv != 0U)
    {
        PowerStage_CV_Loop_1kHz();
    }

    PowerStage_CC_Loop_5kHz();
}


/* =========================================================
 * ADC DMA CALLBACK
 * ========================================================= */

void HAL_ADC_ConvCpltCallback(
    ADC_HandleTypeDef *hadc)
{
    if(hadc->Instance != ADC1)
        return;

    adc_sum[0] +=
        adc1_dma_buf[0];

    adc_sum[1] +=
        adc1_dma_buf[1];

    adc_sum[3] +=
        adc1_dma_buf[3];

    adc_sample_cnt++;

    if(adc_sample_cnt >=
       ADC_AVG_SAMPLES)
    {
        adc_avg[0] =
            (uint16_t)(
                adc_sum[0] /
                ADC_AVG_SAMPLES
            );

        adc_avg[1] =
            (uint16_t)(
                adc_sum[1] /
                ADC_AVG_SAMPLES
            );

        adc_avg[3] =
            (uint16_t)(
                adc_sum[3] /
                ADC_AVG_SAMPLES
            );

        adc_sum[0] = 0U;
        adc_sum[1] = 0U;
        adc_sum[3] = 0U;

        adc_sample_cnt = 0U;

        adc1_dma_ready = 1U;
        adc_calib_offset = 1;
    }
}


/* =========================================================
 * GPIO CALLBACK
 * ========================================================= */

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    (void)GPIO_Pin;

    /*
     * PB9 ON/OFF is polled + debounced
     * inside BBUI_Task().
     */
}


/* =========================================================
 * INIT
 * ========================================================= */

void Buck_UI_Init(void)
{
    HAL_ADCEx_Calibration_Start(
        &hadc1
    );

    HAL_ADCEx_Calibration_Start(
        &hadc2
    );

    /*
     * ADC must run before the output can be enabled.
     */
    HAL_ADC_Start_DMA(
        &hadc1,
        (uint32_t *)adc1_dma_buf,
        ADC1_DMA_LEN
    );

    HAL_Delay(100);

    /*
     * UI / encoders.
     */
    BBUI_Init(
        &PowerStage,
        &htim4,
        &htim2
    );

    /*
     * Prepare minimum ratio before TIM1 PWM channels start.
     */
    PowerStage_SetRatio(
        RATIO_MIN
    );

    /*
     * Start TIM1 PWM channels ONCE.
     *
     * TIM1 counter will stay alive permanently.
     */
    HAL_TIM_PWM_Start(
        &htim1,
        TIM_CHANNEL_1
    );

    HAL_TIMEx_PWMN_Start(
        &htim1,
        TIM_CHANNEL_1
    );

    HAL_TIM_PWM_Start(
        &htim1,
        TIM_CHANNEL_2
    );

    HAL_TIMEx_PWMN_Start(
        &htim1,
        TIM_CHANNEL_2
    );

    /*
     * Boot with BB output OFF.
     */
    PowerStage_Stop();

    /*
     * TIM1 Update interrupt = 5 kHz.
     */
    HAL_TIM_Base_Start_IT(
        &htim1
    );
}


/* =========================================================
 * TEMPERATURE TASK
 * ========================================================= */

uint32_t lastTime_readTemp = 0U;
float temp_new = 0.0f;

void handle_temp(void)
{
    if(
        (HAL_GetTick() -
         lastTime_readTemp) >
        500U
    )
    {
        lastTime_readTemp =
            HAL_GetTick();

        temp_new =
            Read_NTC_Temp();

        PowerStage.temp =
            PowerStage.temp *
            0.90f +
            temp_new *
            0.10f;
    }
}


int sleep = 0;

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_SPI1_Init();
  MX_ADC1_Init();
  MX_ADC2_Init();
  MX_TIM1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_USB_DEVICE_Init();
  MX_TIM4_Init();
  /* USER CODE BEGIN 2 */
    /*
     * Buck_UI_Init():
     *  - ADC1 calibration + DMA
     *  - power stage OFF
     *  - BBUI_Init(&PowerStage, &htim2, &htim4)
     *  - TIM1 Update = 5 kHz control interrupt
     *  - ST7789 init is performed by BBUI_Init()
     */
    Buck_UI_Init();
    /* Normal buck-boost path selected at boot. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);

    /* Buzzer OFF */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET);

    /* Fan/control output initial state */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_SET);
		HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    /*
     * UI:
     *  TIM2 encoder -> VSET
     *  TIM4 encoder -> ISET
     *  PB9          -> Output ON/OFF
     */
    BBUI_Task();
    handle_temp();
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = ENABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 4;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_1;
  sConfig.Rank = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc2.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 5;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 599;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 3;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_OC3REF;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_LOW;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 25;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 65535;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 35;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_LOW;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI12;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim4, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
  /* DMA1_Channel3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 PC14 PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB10 PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB11 */
  GPIO_InitStruct.Pin = GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PB12 PB4 PB8 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_12|GPIO_PIN_4|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
