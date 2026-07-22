#ifndef __BUTTON_H
#define __BUTTON_H

#include "main.h"


#define BUTTON_NUM          5
#define DEBOUNCE_TIME       20


typedef enum
{
    BTN_PB5 = 0,
    BTN_PB6,
    BTN_PB7,
    BTN_PB8,
    BTN_PB9

}BUTTON_t;


void Button_Init(void);

void Button_EXTI_Callback(uint16_t GPIO_Pin);

uint8_t Button_IsPressed(BUTTON_t button);

uint8_t Button_GetEvent(BUTTON_t button);

#endif