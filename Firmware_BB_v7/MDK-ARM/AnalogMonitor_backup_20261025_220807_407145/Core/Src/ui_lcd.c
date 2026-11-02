#include "ui_lcd.h"
#include "st7789_min.h"
#include "input_buttons.h"
#include "input_encoder.h"
#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <limits.h>

#define LCD_REFRESH_MS 60U
#define LABEL_X 12U
#define FONT_SCALE 2U

static uint8_t s_last_pressed[BUTTON_COUNT];
static int32_t s_last_position;
static uint32_t s_last_refresh;
static uint8_t s_initialized;

static void draw_row(uint16_t y, const char *text, uint16_t color)
{
    ST7789_FillRect(8U, y, 224U, 23U, ST7789_NAVY);
    ST7789_DrawText(LABEL_X, (uint16_t)(y + 3U), text,
                    color, ST7789_NAVY, FONT_SCALE);
}

void LCD_Init(void)
{
    uint32_t i;
    ST7789_FillScreen(ST7789_NAVY);
    ST7789_DrawText(24U, 12U, "INPUT MONITOR", ST7789_CYAN,
                    ST7789_NAVY, FONT_SCALE);
    ST7789_FillRect(8U, 43U, 224U, 2U, ST7789_GRAY);
    for (i = 0; i < (uint32_t)BUTTON_COUNT; i++) {
        s_last_pressed[i] = 0xFFU; /* force first refresh */
    }
    s_last_position = INT32_MIN;
    s_last_refresh = HAL_GetTick() - LCD_REFRESH_MS;
    s_initialized = 1U;
    LCD_run();
}

void LCD_run(void)
{
    static const uint16_t button_y[5] = {55U, 89U, 123U, 157U, 191U};
    char text[24];
    uint32_t now = HAL_GetTick();
    uint32_t i;
    int32_t pos;
    uint8_t state;
    if (!s_initialized || (uint32_t)(now - s_last_refresh) < LCD_REFRESH_MS) return;
    s_last_refresh = now;

    for (i = 0U; i < 5U; i++) {
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
