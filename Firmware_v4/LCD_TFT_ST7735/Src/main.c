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
#include "ST7735_SPI.h"
#include "fonts.h"
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

#define SOFTSTART_RATE_VS    8.0f
#define I_HARD_MARGIN        1.0f

#define SWITCH_PIN GPIO_PIN_5
#define SWITCH_PORT GPIOB
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

typedef enum
{
    UI_FIELD_VSET = 0,
    UI_FIELD_ISET,
    UI_FIELD_MODE,
    UI_FIELD_OUT,
    UI_FIELD_COUNT
} UI_Field_t;

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

    .preset =
    {
        {12.0f, 5.0f},
        {16.8f, 3.0f},
        {20.0f, 2.5f}
    }
};



static UI_Field_t ui_field = UI_FIELD_VSET;
static volatile uint8_t ui_full_redraw = 1;
static volatile uint8_t ui_need_update = 1;

static volatile uint16_t adc_current_raw = 0;
static volatile uint8_t adc_current_ready = 0;

static int16_t enc_last = 0;
static int32_t enc_acc = 0;

static uint32_t t_adc = 0;
static uint32_t t_lcd = 0;
static uint8_t PowerStage_pwm_running = 0;
#define ADC1_DMA_LEN 4

static uint16_t adc1_dma_buf[ADC1_DMA_LEN];

static volatile uint8_t adc1_dma_ready = 0;
static void fmt_float(char *buf, float value, uint8_t dec, const char *unit)
{
    int32_t scale = 1;

    for(uint8_t i = 0; i < dec; i++)
        scale *= 10;

    int32_t v = (int32_t)(value * scale + (value >= 0 ? 0.5f : -0.5f));

    if(v < 0)
    {
        v = -v;
        if(dec == 1)
            sprintf(buf, "-%ld.%01ld%s", v / scale, v % scale, unit);
        else
            sprintf(buf, "-%ld.%02ld%s", v / scale, v % scale, unit);
    }
    else
    {
        if(dec == 1)
            sprintf(buf, "%ld.%01ld%s", v / scale, v % scale, unit);
        else
            sprintf(buf, "%ld.%02ld%s", v / scale, v % scale, unit);
    }
}
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

uint16_t raw = 0;
static float Read_NTC_Temp(void)
{
    raw = ADC_Read_Channel(&hadc2, ADC_CHANNEL_2);

    float v = raw * 3.3f / 4095.0f;

    if(v < 0.01f) v = 0.01f;
    if(v > 3.29f) v = 3.29f;

    float r_ntc = 10000.0f * v / (3.3f - v);

    float temp_k = 1.0f / 
    (
        (1.0f / 298.15f) + 
        (logf(r_ntc / 10000.0f) / 3950.0f)
    );

    return temp_k - 273.15f;
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
#define ADC1_DMA_LEN        4
#define ADC_AVG_SAMPLES     36
// 14.57  
static uint16_t adc1_dma_buf[ADC1_DMA_LEN];

static uint32_t adc_sum[ADC1_DMA_LEN] = {0};
static uint16_t adc_avg[ADC1_DMA_LEN] = {0};
static uint16_t adc_sample_cnt = 0;
int adc_calib_offset = 0;
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc->Instance == ADC1)
    {
        for(uint8_t i = 0; i < ADC1_DMA_LEN; i++)
        {
            adc_sum[i] += adc1_dma_buf[i];
        }

        adc_sample_cnt++;

        if(adc_sample_cnt >= ADC_AVG_SAMPLES)
        {
            for(uint8_t i = 0; i < ADC1_DMA_LEN; i++)
            {
                adc_avg[i] = adc_sum[i] / ADC_AVG_SAMPLES;
                adc_sum[i] = 0;
            }

            adc_sample_cnt = 0;
            adc1_dma_ready = 1;
						adc_calib_offset = 1;
        }
				
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    static uint32_t last_press = 0;

//    if(GPIO_Pin == GPIO_PIN_4)
//    {

//        BBUI_ButtonIRQ();
//    }
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
		TIM2 -> CCR2 = 0;
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

    uint32_t ccr = (uint32_t)(ratio * (float)(999));
		duty_buck = ccr;
		duty_boost = 999 - ccr;
		TIM1 -> CCR1 = duty_buck;
		TIM1 -> CCR2 = duty_boost;
    
}
float duty = 0;
static void PowerStage_Control_OpenLoop(void)
{
    if(!PowerStage.enable)
    {
				PowerStage_SetRatio(0);
        PowerStage_Stop();
        return;
    }

    if(PowerStage.vin < 2.0f)
    {
        PowerStage_Stop();
        PowerStage.enable = 0;
        return;
    }

    if(PowerStage.current > 3.0f || PowerStage.temp > 80.0f)
    {
        PowerStage_Stop();
        PowerStage.enable = 0;
        return;
    }
		PowerStage_Start();
    duty = PowerStage.vset / PowerStage.vin;
    
    PowerStage_SetRatio(PowerStage.vset / 10.0f);
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
static void PowerStage_SoftStart_1kHz(void)
{
    if(PowerStage.enable == 0)
    {
        vref_soft = 0.0f;
        return;
    }

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
    if(PowerStage.enable == 0)
    {
        PowerStage_CVCC_Reset();

        PowerStage.state = BBUI_STATE_OFF;

        PowerStage_Stop();

        return;
    }
		PowerStage_Start();
    if(PowerStage.vin < VIN_MIN_PROTECT)
    {
        PowerStage_CVCC_Reset();

        PowerStage.enable = 0;
        PowerStage.state = BBUI_STATE_FAULT;

        PowerStage_Stop();

        return;
    }

    if(PowerStage.temp > TEMP_MAX_PROTECT)
    {
        PowerStage_CVCC_Reset();

        PowerStage.enable = 0;
        PowerStage.state = BBUI_STATE_FAULT;

        PowerStage_Stop();

        return;
    }

    if(PowerStage.current > PowerStage.iset + I_HARD_MARGIN)
    {
        PowerStage_CVCC_Reset();

        PowerStage.enable = 0;
        PowerStage.state = BBUI_STATE_FAULT;

        PowerStage_Stop();

        return;
    }

    PowerStage_SoftStart_1kHz();

    float ratio_ff = PowerStage_FeedForward_Ratio(PowerStage.vin, vref_soft);

    float err_v = vref_soft - PowerStage.vout;
    float err_i = PowerStage.iset - PowerStage.current;

    float cv_i_new = cv_i + CV_KI * err_v * CTRL_TS;
    float cc_i_new = cc_i + CC_KI * err_i * CTRL_TS;

    cv_i_new = clampf(cv_i_new, CV_I_MIN, CV_I_MAX);
    cc_i_new = clampf(cc_i_new, CC_I_MIN, CC_I_MAX);

    float ratio_cv_unsat = ratio_ff + CV_KP * err_v + cv_i_new;
    float ratio_cc_unsat = ratio_ff + CC_KP * err_i + cc_i_new;

    float ratio_cv = clampf(ratio_cv_unsat, RATIO_MIN, RATIO_MAX);
    float ratio_cc = clampf(ratio_cc_unsat, RATIO_MIN, RATIO_MAX);

    if(!((ratio_cv_unsat > RATIO_MAX && err_v > 0.0f) ||
         (ratio_cv_unsat < RATIO_MIN && err_v < 0.0f)))
    {
        cv_i = cv_i_new;
    }

    if(!((ratio_cc_unsat > RATIO_MAX && err_i > 0.0f) ||
         (ratio_cc_unsat < RATIO_MIN && err_i < 0.0f)))
    {
        cc_i = cc_i_new;
    }

    ratio_cv = clampf(ratio_ff + CV_KP * err_v + cv_i, RATIO_MIN, RATIO_MAX);
    ratio_cc = clampf(ratio_ff + CC_KP * err_i + cc_i, RATIO_MIN, RATIO_MAX);

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

    ratio_out = clampf(ratio_out, RATIO_MIN, RATIO_MAX);
    PowerStage_SetRatio(ratio_out);
}
void Buck_UI_Init(void)
{
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADCEx_Calibration_Start(&hadc2);

    PowerStage_Stop();

    HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc1_dma_buf, ADC1_DMA_LEN);
		HAL_Delay(700);

    BBUI_Init(&PowerStage, &htim2);
		
    HAL_TIM_Base_Start_IT(&htim3);
		while(!adc_calib_offset);
    adc_offset = adc_avg[2];
}
uint32_t lastTime_readTemp = 0;
void handle_temp(){
		if(HAL_GetTick() -  lastTime_readTemp > 2000)
		{
			lastTime_readTemp = HAL_GetTick();
			float temp_new = Read_NTC_Temp() + 32.4f;
			PowerStage.temp = PowerStage.temp * 0.5f + temp_new * 0.5f;
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
  /* Prevent unused argument(s) compilation warning */
  UNUSED(htim);
	float vin_new = Read_Vin(adc_avg[3]);
	float vout_new = Read_Vout(adc_avg[0]);
	float current_new = Read_Current(adc_avg[1]);
	PowerStage.vin = PowerStage.vin * 0.9f + vin_new * 0.1f;
	PowerStage.vout = PowerStage.vout * 0.9f + vout_new * 0.1f;
  PowerStage.current = PowerStage.current * 0.9f + current_new * 0.1f;
	if(PowerStage.enable == 0)
	{
			PowerStage.state = BBUI_STATE_OFF;
			PowerStage_Stop();
			return;
	}

	if(PowerStage.enable == 0)
	{
			PowerStage.state = BBUI_STATE_FAULT;
			PowerStage.enable = 0;
			PowerStage_Stop();
			return;
	}
	PowerStage_CloseLoop_CVCC_1kHz();
	if(PowerStage.current >= PowerStage.iset - 0.05f)
	{
			PowerStage.state = BBUI_STATE_CC;
	}
	else
	{
			PowerStage.state = BBUI_STATE_CV;
	}
	
	if(PowerStage.enable == 0)
	{
			PowerStage_Stop();
			return;
	}


//	BuckBoost_CV_Control_1kHz();
	
//	PowerStage_Control_OpenLoop();
  /* NOTE : This function should not be modified, when the callback is needed,
            the HAL_TIM_PeriodElapsedCallback could be implemented in the user file
   */
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
  /* USER CODE BEGIN 2 */
	Buck_UI_Init();
	PowerStage_Start();
	HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_3);
	TIM1 -> CCR3 = 400;
	HAL_GPIO_WritePin(SWITCH_PORT, SWITCH_PIN, 1);
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
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
  sConfig.SamplingTime = ADC_SAMPLETIME_71CYCLES_5;
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
  htim1.Init.Prescaler = 3;
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
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_3) != HAL_OK)
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

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 71;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim3, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */

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
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12|GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pin : PC13 */
  GPIO_InitStruct.Pin = GPIO_PIN_13;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PC14 PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB10 PB11
                           PB12 PB5 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10|GPIO_PIN_11
                          |GPIO_PIN_12|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PA10 */
  GPIO_InitStruct.Pin = GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB4 PB6 PB7 PB8
                           PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8
                          |GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI4_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI4_IRQn);

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
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
