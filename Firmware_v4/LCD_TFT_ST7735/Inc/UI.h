#ifndef __UI_H
#define __UI_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

typedef enum
{
    BBUI_STATE_OFF = 0,
    BBUI_STATE_CV,
    BBUI_STATE_CC,
    BBUI_STATE_FAULT
} BBUI_State_t;

typedef struct
{
    float vset;
    float iset;
} BBUI_Preset_t;

typedef struct
{
    float vin;
    float vout;
    float current;
    float temp;

    float vset;
    float iset;

    uint8_t enable;
    BBUI_State_t state;

    uint8_t batt_cells;
    uint8_t active_preset;
    uint8_t mqtt_enable;

    BBUI_Preset_t preset[3];
} BBUI_Data_t;

void BBUI_Init(BBUI_Data_t *data, TIM_HandleTypeDef *htim_encoder);
void BBUI_Task(void);
void BBUI_ButtonIRQ(void);
void BBUI_ForceRefresh(void);

void BBUI_LoadFromFlash(void);
void BBUI_SaveToFlash(void);

#endif