/*
 * UI_Solider.c
 *
 * ST7789 320x240 soldering station UI + non-blocking heater control.
 *
 * Features:
 *   - TIM3 CH2 heater PWM
 *   - PID/ADC loop independent from LCD refresh
 *   - LCD update by SOL_LCD_MS
 *   - SLEEP mode through UI_Solider_SetSleep()
 *   - buzzer events: enter SOLDER, enter SLEEP, target reached
 *   - graph:
 *       temperature : 0..500 C
 *       current     : 0..10 A
 *   - graph thickness: 2 pixels
 *   - temporary temperature conversion:
 *       temp = (adc_raw - 1200) * 1.3
 */

#include "UI_Solider.h"
#include "st7789.h"
#include "UI.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
 * SOLDER RANGE / DISPLAY
 * ========================================================= */

#define SOL_TEMP_MIN                    0.0f
#define SOL_TEMP_MAX                  500.0f

#define SOL_CURR_MIN                  0.0f
#define SOL_CURR_MAX                  10.0f


#define SOL_SET_MIN                   150.0f
#define SOL_SET_MAX                   450.0f
#define SOL_SET_STEP                  5.0f

#define SOL_LCD_MS                    200U


/*
 * Buzzer target detection:
 * target must remain within +/-5 C for 800 ms.
 * A new target beep is re-armed after moving >=15 C away.
 */
#define TARGET_BEEP_BAND_C               5.0f
#define TARGET_BEEP_HOLD_MS              800U

/*
 * TARGET_REACHED is ONE-SHOT for each requested target.
 * It is re-armed only when the user/system changes target,
 * not when temperature oscillates during normal regulation.
 */

static uint8_t sol_theme_light = 0U;

#define SOL_BG                        (sol_theme_light ? ST7789_COLOR_WHITE : ST7789_COLOR_BLACK)
#define SOL_TEMP_COLOR                ST7789_COLOR_RED
#define SOL_CURR_COLOR                (sol_theme_light ? ST7789_COLOR_BLUE : ST7789_COLOR_CYAN)
#define SOL_SET_COLOR                 (sol_theme_light ? ST7789_COLOR_BLUE : ST7789_COLOR_CYAN)
#define SOL_TEXT_COLOR                (sol_theme_light ? ST7789_COLOR_BLACK : ST7789_COLOR_WHITE)
#define SOL_MUTED_COLOR               (sol_theme_light ? ST7789_Color_GetFromRGB(75, 75, 75) : ST7789_COLOR_LIGHTGREY)
#define SOL_PWR_COLOR                 ST7789_Color_GetFromRGB(255, 0, 255)
#define SOL_GRID_COLOR                (sol_theme_light ? ST7789_Color_GetFromRGB(205, 205, 205) : ST7789_Color_GetFromRGB(42, 48, 60))

/* =========================================================
 * GRAPH - landscape 320 x 240
 * ========================================================= */

#define SOL_GRAPH_X0                  6
#define SOL_GRAPH_X1                  313
#define SOL_GRAPH_Y0                  20
#define SOL_GRAPH_Y1                  124

#define SOL_GRAPH_W                   (SOL_GRAPH_X1 - SOL_GRAPH_X0 + 1)
#define SOL_GRAPH_H                   (SOL_GRAPH_Y1 - SOL_GRAPH_Y0 + 1)

#define SOL_PLOT_X0                   (SOL_GRAPH_X0 + 1)
#define SOL_PLOT_X1                   (SOL_GRAPH_X1 - 1)
#define SOL_PLOT_Y0                   (SOL_GRAPH_Y0 + 1)
#define SOL_PLOT_Y1                   (SOL_GRAPH_Y1 - 1)

#define SOL_PLOT_W                    (SOL_PLOT_X1 - SOL_PLOT_X0 + 1)
#define SOL_PLOT_H                    (SOL_PLOT_Y1 - SOL_PLOT_Y0 + 1)

/* =========================================================
 * SOLDER CONTROL - C245 COMMERCIAL-STYLE BURST CONTROL
 *
 * Measured commercial waveform:
 *
 *   HEAT:
 *       ON  = 45 ms
 *       OFF =  5 ms
 *       frame = 50 ms
 *
 *   HOLD:
 *       ON  =  5 ms
 *       OFF = 45 ms
 *       frame = 50 ms
 *
 * Temperature is sampled only while the heater is OFF.
 *
 * NOTE ABOUT TIM3 CH2:
 * Existing hardware uses inverted PWM polarity:
 *
 *     CCR = 1000 -> heater OFF
 *     CCR -> 0    -> heater fully ON
 *
 * During the active burst window TIM3 produces fast PWM.
 * During the sensing window the heater is forced completely OFF.
 * ========================================================= */

#define SOLIDER_ADC_INDEX                 2

#define SOLIDER_FRAME_MS                 50U

#define SOLIDER_HEAT_ON_MS               45U
#define SOLIDER_HEAT_OFF_MS               5U

#define SOLIDER_HOLD_ON_MS                5U
#define SOLIDER_HOLD_OFF_MS              45U

#define SOLIDER_FULL_OFF_MS              50U

/*
 * In the 5-ms OFF window of HEAT mode:
 *
 *   first 2 ms -> switching/sense settling
 *   remaining  -> collect ADC samples
 */
#define SOLIDER_ADC_SETTLE_MS              2U
#define SOLIDER_ADC_MAX_SAMPLES            8U

/*
 * Temperature filtering.
 *
 * One new temperature result is produced roughly once per 50-ms frame.
 */
#define ALPHA_FIR                         0.25f

/*
 * Hysteresis around target.
 *
 * HEAT -> HOLD when temperature reaches within 2 C of target.
 * HOLD -> HEAT only after temperature falls 10 C below target.
 *
 * This prevents HEAT/HOLD chatter from ADC noise.
 */
#define SOLIDER_ENTER_HEAT_ERR_C          10.0f
#define SOLIDER_ENTER_HOLD_ERR_C           2.0f

/*
 * If tip overshoots more than this, stop heating for a full frame.
 * Heating resumes as HOLD after it cools back close to target.
 */
#define SOLIDER_OVER_TEMP_OFF_C            5.0f
#define SOLIDER_OVER_TEMP_RECOVER_C        1.0f

#define SOLIDER_TEMP_MIN_C                25.0f
#define SOLIDER_TEMP_MAX_C               500.0f

#define SOL_CURRENT_EMA_ALPHA             0.06f

/*
 * =========================================================
 * INNER HEATER PWM
 *
 * Outer burst remains:
 *
 *   HEAT = 45 ms active / 5 ms OFF
 *   HOLD =  5 ms active / 45 ms OFF
 *
 * During the ACTIVE portion, the heater is no longer driven
 * continuously ON. TIM3 CH2 generates fast PWM.
 *
 * main.c configures TIM3 to about 24 kHz.
 *
 * Current hardware polarity:
 *
 *   CCR = ARR -> heater OFF
 *   CCR = 0   -> heater 100 % ON
 *
 * Therefore:
 *
 *   CCR = ARR * (1 - heater_duty)
 *
 * Default target duty = 80 %.
 *
 * Each ACTIVE pulse begins at 35 % and ramps to 80 % during
 * the first 2 ms. This reduces the abrupt load step on Cout,
 * the buck-boost converter and the C245 heater.
 * ========================================================= */

#define SOLIDER_PWM_HEAT_DUTY             0.80f
#define SOLIDER_PWM_HOLD_DUTY             0.80f

#define SOLIDER_PWM_START_DUTY            0.35f
#define SOLIDER_PWM_RAMP_MS                2U

#define SOLIDER_PWM_CCR                   (TIM3->CCR2)


extern volatile uint8_t adc1_dma_ready;
extern uint16_t adc1_dma_buf[];
extern BBUI_Data_t PowerStage;


/*
 * Keep the old internal state names so the public API and
 * sleep/enable code require minimal changes.
 *
 * RUN      = burst ON phase / frame start
 * OFF_WAIT = burst OFF phase + ADC sensing
 */
typedef enum
{
    SOLIDER_PID_IDLE = 0,
    SOLIDER_PID_RUN,
    SOLIDER_PID_OFF_WAIT,
    SOLIDER_PID_READ_ADC
} SoliderPID_State_t;


typedef enum
{
    SOLIDER_BURST_HEAT = 0,   /* 45 ms ON / 5 ms OFF */
    SOLIDER_BURST_HOLD,       /* 5 ms ON / 45 ms OFF */
    SOLIDER_BURST_OFF         /* 0 ms ON / 50 ms OFF */
} SoliderBurstMode_t;


static SoliderPID_State_t
solider_pid_state =
    SOLIDER_PID_IDLE;

static SoliderBurstMode_t
solider_burst_mode =
    SOLIDER_BURST_HEAT;


/* phase timing */
static uint32_t solider_pid_tick = 0U;
static uint32_t solider_off_tick = 0U;
static uint8_t solider_phase_started = 0U;

static uint32_t solider_on_ms = SOLIDER_HEAT_ON_MS;
static uint32_t solider_off_ms = SOLIDER_HEAT_OFF_MS;


/* ADC sensing during OFF phase */
static uint32_t solider_adc_sum = 0U;
static uint16_t solider_adc_count = 0U;


/* current measurement during ON phase */
static float solider_current_sum = 0.0f;
static uint16_t solider_current_count = 0U;

static float solider_current_avg = 0.0f;
static float solider_current_filtered = 0.0f;
static uint8_t solider_current_filter_init = 0U;


/* temperature */
static uint8_t solider_temp_filter_init = 0U;

volatile float solider_temp_raw = 0.0f;

/*
 * Retained for compatibility/debug:
 *
 * HEAT = 0.90
 * HOLD = 0.10
 * OFF  = 0.00
 */
volatile float solider_pid_power = 0.0f;
volatile uint16_t solider_pwm_ccr = 1000U;

volatile float measured_temp = 25.0f;
volatile float frev_measured_temp = 25.0f;
volatile float set_temp = 350.0f;


/* useful Keil Watch variables */
volatile uint8_t solider_burst_debug = 0U;
volatile uint32_t solider_debug_on_ms = 0U;
volatile uint32_t solider_debug_off_ms = 0U;
volatile float solider_debug_error = 0.0f;

/*
 * Actual inner PWM duty applied during the ACTIVE phase.
 */
volatile float solider_pwm_duty_debug = 0.0f;


/* Forward declarations */
static float clampf_solider(float x,
                            float min,
                            float max);

static void Solider_SetPwmDuty(float duty);
static float Solider_GetTargetPwmDuty(void);
static void Solider_UpdatePwmRamp(uint32_t on_elapsed_ms);

static void Solider_HeaterOn(void);
static void Solider_HeaterOff(void);

static void Solider_PID_Reset(void);

static void Solider_SelectBurstMode(
    float target_temp,
    float tip_temp
);

static void Solider_StartFrame(void);

static void Solider_FinishCurrentMeasurement(void);

static void Solider_ProcessTemperatureMeasurement(void);

/* =========================================================
 * INTERNAL
 * ========================================================= */

static UI_Solider_Data_t sol;

static uint8_t sol_active = 0U;
static uint8_t sol_sleep = 0U;
static float sol_sleep_temp = 200.0f;

static uint8_t sol_event_flags = 0U;

static uint8_t target_beep_armed = 1U;
static uint8_t target_beep_in_band = 0U;
static uint32_t target_beep_tick = 0U;
static uint8_t force_redraw = 1U;
static uint8_t values_dirty = 1U;

/*
 * Left-scrolling strip-chart history.
 *
 * New sample appears at the right edge.
 * Previous samples move left by 2 pixels.
 */
#define SOL_GRAPH_SCROLL_STEP_PX       2U
#define SOL_GRAPH_HISTORY_CAP          (((SOL_PLOT_W - 1U) / SOL_GRAPH_SCROLL_STEP_PX) + 1U)

static int16_t graph_hist_t[SOL_GRAPH_HISTORY_CAP];
static int16_t graph_hist_i[SOL_GRAPH_HISTORY_CAP];
static uint16_t graph_hist_count = 0U;

static uint8_t graph_new_sample = 0U;
static uint32_t lcd_tick = 0U;

/* caches */
static char c_tip[16] = "";
static char c_set[16] = "";
static char c_pwr[16] = "";
static char c_current[16] = "";
static char c_preset0[8] = "";
static char c_preset1[8] = "";
static char c_preset2[8] = "";
static char c_sleep_tip[16] = "";
static char c_sleep_set[16] = "";

static uint8_t old_active_preset = 0xFFU;

/* =========================================================
 * HELPERS
 * ========================================================= */

static float clampf_sol(float x, float lo, float hi)
{
    if(x < lo)
        return lo;

    if(x > hi)
        return hi;

    return x;
}

static void clear_caches(void)
{
    c_tip[0] = '\0';
    c_set[0] = '\0';
    c_pwr[0] = '\0';
    c_current[0] = '\0';

    c_preset0[0] = '\0';
    c_preset1[0] = '\0';
    c_preset2[0] = '\0';
    c_sleep_tip[0] = '\0';
    c_sleep_set[0] = '\0';

    old_active_preset = 0xFFU;
}

static void draw_rect_outline(uint16_t x,
                              uint16_t y,
                              uint16_t w,
                              uint16_t h,
                              uint16_t color)
{
    if(w < 2U || h < 2U)
        return;

    ST7789_DrawFilledRectangle(x, y, w, 1U, color);
    ST7789_DrawFilledRectangle(x, (uint16_t)(y + h - 1U), w, 1U, color);
    ST7789_DrawFilledRectangle(x, y, 1U, h, color);
    ST7789_DrawFilledRectangle((uint16_t)(x + w - 1U), y, 1U, h, color);
}

static void draw_line_fast(int x0,
                           int y0,
                           int x1,
                           int y1,
                           uint16_t color)
{
    int dx =
        (x1 > x0) ? (x1 - x0) : (x0 - x1);

    int sx =
        (x0 < x1) ? 1 : -1;

    int dy =
        (y1 > y0) ? (y0 - y1) : (y1 - y0);

    int sy =
        (y0 < y1) ? 1 : -1;

    int err = dx + dy;

    while(1)
    {
        if(x0 >= 0 && x0 < 320 &&
           y0 >= 0 && y0 < 240)
        {
            ST7789_DrawPixel(
                (uint16_t)x0,
                (uint16_t)y0,
                color
            );

            /*
             * Second pixel -> graph thickness = 2.
             */
            if(y0 + 1 < 240)
            {
                ST7789_DrawPixel(
                    (uint16_t)x0,
                    (uint16_t)(y0 + 1),
                    color
                );
            }
        }

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

static int map_y(float value, float min, float max)
{
    value = clampf_sol(value, min, max);

    float k = (value - min) / (max - min);

    return SOL_PLOT_Y1 -
           (int)(k * (float)(SOL_PLOT_H - 1));
}

static void draw_text_diff(uint16_t x,
                           uint16_t y,
                           uint8_t scale,
                           uint16_t fg,
                           const char *text,
                           char *cache,
                           uint16_t cache_size)
{
    size_t new_len = strlen(text);
    size_t old_len = strlen(cache);
    size_t count = (new_len > old_len) ? new_len : old_len;

    if(count > (size_t)(cache_size - 1U))
        count = (size_t)(cache_size - 1U);

    uint16_t cell_w = (uint16_t)(6U * scale);
    uint16_t cell_h = (uint16_t)(8U * scale);

    for(size_t i = 0U; i < count; i++)
    {
        char nc = (i < new_len) ? text[i] : '\0';
        char oc = (i < old_len) ? cache[i] : '\0';

        if(nc == oc)
            continue;

        uint16_t cx = (uint16_t)(x + i * cell_w);

        ST7789_DrawFilledRectangle(cx,
                                   y,
                                   cell_w,
                                   cell_h,
                                   SOL_BG);

        if(nc != '\0')
        {
            char ch[2] = { nc, '\0' };

            ST7789_PutString(cx,
                             y,
                             ch,
                             scale,
                             fg,
                             SOL_BG);
        }
    }

    strncpy(cache, text, cache_size - 1U);
    cache[cache_size - 1U] = '\0';
}

/* =========================================================
 * GRAPH
 * ========================================================= */

static void graph_draw_grid_inside(void)
{
    for(uint8_t k = 1U; k < 4U; k++)
    {
        uint16_t y =
            (uint16_t)(
                SOL_GRAPH_Y0 +
                (SOL_GRAPH_H * k) / 4U
            );

        for(uint16_t x = SOL_PLOT_X0;
            x <= SOL_PLOT_X1;
            x += 4U)
        {
            ST7789_DrawPixel(
                x,
                y,
                SOL_GRID_COLOR
            );
        }
    }
}


/*
 * Erase only the old curves instead of clearing the complete graph.
 * This removes the visible blank frame that caused TFT flicker.
 */
static void graph_erase_history(void)
{
    if(graph_hist_count == 0U)
        return;

    int first_x =
        SOL_PLOT_X1 -
        (int)(
            (graph_hist_count - 1U) *
            SOL_GRAPH_SCROLL_STEP_PX
        );

    if(graph_hist_count == 1U)
    {
        ST7789_DrawPixel(
            SOL_PLOT_X1,
            (uint16_t)graph_hist_t[0],
            SOL_BG
        );

        ST7789_DrawPixel(
            SOL_PLOT_X1,
            (uint16_t)graph_hist_i[0],
            SOL_BG
        );

        return;
    }

    for(uint16_t n = 1U;
        n < graph_hist_count;
        n++)
    {
        int x0 =
            first_x +
            (int)(
                (n - 1U) *
                SOL_GRAPH_SCROLL_STEP_PX
            );

        int x1 =
            first_x +
            (int)(
                n *
                SOL_GRAPH_SCROLL_STEP_PX
            );

        draw_line_fast(
            x0,
            graph_hist_t[n - 1U],
            x1,
            graph_hist_t[n],
            SOL_BG
        );

        draw_line_fast(
            x0,
            graph_hist_i[n - 1U],
            x1,
            graph_hist_i[n],
            SOL_BG
        );
    }
}


static void draw_graph_static(void)
{
    /* legend */
    ST7789_PutString(
        7,
        3,
        "T 0-500C",
        1,
        SOL_TEMP_COLOR,
        SOL_BG
    );

    ST7789_PutString(
        102,
        3,
        "I 0-10A",
        1,
        SOL_CURR_COLOR,
        SOL_BG
    );

    ST7789_PutString(
        258,
        3,
        "SOLDER",
        1,
        ST7789_COLOR_YELLOW,
        SOL_BG
    );

    /* border */
    draw_rect_outline(
        SOL_GRAPH_X0,
        SOL_GRAPH_Y0,
        SOL_GRAPH_W,
        SOL_GRAPH_H,
        SOL_TEXT_COLOR
    );

    graph_draw_grid_inside();

    graph_hist_count =
        0U;
}


static void graph_render_history(void)
{
    if(graph_hist_count == 0U)
        return;

    int first_x =
        SOL_PLOT_X1 -
        (int)(
            (graph_hist_count - 1U) *
            SOL_GRAPH_SCROLL_STEP_PX
        );

    for(uint16_t n = 1U;
        n < graph_hist_count;
        n++)
    {
        int x0 =
            first_x +
            (int)(
                (n - 1U) *
                SOL_GRAPH_SCROLL_STEP_PX
            );

        int x1 =
            first_x +
            (int)(
                n *
                SOL_GRAPH_SCROLL_STEP_PX
            );

        draw_line_fast(
            x0,
            graph_hist_t[n - 1U],
            x1,
            graph_hist_t[n],
            SOL_TEMP_COLOR
        );

        draw_line_fast(
            x0,
            graph_hist_i[n - 1U],
            x1,
            graph_hist_i[n],
            SOL_CURR_COLOR
        );
    }

    if(graph_hist_count == 1U)
    {
        ST7789_DrawPixel(
            SOL_PLOT_X1,
            (uint16_t)graph_hist_t[0],
            SOL_TEMP_COLOR
        );

        ST7789_DrawPixel(
            SOL_PLOT_X1,
            (uint16_t)graph_hist_i[0],
            SOL_CURR_COLOR
        );
    }
}


static void graph_push(float temp,
                       float current)
{
    int yt =
        map_y(
            temp,
            SOL_TEMP_MIN,
            SOL_TEMP_MAX
        );

    int yi =
        map_y(
            current,
            SOL_CURR_MIN,
            SOL_CURR_MAX
        );

    /*
     * Remove only the currently visible curves.
     * Do NOT FillRectangle() the complete plot.
     */
    graph_erase_history();

    if(graph_hist_count <
       SOL_GRAPH_HISTORY_CAP)
    {
        graph_hist_t[graph_hist_count] =
            (int16_t)yt;

        graph_hist_i[graph_hist_count] =
            (int16_t)yi;

        graph_hist_count++;
    }
    else
    {
        for(uint16_t n = 1U;
            n < SOL_GRAPH_HISTORY_CAP;
            n++)
        {
            graph_hist_t[n - 1U] =
                graph_hist_t[n];

            graph_hist_i[n - 1U] =
                graph_hist_i[n];
        }

        graph_hist_t[SOL_GRAPH_HISTORY_CAP - 1U] =
            (int16_t)yt;

        graph_hist_i[SOL_GRAPH_HISTORY_CAP - 1U] =
            (int16_t)yi;
    }

    /*
     * Curves may have erased some dotted grid pixels.
     */
    graph_draw_grid_inside();

    graph_render_history();
}


static void graph_task(void)
{
    /*
     * New PID/ADC samples only mark graph_new_sample.
     * Actual graph drawing happens when UI_Solider_Task()
     * reaches the SOL_LCD_MS display tick.
     */
    if(graph_new_sample == 0U)
        return;

    graph_new_sample = 0U;

    graph_push(sol.tip_temp,
               sol.current);
}

/* =========================================================
 * VALUES / PRESETS
 * ========================================================= */

static void draw_base(void)
{
    ST7789_FillScreen(SOL_BG);

    draw_graph_static();

    ST7789_DrawFilledRectangle(0, 132, 320, 1, SOL_GRID_COLOR);

    ST7789_PutString(8, 138,
                     "TIP",
                     2,
                     SOL_TEMP_COLOR,
                     SOL_BG);

    ST7789_PutString(166, 138,
                     "SET",
                     2,
                     SOL_SET_COLOR,
                     SOL_BG);

    ST7789_PutString(8, 181,
                     "PWR",
                     1,
                     SOL_PWR_COLOR,
                     SOL_BG);

    ST7789_PutString(118, 181,
                     "I",
                     1,
                     SOL_CURR_COLOR,
                     SOL_BG);

    ST7789_PutString(225, 181,
                     "V knob: +/-5C",
                     1,
                     SOL_MUTED_COLOR,
                     SOL_BG);

    ST7789_DrawFilledRectangle(0, 202, 320, 1, SOL_GRID_COLOR);

    clear_caches();
}

static void draw_preset_box(uint8_t id,
                            uint16_t x,
                            const char *value,
                            char *cache)
{
    uint16_t color =
        (sol.active_preset == id)
        ? ST7789_COLOR_RED
        : ST7789_COLOR_WHITE;

    /*
     * Erasing old border is cheap and necessary when selection changes.
     */
    draw_rect_outline(x, 207, 98, 30, color);

    draw_text_diff((uint16_t)(x + 28U),
                   214,
                   2,
                   color,
                   value,
                   cache,
                   8U);
}

static void update_presets(uint8_t force)
{
    char b0[8];
    char b1[8];
    char b2[8];

    snprintf(b0, sizeof(b0), "%03u",
             (unsigned)(sol.preset[0] + 0.5f));

    snprintf(b1, sizeof(b1), "%03u",
             (unsigned)(sol.preset[1] + 0.5f));

    snprintf(b2, sizeof(b2), "%03u",
             (unsigned)(sol.preset[2] + 0.5f));

    if(force ||
       old_active_preset != sol.active_preset)
    {
        /*
         * Clear only the thin preset region when selection changes,
         * then redraw all three borders/text colors.
         */
        ST7789_DrawFilledRectangle(3, 205, 314, 34, SOL_BG);

        c_preset0[0] = '\0';
        c_preset1[0] = '\0';
        c_preset2[0] = '\0';

        old_active_preset = sol.active_preset;
    }

    draw_preset_box(0U, 4U,   b0, c_preset0);
    draw_preset_box(1U, 111U, b1, c_preset1);
    draw_preset_box(2U, 218U, b2, c_preset2);
}

static void update_values(uint8_t force)
{
    char buf[24];

    snprintf(buf, sizeof(buf), "%03u",
             (unsigned)clampf_sol(sol.tip_temp, 0.0f, 999.0f));

    if(force)
        c_tip[0] = '\0';

    draw_text_diff(47,
                   137,
                   4,
                   SOL_TEMP_COLOR,
                   buf,
                   c_tip,
                   sizeof(c_tip));

    ST7789_PutString(121, 157,
                     "C",
                     2,
                     SOL_TEMP_COLOR,
                     SOL_BG);

    snprintf(buf, sizeof(buf), "%03u",
             (unsigned)clampf_sol(sol.set_temp, 0.0f, 999.0f));

    if(force)
        c_set[0] = '\0';

    draw_text_diff(205,
                   137,
                   4,
                   SOL_SET_COLOR,
                   buf,
                   c_set,
                   sizeof(c_set));

    ST7789_PutString(279, 157,
                     "C",
                     2,
                     SOL_SET_COLOR,
                     SOL_BG);

    snprintf(buf, sizeof(buf), "%.0fW", sol.power);

    if(force)
        c_pwr[0] = '\0';

    draw_text_diff(38,
                   177,
                   2,
                   SOL_PWR_COLOR,
                   buf,
                   c_pwr,
                   sizeof(c_pwr));

    snprintf(buf, sizeof(buf), "%.2fA", sol.current);

    if(force)
        c_current[0] = '\0';

    draw_text_diff(135,
                   177,
                   2,
                   SOL_CURR_COLOR,
                   buf,
                   c_current,
                   sizeof(c_current));

    update_presets(force);
}

/* =========================================================
 * SOLDER EVENT / TARGET BEEP
 * ========================================================= */

static void solder_event_set(uint8_t event_bit)
{
    sol_event_flags |= event_bit;
}

static void target_beep_reset(void)
{
    target_beep_armed = 1U;
    target_beep_in_band = 0U;
    target_beep_tick = HAL_GetTick();
}

static void target_beep_task(void)
{
    if(!sol_active)
        return;

    /*
     * Once TARGET_REACHED has fired, stay disarmed until
     * target_beep_reset() is explicitly called by:
     *
     *   - entering SOLDER
     *   - entering/leaving SLEEP
     *   - changing preset
     *   - changing set temperature with encoder
     *
     * Normal temperature oscillation must NOT re-arm the beep.
     */
    if(!target_beep_armed)
        return;

    float target =
        UI_Solider_GetSetTemp();

    float err =
        sol.tip_temp -
        target;

    if(err < 0.0f)
        err = -err;

    if(err <= TARGET_BEEP_BAND_C)
    {
        uint32_t now =
            HAL_GetTick();

        if(!target_beep_in_band)
        {
            target_beep_in_band =
                1U;

            target_beep_tick =
                now;

            return;
        }

        /*
         * Require the temperature to remain inside the band
         * continuously before announcing TARGET_REACHED.
         */
        if((uint32_t)(
               now -
               target_beep_tick
           ) >=
           TARGET_BEEP_HOLD_MS)
        {
            target_beep_armed =
                0U;

            target_beep_in_band =
                0U;

            solder_event_set(
                UI_SOLDER_EVENT_TARGET_REACHED
            );
        }
    }
    else
    {
        /*
         * Not yet stable at target.
         * Restart only the in-band hold timer.
         * Do NOT re-arm after a completed target beep.
         */
        target_beep_in_band =
            0U;
    }
}



/* =========================================================
 * SLEEP UI
 * ========================================================= */
static void draw_sleep_base(void)
{
    ST7789_FillScreen(SOL_BG);

    ST7789_PutString(
        124,
        10,
        "SOLDER",
        2,
        SOL_MUTED_COLOR,
        SOL_BG
    );

    ST7789_PutString(
        84,
        45,
        "SLEEP",
        5,
        SOL_SET_COLOR,
        SOL_BG
    );

    ST7789_DrawFilledRectangle(
        30,
        102,
        260,
        1,
        SOL_GRID_COLOR
    );

    ST7789_PutString(
        42,
        122,
        "TIP",
        2,
        SOL_TEMP_COLOR,
        SOL_BG
    );

    ST7789_PutString(
        184,
        122,
        "SLEEP SET",
        1,
        SOL_SET_COLOR,
        SOL_BG
    );

    ST7789_PutString(
        54,
        196,
        "PB11 LOW - IRON IN HOLDER",
        1,
        SOL_MUTED_COLOR,
        SOL_BG
    );

    c_sleep_tip[0] = '\0';
    c_sleep_set[0] = '\0';
}

static void update_sleep_values(uint8_t force)
{
    char buf[16];

    if(force)
    {
        c_sleep_tip[0] = '\0';
        c_sleep_set[0] = '\0';
    }

    snprintf(
        buf,
        sizeof(buf),
        "%03uC",
        (unsigned)clampf_sol(
            sol.tip_temp,
            0.0f,
            999.0f
        )
    );

    draw_text_diff(
        34,
        146,
        3,
        SOL_TEMP_COLOR,
        buf,
        c_sleep_tip,
        sizeof(c_sleep_tip)
    );

    snprintf(
        buf,
        sizeof(buf),
        "%03uC",
        (unsigned)clampf_sol(
            sol_sleep_temp,
            0.0f,
            999.0f
        )
    );

    draw_text_diff(
        182,
        146,
        3,
        SOL_SET_COLOR,
        buf,
        c_sleep_set,
        sizeof(c_sleep_set)
    );
}

/* =========================================================
 * PUBLIC API
 * ========================================================= */

void UI_Solider_Init(void)
{
    memset(&sol, 0, sizeof(sol));

    sol.tip_temp = 25.0f;
    sol.current = 0.0f;
    sol.fet_temp = 25.0f;
    sol.power = 0.0f;
    sol.vout = 0.0f;

    sol.preset[0] = 300.0f;
    sol.preset[1] = 350.0f;
    sol.preset[2] = 400.0f;

    sol.active_preset = 1U;
    sol.set_temp = sol.preset[1];

    sol_active = 0U;
    sol_sleep = 0U;
    sol_sleep_temp = 200.0f;

    sol_event_flags = 0U;
    target_beep_armed = 1U;
    target_beep_in_band = 0U;
    target_beep_tick = HAL_GetTick();
    force_redraw = 1U;
    values_dirty = 1U;

    graph_hist_count = 0U;
    graph_new_sample = 0U;

    lcd_tick = HAL_GetTick();

    clear_caches();
}

void UI_Solider_Enter(void)
{
    sol_active = 1U;
    sol_sleep = 0U;

    target_beep_reset();
    solder_event_set(UI_SOLDER_EVENT_ENTER);
    force_redraw = 1U;
    values_dirty = 1U;

    graph_hist_count = 0U;
    graph_new_sample = 0U;

    lcd_tick = HAL_GetTick();

    clear_caches();

    /*
     * New SOLDER session:
     * discard any old temperature-filter history.
     * Sleep/wake transitions do NOT clear this history.
     */
    solider_temp_filter_init = 0U;
    measured_temp = 25.0f;
    frev_measured_temp = 25.0f;

    /* Start 50-ms C245 burst controller. */
    Solider_PID_Enable(1U);
}

void UI_Solider_Exit(void)
{
    sol_active = 0U;
    sol_sleep = 0U;
    Solider_PID_Enable(0U);
}

uint8_t UI_Solider_IsActive(void)
{
    return sol_active;
}

uint8_t UI_Solider_IsSleeping(void)
{
    return sol_sleep;
}

uint8_t UI_Solider_GetEvents(void)
{
    uint8_t events = sol_event_flags;
    sol_event_flags = 0U;
    return events;
}

void UI_Solider_SetTheme(uint8_t light)
{
    uint8_t new_theme =
        light ? 1U : 0U;

    if(sol_theme_light == new_theme)
        return;

    sol_theme_light = new_theme;

    force_redraw = 1U;
    values_dirty = 1U;

    clear_caches();
}

void UI_Solider_SetSleep(uint8_t sleep,
                         float sleep_temp)
{
    sol_sleep_temp =
        clampf_sol(
            sleep_temp,
            100.0f,
            350.0f
        );

    uint8_t new_sleep =
        sleep ? 1U : 0U;

    if(sol_sleep != new_sleep)
    {
        sol_sleep = new_sleep;

        /*
         * New target after entering/leaving sleep.
         * Re-arm the target reached beep.
         */
        target_beep_reset();

        if(sol_sleep)
        {
            /*
             * Iron placed in holder -> beep once immediately.
             */
            solder_event_set(
                UI_SOLDER_EVENT_SLEEP_ENTER
            );
        }

        /*
         * Reset PID internal state for the target step.
         */
        Solider_PID_Reset();

        if(sol_active)
        {
            solider_pid_tick = HAL_GetTick();
            solider_pid_state = SOLIDER_PID_RUN;
        }

        force_redraw = 1U;
        values_dirty = 1U;
        clear_caches();
    }
    else if(sol_sleep)
    {
        values_dirty = 1U;
    }
}

void UI_Solider_SetData(float tip_temp,
                        float current,
                        float fet_temp,
                        float power,
                        float vout)
{
    sol.tip_temp = tip_temp;
    sol.current = current;
    sol.fet_temp = fet_temp;
    sol.power = power;
    sol.vout = vout;

    values_dirty = 1U;
    graph_new_sample = 1U;
}

float UI_Solider_GetSetTemp(void)
{
    return sol_sleep ? sol_sleep_temp : sol.set_temp;
}

void UI_Solider_SetPreset(uint8_t id,
                          float temp)
{
    if(id > 2U)
        return;

    temp = clampf_sol(temp,
                      SOL_SET_MIN,
                      SOL_SET_MAX);

    sol.preset[id] = temp;

    if(sol.active_preset == id)
    {
        sol.set_temp = temp;

        /*
         * A new active target deserves one new TARGET_REACHED beep.
         */
        target_beep_reset();
    }

    values_dirty = 1U;
}

void UI_Solider_SelectPreset(uint8_t id)
{
    if(sol_sleep)
        return;

    if(id > 2U)
        return;

    sol.active_preset = id;
    sol.set_temp = sol.preset[id];

    target_beep_reset();

    values_dirty = 1U;
}

void UI_Solider_EncoderAdjust(int8_t dir)
{
    if(sol_sleep)
        return;

    if(dir == 0)
        return;

    sol.set_temp +=
        (float)dir * SOL_SET_STEP;

    sol.set_temp =
        clampf_sol(sol.set_temp,
                   SOL_SET_MIN,
                   SOL_SET_MAX);

    /*
     * Same behavior as the old solder UI:
     * editing SET also updates the active preset.
     */
    sol.preset[sol.active_preset] =
        sol.set_temp;

    /*
     * New requested temperature -> allow one new reached-target beep.
     */
    target_beep_reset();

    values_dirty = 1U;
}

void UI_Solider_Task(uint8_t force)
{
    if(!sol_active)
        return;

    /*
     * Control loop remains fast and completely independent
     * from the LCD refresh rate.
     */
    Solider_PID_Task(0.0f);

    /*
     * Target-reached detection runs independently of LCD refresh.
     */
    target_beep_task();

    uint32_t now = HAL_GetTick();

    /*
     * Force redraw is the only case allowed to redraw immediately.
     * Normal sensor/PID updates only set values_dirty/graph_new_sample
     * and wait for SOL_LCD_MS.
     */
    if(force || force_redraw)
    {
        force_redraw = 0U;

        if(sol_sleep)
        {
            draw_sleep_base();
            update_sleep_values(1U);
        }
        else
        {
            draw_base();
            update_values(1U);
        }

        values_dirty = 0U;
        graph_new_sample = 0U;
        lcd_tick = now;
        return;
    }

    /*
     * Do not touch the ST7789 until the LCD period has elapsed.
     * PID/ADC can update many times between two LCD frames.
     */
    if((uint32_t)(now - lcd_tick) < SOL_LCD_MS)
        return;

    lcd_tick = now;

    if(sol_sleep)
    {
        if(values_dirty)
        {
            values_dirty = 0U;
            update_sleep_values(0U);
        }

        /*
         * No graph is drawn in SLEEP mode.
         * Discard pending graph sample so it does not appear on wake.
         */
        graph_new_sample = 0U;
        return;
    }

    /*
     * Graph is also rate-limited by SOL_LCD_MS.
     * If several PID samples arrived during one LCD period,
     * graph_task() plots only the most recent value.
     */
    graph_task();

    if(values_dirty)
    {
        values_dirty = 0U;
        update_values(0U);
    }
}

/* =========================================================
 * SOLDER ADC -> TEMPERATURE
 *
 * Temporary calibration currently used:
 *
 *     temp = (ADC - 1200) * 1.3
 *
 * Replace offset/gain after real 2-point calibration.
 * ========================================================= */

static float clampf_solider(float x,
                            float min,
                            float max)
{
    if(x < min)
        return min;

    if(x > max)
        return max;

    return x;
}

float Solider_ADC_ToTemp(uint16_t adc_raw)
{
    float temp =
        ((float)adc_raw - 1200.0f) * 1.4f;

    temp = clampf_solider(
        temp,
        SOLIDER_TEMP_MIN_C,
        SOLIDER_TEMP_MAX_C
    );

    return temp;
}

/* =========================================================
 * HEATER OUTPUT - FAST INNER PWM
 * ========================================================= */

static void Solider_SetPwmDuty(float duty)
{
    duty =
        clampf_solider(
            duty,
            0.0f,
            1.0f
        );

    uint32_t arr =
        TIM3->ARR;

    /*
     * Inverted heater polarity:
     *
     * duty=0.0 -> CCR=ARR -> heater OFF
     * duty=1.0 -> CCR=0   -> heater continuously ON
     */
    uint32_t ccr =
        (uint32_t)(
            (1.0f - duty) *
            (float)arr
        );

    if(ccr > arr)
        ccr = arr;

    SOLIDER_PWM_CCR =
        ccr;

    solider_pwm_ccr =
        (uint16_t)ccr;

    solider_pwm_duty_debug =
        duty;
}


static float Solider_GetTargetPwmDuty(void)
{
    if(solider_burst_mode ==
       SOLIDER_BURST_HOLD)
    {
        return SOLIDER_PWM_HOLD_DUTY;
    }

    if(solider_burst_mode ==
       SOLIDER_BURST_HEAT)
    {
        return SOLIDER_PWM_HEAT_DUTY;
    }

    return 0.0f;
}


static void Solider_UpdatePwmRamp(uint32_t on_elapsed_ms)
{
    float target =
        Solider_GetTargetPwmDuty();

    if(target <= 0.0f)
    {
        Solider_SetPwmDuty(
            0.0f
        );

        return;
    }

    if((SOLIDER_PWM_RAMP_MS == 0U) ||
       (on_elapsed_ms >=
        SOLIDER_PWM_RAMP_MS))
    {
        Solider_SetPwmDuty(
            target
        );

        return;
    }

    /*
     * Linear 35 % -> target duty during the first 2 ms.
     */
    float k =
        (float)on_elapsed_ms /
        (float)SOLIDER_PWM_RAMP_MS;

    float duty =
        SOLIDER_PWM_START_DUTY +
        (
            target -
            SOLIDER_PWM_START_DUTY
        ) *
        k;

    Solider_SetPwmDuty(
        duty
    );
}


static void Solider_HeaterOn(void)
{
    /*
     * Start each ACTIVE burst softly rather than applying
     * the final PWM duty immediately.
     */
    Solider_SetPwmDuty(
        SOLIDER_PWM_START_DUTY
    );
}


static void Solider_HeaterOff(void)
{
    Solider_SetPwmDuty(
        0.0f
    );
}


/* =========================================================
 * CONTROLLER RESET / ENABLE
 * ========================================================= */

static void Solider_PID_Reset(void)
{
    Solider_HeaterOff();

    solider_pid_state =
        SOLIDER_PID_IDLE;

    solider_burst_mode =
        SOLIDER_BURST_HEAT;

    solider_phase_started =
        0U;

    solider_pid_tick =
        HAL_GetTick();

    solider_off_tick =
        HAL_GetTick();

    solider_on_ms =
        SOLIDER_HEAT_ON_MS;

    solider_off_ms =
        SOLIDER_HEAT_OFF_MS;

    solider_adc_sum =
        0U;

    solider_adc_count =
        0U;

    solider_current_sum =
        0.0f;

    solider_current_count =
        0U;

    solider_current_avg =
        0.0f;

    /*
     * Keep the current EMA history if available.
     * It makes sleep/wake transitions smoother.
     */

    solider_pid_power =
        0.0f;

    solider_pwm_duty_debug =
        0.0f;

    solider_burst_debug =
        (uint8_t)SOLIDER_BURST_HEAT;

    solider_debug_on_ms =
        SOLIDER_HEAT_ON_MS;

    solider_debug_off_ms =
        SOLIDER_HEAT_OFF_MS;

    solider_debug_error =
        0.0f;
}


void Solider_PID_Enable(uint8_t enable)
{
    if(enable)
    {
        /*
         * Reset timing/control, but keep latest valid tip
         * temperature if this is only a sleep/wake transition.
         */
        Solider_PID_Reset();

        solider_pid_tick =
            HAL_GetTick();

        solider_pid_state =
            SOLIDER_PID_RUN;

        solider_phase_started =
            0U;
    }
    else
    {
        Solider_PID_Reset();

        solider_pid_state =
            SOLIDER_PID_IDLE;
    }
}


/* =========================================================
 * BURST MODE DECISION
 *
 * Commercial-like behavior:
 *
 *   Far below set point:
 *       HEAT = 45 ms ON / 5 ms OFF
 *
 *   Near / at set point:
 *       HOLD = 5 ms ON / 45 ms OFF
 *
 *   Excessive overshoot:
 *       OFF = 0 ms ON / 50 ms OFF
 *
 * Hysteresis:
 *
 *   HOLD -> HEAT:
 *       error >= 10 C
 *
 *   HEAT -> HOLD:
 *       error <= 2 C
 *
 * ========================================================= */

static void Solider_SelectBurstMode(float target_temp,
                                    float tip_temp)
{
    /*
     * Until the first valid temperature sample arrives,
     * start exactly like the commercial unit: HEAT.
     */
    if(solider_temp_filter_init == 0U)
    {
        solider_burst_mode =
            SOLIDER_BURST_HEAT;

        return;
    }

    float error =
        target_temp -
        tip_temp;

    solider_debug_error =
        error;

    /*
     * Strong overshoot:
     * stop heating completely until temperature comes down.
     */
    if(tip_temp >
       target_temp +
       SOLIDER_OVER_TEMP_OFF_C)
    {
        solider_burst_mode =
            SOLIDER_BURST_OFF;

        return;
    }

    switch(solider_burst_mode)
    {
        case SOLIDER_BURST_HEAT:
        {
            /*
             * Stay HIGH-power until almost at target.
             */
            if(error <=
               SOLIDER_ENTER_HOLD_ERR_C)
            {
                solider_burst_mode =
                    SOLIDER_BURST_HOLD;
            }
        }
        break;

        case SOLIDER_BURST_HOLD:
        {
            /*
             * A real thermal load must pull the tip down
             * substantially before full heating returns.
             */
            if(error >=
               SOLIDER_ENTER_HEAT_ERR_C)
            {
                solider_burst_mode =
                    SOLIDER_BURST_HEAT;
            }
        }
        break;

        case SOLIDER_BURST_OFF:
        default:
        {
            /*
             * After an overshoot, return to HOLD once the tip
             * has cooled back close to target.
             */
            if(tip_temp <=
               target_temp +
               SOLIDER_OVER_TEMP_RECOVER_C)
            {
                if(error >=
                   SOLIDER_ENTER_HEAT_ERR_C)
                {
                    solider_burst_mode =
                        SOLIDER_BURST_HEAT;
                }
                else
                {
                    solider_burst_mode =
                        SOLIDER_BURST_HOLD;
                }
            }
        }
        break;
    }
}


/* =========================================================
 * START A NEW 50-ms FRAME
 * ========================================================= */

static void Solider_StartFrame(void)
{
    set_temp =
        UI_Solider_GetSetTemp();

    Solider_SelectBurstMode(
        set_temp,
        measured_temp
    );

    switch(solider_burst_mode)
    {
        case SOLIDER_BURST_HEAT:
        {
            solider_on_ms =
                SOLIDER_HEAT_ON_MS;

            solider_off_ms =
                SOLIDER_HEAT_OFF_MS;

            /*
             * Effective nominal frame duty:
             * 90 % outer burst * 80 % inner PWM = 72 %.
             */
            solider_pid_power =
                0.90f *
                SOLIDER_PWM_HEAT_DUTY;
        }
        break;

        case SOLIDER_BURST_HOLD:
        {
            solider_on_ms =
                SOLIDER_HOLD_ON_MS;

            solider_off_ms =
                SOLIDER_HOLD_OFF_MS;

            /*
             * Effective nominal frame duty:
             * 10 % outer burst * 80 % inner PWM = 8 %.
             */
            solider_pid_power =
                0.10f *
                SOLIDER_PWM_HOLD_DUTY;
        }
        break;

        case SOLIDER_BURST_OFF:
        default:
        {
            solider_on_ms =
                0U;

            solider_off_ms =
                SOLIDER_FULL_OFF_MS;

            solider_pid_power =
                0.00f;
        }
        break;
    }

    solider_burst_debug =
        (uint8_t)solider_burst_mode;

    solider_debug_on_ms =
        solider_on_ms;

    solider_debug_off_ms =
        solider_off_ms;

    /*
     * Reset per-frame current measurement.
     */
    solider_current_sum =
        0.0f;

    solider_current_count =
        0U;

    uint32_t now =
        HAL_GetTick();

    /*
     * OFF mode has no ON phase.
     */
    if(solider_on_ms == 0U)
    {
        Solider_HeaterOff();

        solider_off_tick =
            now;

        solider_adc_sum =
            0U;

        solider_adc_count =
            0U;

        adc1_dma_ready =
            0U;

        solider_pid_state =
            SOLIDER_PID_OFF_WAIT;

        solider_phase_started =
            1U;

        return;
    }

    /*
     * HEAT/HOLD:
     * begin ON portion.
     */
    Solider_HeaterOn();

    solider_pid_tick =
        now;

    solider_pid_state =
        SOLIDER_PID_RUN;

    solider_phase_started =
        1U;
}


/* =========================================================
 * CURRENT FILTER
 * ========================================================= */

static void Solider_FinishCurrentMeasurement(void)
{
    if(solider_current_count == 0U)
        return;

    solider_current_avg =
        solider_current_sum /
        (float)solider_current_count;

    if(solider_current_filter_init == 0U)
    {
        solider_current_filtered =
            solider_current_avg;

        solider_current_filter_init =
            1U;
    }
    else
    {
        solider_current_filtered =
            (
                1.0f -
                SOL_CURRENT_EMA_ALPHA
            ) *
            solider_current_filtered +
            SOL_CURRENT_EMA_ALPHA *
            solider_current_avg;
    }
}


/* =========================================================
 * TEMPERATURE PROCESSING
 * ========================================================= */

static void Solider_ProcessTemperatureMeasurement(void)
{
    if(solider_adc_count == 0U)
    {
        /*
         * It is possible for the main loop to miss all ADC-ready
         * events during the short 5-ms OFF window.
         *
         * In that case keep the previous valid temperature and
         * simply try again next frame.
         */
        return;
    }

    uint16_t adc_avg =
        (uint16_t)(
            solider_adc_sum /
            (uint32_t)solider_adc_count
        );

    solider_temp_raw =
        (float)adc_avg;

    float temp_sample =
        Solider_ADC_ToTemp(
            adc_avg
        );

    if(solider_temp_filter_init == 0U)
    {
        measured_temp =
            temp_sample;

        frev_measured_temp =
            temp_sample;

        solider_temp_filter_init =
            1U;
    }
    else
    {
        measured_temp =
            temp_sample *
            ALPHA_FIR +
            frev_measured_temp *
            (1.0f - ALPHA_FIR);

        frev_measured_temp =
            measured_temp;
    }

    /*
     * Update UI only after a valid temperature reading.
     */
    UI_Solider_SetData(
        measured_temp,
        solider_current_filtered,
        PowerStage.temp,
        solider_current_filtered *
        PowerStage.vout,
        PowerStage.vout
    );
}


/* =========================================================
 * NON-BLOCKING 50-ms BURST CONTROLLER
 *
 * HEAT:
 *
 *   |<---------- 50 ms ---------->|
 *   | ON 45 ms | OFF 5 ms         |
 *                  ^
 *                  +-- ADC sensing
 *
 * HOLD:
 *
 *   |<---------- 50 ms ---------->|
 *   |ON 5| OFF 45 ms              |
 *          ^
 *          +-- ADC sensing
 *
 * No HAL_Delay() is used here.
 * ========================================================= */

void Solider_PID_Task(float set_adc)
{
    /*
     * Retained for compatibility with the old API.
     */
    (void)set_adc;

    if(solider_pid_state ==
       SOLIDER_PID_IDLE)
    {
        Solider_HeaterOff();
        return;
    }

    uint32_t now =
        HAL_GetTick();

    /*
     * Start next 50-ms frame.
     */
    if(solider_phase_started == 0U)
    {
        Solider_StartFrame();
        return;
    }


    /* -----------------------------------------------------
     * ON PHASE
     * ----------------------------------------------------- */
    if(solider_pid_state ==
       SOLIDER_PID_RUN)
    {
        uint32_t on_elapsed =
            (uint32_t)(
                now -
                solider_pid_tick
            );

        /*
         * Fast TIM3 PWM is active only inside the outer ON window.
         * Ramp its duty for the first few milliseconds to reduce
         * the load step at each HEAT/HOLD pulse.
         */
        Solider_UpdatePwmRamp(
            on_elapsed
        );

        /*
         * Accumulate measured heater/output current while ON.
         */
        float current_sample =
            PowerStage.current;

        if(current_sample >= 0.0f &&
           current_sample <= SOL_CURR_MAX)
        {
            solider_current_sum +=
                current_sample;

            solider_current_count++;
        }

        if(on_elapsed >=
           solider_on_ms)
        {
            /*
             * End ON phase.
             */
            Solider_HeaterOff();

            Solider_FinishCurrentMeasurement();

            solider_off_tick =
                now;

            solider_adc_sum =
                0U;

            solider_adc_count =
                0U;

            adc1_dma_ready =
                0U;

            solider_pid_state =
                SOLIDER_PID_OFF_WAIT;
        }

        return;
    }


    /* -----------------------------------------------------
     * OFF PHASE + TEMPERATURE SENSING
     * ----------------------------------------------------- */
    if(solider_pid_state ==
       SOLIDER_PID_OFF_WAIT)
    {
        uint32_t off_elapsed =
            (uint32_t)(
                now -
                solider_off_tick
            );

        /*
         * Wait a short time for heater switching/sense
         * transients to settle, then collect as many ADC-ready
         * samples as are available.
         */
        if(off_elapsed >=
           SOLIDER_ADC_SETTLE_MS)
        {
            if(adc1_dma_ready != 0U)
            {
                adc1_dma_ready =
                    0U;

                if(solider_adc_count <
                   SOLIDER_ADC_MAX_SAMPLES)
                {
                    solider_adc_sum +=
                        adc1_dma_buf[
                            SOLIDER_ADC_INDEX
                        ];

                    solider_adc_count++;
                }
            }
        }

        /*
         * End OFF phase -> complete exactly one 50-ms frame.
         */
        if(off_elapsed >=
           solider_off_ms)
        {
            Solider_ProcessTemperatureMeasurement();

            /*
             * Force next task pass to choose HEAT/HOLD/OFF again
             * from the latest temperature.
             */
            solider_phase_started =
                0U;

            solider_pid_state =
                SOLIDER_PID_RUN;
        }

        return;
    }


    /*
     * READ_ADC is no longer needed by the burst controller.
     * Recover safely if an old/stale state somehow appears.
     */
    solider_pid_state =
        SOLIDER_PID_RUN;

    solider_phase_started =
        0U;

    Solider_HeaterOff();
}
