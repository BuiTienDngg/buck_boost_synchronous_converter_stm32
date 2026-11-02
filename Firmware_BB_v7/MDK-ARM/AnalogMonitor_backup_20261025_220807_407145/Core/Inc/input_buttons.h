#ifndef INPUT_BUTTONS_H
#define INPUT_BUTTONS_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* 5 physical buttons + EC11's integrated push switch on PB4. */
typedef enum {
    BUTTON_1 = 0,             /* PB13 */
    BUTTON_2,                 /* PB8  */
    BUTTON_3,                 /* PB7  */
    BUTTON_4,                 /* PB6  */
    BUTTON_5,                 /* PB5  */
    BUTTON_ENCODER_SWITCH,    /* PB4  */
    BUTTON_COUNT
} ButtonId;

#ifndef BUTTON_DEBOUNCE_MS
#define BUTTON_DEBOUNCE_MS 25U
#endif

void Button_Init(void);
void button_handle(void);
void Buttons_OnExti(uint16_t gpio_pin); /* call from HAL_GPIO_EXTI_Callback */
uint8_t Button_IsPressed(ButtonId id);
uint8_t Button_TakePressedEvent(ButtonId id);  /* one-shot rising event */
uint8_t Button_TakeReleasedEvent(ButtonId id); /* one-shot release event */

#endif
