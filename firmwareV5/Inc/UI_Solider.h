#ifndef UI_SOLIDER_H
#define UI_SOLIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>


/* =========================================================
 * SOLDER UI DATA
 * ========================================================= */

typedef struct
{
    float tip_temp;
    float current;
    float fet_temp;
    float power;

    /* Buck-Boost output voltage for SOLDER graph */
    float vout;

    float set_temp;
    float preset[3];

    uint8_t active_preset;

} UI_Solider_Data_t;


/* =========================================================
 * SOLDER UI
 * ========================================================= */

void UI_Solider_Init(void);

void UI_Solider_Enter(void);
void UI_Solider_Exit(void);

uint8_t UI_Solider_IsActive(void);
uint8_t UI_Solider_IsSleeping(void);


/* =========================================================
 * SOLDER EVENTS / BUZZER
 * ========================================================= */

typedef enum
{
    UI_SOLDER_EVENT_NONE            = 0x00U,
    UI_SOLDER_EVENT_ENTER           = 0x01U,
    UI_SOLDER_EVENT_SLEEP_ENTER     = 0x02U,
    UI_SOLDER_EVENT_TARGET_REACHED  = 0x04U

} UI_Solider_Event_t;


/*
 * Read and clear pending SOLDER events.
 */
uint8_t UI_Solider_GetEvents(void);


/* =========================================================
 * SLEEP / THEME
 * ========================================================= */

void UI_Solider_SetSleep(uint8_t sleep,
                         float sleep_temp);

void UI_Solider_SetTheme(uint8_t light);


/* =========================================================
 * LIVE DATA
 *
 * vout = PowerStage.vout
 * ========================================================= */

void UI_Solider_SetData(float tip_temp,
                        float current,
                        float fet_temp,
                        float power,
                        float vout);


/* =========================================================
 * TEMPERATURE SETPOINT / PRESETS
 * ========================================================= */

float UI_Solider_GetSetTemp(void);

void UI_Solider_SetPreset(uint8_t id,
                          float temp);

void UI_Solider_SelectPreset(uint8_t id);


/*
 * dir:
 *   +1 -> increase
 *   -1 -> decrease
 *
 * Current SOLDER UI step = 5 degC.
 */
void UI_Solider_EncoderAdjust(int8_t dir);


/* =========================================================
 * SOLDER UI TASK
 * ========================================================= */

void UI_Solider_Task(uint8_t force);


/* =========================================================
 * SOLDER CONTROL / PID
 * ========================================================= */

void Solider_PID_Enable(uint8_t enable);

void Solider_PID_Task(float set_adc);

float Solider_ADC_ToTemp(uint16_t adc_raw);


/* =========================================================
 * SOLDER DEBUG / EXTERNAL VALUES
 * ========================================================= */

extern volatile float solider_temp_raw;
extern volatile float solider_pid_power;

extern volatile uint16_t solider_pwm_ccr;

extern volatile float measured_temp;
extern volatile float frev_measured_temp;
extern volatile float set_temp;


#ifdef __cplusplus
}
#endif

#endif /* UI_SOLIDER_H */
