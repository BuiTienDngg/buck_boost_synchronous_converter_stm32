#include "UI.h"
#include "st7789.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
 * HARDWARE
 * ========================================================= */

#define BBUI_BTN_PORT                 GPIOB
#define BBUI_BTN_PIN                  GPIO_PIN_9
#define BBUI_BTN_ACTIVE               GPIO_PIN_RESET

/*
 * STM32 hardware encoder mode normally gives 4 counts/detent
 * for common mechanical encoders.
 *
 * If your encoder gives 2 or 1 count/detent, change this only.
 */
#define BBUI_ENC_COUNTS_PER_DETENT    4

/* =========================================================
 * SETTING RANGE
 * ========================================================= */

#define BBUI_VSET_MIN                 1.0f
#define BBUI_VSET_MAX                 30.0f
#define BBUI_VSET_STEP                0.1f

#define BBUI_ISET_MIN                 0.1f
#define BBUI_ISET_MAX                 10.0f
#define BBUI_ISET_STEP                0.1f

/* =========================================================
 * TIMING / FILTER
 * ========================================================= */

#define BBUI_LCD_PERIOD_MS            80U
#define BBUI_BTN_DEBOUNCE_MS          35U

#define BBUI_DISPLAY_FILTER_MS        100U
#define BBUI_DISPLAY_ALPHA            0.25f

/* =========================================================
 * COLORS
 * ========================================================= */

#define C_BG                          ST7789_COLOR_BLACK
#define C_PANEL                       ST7789_Color_GetFromRGB(14, 18, 24)
#define C_PANEL_2                     ST7789_Color_GetFromRGB(22, 27, 35)
#define C_GRID                        ST7789_Color_GetFromRGB(55, 62, 72)

#define C_TEXT                        ST7789_COLOR_WHITE
#define C_MUTED                       ST7789_COLOR_LIGHTGREY

#define C_VOLT                        ST7789_COLOR_CYAN
#define C_CURR                        ST7789_COLOR_YELLOW
#define C_POWER                       ST7789_COLOR_ORANGE
#define C_OK                          ST7789_COLOR_GREEN
#define C_ERR                         ST7789_COLOR_RED
#define C_OFF                         ST7789_COLOR_DARKGREY

/* =========================================================
 * INTERNAL STATE
 * ========================================================= */

static BBUI_Data_t *ui = NULL;

static TIM_HandleTypeDef *tim_vset = NULL;
static TIM_HandleTypeDef *tim_iset = NULL;

static int16_t enc_v_last = 0;
static int16_t enc_i_last = 0;

static int32_t enc_v_acc = 0;
static int32_t enc_i_acc = 0;

static GPIO_PinState btn_raw_last = GPIO_PIN_SET;
static GPIO_PinState btn_stable = GPIO_PIN_SET;
static uint32_t btn_change_tick = 0U;

static uint8_t force_redraw = 1U;
static uint32_t lcd_tick = 0U;
static uint32_t display_filter_tick = 0U;

static uint8_t display_filter_init = 0U;
static float dvout = 0.0f;
static float diout = 0.0f;
static float dvin = 0.0f;
static float dtemp = 0.0f;

/*
 * Cache only dynamic text.
 * Reduces unnecessary ST7789 writes.
 */
static char cache_vout[24] = "";
static char cache_iout[24] = "";
static char cache_power[24] = "";
static char cache_vin[24] = "";
static char cache_temp[24] = "";
static char cache_vset[24] = "";
static char cache_iset[24] = "";
static char cache_state[16] = "";
static char cache_out[16] = "";

/* =========================================================
 * HELPERS
 * ========================================================= */

static float clampf_local(float x, float lo, float hi)
{
    if(x < lo) return lo;
    if(x > hi) return hi;
    return x;
}

static void clear_text_cache(void)
{
    cache_vout[0] = '\0';
    cache_iout[0] = '\0';
    cache_power[0] = '\0';
    cache_vin[0] = '\0';
    cache_temp[0] = '\0';
    cache_vset[0] = '\0';
    cache_iset[0] = '\0';
    cache_state[0] = '\0';
    cache_out[0] = '\0';
}

static void write_cached(char *cache,
                         uint16_t x,
                         uint16_t y,
                         uint16_t clear_w,
                         uint16_t clear_h,
                         const char *text,
                         uint8_t scale,
                         uint16_t color,
                         uint16_t bg,
                         uint8_t force)
{
    if(force || strcmp(cache, text) != 0)
    {
        ST7789_DrawFilledRectangle(x, y, clear_w, clear_h, bg);
        ST7789_PutString(x, y, text, scale, color, bg);

        strncpy(cache, text, 23U);
        cache[23] = '\0';
    }
}

static const char *state_text(BBUI_State_t state)
{
    switch(state)
    {
        case BBUI_STATE_CV:    return "CV";
        case BBUI_STATE_CC:    return "CC";
        case BBUI_STATE_FAULT: return "FAULT";
        default:               return "OFF";
    }
}

static uint16_t state_color(BBUI_State_t state)
{
    switch(state)
    {
        case BBUI_STATE_CV:    return C_OK;
        case BBUI_STATE_CC:    return C_CURR;
        case BBUI_STATE_FAULT: return C_ERR;
        default:               return C_OFF;
    }
}

/* =========================================================
 * ENCODERS
 * ========================================================= */

static void encoder_apply_steps(int32_t *acc,
                                int16_t delta,
                                float *value,
                                float step,
                                float vmin,
                                float vmax)
{
    *acc += (int32_t)delta;

    while(*acc >= BBUI_ENC_COUNTS_PER_DETENT)
    {
        *value += step;
        *acc -= BBUI_ENC_COUNTS_PER_DETENT;
    }

    while(*acc <= -BBUI_ENC_COUNTS_PER_DETENT)
    {
        *value -= step;
        *acc += BBUI_ENC_COUNTS_PER_DETENT;
    }

    *value = clampf_local(*value, vmin, vmax);
}

static void encoder_task(void)
{
    if(ui == NULL)
        return;

    if(tim_vset != NULL)
    {
        int16_t now = (int16_t)__HAL_TIM_GET_COUNTER(tim_vset);

        /*
         * int16 arithmetic naturally handles 16-bit counter wrap.
         */
        int16_t delta = (int16_t)(now - enc_v_last);
        enc_v_last = now;

        if(delta != 0)
        {
            float before = ui->vset;

            encoder_apply_steps(&enc_v_acc,
                                delta,
                                &ui->vset,
                                BBUI_VSET_STEP,
                                BBUI_VSET_MIN,
                                BBUI_VSET_MAX);

            if(ui->vset != before)
                force_redraw = 1U;
        }
    }

    if(tim_iset != NULL)
    {
        int16_t now = (int16_t)__HAL_TIM_GET_COUNTER(tim_iset);

        int16_t delta = (int16_t)(now - enc_i_last);
        enc_i_last = now;

        if(delta != 0)
        {
            float before = ui->iset;

            encoder_apply_steps(&enc_i_acc,
                                delta,
                                &ui->iset,
                                BBUI_ISET_STEP,
                                BBUI_ISET_MIN,
                                BBUI_ISET_MAX);

            if(ui->iset != before)
                force_redraw = 1U;
        }
    }
}

/* =========================================================
 * PB9 ON/OFF BUTTON
 * ========================================================= */

static void output_button_task(void)
{
    if(ui == NULL)
        return;

    uint32_t now = HAL_GetTick();
    GPIO_PinState raw =
        HAL_GPIO_ReadPin(BBUI_BTN_PORT, BBUI_BTN_PIN);

    if(raw != btn_raw_last)
    {
        btn_raw_last = raw;
        btn_change_tick = now;
    }

    if((now - btn_change_tick) >= BBUI_BTN_DEBOUNCE_MS)
    {
        if(raw != btn_stable)
        {
            btn_stable = raw;

            /*
             * Trigger only on press.
             * PB9 configured INPUT_PULLUP, button to GND.
             */
            if(btn_stable == BBUI_BTN_ACTIVE)
            {
                ui->enable ^= 1U;

                if(ui->enable == 0U)
                {
                    ui->state = BBUI_STATE_OFF;
                }
                else
                {
                    /*
                     * The control loop will decide CV or CC.
                     * CV is just the initial UI indication.
                     */
                    if(ui->state == BBUI_STATE_OFF)
                        ui->state = BBUI_STATE_CV;
                }

                force_redraw = 1U;
            }
        }
    }
}

/* =========================================================
 * DISPLAY FILTER
 * ========================================================= */

static void update_display_values(void)
{
    if(ui == NULL)
        return;

    uint32_t now = HAL_GetTick();

    if((now - display_filter_tick) < BBUI_DISPLAY_FILTER_MS)
        return;

    display_filter_tick = now;

    if(!display_filter_init)
    {
        dvin = ui->vin;
        dvout = ui->vout;
        diout = ui->current;
        dtemp = ui->temp;

        display_filter_init = 1U;
        return;
    }

    dvin  += BBUI_DISPLAY_ALPHA * (ui->vin     - dvin);
    dvout += BBUI_DISPLAY_ALPHA * (ui->vout    - dvout);
    diout += BBUI_DISPLAY_ALPHA * (ui->current - diout);
    dtemp += BBUI_DISPLAY_ALPHA * (ui->temp    - dtemp);
}

/* =========================================================
 * UI DRAWING
 * 320 x 240 LANDSCAPE
 * ========================================================= */

static void draw_static_ui(void)
{
    ST7789_FillScreen(C_BG);

    /* Header */
    ST7789_DrawFilledRectangle(0, 0, 320, 30, C_PANEL);
    ST7789_PutString(10, 8,
                     "BUCK-BOOST POWER SUPPLY",
                     2,
                     C_TEXT,
                     C_PANEL);

    /* Main V/I cards */
    ST7789_DrawFilledRectangle(8, 38, 150, 88, C_PANEL);
    ST7789_DrawRectangle(8, 38, 150, 88, C_GRID);

    ST7789_DrawFilledRectangle(162, 38, 150, 88, C_PANEL);
    ST7789_DrawRectangle(162, 38, 150, 88, C_GRID);

    ST7789_PutString(18, 48, "VOUT", 2, C_MUTED, C_PANEL);
    ST7789_PutString(172, 48, "IOUT", 2, C_MUTED, C_PANEL);

    /* Middle information */
    ST7789_DrawFilledRectangle(8, 134, 304, 42, C_PANEL_2);
    ST7789_DrawRectangle(8, 134, 304, 42, C_GRID);

    ST7789_PutString(18, 144, "VIN", 1, C_MUTED, C_PANEL_2);
    ST7789_PutString(117, 144, "POWER", 1, C_MUTED, C_PANEL_2);
    ST7789_PutString(232, 144, "TEMP", 1, C_MUTED, C_PANEL_2);

    /* Bottom setting cards */
    ST7789_DrawFilledRectangle(8, 184, 150, 48, C_PANEL);
    ST7789_DrawRectangle(8, 184, 150, 48, C_VOLT);

    ST7789_DrawFilledRectangle(162, 184, 150, 48, C_PANEL);
    ST7789_DrawRectangle(162, 184, 150, 48, C_CURR);

    ST7789_PutString(16, 190, "V SET  TIM2", 1, C_VOLT, C_PANEL);
    ST7789_PutString(170, 190, "I SET  TIM4", 1, C_CURR, C_PANEL);

    force_redraw = 1U;
    clear_text_cache();
}

static void draw_dynamic_ui(uint8_t force)
{
    if(ui == NULL)
        return;

    char buf[24];

    /* VOUT */
    snprintf(buf, sizeof(buf), "%05.2f V", dvout);
    write_cached(cache_vout,
                 18, 76,
                 132, 38,
                 buf,
                 3,
                 C_VOLT,
                 C_PANEL,
                 force);

    /* IOUT */
    snprintf(buf, sizeof(buf), "%04.2f A", diout);
    write_cached(cache_iout,
                 172, 76,
                 132, 38,
                 buf,
                 3,
                 C_CURR,
                 C_PANEL,
                 force);

    /* VIN */
    snprintf(buf, sizeof(buf), "%04.1fV", dvin);
    write_cached(cache_vin,
                 18, 158,
                 75, 14,
                 buf,
                 2,
                 C_TEXT,
                 C_PANEL_2,
                 force);

    /* Output power */
    float power = dvout * diout;
    snprintf(buf, sizeof(buf), "%05.1fW", power);
    write_cached(cache_power,
                 117, 158,
                 90, 14,
                 buf,
                 2,
                 C_POWER,
                 C_PANEL_2,
                 force);

    /* Temperature */
    snprintf(buf, sizeof(buf), "%03.0fC", dtemp);
    write_cached(cache_temp,
                 232, 158,
                 72, 14,
                 buf,
                 2,
                 C_TEXT,
                 C_PANEL_2,
                 force);

    /* VSET */
    snprintf(buf, sizeof(buf), "%05.1f V", ui->vset);
    write_cached(cache_vset,
                 16, 207,
                 135, 20,
                 buf,
                 2,
                 C_VOLT,
                 C_PANEL,
                 force);

    /* ISET */
    snprintf(buf, sizeof(buf), "%04.1f A", ui->iset);
    write_cached(cache_iset,
                 170, 207,
                 135, 20,
                 buf,
                 2,
                 C_CURR,
                 C_PANEL,
                 force);

    /* Status block in upper-right header */
    const char *s = state_text(ui->state);
    write_cached(cache_state,
                 245, 8,
                 65, 15,
                 s,
                 2,
                 state_color(ui->state),
                 C_PANEL,
                 force);

    const char *out_text = ui->enable ? "ON" : "OFF";

    write_cached(cache_out,
                 205, 8,
                 35, 15,
                 out_text,
                 2,
                 ui->enable ? C_OK : C_OFF,
                 C_PANEL,
                 force);
}

/* =========================================================
 * PUBLIC API
 * ========================================================= */

void BBUI_Init(BBUI_Data_t *data,
               TIM_HandleTypeDef *htim_vset,
               TIM_HandleTypeDef *htim_iset)
{
    ui = data;
    tim_vset = htim_vset;
    tim_iset = htim_iset;

    if(ui != NULL)
    {
        ui->vset = clampf_local(ui->vset,
                                BBUI_VSET_MIN,
                                BBUI_VSET_MAX);

        ui->iset = clampf_local(ui->iset,
                                BBUI_ISET_MIN,
                                BBUI_ISET_MAX);

        /*
         * Safe boot: output remains disabled.
         */
        ui->enable = 0U;
        ui->state = BBUI_STATE_OFF;
    }

    if(tim_vset != NULL)
    {
        HAL_TIM_Encoder_Start_IT(tim_vset, TIM_CHANNEL_ALL);
        enc_v_last =
            (int16_t)__HAL_TIM_GET_COUNTER(tim_vset);
    }

    if(tim_iset != NULL)
    {
        HAL_TIM_Encoder_Start_IT(tim_iset, TIM_CHANNEL_ALL);
        enc_i_last =
            (int16_t)__HAL_TIM_GET_COUNTER(tim_iset);
    }

    btn_raw_last =
        HAL_GPIO_ReadPin(BBUI_BTN_PORT, BBUI_BTN_PIN);

    btn_stable = btn_raw_last;
    btn_change_tick = HAL_GetTick();

    ST7789_Init();
    ST7789_SetRotation(1U);

    draw_static_ui();

    display_filter_init = 0U;
    update_display_values();

    draw_dynamic_ui(1U);

    lcd_tick = HAL_GetTick();
}

void BBUI_Task(void)
{
    encoder_task();
    output_button_task();
    update_display_values();

    uint32_t now = HAL_GetTick();

    if(force_redraw ||
       ((now - lcd_tick) >= BBUI_LCD_PERIOD_MS))
    {
        lcd_tick = now;

        draw_dynamic_ui(force_redraw);
        force_redraw = 0U;
    }
}

void BBUI_ForceRefresh(void)
{
    force_redraw = 1U;
    clear_text_cache();
}

void BBUI_ButtonIRQ(void)
{
    /*
     * Not needed.
     * PB9 is intentionally handled by polling + debounce in BBUI_Task().
     */
}
