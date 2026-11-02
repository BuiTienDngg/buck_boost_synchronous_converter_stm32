/* Single power monitor UI: replaces the old two-page button/ADC monitor. */
#include "ui_lcd.h"
#include "st7789_min.h"
#include "analog_monitor.h"
#include "power_stage.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdio.h>

#define LCD_REFRESH_MS 200U
#define FONT_SCALE 2U

static uint32_t s_last_refresh;
static uint8_t s_initialized;

static void draw_row(uint16_t y, const char *value, uint16_t color)
{
    ST7789_FillRect(8U, y, 224U, 24U, ST7789_NAVY);
    ST7789_DrawText(12U, (uint16_t)(y + 3U), value,
                    color, ST7789_NAVY, FONT_SCALE);
}

static void draw_number(uint16_t y, const char *name, float val, const char *unit,
                        uint16_t color)
{
    char out[32];
    int32_t n;
    uint32_t positive;
    if (!(val > -100000.0f && val < 100000.0f)) {
        draw_row(y, "VALUE ERROR", ST7789_RED);
        return;
    }
    n = (int32_t)(val * 100.0f + (val >= 0.0f ? 0.5f : -0.5f));
    positive = (n < 0) ? (uint32_t)(-n) : (uint32_t)n;
    (void)snprintf(out, sizeof(out), "%s%c%lu.%02lu%s", name,
                   n < 0 ? '-' : ' ',
                   (unsigned long)(positive / 100U),
                   (unsigned long)(positive % 100U), unit);
    draw_row(y, out, color);
}

static const char *fault_string(BB_Fault f)
{
    switch (f) {
        case BB_FAULT_ADC_STALE: return "ADC NO DATA";
        case BB_FAULT_INPUT_RANGE: return "VIN LIMIT";
        case BB_FAULT_OUTPUT_OVERVOLT: return "VOUT LIMIT";
        case BB_FAULT_OUTPUT_OVERCURRENT: return "IOUT LIMIT";
        case BB_FAULT_BOOST_INPUT_TOO_HIGH: return "BOOST VIN HI";
        case BB_FAULT_PWM_INIT: return "TIM1 CONFIG";
        default: return "UNKNOWN FAULT";
    }
}

void LCD_Init(void)
{
    s_initialized = 1U;
    ST7789_FillScreen(ST7789_NAVY);
    ST7789_DrawText(12U, 9U, "BUCK BOOST PSU",
                    ST7789_CYAN, ST7789_NAVY, FONT_SCALE);
    ST7789_FillRect(8U, 35U, 224U, 2U, ST7789_GRAY);
    s_last_refresh = HAL_GetTick() - LCD_REFRESH_MS;
    LCD_run();
}

void LCD_run(void)
{
    char text[32];
    uint32_t now = HAL_GetTick();
    AnalogMonitor_Data adc;
    BB_Status bb;
    if (!s_initialized || (uint32_t)(now - s_last_refresh) < LCD_REFRESH_MS) return;
    s_last_refresh = now;
    AnalogMonitor_GetSnapshot(&adc);
    bb = PowerStage_GetStatus();
    (void)snprintf(text, sizeof(text), "MODE: %s", bb.mode == BB_MODE_BUCK ? "BUCK" : "BOOST");
    draw_row(44U, text, ST7789_CYAN);
    (void)snprintf(text, sizeof(text), "STATE: %s",
                   bb.state == BB_STATE_RUNNING ? "ON" :
                   (bb.state == BB_STATE_FAULT ? "FAULT" : "OFF"));
    draw_row(72U, text, bb.state == BB_STATE_RUNNING ? ST7789_GREEN : ST7789_YELLOW);
    if (adc.adc1_valid) {
        draw_number(102U, "VIN :", adc.vin_scaled, "V", ST7789_WHITE);
        draw_number(130U, "VOUT:", adc.vout_v, "V", ST7789_GREEN);
        draw_number(158U, "IOUT:", adc.current_a, "A", ST7789_WHITE);
    } else {
        draw_row(102U, "WAIT ADC1", ST7789_YELLOW);
        draw_row(130U, "VOUT: --", ST7789_YELLOW);
        draw_row(158U, "IOUT: --", ST7789_YELLOW);
    }
    if (adc.temp_fet_valid) {
        /* No NTC divider/Beta/R25 given; display measured NTC voltage,
         * never a fabricated temperature in Celsius. */
        draw_number(186U, "FET :", adc.temp_fet_v, "V", ST7789_YELLOW);
    } else {
        draw_row(186U, "FET : --V", ST7789_YELLOW);
    }
    (void)snprintf(text, sizeof(text), "DUTY:%3u%% / %3u%%",
                   (unsigned)bb.duty_applied_pct,
                   (unsigned)bb.duty_request_pct);
    draw_row(216U, text, ST7789_CYAN);
    if (bb.state == BB_STATE_FAULT) {
        draw_row(251U, fault_string(bb.fault), ST7789_RED);
        draw_row(281U, "BTN3: RESET", ST7789_YELLOW);
    } else {
        draw_row(251U, "BTN1: ON/OFF", ST7789_WHITE);
        draw_row(281U, "BTN2: BUCK/BOOST", ST7789_WHITE);
    }
}
