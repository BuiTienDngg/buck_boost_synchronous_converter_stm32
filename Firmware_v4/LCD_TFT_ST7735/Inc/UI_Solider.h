#ifndef __UI_SOLIDER_H
#define __UI_SOLIDER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef struct
{
    float tip_temp;
    float current;
    float fet_temp;
    float power;

    float set_temp;

    float preset[3];
    uint8_t active_preset;
} UI_Solider_Data_t;

void UI_Solider_Init(void);

void UI_Solider_Enter(void);
void UI_Solider_Exit(void);

void UI_Solider_Task(uint8_t force);

void UI_Solider_SetData(float tip_temp,
                        float current,
                        float fet_temp,
                        float power);

float UI_Solider_GetSetTemp(void);

void UI_Solider_EncoderAdjust(int8_t dir);

void UI_Solider_SelectPreset(uint8_t id);
void UI_Solider_SetPreset(uint8_t id, float temp);

uint8_t UI_Solider_IsActive(void);

#endif