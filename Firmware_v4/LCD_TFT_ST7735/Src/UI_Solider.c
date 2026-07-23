#include "UI_Solider.h"
#include "ST7735_SPI.h"
#include <stdio.h>
#include <string.h>

#define SOL_FONT_SMALL              Font_7x10
#define SOL_FONT_MEDIUM             Font_11x18
#define SOL_FONT_BIG                Font_16x26

#define SOL_LCD_W                   128
#define SOL_LCD_H                   160

#define SOL_TEMP_MIN                150.0f
#define SOL_TEMP_MAX                450.0f

#define SOL_CURR_MIN                0.0f
#define SOL_CURR_MAX                5.0f

#define SOL_SET_MIN                 150.0f
#define SOL_SET_MAX                 450.0f
#define SOL_SET_STEP                5.0f

#define SOL_GRAPH_X0                15
#define SOL_GRAPH_Y0                12
#define SOL_GRAPH_X1                111
#define SOL_GRAPH_Y1                88

#define SOL_GRAPH_W                 (SOL_GRAPH_X1 - SOL_GRAPH_X0)
#define SOL_GRAPH_H                 (SOL_GRAPH_Y1 - SOL_GRAPH_Y0)

#define SOL_PLOT_X0                 (SOL_GRAPH_X0 + 1)
#define SOL_PLOT_X1                 (SOL_GRAPH_X1 - 1)
#define SOL_PLOT_Y0                 (SOL_GRAPH_Y0 + 1)
#define SOL_PLOT_Y1                 (SOL_GRAPH_Y1 - 1)

#define SOL_PLOT_W                  (SOL_PLOT_X1 - SOL_PLOT_X0 + 1)
#define SOL_PLOT_H                  (SOL_PLOT_Y1 - SOL_PLOT_Y0 + 1)

#define SOL_GRAPH_N                 SOL_PLOT_W

#define SOL_SAMPLE_MS               50
#define SOL_LCD_PERIOD_MS           120

static UI_Solider_Data_t sol;

static uint8_t sol_active = 0;
static uint8_t sol_force_redraw = 1;
static uint8_t sol_dirty = 1;

static float graph_temp[SOL_GRAPH_N];
static float graph_curr[SOL_GRAPH_N];

static uint16_t graph_count = 0;
static uint16_t graph_head = 0;

static int graph_last_x = -1;
static int graph_last_yt = -1;
static int graph_last_yi = -1;
static uint8_t graph_has_last = 0;

static uint32_t t_sample = 0;
static uint32_t t_lcd = 0;

static char c_set[32] = "";
static char c_ch[16] = "";
static char c_fet[32] = "";
static char c_power[32] = "";
static char c_p1[16] = "";
static char c_p2[16] = "";
static char c_p3[16] = "";

static float clampf_sol(float x, float min, float max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

static void ClearCache(void)
{
    c_set[0] = 0;
    c_ch[0] = 0;
    c_fet[0] = 0;
    c_power[0] = 0;
    c_p1[0] = 0;
    c_p2[0] = 0;
    c_p3[0] = 0;
}

static void FmtNumber(char *buf, float value, uint8_t dec)
{
    int32_t scale = 1;

    for(uint8_t i = 0; i < dec; i++)
        scale *= 10;

    int32_t v = (int32_t)(value * scale + (value >= 0.0f ? 0.5f : -0.5f));

    if(v < 0)
    {
        v = -v;

        if(dec == 0)
            sprintf(buf, "-%03ld", (long)v);
        else if(dec == 1)
            sprintf(buf, "-%03ld.%01ld", (long)(v / scale), (long)(v % scale));
        else
            sprintf(buf, "-%03ld.%02ld", (long)(v / scale), (long)(v % scale));
    }
    else
    {
        if(dec == 0)
            sprintf(buf, "%03ld", (long)v);
        else if(dec == 1)
            sprintf(buf, "%03ld.%01ld", (long)(v / scale), (long)(v % scale));
        else
            sprintf(buf, "%03ld.%02ld", (long)(v / scale), (long)(v % scale));
    }
}

static void WriteCached(char *cache,
                        uint16_t x,
                        uint16_t y,
                        uint16_t w,
                        const char *str,
                        FontDef font,
                        uint16_t color,
                        uint16_t bg,
                        uint8_t force)
{
    if(force || strcmp(cache, str) != 0)
    {
        uint8_t old_len = strlen(cache);
        uint8_t new_len = strlen(str);

        if(force || old_len != new_len)
        {
            ST7735_FillRectangle(x, y, w, font.height, bg);
        }
        else
        {
            for(uint8_t i = 0; i < new_len; i++)
            {
                if(cache[i] != str[i])
                {
                    ST7735_FillRectangle(x + i * font.width,
                                         y,
                                         font.width,
                                         font.height,
                                         bg);
                }
            }
        }

        ST7735_WriteString(x, y, str, font, color, bg);

        strncpy(cache, str, 31);
        cache[31] = 0;
    }
}

static void DrawLineFast(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;

    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sy = y0 < y1 ? 1 : -1;

    int err = dx + dy;

    while(1)
    {
        if(x0 >= 0 && x0 < SOL_LCD_W && y0 >= 0 && y0 < SOL_LCD_H)
            ST7735_DrawPixel(x0, y0, color);

        if(x0 == x1 && y0 == y1)
            break;

        int e2 = 2 * err;

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

static int MapY(float value, float min, float max)
{
    value = clampf_sol(value, min, max);

    float k = (value - min) / (max - min);

    return SOL_PLOT_Y1 - (int)(k * (float)SOL_PLOT_H);
}

static void GraphDrawGridPixel(int x)
{
    if(x < SOL_PLOT_X0 || x > SOL_PLOT_X1)
        return;

    int y1 = SOL_GRAPH_Y0 + SOL_GRAPH_H / 4;
    int y2 = SOL_GRAPH_Y0 + SOL_GRAPH_H / 2;
    int y3 = SOL_GRAPH_Y0 + 3 * SOL_GRAPH_H / 4;

    ST7735_DrawPixel(x, y1, ST7735_BLUE);
    ST7735_DrawPixel(x, y2, ST7735_BLUE);
    ST7735_DrawPixel(x, y3, ST7735_BLUE);
}

static void GraphClearColumn(int x)
{
    if(x < SOL_PLOT_X0 || x > SOL_PLOT_X1)
        return;

    ST7735_FillRectangle(x,
                         SOL_PLOT_Y0,
                         1,
                         SOL_PLOT_H,
                         ST7735_BLACK);

    GraphDrawGridPixel(x);
}

static void GraphClearCursor(int x)
{
    GraphClearColumn(x);

    if(x + 1 <= SOL_PLOT_X1)
        GraphClearColumn(x + 1);
}

static void DrawGraphAxes(void)
{
    ST7735_FillRectangle(0, 0, SOL_LCD_W, 94, ST7735_BLACK);

    ST7735_WriteString(2, 0, "T", SOL_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(119, 0, "A", SOL_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);

    drawVline(SOL_GRAPH_X0, SOL_GRAPH_Y0, SOL_GRAPH_H, ST7735_RED);
    drawVline(SOL_GRAPH_X1, SOL_GRAPH_Y0, SOL_GRAPH_H, ST7735_CYAN);
    drawHline(SOL_GRAPH_X0, SOL_GRAPH_Y1, SOL_GRAPH_W, ST7735_WHITE);

    drawHline(SOL_GRAPH_X0,
              SOL_GRAPH_Y0 + SOL_GRAPH_H / 4,
              SOL_GRAPH_W,
              ST7735_BLUE);

    drawHline(SOL_GRAPH_X0,
              SOL_GRAPH_Y0 + SOL_GRAPH_H / 2,
              SOL_GRAPH_W,
              ST7735_BLUE);

    drawHline(SOL_GRAPH_X0,
              SOL_GRAPH_Y0 + 3 * SOL_GRAPH_H / 4,
              SOL_GRAPH_W,
              ST7735_BLUE);

    ST7735_WriteString(0, SOL_GRAPH_Y0 - 2, "450", SOL_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(0, SOL_GRAPH_Y1 - 8, "150", SOL_FONT_SMALL, ST7735_RED, ST7735_BLACK);

    ST7735_WriteString(114, SOL_GRAPH_Y0 - 2, "5", SOL_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(114, SOL_GRAPH_Y1 - 8, "0", SOL_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);

    graph_last_x = -1;
    graph_last_yt = -1;
    graph_last_yi = -1;
    graph_has_last = 0;
}

static void DrawGraphPoint(uint16_t x_index, float temp, float curr)
{
    int x = SOL_PLOT_X0 + x_index;

    int yt = MapY(temp, SOL_TEMP_MIN, SOL_TEMP_MAX);
    int yi = MapY(curr, SOL_CURR_MIN, SOL_CURR_MAX);

    GraphClearCursor(x);

    if(graph_has_last && x > graph_last_x)
    {
        DrawLineFast(graph_last_x, graph_last_yt, x, yt, ST7735_RED);
        DrawLineFast(graph_last_x, graph_last_yi, x, yi, ST7735_CYAN);
    }
    else
    {
        ST7735_DrawPixel(x, yt, ST7735_RED);
        ST7735_DrawPixel(x, yi, ST7735_CYAN);
    }

    graph_last_x = x;
    graph_last_yt = yt;
    graph_last_yi = yi;
    graph_has_last = 1;
}

static void GraphPush(float temp, float curr)
{
    uint16_t x_index = graph_head;

    graph_temp[graph_head] = temp;
    graph_curr[graph_head] = curr;

    graph_head++;

    if(graph_head >= SOL_GRAPH_N)
    {
        graph_head = 0;
        graph_has_last = 0;
    }

    if(graph_count < SOL_GRAPH_N)
        graph_count++;

    DrawGraphPoint(x_index, temp, curr);
}

static void GraphSampleTask(void)
{
    uint32_t now = HAL_GetTick();

    if(now - t_sample < SOL_SAMPLE_MS)
        return;

    t_sample = now;

    GraphPush(sol.tip_temp, sol.current);
}

static void DrawBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    ST7735_WriteString(2, 96, "SET", SOL_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(78, 99, "*C", SOL_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);

    ST7735_WriteString(2, 116, "FET", SOL_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(48, 116, "*C", SOL_FONT_SMALL, ST7735_RED, ST7735_BLACK);

    ST7735_WriteString(68, 116, "P", SOL_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
    ST7735_WriteString(112, 116, "W", SOL_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);

    drawRect(0, 140, 41, 19, ST7735_GREEN);
    drawRect(43, 140, 41, 19, ST7735_YELLOW);
    drawRect(86, 140, 41, 19, ST7735_MAGENTA);

    ClearCache();
}

static void UpdatePresets(uint8_t force)
{
    char buf[16];

    uint16_t c1 = sol.active_preset == 0 ? ST7735_WHITE : ST7735_GREEN;
    uint16_t c2 = sol.active_preset == 1 ? ST7735_WHITE : ST7735_YELLOW;
    uint16_t c3 = sol.active_preset == 2 ? ST7735_WHITE : ST7735_MAGENTA;

    drawRect(0, 140, 41, 19, c1);
    drawRect(43, 140, 41, 19, c2);
    drawRect(86, 140, 41, 19, c3);

    sprintf(buf, "P1%03ld", (long)(sol.preset[0] + 0.5f));
    WriteCached(c_p1,
                3, 146, 36,
                buf,
                SOL_FONT_SMALL,
                c1,
                ST7735_BLACK,
                force);

    sprintf(buf, "P2%03ld", (long)(sol.preset[1] + 0.5f));
    WriteCached(c_p2,
                46, 146, 36,
                buf,
                SOL_FONT_SMALL,
                c2,
                ST7735_BLACK,
                force);

    sprintf(buf, "P3%03ld", (long)(sol.preset[2] + 0.5f));
    WriteCached(c_p3,
                89, 146, 36,
                buf,
                SOL_FONT_SMALL,
                c3,
                ST7735_BLACK,
                force);
}

static void UpdateValues(uint8_t force)
{
    char buf[32];

    FmtNumber(buf, sol.set_temp, 0);
    WriteCached(c_set,
                28, 94, 48,
                buf,
                SOL_FONT_MEDIUM,
                ST7735_CYAN,
                ST7735_BLACK,
                force);

    sprintf(buf, "CH%d", sol.active_preset + 1);
    WriteCached(c_ch,
                96, 99, 28,
                buf,
                SOL_FONT_SMALL,
                ST7735_WHITE,
                ST7735_BLACK,
                force);

    FmtNumber(buf, sol.fet_temp, 0);
    WriteCached(c_fet,
                26, 113, 24,
                buf,
                SOL_FONT_SMALL,
                ST7735_RED,
                ST7735_BLACK,
                force);

    FmtNumber(buf, sol.power, 0);
    WriteCached(c_power,
                80, 113, 30,
                buf,
                SOL_FONT_SMALL,
                ST7735_MAGENTA,
                ST7735_BLACK,
                force);

    UpdatePresets(force);
}

void UI_Solider_Init(void)
{
    sol.tip_temp = 25.0f;
    sol.current = 0.0f;
    sol.fet_temp = 25.0f;
    sol.power = 0.0f;

    sol.preset[0] = 300.0f;
    sol.preset[1] = 350.0f;
    sol.preset[2] = 400.0f;

    sol.active_preset = 1;
    sol.set_temp = sol.preset[1];

    graph_count = 0;
    graph_head = 0;

    graph_last_x = -1;
    graph_last_yt = -1;
    graph_last_yi = -1;
    graph_has_last = 0;

    t_sample = HAL_GetTick();
    t_lcd = HAL_GetTick();

    sol_active = 0;
    sol_force_redraw = 1;
    sol_dirty = 1;

    ClearCache();
}

void UI_Solider_Enter(void)
{
    sol_active = 1;
    sol_force_redraw = 1;
    sol_dirty = 1;

    graph_count = 0;
    graph_head = 0;

    graph_last_x = -1;
    graph_last_yt = -1;
    graph_last_yi = -1;
    graph_has_last = 0;

    t_sample = HAL_GetTick();
    t_lcd = HAL_GetTick();

    ClearCache();
}

void UI_Solider_Exit(void)
{
    sol_active = 0;
}

uint8_t UI_Solider_IsActive(void)
{
    return sol_active;
}

void UI_Solider_SetData(float tip_temp,
                        float current,
                        float fet_temp,
                        float power)
{
    sol.tip_temp = tip_temp;
    sol.current = current;
    sol.fet_temp = fet_temp;
    sol.power = power;
}

float UI_Solider_GetSetTemp(void)
{
    return sol.set_temp;
}

void UI_Solider_SetPreset(uint8_t id, float temp)
{
    if(id > 2)
        return;

    temp = clampf_sol(temp, SOL_SET_MIN, SOL_SET_MAX);

    sol.preset[id] = temp;

    if(sol.active_preset == id)
        sol.set_temp = temp;

    c_set[0] = 0;
    c_p1[0] = 0;
    c_p2[0] = 0;
    c_p3[0] = 0;

    sol_dirty = 1;
}

void UI_Solider_SelectPreset(uint8_t id)
{
    if(id > 2)
        return;

    sol.active_preset = id;
    sol.set_temp = sol.preset[id];

    c_set[0] = 0;
    c_ch[0] = 0;
    c_p1[0] = 0;
    c_p2[0] = 0;
    c_p3[0] = 0;

    sol_dirty = 1;
}

void UI_Solider_EncoderAdjust(int8_t dir)
{
    if(dir == 0)
        return;

    sol.set_temp += (float)dir * SOL_SET_STEP;
    sol.set_temp = clampf_sol(sol.set_temp, SOL_SET_MIN, SOL_SET_MAX);

    sol.preset[sol.active_preset] = sol.set_temp;

    c_set[0] = 0;
    c_p1[0] = 0;
    c_p2[0] = 0;
    c_p3[0] = 0;

    sol_dirty = 1;
}

void UI_Solider_Task(uint8_t force)
{
    if(sol_active == 0)
        return;

    if(force || sol_force_redraw)
    {
        sol_force_redraw = 0;
        sol_dirty = 0;

        DrawBase();
        DrawGraphAxes();
        UpdateValues(1);

        return;
    }

    GraphSampleTask();

    if(sol_dirty || HAL_GetTick() - t_lcd >= SOL_LCD_PERIOD_MS)
    {
        t_lcd = HAL_GetTick();
        sol_dirty = 0;

        UpdateValues(0);
    }
}