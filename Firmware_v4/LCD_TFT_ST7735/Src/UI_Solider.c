#include "UI_Solider.h"
#include "ST7735_SPI.h"
#include <stdio.h>
#include <string.h>
#include "main.h"
#include <stdint.h>
#include "ui.h"
#define SOL_FONT_SMALL              Font_7x10
#define SOL_FONT_MEDIUM             Font_11x18
#define SOL_FONT_BIG                Font_16x26

#define SOL_LCD_W                   128
#define SOL_LCD_H                   160

#define SOL_TEMP_MIN                150.0f
#define SOL_TEMP_MAX                450.0f

#define SOL_CURR_MIN                0.0f
#define SOL_CURR_MAX                10.0f
#define SOL_POWER_MAX								24.0f * SOL_CURR_MAX
#define SOL_SET_MIN                 150.0f
#define SOL_SET_MAX                 450.0f
#define SOL_SET_STEP                5.0f

#define SOL_GRAPH_X0                2
#define SOL_GRAPH_Y0                13
#define SOL_GRAPH_X1                125
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

#define SOL_SAMPLE_MS               100
#define SOL_LCD_PERIOD_MS           120

#define SOLIDER_ADC_INDEX              2

#define SOLIDER_PID_PERIOD_MS          100
#define SOLIDER_OFF_READ_DELAY_MS      50
#define SOLIDER_ADC_AVG_N              20
#define ALPHA_FIR											0.5f
#define SOLIDER_CCR_MAX_POWER          200
#define SOLIDER_CCR_OFF                1000

#define SOLIDER_PID_DT                 0.5f

#define SOLIDER_KP                  0.1f
#define SOLIDER_KI                  0.05f
#define SOLIDER_KD                  0.000f


#define SOLIDER_ADC_TEMP_K          (350.0f / 600.0f)
#define SOLIDER_ADC_TEMP_B          0.0f

#define SOLIDER_TEMP_MIN_C          25.0f
#define SOLIDER_TEMP_MAX_C          500.0f

#define SOL_CURRENT_EMA_ALPHA  0.12f

static float solider_current_sum = 0.0f;
static uint16_t solider_current_count = 0;

static float solider_current_avg = 0.0f;
static float solider_current_filtered = 0.0f;
static uint8_t solider_current_filter_init = 0;
extern volatile uint8_t adc1_dma_ready;
extern uint16_t adc1_dma_buf[];
extern BBUI_Data_t PowerStage;

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
static char c_tip[32] = "";
static char c_power[32] = "";
static char c_p1[16] = "";
static char c_p2[16] = "";
static char c_p3[16] = "";
typedef enum
{
    SOLIDER_PID_IDLE = 0,
    SOLIDER_PID_RUN,
    SOLIDER_PID_OFF_WAIT,
    SOLIDER_PID_READ_ADC
} SoliderPID_State_t;

static SoliderPID_State_t solider_pid_state = SOLIDER_PID_IDLE;

static uint32_t solider_pid_tick = 0;
static uint32_t solider_off_tick = 0;

static uint32_t solider_adc_sum = 0;
static uint16_t solider_adc_count = 0;

static float solider_pid_i = 0.0f;
static float solider_pid_prev_err = 0.0f;

volatile float solider_temp_raw = 0.0f;
volatile float solider_pid_power = 0.0f;
volatile uint16_t solider_pwm_ccr = SOLIDER_CCR_OFF;

volatile float measured_temp;
volatile float frev_measured_temp;
volatile float set_temp;

#define SOLIDER_ADC_TEMP_K          (350.0f / 600.0f)
#define SOLIDER_ADC_TEMP_B          0.0f

#define SOLIDER_TEMP_MIN_C          25.0f
#define SOLIDER_TEMP_MAX_C          500.0f

static float clampf_solider(float x, float min, float max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

float Solider_ADC_ToTemp(uint16_t adc_raw)
{
    //float temp = SOLIDER_ADC_TEMP_K * (float)adc_raw + SOLIDER_ADC_TEMP_B;
		float temp = ((float)adc_raw - 450.0f) * 1.85f;
    temp = clampf_solider(temp , SOLIDER_TEMP_MIN_C, SOLIDER_TEMP_MAX_C);

    return temp;
}
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
    c_tip[0] = 0;
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

    return SOL_PLOT_Y1 - (int)(k * (float)(SOL_PLOT_H - 1));
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
    /* X?a ri?ng khu v?c d? th? v? h?ng ch? th?ch ph?a tr?n. */
    ST7735_FillRectangle(0, 0, SOL_LCD_W, 94, ST7735_BLACK);

    /*
     * Ch? th?ch d?t tr?n m?t h?ng ri?ng, kh?ng n?m trong v?ng plot n?n
     * kh?ng b? du?ng d? th? ho?c c?c gi? tr? thang do che m?t.
     */
    ST7735_WriteString(2, 1,
                       "T:150-450C",
                       SOL_FONT_SMALL,
                       ST7735_RED,
                       ST7735_BLACK);

    ST7735_WriteString(72, 1,
                       "I:0-10A",
                       SOL_FONT_SMALL,
                       ST7735_CYAN,
                       ST7735_BLACK);

    /* Khung d? th? g?n s?t hai m?p m?n h?nh. */
    drawVline(SOL_GRAPH_X0, SOL_GRAPH_Y0, SOL_GRAPH_H, ST7735_RED);
    drawVline(SOL_GRAPH_X1, SOL_GRAPH_Y0, SOL_GRAPH_H, ST7735_CYAN);
    drawHline(SOL_GRAPH_X0, SOL_GRAPH_Y0, SOL_GRAPH_W, ST7735_WHITE);
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

    /* Hai gi? tr? quan tr?ng nh?t: nhi?t d? th?t v? nhi?t d? d?t. */
    ST7735_WriteString(2, 96, "TIP", SOL_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(66, 96, "SET", SOL_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);

    ST7735_WriteString(39, 111, "*C", SOL_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(103, 111, "*C", SOL_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);

    /* D?ng tr?ng th?i g?n ph?a tr?n c?c preset. */
    ST7735_WriteString(2, 128, "PWR", SOL_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
    ST7735_WriteString(58, 128, "W", SOL_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);

    drawHline(0, 138, 128, ST7735_BLUE);

    drawRect(0, 140, 41, 19, ST7735_GREEN);
    drawRect(43, 140, 41, 19, ST7735_YELLOW);
    drawRect(86, 140, 41, 19, ST7735_MAGENTA);

    ClearCache();
}

static void UpdatePresets(uint8_t force)
{
    char buf[16];

    /* Preset dang ch?n: d?. Hai preset c?n l?i: tr?ng. */
    uint16_t c1 = (sol.active_preset == 0) ? ST7735_RED : ST7735_WHITE;
    uint16_t c2 = (sol.active_preset == 1) ? ST7735_RED : ST7735_WHITE;
    uint16_t c3 = (sol.active_preset == 2) ? ST7735_RED : ST7735_WHITE;

    drawRect(0, 140, 41, 19, c1);
    drawRect(43, 140, 41, 19, c2);
    drawRect(86, 140, 41, 19, c3);

    /* Ch? hi?n th? nhi?t d?, kh?ng hi?n th? P1/P2/P3. */
    sprintf(buf, "%03ld", (long)(sol.preset[0] + 0.5f));
    WriteCached(c_p1,
                10, 146, 24,
                buf,
                SOL_FONT_SMALL,
                c1,
                ST7735_BLACK,
                force);

    sprintf(buf, "%03ld", (long)(sol.preset[1] + 0.5f));
    WriteCached(c_p2,
                53, 146, 24,
                buf,
                SOL_FONT_SMALL,
                c2,
                ST7735_BLACK,
                force);

    sprintf(buf, "%03ld", (long)(sol.preset[2] + 0.5f));
    WriteCached(c_p3,
                96, 146, 24,
                buf,
                SOL_FONT_SMALL,
                c3,
                ST7735_BLACK,
                force);
}

static void UpdateValues(uint8_t force)
{
    char buf[32];

    /* Nhi?t d? mui h?n th?c t?. */
    FmtNumber(buf, sol.tip_temp, 0);
    WriteCached(c_tip,
                2, 105, 36,
                buf,
                SOL_FONT_MEDIUM,
                ST7735_RED,
                ST7735_BLACK,
                force);

    /* Nhi?t d? d?t. */
    FmtNumber(buf, sol.set_temp, 0);
    WriteCached(c_set,
                66, 105, 36,
                buf,
                SOL_FONT_MEDIUM,
                ST7735_CYAN,
                ST7735_BLACK,
                force);

    /* C?ng su?t heater. */
    FmtNumber(buf, sol.power, 0);
    WriteCached(c_power,
                28, 128, 28,
                buf,
                SOL_FONT_SMALL,
                ST7735_MAGENTA,
                ST7735_BLACK,
                force);

    /* Kh?ng c?n hi?n th? CH1/CH2/CH3; khung d? ph?a du?i d? ch? preset dang ch?n. */
    WriteCached(c_ch,
                98, 128, 28,
                "   ",
                SOL_FONT_SMALL,
                ST7735_BLACK,
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
    sol.power = power;

    /* Gi? tham s? d? kh?ng ph?i s?a API cu, nhung kh?ng c?n hi?n th? FET. */
    (void)fet_temp;
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
uint16_t ccr = 0;
float power_ = 0;
//#define SOLIDER_CCR_MAX_POWER          500
//#define SOLIDER_CCR_OFF                999
static void Solider_SetPower(float power)
{
    power = clampf_solider(power, 0.0f, 1.0f);

    /*
       V? m?ch c?a b?n:
       power = 1.0 -> CCR = 500, m?nh nh?t
       power = 0.0 -> CCR = 999, t?t
    */
    ccr = (uint16_t)(SOLIDER_CCR_OFF - power * (float)(SOLIDER_CCR_OFF - SOLIDER_CCR_MAX_POWER));
    if(ccr > SOLIDER_CCR_OFF)
        ccr = SOLIDER_CCR_OFF;

    if(ccr < SOLIDER_CCR_MAX_POWER)
        ccr = SOLIDER_CCR_MAX_POWER;

    TIM3->CCR2 = ccr;

    solider_pid_power = power;
    solider_pwm_ccr = ccr;
}

static void Solider_PID_Reset(void)
{
    solider_pid_i = 0.0f;
    solider_pid_prev_err = 0.0f;

    solider_adc_sum = 0;
    solider_adc_count = 0;

    solider_pid_power = 0.0f;
    solider_pwm_ccr = SOLIDER_CCR_OFF;
		solider_current_sum = 0.0f;
		solider_current_count = 0;
		solider_current_avg = 0.0f;

		solider_current_filtered = 0.0f;
		solider_current_filter_init = 0;
    TIM3->CCR2 = SOLIDER_CCR_OFF;
}

void Solider_PID_Enable(uint8_t enable)
{
    if(enable)
    {
        if(solider_pid_state == SOLIDER_PID_IDLE)
        {
            Solider_PID_Reset();
            solider_pid_tick = HAL_GetTick();
            solider_pid_state = SOLIDER_PID_RUN;
        }
    }
    else
    {
        solider_pid_state = SOLIDER_PID_IDLE;
        Solider_PID_Reset();
    }
}

static float Solider_PID_Compute(float set_temp, float measured_temp)
{
    float err = set_temp - measured_temp;

    float p = SOLIDER_KP * err;
    float d = SOLIDER_KD * (err - solider_pid_prev_err) / SOLIDER_PID_DT;

    float i_new = solider_pid_i + SOLIDER_KI * err * SOLIDER_PID_DT;

    float out_unsat = p + i_new + d;

    if(!((out_unsat > 1.0f && err > 0.0f) ||
         (out_unsat < 0.0f && err < 0.0f)))
    {
        solider_pid_i = i_new;
    }

    solider_pid_i = clampf_solider(solider_pid_i, -0.5f, 1.0f);

    float out = p + solider_pid_i + d;
    out = clampf_solider(out, 0.0f, 1.0f);

    solider_pid_prev_err = err;

    return out;
}

void Solider_PID_Task(float set_adc)
{
    uint32_t now = HAL_GetTick();

    switch(solider_pid_state)
    {
        case SOLIDER_PID_IDLE:
        {
            TIM3->CCR2 = SOLIDER_CCR_OFF;
        }
        break;

        case SOLIDER_PID_RUN:
				{
						/*
						 * T?ch luy d?ng trong th?i gian heater dang b?t.
						 * Lo?i b? gi? tr? l?i ngo?i thang do.
						 */
						float current_sample = PowerStage.current;

						if(current_sample >= 0.0f &&
							 current_sample <= SOL_CURR_MAX)
						{
								solider_current_sum += current_sample;
								solider_current_count++;
						}

						if(now - solider_pid_tick >= SOLIDER_PID_PERIOD_MS)
						{
								solider_pid_tick = now;

								/*
								 * Trung b?nh c?c m?u d?ng c?a chu k? heater ON.
								 */
								if(solider_current_count > 0)
								{
										solider_current_avg =
												solider_current_sum /
												(float)solider_current_count;

										/*
										 * EMA ch? d?ng cho hi?n th? d? th?.
										 */
										if(solider_current_filter_init == 0)
										{
												solider_current_filtered = solider_current_avg;
												solider_current_filter_init = 1;
										}
										else
										{
												solider_current_filtered =
														(1.0f - SOL_CURRENT_EMA_ALPHA) *
														solider_current_filtered +
														SOL_CURRENT_EMA_ALPHA *
														solider_current_avg;
										}
								}

								solider_current_sum = 0.0f;
								solider_current_count = 0;

								/*
								 * T?t heater d? do nhi?t d?.
								 */
								TIM3->CCR2 = SOLIDER_CCR_OFF;

								solider_off_tick = now;
								solider_adc_sum = 0;
								solider_adc_count = 0;
								adc1_dma_ready = 0;

								solider_pid_state = SOLIDER_PID_OFF_WAIT;
						}
				}
				break;

        case SOLIDER_PID_OFF_WAIT:
        {
            if(now - solider_off_tick >= SOLIDER_OFF_READ_DELAY_MS)
            {
                solider_adc_sum = 0;
                solider_adc_count = 0;
                adc1_dma_ready = 0;

                solider_pid_state = SOLIDER_PID_READ_ADC;
            }
        }
        break;

        case SOLIDER_PID_READ_ADC:
        {
            if(adc1_dma_ready)
            {
                adc1_dma_ready = 0;

                solider_adc_sum += adc1_dma_buf[SOLIDER_ADC_INDEX];
                solider_adc_count++;

                if(solider_adc_count >= SOLIDER_ADC_AVG_N)
								{
										uint16_t adc_avg = solider_adc_sum / SOLIDER_ADC_AVG_N;
										solider_temp_raw = adc_avg;
										
										measured_temp = Solider_ADC_ToTemp(adc_avg);
										measured_temp = measured_temp * ALPHA_FIR + frev_measured_temp * (1.0f - ALPHA_FIR);
										frev_measured_temp = measured_temp;
										set_temp = UI_Solider_GetSetTemp();

										power_ = Solider_PID_Compute(set_temp, measured_temp);

										Solider_SetPower(power_);

										UI_Solider_SetData(measured_temp,
																			 solider_current_filtered,
																			 PowerStage.temp,
																			 solider_current_filtered * PowerStage.vout);

										solider_pid_state = SOLIDER_PID_RUN;
								}
            }
        }
        break;

        default:
        {
            solider_pid_state = SOLIDER_PID_IDLE;
            Solider_PID_Reset();
        }
        break;
    }
}