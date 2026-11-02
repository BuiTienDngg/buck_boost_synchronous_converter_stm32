#ifndef INPUT_ENCODER_H
#define INPUT_ENCODER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* EC11 detents may generate 2 or 4 TIM2 counts; change after observing. */
#ifndef ENCODER_COUNTS_PER_DETENT
#define ENCODER_COUNTS_PER_DETENT 4
#endif

HAL_StatusTypeDef Encoder_Init(TIM_HandleTypeDef *htim);
void encoder_hander(void);  /* exact name requested */
void encoder_handler(void); /* optional conventional spelling */
int32_t Encoder_GetPosition(void);   /* accumulated detents */
int32_t Encoder_GetRawTicks(void);   /* signed accumulated quadrature counts */
int32_t Encoder_TakeDelta(void);     /* accumulated detents since last read */
uint8_t Encoder_IsSwitchPressed(void); /* PB4 (debounced by button_handle) */

#endif
