#ifndef INPUT_UI_PORT_H
#define INPUT_UI_PORT_H

#include "stm32f1xx_hal.h"

/* Khoi tao GPIO, SPI1 (ST7789), TIM2 encoder, 6 EXTI, va giao dien. */
HAL_StatusTypeDef InputUI_Init(void);

#endif
