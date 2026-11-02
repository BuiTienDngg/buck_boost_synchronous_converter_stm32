#include "ui_lcd.h"
#include "st7789_min.h"
#include "input_buttons.h"
#include "input_encoder.h"
#include "analog_monitor.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

#define LCD_INPUT_REFRESH_MS   60U
#define LCD_ANALOG_REFRESH_MS  250U
#define LABEL_X 12U
#define FONT_SCALE 2U

static uint8_t s_last_pressed[BUTTON_COUNT];
static int32_t s_last_position;
static uint32_t s_last_refresh;
static uint8_t s_initialized;
static uint8_t s_page; /* 0: buttons/encoder; 1: analog monitor */
static uint8_t s_last_btn5;

static void draw_row(uint16_t y, const char *text, uint16_t color)
{
    ST7789_FillRect(8U, y, 224U, 23U, ST7789_NAVY);
    ST7789_DrawText(LABEL_X, (uint16_t)(y + 3U), text,
                    color, ST7789_NAVY, FONT_SCALE);
}

static void reset_cache(void)
{
    uint32_t i;
    for (i = 0U; i < (uint32_t)BUTTON_COUNT; ++i) s_last_pressed[i] = 0xFFU;
    s_last_position = INT32_MIN;
    s_last_refresh = HAL_GetTick() - LCD_ANALOG_REFRESH_MS;
}

static void draw_background(void)
{
    ST7789_FillScreen(ST7789_NAVY);
    ST7789_DrawText(12U, 12U, s_page ? "ADC MONITOR" : "INPUT MONITOR",
                    ST7789_CYAN, ST7789_NAVY, FONT_SCALE);
    ST7789_FillRect(8U, 43U, 224U, 2U, ST7789_GRAY);
    reset_cache();
}

/* Integer fixed-point formatting: no embedded printf float-linking needed. */
static void draw_fixed(uint16_t y, const char *prefix, float value,
                       const char *suffix, uint16_t color)
{
    char line[28];
    int32_t scaled;
    uint32_t abs_scaled;
    char sign;
    if (value > 100000.0f || value < -100000.0f) {
        draw_row(y, "VALUE OVERFLOW", ST7789_YELLOW);
        return;
    }
    scaled = (int32_t)(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
    sign = scaled < 0 ? '-' : ' ';
    abs_scaled = (scaled < 0) ? (uint32_t)(-scaled) : (uint32_t)scaled;
    (void)snprintf(line, sizeof(line), "%s%c%lu.%02lu%s", prefix, sign,
                   (unsigned long)(abs_scaled / 100U),
                   (unsigned long)(abs_scaled % 100U), suffix);
    draw_row(y, line, color);
}

void LCD_Init(void)
{
    s_page = 0U;
    s_last_btn5 = Button_IsPressed(BUTTON_5);
    s_initialized = 1U;
    draw_background();
    LCD_run();
}

void LCD_run(void)
{
    static const uint16_t button_y[5] = {55U, 89U, 123U, 157U, 191U};
    char text[28];
    uint32_t now = HAL_GetTick();
    uint32_t i;
    int32_t pos;
    uint8_t state;
    uint8_t btn5;
    AnalogMonitor_Data analog;
    if (!s_initialized) return;

    /* BTN5 toggles pages without consuming Button_TakePressedEvent(). */
    btn5 = Button_IsPressed(BUTTON_5);
    if (btn5 && !s_last_btn5) {
        s_page ^= 1U;
        draw_background();
    }
    s_last_btn5 = btn5;
    if ((uint32_t)(now - s_last_refresh) <
        (s_page ? LCD_ANALOG_REFRESH_MS : LCD_INPUT_REFRESH_MS)) return;
    s_last_refresh = now;

    if (s_page != 0U) {
        AnalogMonitor_GetSnapshot(&analog);
        if (!analog.adc1_valid) {
            draw_row(60U, "WAIT ADC1 DMA...", ST7789_YELLOW);
        } else {
            draw_fixed(55U, "VOUT:", analog.vout_v, "V", ST7789_GREEN);
            draw_fixed(89U, "IOUT:", analog.current_a, "A", ST7789_GREEN);
            draw_fixed(123U, "T-S:", analog.temp_solder_scaled, "C", ST7789_WHITE);
            draw_fixed(157U, "VIN:", analog.vin_scaled, "V", ST7789_WHITE);
        }
        if (analog.temp_fet_valid) {
            draw_fixed(208U, "FET RAW:", analog.temp_fet_adc, "", ST7789_YELLOW);
            draw_fixed(247U, "FET V:", analog.temp_fet_v, "V", ST7789_YELLOW);
        } else {
            draw_row(208U, "WAIT ADC2...", ST7789_YELLOW);
        }
        if (analog.temp_fet_error) draw_row(285U, "ADC2 ERROR", ST7789_YELLOW);
        else draw_row(285U, "BTN5: BACK", ST7789_CYAN);
        return;
    }
    for (i = 0U; i < 5U; ++i) {
        state = Button_IsPressed((ButtonId)i);
        if (state != s_last_pressed[i]) {
            (void)snprintf(text, sizeof(text), "BTN%lu: %s",
                           (unsigned long)(i + 1U), state ? "PRESSED" : "RELEASED");
            draw_row(button_y[i], text, state ? ST7789_GREEN : ST7789_WHITE);
            s_last_pressed[i] = state;
        }
    }
    pos = Encoder_GetPosition();
    if (pos != s_last_position) {
        (void)snprintf(text, sizeof(text), "ENC: %ld", (long)pos);
        draw_row(235U, text, ST7789_YELLOW);
        s_last_position = pos;
    }
    state = Encoder_IsSwitchPressed();
    if (state != s_last_pressed[BUTTON_ENCODER_SWITCH]) {
        (void)snprintf(text, sizeof(text), "ENC SW: %s",
                       state ? "PRESSED" : "RELEASED");
        draw_row(279U, text, state ? ST7789_GREEN : ST7789_WHITE);
        s_last_pressed[BUTTON_ENCODER_SWITCH] = state;
    }
}
