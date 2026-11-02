#include "ui_lcd.h"
#include "st7789_min.h"
#include "power_stage.h"
#include "analog_monitor.h"
#include "stm32f1xx_hal.h"
#include <stdio.h>

/* ST7789 RGB565, color inversion OFF in st7789_min.h. */
#define LCD_BLACK  0x0000U
#define LCD_RED    0xF800U
#define LCD_BLUE   0x001FU
#define LCD_PINK   0xF81FU
#define LCD_WHITE  0xFFFFU
#define LCD_GRAY   0xAD55U
#define LCD_REFRESH_MS 200U

static uint8_t s_lcd_ready;
static uint32_t s_last_draw_ms;
/* FAULT_LCD_V1: matching fault messages on local LCD and USB PC app. */
static const char *psu_fault_name(PSU_Error error) {
    switch(error) {
    case PSU_ERR_ADC:    return "ADC STALE";
    case PSU_ERR_VIN:    return "VIN RANGE";
    case PSU_ERR_OVP:    return "OVP";
    case PSU_ERR_OCP:    return "OCP";
    case PSU_ERR_TIMERS: return "TIMER CFG";
    case PSU_ERR_NONE:   return "NONE";
    default:             return "UNKNOWN";
    }
}

static void format_measurement(char *out, unsigned length, float value, const char *unit)
{
    long scaled;
    unsigned long magnitude;
    if (!(value >= -999.0f && value <= 999.0f)) {
        (void)snprintf(out, length, "--.--%s", unit);
        return;
    }
    scaled = (long)(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
    magnitude = (unsigned long)(scaled < 0L ? -scaled : scaled);
    (void)snprintf(out, length, "%s%lu.%02lu%s",
                   scaled < 0L ? "-" : "", magnitude / 100UL,
                   magnitude % 100UL, unit);
}

/* Only redraw the dynamic number region. Font scale 4 is supported by v8 driver. */
static void value_row(uint16_t y, float value, const char *unit, uint16_t color)
{
    char buffer[24];
    format_measurement(buffer, sizeof(buffer), value, unit);
    ST7789_FillRect(90U, y, 224U, 34U, LCD_BLACK);
    ST7789_DrawText(90U, y, buffer, color, LCD_BLACK, 4U);
}

void LCD_Init(void)
{
    ST7789_FillScreen(LCD_BLACK);
    ST7789_DrawText(8U, 4U, "SMART PSU", LCD_PINK, LCD_BLACK, 2U);
    ST7789_DrawText(238U, 4U, "CV/CC", LCD_WHITE, LCD_BLACK, 2U);
    ST7789_FillRect(0U, 25U, 320U, 2U, LCD_BLUE);

    ST7789_DrawText(10U, 40U, "VOUT", LCD_RED, LCD_BLACK, 2U);
    ST7789_DrawText(10U, 87U, "IOUT", LCD_BLUE, LCD_BLACK, 2U);
    ST7789_DrawText(10U, 134U, "POWER", LCD_PINK, LCD_BLACK, 2U);
    ST7789_FillRect(0U, 165U, 320U, 2U, LCD_BLUE);

    s_lcd_ready = 1U;
    s_last_draw_ms = HAL_GetTick() - LCD_REFRESH_MS;
    LCD_run();
}

void LCD_run(void)
{
    PSU_Status ps;
    AnalogMonitor_Data adc;
    char left[32];
    char right[32];
    float power;
    uint32_t now, elapsed;
    uint8_t col;
    static const uint8_t voltage_digit_cols[4] = {5U, 6U, 8U, 9U};
    static const uint8_t current_digit_cols[3] = {6U, 8U, 9U};

    if (!s_lcd_ready) return;
    now = HAL_GetTick();
    if ((uint32_t)(now - s_last_draw_ms) < LCD_REFRESH_MS) return;
    s_last_draw_ms = now;

    ps = PowerStage_GetStatus();
    AnalogMonitor_GetSnapshot(&adc);
    power = ps.vout_v * ps.iout_a;
    if (!(power >= 0.0f)) power = 0.0f;

    /* Three highest-priority readouts; all the same, largest font. */
    value_row(31U, ps.vout_v, "V", LCD_RED);
    value_row(78U, ps.iout_a, "A", LCD_BLUE);
    value_row(125U, power, "W", LCD_PINK);

    /* Two editable setpoints and underline under the active digit. */
    ST7789_FillRect(8U, 170U, 304U, 22U, LCD_BLACK);
    (void)snprintf(left, sizeof(left), "VSET %02u.%02uV",
                   (unsigned)(ps.vset_cv / 100U),
                   (unsigned)(ps.vset_cv % 100U));
    (void)snprintf(right, sizeof(right), "ISET %02u.%02uA",
                   (unsigned)(ps.iset_ca / 100U),
                   (unsigned)(ps.iset_ca % 100U));
    ST7789_DrawText(8U, 171U, left,
                    ps.editing == PSU_EDIT_V ? LCD_RED : LCD_WHITE, LCD_BLACK, 2U);
    ST7789_DrawText(168U, 171U, right,
                    ps.editing == PSU_EDIT_I ? LCD_BLUE : LCD_WHITE, LCD_BLACK, 2U);
    if (ps.editing == PSU_EDIT_V) {
        col = voltage_digit_cols[ps.digit < 4U ? ps.digit : 0U];
        ST7789_FillRect((uint16_t)(8U + col * 12U), 188U, 11U, 2U, LCD_PINK);
    } else {
        col = current_digit_cols[ps.digit < 3U ? ps.digit : 0U];
        ST7789_FillRect((uint16_t)(168U + col * 12U), 188U, 11U, 2U, LCD_PINK);
    }

    /* VIN and FET NTC sensor VOLTAGE (not degrees Celsius yet). */
    ST7789_FillRect(8U, 196U, 304U, 18U, LCD_BLACK);
    if (ps.vin_v >= 0.0f && ps.vin_v < 100.0f) {
        unsigned vin_tenths = (unsigned)(ps.vin_v * 10.0f + 0.5f);
        (void)snprintf(left, sizeof(left), "VIN %02u.%01uV",
                       vin_tenths / 10U, vin_tenths % 10U);
    } else {
        (void)snprintf(left, sizeof(left), "VIN --.-V");
    }
    if (adc.temp_fet_valid) {
        char converted[24];
        format_measurement(converted, sizeof(converted), adc.temp_fet_v, "V");
        (void)snprintf(right, sizeof(right), "FET %s", converted);
    } else {
        (void)snprintf(right, sizeof(right), "FET --.--V");
    }
    ST7789_DrawText(8U, 196U, left, LCD_GRAY, LCD_BLACK, 2U);
    ST7789_DrawText(168U, 196U, right, LCD_GRAY, LCD_BLACK, 2U);

    /* State, topology, and cumulative ON runtime. */
    ST7789_FillRect(8U, 219U, 304U, 19U, LCD_BLACK);
    if (ps.state == PSU_FAULT) {
        (void)snprintf(left, sizeof(left), "FAULT %s", psu_fault_name(ps.error));
        ST7789_DrawText(8U, 219U, left, LCD_RED, LCD_BLACK, 2U);
        ST7789_DrawText(200U, 219U, "BTN1 CLR", LCD_PINK, LCD_BLACK, 2U);
    } else {
        (void)snprintf(left, sizeof(left), "%s %s",
                       ps.state == PSU_ON ? "ON" : "OFF",
                       ps.mode == PSU_BUCK ? "BUCK" : "BOOST");
        ST7789_DrawText(8U, 219U, left, LCD_PINK, LCD_BLACK, 2U);
        elapsed = ps.running_seconds;
        (void)snprintf(right, sizeof(right), "%02lu:%02lu:%02lu",
                       (unsigned long)(elapsed / 3600UL),
                       (unsigned long)((elapsed / 60UL) % 60UL),
                       (unsigned long)(elapsed % 60UL));
        ST7789_DrawText(210U, 219U, right, LCD_WHITE, LCD_BLACK, 2U);
    }
}
