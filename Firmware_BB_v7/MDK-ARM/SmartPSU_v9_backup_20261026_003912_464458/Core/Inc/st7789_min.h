#ifndef ST7789_MIN_H
#define ST7789_MIN_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* 2.4-inch ST7789 module, default portrait orientation. */
#ifndef ST7789_WIDTH
#define ST7789_WIDTH 320U
#endif
#ifndef ST7789_HEIGHT
#define ST7789_HEIGHT 240U
#endif
#ifndef ST7789_X_OFFSET
#define ST7789_X_OFFSET 0U
#endif
#ifndef ST7789_Y_OFFSET
#define ST7789_Y_OFFSET 0U
#endif
#ifndef ST7789_MADCTL
#define ST7789_MADCTL 0xA0U  /* landscape -90 (MY|MV), RGB565 */
#endif
#ifndef ST7789_INVERT_COLORS
#define ST7789_INVERT_COLORS 0U
#endif

#define ST7789_BLACK  0x0000U
#define ST7789_WHITE  0xFFFFU
#define ST7789_GREEN  0x07E0U
#define ST7789_RED    0xF800U
#define ST7789_YELLOW 0xFFE0U
#define ST7789_CYAN   0x07FFU
#define ST7789_NAVY   0x0841U
#define ST7789_GRAY   0x8410U

void ST7789_Init(SPI_HandleTypeDef *hspi);
void ST7789_FillScreen(uint16_t rgb565);
void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t rgb565);
void ST7789_DrawText(uint16_t x, uint16_t y, const char *str,
                     uint16_t fg, uint16_t bg, uint8_t scale);

#endif
