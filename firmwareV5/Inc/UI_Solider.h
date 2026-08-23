#ifndef UI_SOLIDER_H
#define UI_SOLIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
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
uint8_t UI_Solider_IsActive(void);
uint8_t UI_Solider_IsSleeping(void);

typedef enum
{
    UI_SOLDER_EVENT_NONE           = 0x00U,
    UI_SOLDER_EVENT_ENTER          = 0x01U,
    UI_SOLDER_EVENT_SLEEP_ENTER    = 0x02U,
    UI_SOLDER_EVENT_TARGET_REACHED = 0x04U
} UI_Solider_Event_t;

uint8_t UI_Solider_GetEvents(void);
void UI_Solider_SetSleep(uint8_t sleep, float sleep_temp);
void UI_Solider_SetTheme(uint8_t light);

void UI_Solider_SetData(float tip_temp,
                        float current,
                        float fet_temp,
                        float power);

float UI_Solider_GetSetTemp(void);

void UI_Solider_SetPreset(uint8_t id, float temp);
void UI_Solider_SelectPreset(uint8_t id);

/* dir = +1 / -1, each step = 5 degC */
void UI_Solider_EncoderAdjust(int8_t dir);

void UI_Solider_Task(uint8_t force);


/* =========================================================
 * SOLDER CONTROL
 * ========================================================= */

void Solider_PID_Enable(uint8_t enable);
void Solider_PID_Task(float set_adc);
float Solider_ADC_ToTemp(uint16_t adc_raw);

extern volatile float solider_temp_raw;
extern volatile float solider_pid_power;
extern volatile uint16_t solider_pwm_ccr;

extern volatile float measured_temp;
extern volatile float frev_measured_temp;
extern volatile float set_temp;

#ifdef __cplusplus
}
#endif

#endif
