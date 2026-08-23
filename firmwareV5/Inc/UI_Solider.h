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
