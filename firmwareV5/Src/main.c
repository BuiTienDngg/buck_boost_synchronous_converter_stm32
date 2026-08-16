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
#define ADC_MAX             4095.0f
#define VREF                3.3f

#define VIN_DIV_GAIN        11.0f
#define VOUT_DIV_GAIN       11.0f

#define NTC_R_PULLUP        10000.0f
#define NTC_R0              50000.0f
#define NTC_BETA            3950.0f
#define NTC_T0_K            298.15f

#define SET_V_MIN           2.0f
#define SET_V_MAX           36.0f
#define SET_V_STEP          0.1f

#define ENC_COUNT_PER_STEP  4

#define POWERSTAGE_DUTY_MIN       0.1f
#define POWERSTAGE_DUTY_MAX       0.9f
#define POWERSTAGE_RATIO_MIN    0.1f
#define POWERSTAGE_RATIO_MAX    0.9f

#define CURRENT_MAX 	10.0f		// AMPE
#define TEMP_MAX 			80.0f   //*C
#define VSET_MIN            1.0f
#define VSET_MAX            30.0f
#define VSET_STEP           0.1f

#define ISET_MIN            0.1f
#define ISET_MAX            10.0f
#define ISET_STEP           0.1f


#define CTRL_TS              0.001f

#define VIN_MIN_PROTECT      2.0f
#define TEMP_MAX_PROTECT     80.0f

#define RATIO_MIN            POWERSTAGE_DUTY_MIN
#define RATIO_MAX            POWERSTAGE_DUTY_MAX

#define CV_KP                0.015f
#define CV_KI                0.8f

#define CC_KP                0.025f
#define CC_KI                1.2f

#define CV_I_MIN            -0.30f
#define CV_I_MAX             0.30f

#define CC_I_MIN            -0.30f
#define CC_I_MAX             0.30f

#define SOFTSTART_RATE_VS    6.0f
#define I_HARD_MARGIN        1.0f

#define I_HARD_LIMIT_A              8.5f
#define POWER_START_BLANK_MS        200U

/* Input-voltage droop limiter. Vin may be 12 V, 15 V, 20 V, etc. */
#define VIN_FILTER_ALPHA            0.20f
#define VIN_REF_UPDATE_ALPHA        0.05f
#define VIN_DROOP_ENTER_RATIO       0.90f
#define VIN_DROOP_EXIT_RATIO        0.93f
#define VIN_LIMIT_DOWN_RATE_AS      100.0f
#define VIN_LIMIT_UP_RATE_AS        5.0f
#define VIN_LIMIT_MIN_CURRENT       0.20f
#define VIN_REF_LIGHT_CURRENT       0.30f

volatile int16_t test_tim2;
volatile int16_t test_tim4;

static float vin_filtered = 0.0f;
static float vin_reference = 0.0f;
static float input_limited_iset = 0.0f;
static uint8_t vin_filter_init = 0U;
static uint8_t input_limit_active = 0U;

static uint8_t power_prev_enable = 0;
static uint32_t power_start_tick = 0;
static float cv_i = 0.0f;
static float cc_i = 0.0f;
static float vref_soft = 0.0f;
static float ratio_out = 0.5f;
typedef enum
{
    PS_MODE_CV = 0,
    PS_MODE_CC,
    PS_MODE_CVCC
} PowerMode_t;

typedef struct
{
    float kp;
    float ki;

    float integral;

    float out_min;
    float out_max;

    float output;
} PI_Controller_t;

PI_Controller_t cv_pi =
{
    .kp = CV_KP,
    .ki = CV_KI,

    .integral = 0.0f,

    .out_min = POWERSTAGE_RATIO_MIN,
    .out_max = POWERSTAGE_RATIO_MAX,

    .output = 0.5f       // b?t d?u t?i Vin = Vout
};
BBUI_Data_t PowerStage =
{
    .vin = 0.0f,
    .vout = 0.0f,
    .current = 0.0f,
    .temp = 0.0f,

    .vset = 12.0f,
    .iset = 2.0f,

    .enable = 0,
    .state = BBUI_STATE_OFF,

    .batt_cells = 3,
    .active_preset = 0,
    .mqtt_enable = 0,
		.max_input_power = 200.0f,
    .start_mode = BB_START_SOFT,
    .preset =
    {
        {12.0f, 5.0f},
        {16.8f, 3.0f},
        {20.0f, 2.5f}
    }
};



static volatile uint16_t adc_current_raw = 0;
volatile uint8_t adc_current_ready = 0;

static uint32_t t_adc = 0;
static uint32_t t_lcd = 0;
static uint8_t PowerStage_pwm_running = 0;
#define ADC1_DMA_LEN 4

uint16_t adc1_dma_buf[ADC1_DMA_LEN];
uint8_t adc1_dma_ready = 0;
static uint16_t ADC_Read_Channel(ADC_HandleTypeDef *hadc, uint32_t channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    sConfig.Channel = channel;
    sConfig.Rank = ADC_REGULAR_RANK_1;
    sConfig.SamplingTime = ADC_SAMPLETIME_239CYCLES_5;

    HAL_ADC_ConfigChannel(hadc, &sConfig);

    HAL_ADC_Start(hadc);
    HAL_ADC_PollForConversion(hadc, 10);
		uint32_t adc_value = 0;
		adc_value = HAL_ADC_GetValue(hadc);
    HAL_ADC_Stop(hadc);

    return (uint16_t)adc_value;
}

static float ADC_To_Voltage(uint16_t adc)
{
    return ((float)adc * VREF) / ADC_MAX;
}

static float Read_Vout(uint16_t adc_vout)
{
    return ADC_To_Voltage(adc_vout) * VOUT_DIV_GAIN;
}

static float Read_Vin(uint16_t adc_vin)
{
    return ADC_To_Voltage(adc_vin) * VIN_DIV_GAIN;
}
static void PowerStage_UpdateIdleVinReference(void)
{
    /* Learn source no-load voltage only while the converter is disabled. */
    if((PowerStage.enable == 0U) &&
       (PowerStage.vin > VIN_MIN_PROTECT))
    {
        if(vin_reference < VIN_MIN_PROTECT)
        {
            vin_reference = PowerStage.vin;
        }
        else
        {
            vin_reference += VIN_REF_UPDATE_ALPHA *
                             (PowerStage.vin - vin_reference);
        }
    }
}

static void PowerStage_InputDroopRuntimeReset(void)
{
    /* Keep vin_reference: it was learned while output was OFF. */
    vin_filtered = PowerStage.vin;
    input_limited_iset = PowerStage.iset;
    vin_filter_init = 1U;
    input_limit_active = 0U;
}

static float PowerStage_GetInputDroopCurrentLimit(void)
{
    if(vin_filter_init == 0U)
    {
        PowerStage_InputDroopRuntimeReset();
    }

    vin_filtered += VIN_FILTER_ALPHA *
                    (PowerStage.vin - vin_filtered);

    /* Fallback if output was enabled before a valid idle reference existed. */
    if(vin_reference < VIN_MIN_PROTECT)
    {
        vin_reference = PowerStage.vin;
    }

    float vin_enter = vin_reference * VIN_DROOP_ENTER_RATIO;
    float vin_exit  = vin_reference * VIN_DROOP_EXIT_RATIO;

    if(input_limit_active == 0U)
    {
        if(vin_filtered <= vin_enter)
        {
            input_limit_active = 1U;
        }
    }
    else
    {
        if(vin_filtered >= vin_exit)
        {
            input_limit_active = 0U;
        }
    }

    if(input_limited_iset > PowerStage.iset)
    {
        input_limited_iset = PowerStage.iset;
    }

    if(input_limit_active != 0U)
    {
        input_limited_iset -= VIN_LIMIT_DOWN_RATE_AS * CTRL_TS;
    }
    else
    {
        input_limited_iset += VIN_LIMIT_UP_RATE_AS * CTRL_TS;
    }

    if(input_limited_iset < VIN_LIMIT_MIN_CURRENT)
        input_limited_iset = VIN_LIMIT_MIN_CURRENT;

    if(input_limited_iset > PowerStage.iset)
        input_limited_iset = PowerStage.iset;

    return input_limited_iset;
}
uint16_t raw = 0;
static float Read_NTC_Temp(void)
{
    raw = ADC_Read_Channel(&hadc2, ADC_CHANNEL_2);
    const float RFIX = 50000.0f;

    const float R0   = 50000.0f;   // 50k @ 25°C
    const float T0   = 298.15f;    // 25°C Kelvin
    const float BETA = 3950.0f;    // ph?i dúng v?i NTC c?a b?n

    float v = ((float)raw / 4095.0f) * VREF;

    if(v <= 0.01f || v >= (VREF - 0.01f))
        return -100.0f;

    float r_ntc = RFIX * v / (VREF - v);

    float temp_k =
        1.0f /
        (
            (1.0f / T0) +
            (1.0f / BETA) *
            logf(r_ntc / R0)
        );

    return temp_k - 273.15f + 30.0f;
}
#define CURRENT_GAIN        200.0f // 1.884
#define SHUNT_R             0.002f
uint16_t adc_offset = 0;
static float Read_Current(uint16_t adc_current)
{
    float v_adc = (adc_current) * 3.3f / 4095.0f;
    float current = (v_adc) / CURRENT_GAIN / SHUNT_R;
    if(current < 0.0f)
        current = 0.0f;
    return current;
}
static float Read_Current_linear(uint16_t adc)
{
    float current = 0.00720f * adc - 2.1007f;

    if(current < 0.0f)
        current = 0.0f;

    return current;
}

static uint32_t adc_sum[ADC1_DMA_LEN] = {0};
static uint16_t adc_avg[ADC1_DMA_LEN] = {0};
static uint16_t adc_sample_cnt = 0;
int adc_calib_offset = 0;
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    (void)GPIO_Pin;
    /* PB9 ON/OFF is polled + debounced inside BBUI_Task(). */
}

static void PowerStage_Start(void)
{
//    if(PowerStage_pwm_running)
//        return;

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
		HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    PowerStage_pwm_running = 1;
}

static void PowerStage_Stop(void)
{
//    if(!PowerStage_pwm_running)
//        return;
		TIM1 -> CCR1 = 0;
		TIM1 -> CCR2 = 0;
    HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_1);
		HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Stop(&htim1, TIM_CHANNEL_2);
    PowerStage_pwm_running = 0;
}
uint16_t duty_buck = 0, duty_boost = 0;
static void PowerStage_SetRatio(float ratio)
{
    if(ratio < POWERSTAGE_DUTY_MIN)
        ratio = POWERSTAGE_DUTY_MIN;

    if(ratio > POWERSTAGE_DUTY_MAX)
        ratio = POWERSTAGE_DUTY_MAX;

    uint32_t ccr = (uint32_t)(ratio * (float)(1000));
		duty_buck = ccr;
		duty_boost = 1000 - ccr;
		TIM1 -> CCR1 = duty_buck;
		TIM1 -> CCR2 = duty_boost;
    
}
float duty = 0;
static void PowerStage_Control_OpenLoop(void)
{
//    if(!PowerStage.enable)
//    {
//				PowerStage_SetRatio(0);
//        PowerStage_Stop();
//        return;
//    }

//    if(PowerStage.vin < 2.0f)
//    {
//        PowerStage_Stop();
//        PowerStage.enable = 0;
//        return;
//    }

//    if(PowerStage.current > 3.0f || PowerStage.temp > 80.0f)
//    {
//        PowerStage_Stop();
//        PowerStage.enable = 0;
//        return;
//    }
		
    duty = PowerStage.vset / PowerStage.vin;
    
    PowerStage_SetRatio(0.6f);
}
static void PowerStage_Control_CloseLoop(void)
{
    if(!PowerStage.enable)
    {
        cv_pi.integral = 0.0f;
        cv_pi.output = 0.5f;

        PowerStage_SetRatio(0.5f);
        PowerStage_Stop();
        return;
    }

    if(PowerStage.vin < 2.0f)
    {
        PowerStage_Stop();
        PowerStage.enable = 0;
        return;
    }

    if(PowerStage.current > CURRENT_MAX || PowerStage.temp > TEMP_MAX)
    {
        PowerStage_Stop();
        PowerStage.enable = 0;
        return;
    }

    PowerStage_Start();

    //---------------------------------------
    // Voltage PI
    //---------------------------------------

    float error = PowerStage.vset - PowerStage.vout;

    cv_pi.integral += cv_pi.ki * error * CTRL_TS;

    float output = cv_pi.kp * error + cv_pi.integral;

    //---------------------------------------
    // Anti-windup
    //---------------------------------------

    if(output > cv_pi.out_max)
    {
        output = cv_pi.out_max;

        if(error > 0)
            cv_pi.integral -= cv_pi.ki * error * CTRL_TS;
    }

    if(output < cv_pi.out_min)
    {
        output = cv_pi.out_min;

        if(error < 0)
            cv_pi.integral -= cv_pi.ki * error * CTRL_TS;
    }

    cv_pi.output = output;

    PowerStage_SetRatio(output);
}


static float clampf(float x, float min, float max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}
static float PowerStage_FeedForward_Ratio(float vin, float vref)
{
    if(vin < 0.5f)
        return 0.5f;

    if(vref < 0.5f)
        return RATIO_MIN;

    float ratio = vref / (vin + vref);

    return clampf(ratio, RATIO_MIN, RATIO_MAX);
}
static void PowerStage_StartReference_1kHz(void)
{
    if(PowerStage.enable == 0)
    {
        vref_soft = 0.0f;
        return;
    }

    if(PowerStage.start_mode == BB_START_HARD)
    {
        vref_soft = PowerStage.vset;
        return;
    }

    /*
     * Soft-start.
     */
    if(vref_soft < PowerStage.vset)
    {
        vref_soft += SOFTSTART_RATE_VS * CTRL_TS;

        if(vref_soft > PowerStage.vset)
            vref_soft = PowerStage.vset;
    }
    else
    {
        vref_soft = PowerStage.vset;
    }
}
static void PowerStage_CVCC_Reset(void)
{
    cv_i = 0.0f;
    cc_i = 0.0f;

    vref_soft = 0.0f;
    ratio_out = 0.5f;
}
void PowerStage_CloseLoop_CVCC_1kHz(void)
{
    uint32_t now = HAL_GetTick();

    /*
     * =====================================================
     * PH?T HI?N C?NH B?T NGU?N
     * =====================================================
     */
    if((PowerStage.enable != 0U) &&
			 (power_prev_enable == 0U))
		{
				power_start_tick = now;

				cv_i = 0.0f;
				cc_i = 0.0f;
				ratio_out = RATIO_MIN;
				vref_soft = 0.0f;

                PowerStage_InputDroopRuntimeReset();

				PowerStage_Start();
		}

    power_prev_enable = PowerStage.enable;

    /* Fast droop immediate check: use raw DMA ADC sample (no averaging)
       to detect large input sag from inrush and trip immediately. */
//    {
//      uint16_t vin_raw_fast = adc1_dma_buf[3];
//      float vin_fast = Read_Vin(vin_raw_fast);
//      if(vin_reference > VIN_MIN_PROTECT)
//      {
//        float vin_fast_enter = vin_reference * 0.88f;
//        if(vin_fast <= vin_fast_enter)
//        {
//          /* Immediate fault: stop PWM and disable output */
//          PowerStage_CVCC_Reset();

//          PowerStage.enable = 0U;
//          PowerStage.state = BBUI_STATE_FAULT;

//          PowerStage_Stop();

//          power_prev_enable = 0U;

//          return;
//        }
//      }
//    }

    /*
     * =====================================================
     * OUTPUT OFF
     * =====================================================
     */
    if(PowerStage.enable == 0U)
    {
        PowerStage_CVCC_Reset();

        PowerStage.state = BBUI_STATE_OFF;

        PowerStage_Stop();

        return;
    }

    /*
     * =====================================================
     * B?O V? ?I?N ?P V?O
     * =====================================================
     */
    if(PowerStage.vin < VIN_MIN_PROTECT)
    {
        PowerStage_CVCC_Reset();

        PowerStage.enable = 0U;
        PowerStage.state = BBUI_STATE_FAULT;

        PowerStage_Stop();

        power_prev_enable = 0U;

        return;
    }

    /*
     * =====================================================
     * B?O V? NHI?T ??
     * =====================================================
     */
    if(PowerStage.temp > TEMP_MAX_PROTECT)
    {
        PowerStage_CVCC_Reset();

        PowerStage.enable = 0U;
        PowerStage.state = BBUI_STATE_FAULT;

        PowerStage_Stop();

        power_prev_enable = 0U;

        return;
    }

    /*
     * =====================================================
     * B?O V? QU? D?NG C?NG
     * =====================================================
     *
     * B? qua trong 200 ms d?u d? tr?nh d?ng do cu,
     * spike chuy?n relay ho?c xung kh?i d?ng g?y false fault.
     *
     * Gi?i h?n c?ng su?t kh?ng du?c d?ng l?m ngu?ng FAULT.
     */
    if((uint32_t)(now - power_start_tick) >=
       POWER_START_BLANK_MS)
    {
        if(PowerStage.current > I_HARD_LIMIT_A)
        {
            PowerStage_CVCC_Reset();

            PowerStage.enable = 0U;
            PowerStage.state = BBUI_STATE_FAULT;

            PowerStage_Stop();

            power_prev_enable = 0U;

            return;
        }
    }

    /*
     * B?o d?m PWM d? du?c b?t.
     * N?u PowerStage_Start() ch? c?n g?i m?t l?n,
     * c? th? b? d?ng n?y v? d? g?i ? c?nh enable.
     */
    PowerStage_Start();

    /*
     * =====================================================
     * HARD-START / SOFT-START REFERENCE
     * =====================================================
     */
    PowerStage_StartReference_1kHz();

    /*
     * Ph?i t?nh effective_iset sau khi c?p nh?t vref_soft,
     * v? gi?i h?n c?ng su?t c? th? ph? thu?c vref_soft.
     */
    float effective_iset = PowerStage_GetInputDroopCurrentLimit();

    /*
     * =====================================================
     * FEED-FORWARD
     * =====================================================
     */
    float ratio_ff =
        PowerStage_FeedForward_Ratio(PowerStage.vin,
                                     vref_soft);

    /*
     * =====================================================
     * SAI S? CV V? CC
     * =====================================================
     */
    float err_v =
        vref_soft - PowerStage.vout;

    float err_i =
        effective_iset - PowerStage.current;

    /*
     * =====================================================
     * T?CH PH?N T?M TH?I
     * =====================================================
     */
    float cv_i_new =
        cv_i + CV_KI * err_v * CTRL_TS;

    float cc_i_new =
        cc_i + CC_KI * err_i * CTRL_TS;

    cv_i_new =
        clampf(cv_i_new, CV_I_MIN, CV_I_MAX);

    cc_i_new =
        clampf(cc_i_new, CC_I_MIN, CC_I_MAX);

    /*
     * =====================================================
     * ??U RA PI CHUA B?O H?A
     * =====================================================
     */
    float ratio_cv_unsat =
        ratio_ff +
        CV_KP * err_v +
        cv_i_new;

    float ratio_cc_unsat =
        ratio_ff +
        CC_KP * err_i +
        cc_i_new;

    /*
     * =====================================================
     * ANTI-WINDUP CV
     * =====================================================
     */
    if(!((ratio_cv_unsat > RATIO_MAX &&
          err_v > 0.0f) ||
         (ratio_cv_unsat < RATIO_MIN &&
          err_v < 0.0f)))
    {
        cv_i = cv_i_new;
    }

    /*
     * =====================================================
     * ANTI-WINDUP CC
     * =====================================================
     */
    if(!((ratio_cc_unsat > RATIO_MAX &&
          err_i > 0.0f) ||
         (ratio_cc_unsat < RATIO_MIN &&
          err_i < 0.0f)))
    {
        cc_i = cc_i_new;
    }

    /*
     * T?nh l?i output b?ng gi? tr? t?ch ph?n d? du?c ch?p nh?n.
     */
    float ratio_cv =
        ratio_ff +
        CV_KP * err_v +
        cv_i;

    float ratio_cc =
        ratio_ff +
        CC_KP * err_i +
        cc_i;

    ratio_cv =
        clampf(ratio_cv, RATIO_MIN, RATIO_MAX);

    ratio_cc =
        clampf(ratio_cc, RATIO_MIN, RATIO_MAX);

    /*
     * =====================================================
     * CH?N V?NG ?I?U KHI?N CH?T HON
     * =====================================================
     *
     * Gi? tr? ratio nh? hon s? h?n ch? c?ng su?t nhi?u hon.
     */
    if(ratio_cc < ratio_cv)
    {
        ratio_out = ratio_cc;
        PowerStage.state = BBUI_STATE_CC;
    }
    else
    {
        ratio_out = ratio_cv;
        PowerStage.state = BBUI_STATE_CV;
    }

    ratio_out =
        clampf(ratio_out, RATIO_MIN, RATIO_MAX);

    PowerStage_SetRatio(ratio_out);
}
void Buck_UI_Init(void)
{
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADCEx_Calibration_Start(&hadc2);

    PowerStage_Stop();

    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc1_dma_buf, ADC1_DMA_LEN);
		HAL_Delay(700);

    BBUI_Init(&PowerStage, &htim4, &htim2);
		
    HAL_TIM_Base_Start_IT(&htim3);
		//while(!adc_calib_offset);
}
uint32_t lastTime_readTemp = 0;
float temp_new;
void handle_temp(){
		if(HAL_GetTick() -  lastTime_readTemp > 500)
		{
			lastTime_readTemp = HAL_GetTick();
			temp_new = Read_NTC_Temp();
			PowerStage.temp = PowerStage.temp * 0.9f + temp_new * 0.1f;
			if(PowerStage.temp >= 40.0f)
			{
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);
			}
			else if(PowerStage.temp < 38.0f)
			{
				HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 0);
			}
		}
		
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance != TIM3)
        return;

    /* Update measurements first. */
    float vin_new = Read_Vin(adc_avg[3]);
    float vout_new = Read_Vout(adc_avg[0]);
    float current_new = Read_Current(adc_avg[1]);
   
    PowerStage.vin += 0.4f * (vin_new - PowerStage.vin);
    PowerStage.vout += 0.4f * (vout_new - PowerStage.vout);
    PowerStage.current += 0.4f * (current_new - PowerStage.current);

    /* Learn the actual source voltage while output is off. */
//    PowerStage_UpdateIdleVinReference();
		//PowerStage_Control_OpenLoop();
    /* CloseLoop handles OFF, protection, CV, CC and Vin-droop limiting. */
    PowerStage_CloseLoop_CVCC_1kHz();
}

#define ADC_AVG_SAMPLES 30
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc->Instance == ADC1)
    {
        adc_sum[0] += adc1_dma_buf[0];
        adc_sum[1] += adc1_dma_buf[1];
        adc_sum[3] += adc1_dma_buf[3];

        adc_sample_cnt++;

        if(adc_sample_cnt >= ADC_AVG_SAMPLES)
        {
            adc_avg[0] = adc_sum[0] / ADC_AVG_SAMPLES;
            adc_avg[1] = adc_sum[1] / ADC_AVG_SAMPLES;
            adc_avg[3] = adc_sum[3] / ADC_AVG_SAMPLES;

            adc_sum[0] = 0;
            adc_sum[1] = 0;
            adc_sum[3] = 0;

            adc_sample_cnt = 0;
            adc1_dma_ready = 1;
            adc_calib_offset = 1;
        }
    }
}
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
     *  - TIM3 1 kHz control interrupt
     *  - ST7789 init is performed by BBUI_Init()
     */
    Buck_UI_Init();
		PowerStage_Start();
    /* Normal buck-boost path selected at boot. */
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_RESET);

    /* Buzzer OFF */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14, GPIO_PIN_RESET);

    /* Fan/control output initial state */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, GPIO_PIN_SET);
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

    /* Slow NTC / fan handling */
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
  htim1.Init.Period = 1000;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
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
  sConfigOC.Pulse = 5;
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
  sBreakDeadTimeConfig.DeadTime = 50;
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
  htim3.Init.Prescaler = 10;
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
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 PC14 PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB10 PB11
                           PB15 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
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
