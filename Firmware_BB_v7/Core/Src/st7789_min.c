#include "st7789_min.h"
#include <stddef.h>

#define LCD_PORT GPIOB
#define LCD_DC_PIN GPIO_PIN_0
#define LCD_RST_PIN GPIO_PIN_1
#define LCD_CS_PIN GPIO_PIN_10
#define MAX_GLYPH_SCALE 4U

static SPI_HandleTypeDef *s_spi;
static uint8_t s_glyph_buffer[6U * 8U * MAX_GLYPH_SCALE * MAX_GLYPH_SCALE * 2U];

static void lcd_select(void)   { HAL_GPIO_WritePin(LCD_PORT, LCD_CS_PIN, GPIO_PIN_RESET); }
static void lcd_deselect(void) { HAL_GPIO_WritePin(LCD_PORT, LCD_CS_PIN, GPIO_PIN_SET); }
static void lcd_dc(uint8_t data)
{
    HAL_GPIO_WritePin(LCD_PORT, LCD_DC_PIN, data ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void spi_send(const uint8_t *data, uint32_t length)
{
    while (length > 0U) {
        uint16_t amount = (length > 65535U) ? 65535U : (uint16_t)length;
        (void)HAL_SPI_Transmit(s_spi, (uint8_t *)data, amount, 1000U);
        data += amount;
        length -= amount;
    }
}

static void write_command(uint8_t command)
{
    lcd_dc(0);
    lcd_select();
    spi_send(&command, 1U);
    lcd_deselect();
}

static void write_data(const uint8_t *data, uint16_t size)
{
    lcd_dc(1);
    lcd_select();
    spi_send(data, size);
    lcd_deselect();
}

static void set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    uint16_t x1 = (uint16_t)(x + ST7789_X_OFFSET);
    uint16_t y1 = (uint16_t)(y + ST7789_Y_OFFSET);
    uint16_t x2 = (uint16_t)(x1 + width - 1U);
    uint16_t y2 = (uint16_t)(y1 + height - 1U);
    uint8_t data[4];

    data[0] = (uint8_t)(x1 >> 8); data[1] = (uint8_t)x1;
    data[2] = (uint8_t)(x2 >> 8); data[3] = (uint8_t)x2;
    write_command(0x2A); /* CASET */
    write_data(data, 4U);
    data[0] = (uint8_t)(y1 >> 8); data[1] = (uint8_t)y1;
    data[2] = (uint8_t)(y2 >> 8); data[3] = (uint8_t)y2;
    write_command(0x2B); /* RASET */
    write_data(data, 4U);
    write_command(0x2C); /* RAMWR */
}

void ST7789_Init(SPI_HandleTypeDef *hspi)
{
    uint8_t val;
    s_spi = hspi;
    lcd_deselect();
    HAL_GPIO_WritePin(LCD_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
    HAL_Delay(20);
    HAL_GPIO_WritePin(LCD_PORT, LCD_RST_PIN, GPIO_PIN_SET);
    HAL_Delay(120);

    write_command(0x01); /* SWRESET */
    HAL_Delay(150);
    write_command(0x11); /* SLPOUT */
    HAL_Delay(120);
    val = 0x55; write_command(0x3A); write_data(&val, 1U); /* RGB565 */
    val = ST7789_MADCTL; write_command(0x36); write_data(&val, 1U);
#if ST7789_INVERT_COLORS
    write_command(0x21); /* INVON */
#else
    write_command(0x20); /* INVOFF */
#endif
    write_command(0x13); /* NORON */
    write_command(0x29); /* DISPON */
    HAL_Delay(20);
}

void ST7789_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t rgb565)
{
    uint8_t chunk[128];
    uint32_t pixels, i;
    if (s_spi == NULL || width == 0U || height == 0U ||
        x >= ST7789_WIDTH || y >= ST7789_HEIGHT) return;
    if ((uint32_t)x + width > ST7789_WIDTH) width = (uint16_t)(ST7789_WIDTH - x);
    if ((uint32_t)y + height > ST7789_HEIGHT) height = (uint16_t)(ST7789_HEIGHT - y);
    for (i = 0; i < sizeof(chunk); i += 2U) {
        chunk[i] = (uint8_t)(rgb565 >> 8);
        chunk[i + 1U] = (uint8_t)rgb565;
    }
    set_window(x, y, width, height);
    pixels = (uint32_t)width * (uint32_t)height;
    lcd_dc(1);
    lcd_select();
    while (pixels > 0U) {
        uint32_t n = (pixels > 64U) ? 64U : pixels;
        spi_send(chunk, n * 2U);
        pixels -= n;
    }
    lcd_deselect();
}

void ST7789_FillScreen(uint16_t rgb565)
{
    ST7789_FillRect(0U, 0U, ST7789_WIDTH, ST7789_HEIGHT, rgb565);
}

/* Small handcrafted 5x7 uppercase font. Each row stores 5 left-to-right bits. */
typedef struct { char letter; uint8_t row[7]; } FontEntry;
static const FontEntry s_font[] = {
    {' ', {0,0,0,0,0,0,0}},
    {'0', {14,17,19,21,25,17,14}}, {'1', {4,12,4,4,4,4,14}},
    {'2', {14,17,1,2,4,8,31}}, {'3', {30,1,1,14,1,1,30}},
    {'4', {2,6,10,18,31,2,2}}, {'5', {31,16,16,30,1,1,30}},
    {'6', {14,16,16,30,17,17,14}}, {'7', {31,1,2,4,8,8,8}},
    {'8', {14,17,17,14,17,17,14}}, {'9', {14,17,17,15,1,1,14}},
    {'A', {14,17,17,31,17,17,17}}, {'B', {30,17,17,30,17,17,30}},
    {'C', {14,17,16,16,16,17,14}}, {'D', {30,17,17,17,17,17,30}},
    {'E', {31,16,16,30,16,16,31}}, {'F', {31,16,16,30,16,16,16}},
    {'G', {14,17,16,23,17,17,14}}, {'H', {17,17,17,31,17,17,17}},
    {'I', {14,4,4,4,4,4,14}}, {'J', {1,1,1,1,17,17,14}},
    {'K', {17,18,20,24,20,18,17}}, {'L', {16,16,16,16,16,16,31}},
    {'M', {17,27,21,21,17,17,17}}, {'N', {17,25,21,19,17,17,17}},
    {'O', {14,17,17,17,17,17,14}}, {'P', {30,17,17,30,16,16,16}},
    {'Q', {14,17,17,17,21,18,13}}, {'R', {30,17,17,30,20,18,17}},
    {'S', {15,16,16,14,1,1,30}}, {'T', {31,4,4,4,4,4,4}},
    {'U', {17,17,17,17,17,17,14}}, {'V', {17,17,17,17,17,10,4}},
    {'W', {17,17,17,21,21,21,10}}, {'X', {17,17,10,4,10,17,17}},
    {'Y', {17,17,10,4,4,4,4}}, {'Z', {31,1,2,4,8,16,31}},
    {':', {0,4,4,0,4,4,0}}, {'.', {0,0,0,0,0,6,6}},
    {'+', {0,4,4,31,4,4,0}}, {'-', {0,0,0,31,0,0,0}},
    {'*', {0,10,4,31,4,10,0}}, {'/', {1,2,2,4,8,8,16}},
    {'?', {14,17,1,2,4,0,4}}
};

static const uint8_t *glyph_rows(char c)
{
    uint32_t i;
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    for (i = 0U; i < sizeof(s_font) / sizeof(s_font[0]); i++) {
        if (s_font[i].letter == c) return s_font[i].row;
    }
    return s_font[(sizeof(s_font) / sizeof(s_font[0])) - 1U].row;
}

static void draw_char(uint16_t x, uint16_t y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    uint16_t width = (uint16_t)(6U * scale);
    uint16_t height = (uint16_t)(8U * scale);
    const uint8_t *rows = glyph_rows(c);
    uint32_t px, py, index = 0U;
    if (x + width > ST7789_WIDTH || y + height > ST7789_HEIGHT) return;

    for (py = 0U; py < height; py++) {
        for (px = 0U; px < width; px++) {
            uint32_t gx = px / scale, gy = py / scale;
            uint16_t col = (gx < 5U && gy < 7U &&
                (rows[gy] & (uint8_t)(1U << (4U - gx)))) ? fg : bg;
            s_glyph_buffer[index++] = (uint8_t)(col >> 8);
            s_glyph_buffer[index++] = (uint8_t)col;
        }
    }
    set_window(x, y, width, height);
    write_data(s_glyph_buffer, (uint16_t)index);
}

void ST7789_DrawText(uint16_t x, uint16_t y, const char *str,
                     uint16_t fg, uint16_t bg, uint8_t scale)
{
    if (str == NULL || s_spi == NULL || scale == 0U || scale > MAX_GLYPH_SCALE) return;
    while (*str != '\0' && ((uint32_t)x + 6U * scale <= ST7789_WIDTH)) {
        draw_char(x, y, *str, fg, bg, scale);
        x = (uint16_t)(x + 6U * scale);
        str++;
    }
}
