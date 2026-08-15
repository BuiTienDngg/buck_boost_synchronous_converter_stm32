#include "st7789.h"
#include "st7789_font.h"

/*
 * Adapted implementation for STM32F103 HAL.
 * The initialization sequence follows the same ST7789 register strategy
 * as Majid-Derhambakhsh/ST7789:
 * COLMOD -> PORCTRL -> MADCTL -> GCTRL/VCOM/LCM/VRH/VDV/FR/PW ->
 * gamma -> INVON -> SLPOUT -> NORON -> DISPON.
 */

/* ST7789 commands */
#define ST7789_CMD_SWRESET      0x01U
#define ST7789_CMD_SLPOUT       0x11U
#define ST7789_CMD_NORON        0x13U
#define ST7789_CMD_INVOFF       0x20U
#define ST7789_CMD_INVON        0x21U
#define ST7789_CMD_DISPON       0x29U
#define ST7789_CMD_CASET        0x2AU
#define ST7789_CMD_RASET        0x2BU
#define ST7789_CMD_RAMWR        0x2CU
#define ST7789_CMD_MADCTL       0x36U
#define ST7789_CMD_COLMOD       0x3AU
#define ST7789_CMD_PORCTRL      0xB2U
#define ST7789_CMD_GCTRL        0xB7U
#define ST7789_CMD_VCOMS        0xBBU
#define ST7789_CMD_LCMCTRL      0xC0U
#define ST7789_CMD_VDVVRHEN     0xC2U
#define ST7789_CMD_VRHS         0xC3U
#define ST7789_CMD_VDVS         0xC4U
#define ST7789_CMD_FRCTRL2      0xC6U
#define ST7789_CMD_PWCTRL1      0xD0U
#define ST7789_CMD_PVGAMCTRL    0xE0U
#define ST7789_CMD_NVGAMCTRL    0xE1U

/* MADCTL bits */
#define ST7789_MADCTL_MY        0x80U
#define ST7789_MADCTL_MX        0x40U
#define ST7789_MADCTL_MV        0x20U
#define ST7789_MADCTL_RGB       0x00U
#define ST7789_MADCTL_BGR       0x08U

#define ST7789_TX_CHUNK_PIXELS  128U

uint16_t ST7789_Width  = ST7789_WIDTH;
uint16_t ST7789_Height = ST7789_HEIGHT;

static uint16_t s_xstart = ST7789_XSTART;
static uint16_t s_ystart = ST7789_YSTART;

static inline void ST7789_CS_Low(void)
{
    HAL_GPIO_WritePin(ST7789_CS_GPIO_PORT,
                      ST7789_CS_GPIO_PIN,
                      GPIO_PIN_RESET);
}

static inline void ST7789_CS_High(void)
{
    HAL_GPIO_WritePin(ST7789_CS_GPIO_PORT,
                      ST7789_CS_GPIO_PIN,
                      GPIO_PIN_SET);
}

static inline void ST7789_DC_Command(void)
{
    HAL_GPIO_WritePin(ST7789_DC_GPIO_PORT,
                      ST7789_DC_GPIO_PIN,
                      GPIO_PIN_RESET);
}

static inline void ST7789_DC_Data(void)
{
    HAL_GPIO_WritePin(ST7789_DC_GPIO_PORT,
                      ST7789_DC_GPIO_PIN,
                      GPIO_PIN_SET);
}

void ST7789_TransmitCommand(uint8_t cmd)
{
    ST7789_CS_Low();
    ST7789_DC_Command();

    HAL_SPI_Transmit(&ST7789_SPI,
                     &cmd,
                     1U,
                     ST7789_SPI_TIMEOUT);

    ST7789_CS_High();
}

void ST7789_TransmitSingleData(uint8_t data)
{
    ST7789_CS_Low();
    ST7789_DC_Data();

    HAL_SPI_Transmit(&ST7789_SPI,
                     &data,
                     1U,
                     ST7789_SPI_TIMEOUT);

    ST7789_CS_High();
}

void ST7789_TransmitData(const uint8_t *data, uint32_t size)
{
    if((data == NULL) || (size == 0U))
        return;

    ST7789_CS_Low();
    ST7789_DC_Data();

    while(size > 0U)
    {
        uint16_t chunk =
            (size > 65535U) ? 65535U : (uint16_t)size;

        HAL_SPI_Transmit(&ST7789_SPI,
                         (uint8_t *)data,
                         chunk,
                         ST7789_SPI_TIMEOUT);

        data += chunk;
        size -= chunk;
    }

    ST7789_CS_High();
}

void ST7789_Reset(void)
{
    /*
     * Same timing style as the referenced repo:
     * wait -> reset low -> reset high -> settle.
     */
    HAL_Delay(25U);

    HAL_GPIO_WritePin(ST7789_RST_GPIO_PORT,
                      ST7789_RST_GPIO_PIN,
                      GPIO_PIN_RESET);

    HAL_Delay(25U);

    HAL_GPIO_WritePin(ST7789_RST_GPIO_PORT,
                      ST7789_RST_GPIO_PIN,
                      GPIO_PIN_SET);

    HAL_Delay(50U);
}

static void ST7789_SetWindowAddress(uint16_t x0,
                                    uint16_t y0,
                                    uint16_t x1,
                                    uint16_t y1)
{
    uint16_t xs0 = (uint16_t)(x0 + s_xstart);
    uint16_t xs1 = (uint16_t)(x1 + s_xstart);
    uint16_t ys0 = (uint16_t)(y0 + s_ystart);
    uint16_t ys1 = (uint16_t)(y1 + s_ystart);

    uint8_t col[4] =
    {
        (uint8_t)(xs0 >> 8),
        (uint8_t)(xs0 & 0xFFU),
        (uint8_t)(xs1 >> 8),
        (uint8_t)(xs1 & 0xFFU)
    };

    uint8_t row[4] =
    {
        (uint8_t)(ys0 >> 8),
        (uint8_t)(ys0 & 0xFFU),
        (uint8_t)(ys1 >> 8),
        (uint8_t)(ys1 & 0xFFU)
    };

    ST7789_TransmitCommand(ST7789_CMD_CASET);
    ST7789_TransmitData(col, sizeof(col));

    ST7789_TransmitCommand(ST7789_CMD_RASET);
    ST7789_TransmitData(row, sizeof(row));

    ST7789_TransmitCommand(ST7789_CMD_RAMWR);
}

void ST7789_SetRotation(uint8_t rotation)
{
    uint8_t madctl;

    rotation &= 0x03U;

    switch(rotation)
    {
        case 0:
            madctl = ST7789_MADCTL_MX |
                     ST7789_MADCTL_MY |
                     ST7789_MADCTL_RGB;

            ST7789_Width = ST7789_WIDTH;
            ST7789_Height = ST7789_HEIGHT;
            s_xstart = ST7789_XSTART;
            s_ystart = ST7789_YSTART;
            break;

        case 1:
            madctl = ST7789_MADCTL_MY |
                     ST7789_MADCTL_MV |
                     ST7789_MADCTL_RGB;

            ST7789_Width = ST7789_HEIGHT;
            ST7789_Height = ST7789_WIDTH;

            /*
             * For a full 240x320 panel with 0,0 offsets this remains 0,0.
             * If your panel requires offsets, they are swapped on rotation.
             */
            s_xstart = ST7789_YSTART;
            s_ystart = ST7789_XSTART;
            break;

        case 2:
            madctl = ST7789_MADCTL_RGB;

            ST7789_Width = ST7789_WIDTH;
            ST7789_Height = ST7789_HEIGHT;
            s_xstart = ST7789_XSTART;
            s_ystart = ST7789_YSTART;
            break;

        default:
        case 3:
            madctl = ST7789_MADCTL_MX |
                     ST7789_MADCTL_MV |
                     ST7789_MADCTL_RGB;

            ST7789_Width = ST7789_HEIGHT;
            ST7789_Height = ST7789_WIDTH;
            s_xstart = ST7789_YSTART;
            s_ystart = ST7789_XSTART;
            break;
    }

    ST7789_TransmitCommand(ST7789_CMD_MADCTL);
    ST7789_TransmitSingleData(madctl);
}

void ST7789_InvertColors(ST7789_InvTypeDef invert)
{
    ST7789_TransmitCommand(
        invert ? ST7789_CMD_INVON : ST7789_CMD_INVOFF
    );
}

void ST7789_Init(void)
{
    static const uint8_t porch[5] =
    {
        0x0CU, 0x0CU, 0x00U, 0x33U, 0x33U
    };

    static const uint8_t positive_gamma[14] =
    {
        0xD0U,0x04U,0x0DU,0x11U,0x13U,0x2BU,0x3FU,
        0x54U,0x4CU,0x18U,0x0DU,0x0BU,0x1FU,0x23U
    };

    static const uint8_t negative_gamma[14] =
    {
        0xD0U,0x04U,0x0CU,0x11U,0x13U,0x2CU,0x3FU,
        0x44U,0x51U,0x2FU,0x1FU,0x1FU,0x20U,0x23U
    };

    ST7789_CS_High();

    HAL_GPIO_WritePin(ST7789_RST_GPIO_PORT,
                      ST7789_RST_GPIO_PIN,
                      GPIO_PIN_SET);

    ST7789_Reset();

    /*
     * 16-bit RGB565
     */
    ST7789_TransmitCommand(ST7789_CMD_COLMOD);
    ST7789_TransmitSingleData(0x55U);

    /*
     * Porch control
     */
    ST7789_TransmitCommand(ST7789_CMD_PORCTRL);
    ST7789_TransmitData(porch, sizeof(porch));

    /*
     * Rotation / memory access control
     */
    ST7789_SetRotation(ST7789_ROTATION);

    /*
     * Internal voltage / timing configuration.
     * Values follow the initialization strategy of the referenced library.
     */
    ST7789_TransmitCommand(ST7789_CMD_GCTRL);
    ST7789_TransmitSingleData(0x35U);

    ST7789_TransmitCommand(ST7789_CMD_VCOMS);
    ST7789_TransmitSingleData(0x19U);

    ST7789_TransmitCommand(ST7789_CMD_LCMCTRL);
    ST7789_TransmitSingleData(0x2CU);

    ST7789_TransmitCommand(ST7789_CMD_VDVVRHEN);
    ST7789_TransmitSingleData(0x01U);

    ST7789_TransmitCommand(ST7789_CMD_VRHS);
    ST7789_TransmitSingleData(0x12U);

    ST7789_TransmitCommand(ST7789_CMD_VDVS);
    ST7789_TransmitSingleData(0x20U);

    ST7789_TransmitCommand(ST7789_CMD_FRCTRL2);
    ST7789_TransmitSingleData(0x0FU);

    ST7789_TransmitCommand(ST7789_CMD_PWCTRL1);
    ST7789_TransmitSingleData(0xA4U);
    ST7789_TransmitSingleData(0xA1U);

    ST7789_TransmitCommand(ST7789_CMD_PVGAMCTRL);
    ST7789_TransmitData(positive_gamma,
                        sizeof(positive_gamma));

    ST7789_TransmitCommand(ST7789_CMD_NVGAMCTRL);
    ST7789_TransmitData(negative_gamma,
                        sizeof(negative_gamma));

    /*
     * Important ordering adapted from Majid's library.
     */
    ST7789_TransmitCommand(ST7789_CMD_INVON);
    ST7789_TransmitCommand(ST7789_CMD_SLPOUT);

    /*
     * Give the panel more time than the original library.
     * This is intentionally conservative for bring-up.
     */
    HAL_Delay(120U);

    ST7789_TransmitCommand(ST7789_CMD_NORON);
    ST7789_TransmitCommand(ST7789_CMD_DISPON);

    HAL_Delay(100U);

    ST7789_FillScreen(ST7789_COLOR_BLACK);
}

void ST7789_Fill(uint16_t x0, uint16_t y0,
                 uint16_t x1, uint16_t y1,
                 ST7789_ColorTypeDef color)
{
    if(x0 >= ST7789_Width || y0 >= ST7789_Height)
        return;

    if(x1 >= ST7789_Width)
        x1 = (uint16_t)(ST7789_Width - 1U);

    if(y1 >= ST7789_Height)
        y1 = (uint16_t)(ST7789_Height - 1U);

    if(x1 < x0 || y1 < y0)
        return;

    uint8_t hi = (uint8_t)(color >> 8);
    uint8_t lo = (uint8_t)(color & 0xFFU);

    uint8_t buffer[ST7789_TX_CHUNK_PIXELS * 2U];

    for(uint16_t i = 0; i < ST7789_TX_CHUNK_PIXELS; i++)
    {
        buffer[2U * i] = hi;
        buffer[2U * i + 1U] = lo;
    }

    uint32_t count =
        (uint32_t)(x1 - x0 + 1U) *
        (uint32_t)(y1 - y0 + 1U);

    ST7789_SetWindowAddress(x0, y0, x1, y1);

    ST7789_CS_Low();
    ST7789_DC_Data();

    while(count > 0U)
    {
        uint16_t n =
            (count > ST7789_TX_CHUNK_PIXELS)
            ? ST7789_TX_CHUNK_PIXELS
            : (uint16_t)count;

        HAL_SPI_Transmit(&ST7789_SPI,
                         buffer,
                         (uint16_t)(n * 2U),
                         ST7789_SPI_TIMEOUT);

        count -= n;
    }

    ST7789_CS_High();
}

void ST7789_FillScreen(ST7789_ColorTypeDef color)
{
    ST7789_Fill(0U,
                0U,
                (uint16_t)(ST7789_Width - 1U),
                (uint16_t)(ST7789_Height - 1U),
                color);
}

void ST7789_DrawPixel(uint16_t x,
                      uint16_t y,
                      ST7789_ColorTypeDef color)
{
    if(x >= ST7789_Width || y >= ST7789_Height)
        return;

    uint8_t data[2] =
    {
        (uint8_t)(color >> 8),
        (uint8_t)(color & 0xFFU)
    };

    ST7789_SetWindowAddress(x, y, x, y);
    ST7789_TransmitData(data, 2U);
}

void ST7789_DrawFilledRectangle(uint16_t x,
                                uint16_t y,
                                uint16_t w,
                                uint16_t h,
                                ST7789_ColorTypeDef color)
{
    if((w == 0U) || (h == 0U))
        return;

    ST7789_Fill(x,
                y,
                (uint16_t)(x + w - 1U),
                (uint16_t)(y + h - 1U),
                color);
}

void ST7789_DrawRectangle(uint16_t x,
                          uint16_t y,
                          uint16_t w,
                          uint16_t h,
                          ST7789_ColorTypeDef color)
{
    if((w == 0U) || (h == 0U))
        return;

    ST7789_DrawFilledRectangle(x, y, w, 1U, color);

    if(h > 1U)
        ST7789_DrawFilledRectangle(x,
                                   (uint16_t)(y + h - 1U),
                                   w, 1U, color);

    ST7789_DrawFilledRectangle(x, y, 1U, h, color);

    if(w > 1U)
        ST7789_DrawFilledRectangle((uint16_t)(x + w - 1U),
                                   y, 1U, h, color);
}

void ST7789_DrawLine(int16_t x0,
                     int16_t y0,
                     int16_t x1,
                     int16_t y1,
                     ST7789_ColorTypeDef color)
{
    int16_t dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int16_t sx = (x0 < x1) ? 1 : -1;

    int16_t dy = -((y1 >= y0) ? (y1 - y0) : (y0 - y1));
    int16_t sy = (y0 < y1) ? 1 : -1;

    int16_t err = dx + dy;

    while(1)
    {
        if((x0 >= 0) && (y0 >= 0) &&
           (x0 < (int16_t)ST7789_Width) &&
           (y0 < (int16_t)ST7789_Height))
        {
            ST7789_DrawPixel((uint16_t)x0,
                             (uint16_t)y0,
                             color);
        }

        if((x0 == x1) && (y0 == y1))
            break;

        int16_t e2 = (int16_t)(2 * err);

        if(e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }

        if(e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

void ST7789_DrawCircle(int16_t x0,
                       int16_t y0,
                       int16_t r,
                       ST7789_ColorTypeDef color)
{
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;

    while(x >= y)
    {
        const int16_t p[8][2] =
        {
            {x0+x,y0+y},{x0+y,y0+x},{x0-y,y0+x},{x0-x,y0+y},
            {x0-x,y0-y},{x0-y,y0-x},{x0+y,y0-x},{x0+x,y0-y}
        };

        for(uint8_t i = 0; i < 8U; i++)
        {
            if((p[i][0] >= 0) && (p[i][1] >= 0) &&
               (p[i][0] < (int16_t)ST7789_Width) &&
               (p[i][1] < (int16_t)ST7789_Height))
            {
                ST7789_DrawPixel((uint16_t)p[i][0],
                                 (uint16_t)p[i][1],
                                 color);
            }
        }

        y++;

        if(err <= 0)
            err += (int16_t)(2 * y + 1);

        if(err > 0)
        {
            x--;
            err -= (int16_t)(2 * x + 1);
        }
    }
}

void ST7789_DrawFilledCircle(int16_t x0,
                             int16_t y0,
                             int16_t r,
                             ST7789_ColorTypeDef color)
{
    for(int16_t y = -r; y <= r; y++)
    {
        int16_t x = 0;

        while(((x + 1) * (x + 1) + y * y) <= r * r)
            x++;

        int16_t xs = x0 - x;
        int16_t xe = x0 + x;
        int16_t yy = y0 + y;

        if(yy < 0 || yy >= (int16_t)ST7789_Height)
            continue;

        if(xs < 0)
            xs = 0;

        if(xe >= (int16_t)ST7789_Width)
            xe = (int16_t)ST7789_Width - 1;

        if(xe >= xs)
        {
            ST7789_Fill((uint16_t)xs,
                        (uint16_t)yy,
                        (uint16_t)xe,
                        (uint16_t)yy,
                        color);
        }
    }
}

void ST7789_PutImage(uint16_t x,
                     uint16_t y,
                     uint16_t w,
                     uint16_t h,
                     const uint16_t *image)
{
    if(image == NULL || w == 0U || h == 0U)
        return;

    if(x >= ST7789_Width || y >= ST7789_Height)
        return;

    if((uint32_t)x + w > ST7789_Width)
        w = (uint16_t)(ST7789_Width - x);

    if((uint32_t)y + h > ST7789_Height)
        h = (uint16_t)(ST7789_Height - y);

    ST7789_SetWindowAddress(x, y,
                            (uint16_t)(x + w - 1U),
                            (uint16_t)(y + h - 1U));

    ST7789_CS_Low();
    ST7789_DC_Data();

    uint8_t buffer[ST7789_TX_CHUNK_PIXELS * 2U];
    uint32_t total = (uint32_t)w * h;
    uint32_t pos = 0U;

    while(pos < total)
    {
        uint16_t n =
            ((total - pos) > ST7789_TX_CHUNK_PIXELS)
            ? ST7789_TX_CHUNK_PIXELS
            : (uint16_t)(total - pos);

        for(uint16_t i = 0; i < n; i++)
        {
            uint16_t c = image[pos + i];
            buffer[2U * i] = (uint8_t)(c >> 8);
            buffer[2U * i + 1U] = (uint8_t)c;
        }

        HAL_SPI_Transmit(&ST7789_SPI,
                         buffer,
                         (uint16_t)(n * 2U),
                         ST7789_SPI_TIMEOUT);

        pos += n;
    }

    ST7789_CS_High();
}

void ST7789_PutChar(uint16_t x,
                    uint16_t y,
                    char ch,
                    uint8_t scale,
                    ST7789_ColorTypeDef color,
                    ST7789_ColorTypeDef background)
{
    if(scale == 0U)
        scale = 1U;

    if(ch < 0x20 || ch > 0x7E)
        ch = '?';

    uint8_t index = (uint8_t)(ch - 0x20);

    for(uint8_t col = 0; col < 5U; col++)
    {
        uint8_t bits = ST7789_Font5x7[index][col];

        for(uint8_t row = 0; row < 7U; row++)
        {
            ST7789_ColorTypeDef c =
                (bits & (1U << row))
                ? color
                : background;

            ST7789_DrawFilledRectangle(
                (uint16_t)(x + col * scale),
                (uint16_t)(y + row * scale),
                scale,
                scale,
                c
            );
        }
    }

    ST7789_DrawFilledRectangle(
        (uint16_t)(x + 5U * scale),
        y,
        scale,
        (uint16_t)(7U * scale),
        background
    );
}

void ST7789_PutString(uint16_t x,
                      uint16_t y,
                      const char *str,
                      uint8_t scale,
                      ST7789_ColorTypeDef color,
                      ST7789_ColorTypeDef background)
{
    if(str == NULL)
        return;

    if(scale == 0U)
        scale = 1U;

    uint16_t cx = x;
    uint16_t cy = y;
    uint16_t cw = (uint16_t)(6U * scale);
    uint16_t ch = (uint16_t)(8U * scale);

    while(*str != '\0')
    {
        if(*str == '\n')
        {
            cx = x;
            cy = (uint16_t)(cy + ch);
            str++;
            continue;
        }

        if((uint32_t)cx + cw > ST7789_Width)
        {
            cx = x;
            cy = (uint16_t)(cy + ch);
        }

        if((uint32_t)cy + ch > ST7789_Height)
            break;

        ST7789_PutChar(cx, cy, *str,
                       scale,
                       color,
                       background);

        cx = (uint16_t)(cx + cw);
        str++;
    }
}

ST7789_ColorTypeDef ST7789_Color_GetFromRGB(uint8_t r,
                                            uint8_t g,
                                            uint8_t b)
{
    return (uint16_t)(
        ((uint16_t)(r & 0xF8U) << 8) |
        ((uint16_t)(g & 0xFCU) << 3) |
        ((uint16_t)b >> 3)
    );
}

ST7789_ColorTypeDef ST7789_Color_GetFromHex(uint32_t hex)
{
    return ST7789_Color_GetFromRGB(
        (uint8_t)((hex >> 16) & 0xFFU),
        (uint8_t)((hex >> 8) & 0xFFU),
        (uint8_t)(hex & 0xFFU)
    );
}
