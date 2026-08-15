#ifndef ST7789_H
#define ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include "st7789_conf.h"
#include <stdint.h>
#include <stddef.h>

typedef uint16_t ST7789_ColorTypeDef;

typedef enum
{
    ST7789_INVERT_DISABLE = 0,
    ST7789_INVERT_ENABLE  = 1
} ST7789_InvTypeDef;

/* RGB565 colors */
#define ST7789_COLOR_BLACK       0x0000U
#define ST7789_COLOR_NAVY        0x000FU
#define ST7789_COLOR_DARKGREEN   0x03E0U
#define ST7789_COLOR_DARKCYAN    0x03EFU
#define ST7789_COLOR_MAROON      0x7800U
#define ST7789_COLOR_PURPLE      0x780FU
#define ST7789_COLOR_OLIVE       0x7BE0U
#define ST7789_COLOR_LIGHTGREY   0xC618U
#define ST7789_COLOR_DARKGREY    0x7BEFU
#define ST7789_COLOR_BLUE        0x001FU
#define ST7789_COLOR_GREEN       0x07E0U
#define ST7789_COLOR_CYAN        0x07FFU
#define ST7789_COLOR_RED         0xF800U
#define ST7789_COLOR_MAGENTA     0xF81FU
#define ST7789_COLOR_YELLOW      0xFFE0U
#define ST7789_COLOR_WHITE       0xFFFFU
#define ST7789_COLOR_ORANGE      0xFD20U

/* Current logical dimensions after rotation */
extern uint16_t ST7789_Width;
extern uint16_t ST7789_Height;

/* Core */
void ST7789_Init(void);
void ST7789_Reset(void);
void ST7789_SetRotation(uint8_t rotation);
void ST7789_InvertColors(ST7789_InvTypeDef invert);

/* Drawing */
void ST7789_FillScreen(ST7789_ColorTypeDef color);
void ST7789_Fill(uint16_t x0, uint16_t y0,
                 uint16_t x1, uint16_t y1,
                 ST7789_ColorTypeDef color);

void ST7789_DrawPixel(uint16_t x, uint16_t y,
                      ST7789_ColorTypeDef color);

void ST7789_DrawLine(int16_t x0, int16_t y0,
                     int16_t x1, int16_t y1,
                     ST7789_ColorTypeDef color);

void ST7789_DrawRectangle(uint16_t x, uint16_t y,
                          uint16_t w, uint16_t h,
                          ST7789_ColorTypeDef color);

void ST7789_DrawFilledRectangle(uint16_t x, uint16_t y,
                                uint16_t w, uint16_t h,
                                ST7789_ColorTypeDef color);

void ST7789_DrawCircle(int16_t x0, int16_t y0,
                       int16_t r,
                       ST7789_ColorTypeDef color);

void ST7789_DrawFilledCircle(int16_t x0, int16_t y0,
                             int16_t r,
                             ST7789_ColorTypeDef color);

/* Image: RGB565 */
void ST7789_PutImage(uint16_t x, uint16_t y,
                     uint16_t w, uint16_t h,
                     const uint16_t *image);

/* Text: built-in 5x7 ASCII */
void ST7789_PutChar(uint16_t x, uint16_t y,
                    char ch,
                    uint8_t scale,
                    ST7789_ColorTypeDef color,
                    ST7789_ColorTypeDef background);

void ST7789_PutString(uint16_t x, uint16_t y,
                      const char *str,
                      uint8_t scale,
                      ST7789_ColorTypeDef color,
                      ST7789_ColorTypeDef background);

/* Helpers */
ST7789_ColorTypeDef ST7789_Color_GetFromRGB(uint8_t r,
                                            uint8_t g,
                                            uint8_t b);
ST7789_ColorTypeDef ST7789_Color_GetFromHex(uint32_t hex);

/* Low level, exposed for debugging */
void ST7789_TransmitCommand(uint8_t cmd);
void ST7789_TransmitSingleData(uint8_t data);
void ST7789_TransmitData(const uint8_t *data, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
