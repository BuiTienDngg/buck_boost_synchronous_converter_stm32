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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "ST7735_SPI.h"
#include "fonts.h"
#include <stdio.h>
#include <math.h>
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
#define NTC_R0              3000.0f
#define NTC_BETA            3950.0f
#define NTC_T0_K            298.15f

#define SET_V_MIN           1.0f
#define SET_V_MAX           30.0f
#define SET_V_STEP          0.1f

#define ENC_COUNT_PER_STEP  4

#define POWERSTAGE_DUTY_MIN       0.15f
#define POWERSTAGE_DUTY_MAX       0.85f
#define POWERSTAGE_RATIO_MIN    0.15f
#define POWERSTAGE_RATIO_MAX    0.85f
#define CTRL_TS                 0.001f      // 1 kHz
#define CV_KP                   0.03f
#define CV_KI                   2.0f

#define CURRENT_MAX 	5.0f		// AMPE
#define TEMP_MAX 			40.0f   //*C
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
typedef struct
{
    float vin;
    float vout;
    float current;
    float temp;
    float vset;
    uint8_t enable;
} PowerStage_t;
static PowerStage_t PowerStage =
{
    .vin = 0,
    .vout = 0,
    .current = 0,
    .temp = 0,
    .vset = 5.0f,
    .enable = 0
};
static volatile uint16_t adc_current_raw = 0;
static volatile uint8_t adc_current_ready = 0;

static int16_t enc_last = 0;
static int32_t enc_acc = 0;

static uint32_t t_adc = 0;
static uint32_t t_lcd = 0;
static uint8_t PowerStage_pwm_running = 0;
#define ADC1_DMA_LEN 3

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

static float Read_NTC_Temp(void)
{
    uint16_t raw = ADC_Read_Channel(&hadc2, ADC_CHANNEL_3);

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
#define CURRENT_GAIN        12.0f // 1.884
#define SHUNT_R             0.01853f
uint16_t adc_offset = 0;
static float Read_Current(uint16_t adc_current)
{
    float v_adc = (adc_current - adc_offset) * 3.3f / 4095.0f;
    float current = (v_adc) / CURRENT_GAIN / SHUNT_R;
    if(current < 0.0f)
        current = 0.0f;
    return current;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if(hadc->Instance == ADC1)
    {
        adc1_dma_ready = 1;
    }
}
static void Encoder_Update(void)
{
    int16_t enc_now = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);
    int16_t diff = enc_now - enc_last;

    enc_last = enc_now;

    enc_acc += diff;

    while(enc_acc >= ENC_COUNT_PER_STEP)
    {
        enc_acc -= ENC_COUNT_PER_STEP;
        PowerStage.vset += SET_V_STEP;

        if(PowerStage.vset > SET_V_MAX)
            PowerStage.vset = SET_V_MAX;
    }

    while(enc_acc <= -ENC_COUNT_PER_STEP)
    {
        enc_acc += ENC_COUNT_PER_STEP;
        PowerStage.vset -= SET_V_STEP;

        if(PowerStage.vset < SET_V_MIN)
            PowerStage.vset = SET_V_MIN;
    }
}
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if(GPIO_Pin == GPIO_PIN_4)
    {
        PowerStage.enable = !PowerStage.enable;
    }
}
static void PowerStage_Start(void)
{
    if(PowerStage_pwm_running)
        return;

    HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_1);
		HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_2);
    HAL_TIMEx_PWMN_Start(&htim1, TIM_CHANNEL_2);
    PowerStage_pwm_running = 1;
}

static void PowerStage_Stop(void)
{
    if(!PowerStage_pwm_running)
        return;
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

    uint32_t ccr = (uint32_t)(ratio * (float)(999 + 1));
		duty_buck = ccr;
		duty_boost = 1000 - ccr;
		TIM1 -> CCR1 = duty_buck;
		TIM1 -> CCR2 = duty_boost;
//    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, duty_buck);
//		__HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_2, duty_boost);
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
static void LCD_DrawBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    ST7735_WriteString(4, 2, "BUCK MODE", Font_7x10, ST7735_YELLOW, ST7735_BLACK);

    drawHline(0, 15, 160, ST7735_BLUE);

    ST7735_WriteString(4, 22, "Vin :", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(4, 40, "Vout:", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(4, 58, "Iout:", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(4, 76, "Temp:", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(4, 94, "Set :", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(4, 112, "OUT :", Font_7x10, ST7735_WHITE, ST7735_BLACK);
}

static void LCD_PrintValue(uint16_t x, uint16_t y, char *str, uint16_t color)
{
    ST7735_FillRectangle(x, y, 90, 12, ST7735_BLACK);
    ST7735_WriteString(x, y, str, Font_7x10, color, ST7735_BLACK);
}

static void LCD_Update(void)
{
    char buf[24];

    fmt_float(buf, PowerStage.vin, 2, " V");
    LCD_PrintValue(55, 22, buf, ST7735_CYAN);

    fmt_float(buf, PowerStage.vout, 2, " V");
    LCD_PrintValue(55, 40, buf, ST7735_GREEN);

    fmt_float(buf, PowerStage.current, 2, " A");
    LCD_PrintValue(55, 58, buf, ST7735_YELLOW);

    fmt_float(buf, PowerStage.temp, 1, " C");
    LCD_PrintValue(55, 76, buf, ST7735_MAGENTA);

    fmt_float(buf, PowerStage.vset, 1, " V");
    LCD_PrintValue(55, 94, buf, ST7735_WHITE);

    ST7735_FillRectangle(55, 112, 90, 12, ST7735_BLACK);

    if(PowerStage.enable)
        ST7735_WriteString(55, 112, "ON", Font_7x10, ST7735_GREEN, ST7735_BLACK);
    else
        ST7735_WriteString(55, 112, "OFF", Font_7x10, ST7735_RED, ST7735_BLACK);
}

void Buck_UI_Init(void)
{

    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADCEx_Calibration_Start(&hadc2);

    HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);

    enc_last = (int16_t)__HAL_TIM_GET_COUNTER(&htim2);

    PowerStage_Stop();
	
		HAL_ADC_Start_DMA(&hadc1, (uint32_t*)adc1_dma_buf, ADC1_DMA_LEN);
    ST7735_Init();
    ST7735_FillScreen(ST7735_BLACK);

    LCD_DrawBase();
    LCD_Update();
		
		HAL_TIM_Base_Start_IT(&htim3);
		HAL_Delay(500);
		adc_offset = adc1_dma_buf[2];
}

void handle_temp(){
		float temp_new = Read_NTC_Temp() + 32.4f;
		PowerStage.temp = PowerStage.temp * 0.5f + temp_new * 0.5f;
		if(PowerStage.temp >= 60.0f)
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 1);
		}
		else if(PowerStage.temp < 50.0f)
		{
			HAL_GPIO_WritePin(GPIOC, GPIO_PIN_15, 0);
		}
}
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* Prevent unused argument(s) compilation warning */
  UNUSED(htim);
	float vin_new = Read_Vin(adc1_dma_buf[1]);
	float vout_new = Read_Vout(adc1_dma_buf[0]);
	float current_new = Read_Current(adc1_dma_buf[2]);
	PowerStage.vin = PowerStage.vin * 0.8f + vin_new * 0.2f;
	PowerStage.vout = PowerStage.vout * 0.8f + vout_new * 0.2f;
  PowerStage.current = PowerStage.current * 0.8f + current_new * 0.2f;
//	BuckBoost_CV_Control_1kHz();
	PowerStage_Control_CloseLoop();
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
  /* USER CODE BEGIN 2 */
	Buck_UI_Init();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
		Encoder_Update();
		if(HAL_GetTick() - t_lcd >= 200)
    {
        t_lcd = HAL_GetTick();
				handle_temp();
        LCD_Update();
    }
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
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
  hadc1.Init.NbrOfConversion = 3;
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
  sConfig.Channel = ADC_CHANNEL_2;
  sConfig.Rank = ADC_REGULAR_RANK_3;
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
  sConfig.Channel = ADC_CHANNEL_3;
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
  htim1.Init.Prescaler = 0;
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
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM2;
  sConfigOC.Pulse = 10;
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
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC13 PC14 PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB10 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_10;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pin : PB4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
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
