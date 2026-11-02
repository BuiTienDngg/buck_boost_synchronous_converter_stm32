#include "input_ui_port.h"
#include "input_buttons.h"
#include "input_encoder.h"
#include "st7789_min.h"
#include "ui_lcd.h"

/* Driver tu cau hinh phan cung, khong can bien hspi1/htim2 tu CubeMX.
 * Goi SAU MX_GPIO_Init() va SAU cac khoi tao ngoai vi hien co.
 * TIM2 va SPI1 phai duoc danh rieng cho encoder va man hinh.
 */
static TIM_HandleTypeDef s_encoder_tim;
static SPI_HandleTypeDef s_display_spi;

static void lcd_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_AFIO_CLK_ENABLE();

    /* PB4 = NJTRST: free JTAG pins, retain SWD on PA13/PA14. */
    __HAL_AFIO_REMAP_SWJ_NOJTAG();

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_10,
                      GPIO_PIN_SET);
    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static void input_gpio_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7 |
               GPIO_PIN_8 | GPIO_PIN_13;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);
}

static HAL_StatusTypeDef display_spi_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_SPI1_CLK_ENABLE();

    s_display_spi.Instance = SPI1;
    s_display_spi.Init.Mode = SPI_MODE_MASTER;
    s_display_spi.Init.Direction = SPI_DIRECTION_1LINE;
    s_display_spi.Init.DataSize = SPI_DATASIZE_8BIT;
    s_display_spi.Init.CLKPolarity = SPI_POLARITY_LOW;
    s_display_spi.Init.CLKPhase = SPI_PHASE_1EDGE;
    s_display_spi.Init.NSS = SPI_NSS_SOFT;
    s_display_spi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    s_display_spi.Init.FirstBit = SPI_FIRSTBIT_MSB;
    s_display_spi.Init.TIMode = SPI_TIMODE_DISABLE;
    s_display_spi.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    s_display_spi.Init.CRCPolynomial = 7;
    if (HAL_SPI_Init(&s_display_spi) != HAL_OK) return HAL_ERROR;

    /* F103: SPI1 SCK = PA5, MOSI = PA7; configure after HAL MSP init. */
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);
    return HAL_OK;
}

static HAL_StatusTypeDef encoder_timer_init(void)
{
    GPIO_InitTypeDef gpio = {0};
    TIM_Encoder_InitTypeDef encoder = {0};
    __HAL_RCC_TIM2_CLK_ENABLE();

    s_encoder_tim.Instance = TIM2;
    s_encoder_tim.Init.Prescaler = 0;
    s_encoder_tim.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_encoder_tim.Init.Period = 65535;
    s_encoder_tim.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_encoder_tim.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    encoder.EncoderMode = TIM_ENCODERMODE_TI12;
    encoder.IC1Polarity = TIM_ICPOLARITY_RISING;
    encoder.IC1Selection = TIM_ICSELECTION_DIRECTTI;
    encoder.IC1Prescaler = TIM_ICPSC_DIV1;
    encoder.IC1Filter = 8;
    encoder.IC2Polarity = TIM_ICPOLARITY_RISING;
    encoder.IC2Selection = TIM_ICSELECTION_DIRECTTI;
    encoder.IC2Prescaler = TIM_ICPSC_DIV1;
    encoder.IC2Filter = 8;
    if (HAL_TIM_Encoder_Init(&s_encoder_tim, &encoder) != HAL_OK)
        return HAL_ERROR;

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);
    return Encoder_Init(&s_encoder_tim);
}

HAL_StatusTypeDef InputUI_Init(void)
{
    /* Configure interrupt handlers in stm32f1xx_it.c before enabling NVIC. */
    HAL_NVIC_DisableIRQ(EXTI4_IRQn);
    HAL_NVIC_DisableIRQ(EXTI9_5_IRQn);
    HAL_NVIC_DisableIRQ(EXTI15_10_IRQn);
    lcd_gpio_init();
    input_gpio_init();
    if (display_spi_init() != HAL_OK) return HAL_ERROR;
    if (encoder_timer_init() != HAL_OK) return HAL_ERROR;

    Button_Init();
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 |
                             GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_13);
    HAL_NVIC_ClearPendingIRQ(EXTI4_IRQn);
    HAL_NVIC_ClearPendingIRQ(EXTI9_5_IRQn);
    HAL_NVIC_ClearPendingIRQ(EXTI15_10_IRQn);
    HAL_NVIC_SetPriority(EXTI4_IRQn, 5, 0);
    HAL_NVIC_SetPriority(EXTI9_5_IRQn, 5, 0);
    HAL_NVIC_SetPriority(EXTI15_10_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(EXTI4_IRQn);
    HAL_NVIC_EnableIRQ(EXTI9_5_IRQn);
    HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

    ST7789_Init(&s_display_spi);
    LCD_Init();
    return HAL_OK;
}
