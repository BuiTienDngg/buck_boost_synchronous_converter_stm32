#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>

typedef enum
{
    BBUI_STATE_OFF = 0,
    BBUI_STATE_CV,
    BBUI_STATE_CC,
    BBUI_STATE_FAULT
} BBUI_State_t;

typedef enum
{
    BB_START_SOFT = 0,
    BB_START_HARD = 1
} BBUI_StartMode_t;

typedef struct
{
    float voltage;
    float current;
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

    /* Compatibility with existing PowerStage initializer */
    uint8_t batt_cells;
    uint8_t active_preset;
    uint8_t mqtt_enable;

    float max_input_power;
    BBUI_StartMode_t start_mode;

    BBUI_Preset_t preset[3];
} BBUI_Data_t;

/*
 * TIM2 encoder -> VSET
 * TIM4 encoder -> ISET
 *
 * Encoder push:
 *   PB4 -> voltage digit
 *   PB8 -> current digit
 *
 * PB9 -> Output ON/OFF
 */
void BBUI_Init(BBUI_Data_t *data,
               TIM_HandleTypeDef *htim_vset,
               TIM_HandleTypeDef *htim_iset);

void BBUI_Task(void);
void BBUI_ForceRefresh(void);
void BBUI_ButtonIRQ(void);

#ifdef __cplusplus
}
#endif

#endif
