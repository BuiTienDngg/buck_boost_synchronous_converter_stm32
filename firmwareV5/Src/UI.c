#include "UI.h"
#include "st7789.h"
#include "UI_Solider.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
 * HARDWARE
 * ========================================================= */

#define BTN_V_PORT                     GPIOB
#define BTN_V_PIN                      GPIO_PIN_8

#define BTN_I_PORT                     GPIOB
#define BTN_I_PIN                      GPIO_PIN_4

#define BTN_OUT_PORT                   GPIOB
#define BTN_OUT_PIN                    GPIO_PIN_9

/* External SOLDER request: falling edge HIGH -> LOW */
#define SOLDER_DETECT_PORT             GPIOB
#define SOLDER_DETECT_PIN              GPIO_PIN_11

/*
 * BUZZER control.
 *
 * PC14 should drive a transistor/MOSFET, NOT the buzzer directly.
 * This implementation assumes an ACTIVE-HIGH active buzzer.
 */
#define BUZZER_PORT                    GPIOC
#define BUZZER_PIN                     GPIO_PIN_14
#define BUZZER_ACTIVE_STATE            GPIO_PIN_SET
#define BUZZER_IDLE_STATE              GPIO_PIN_RESET

/* Optional old power-route / relay output. Set 1 if PB15 is still used. */
#define SOLDER_ROUTE_ENABLE            0U
#define SOLDER_ROUTE_PORT              GPIOB
#define SOLDER_ROUTE_PIN               GPIO_PIN_15

#define SOLDER_BB_VSET                 24.0f
#define SOLDER_BB_ISET                 7.0f

#define BTN_ACTIVE                     GPIO_PIN_RESET
#define BTN_DEBOUNCE_MS                35U

#define ENC_COUNTS_PER_DETENT          4
#define MENU_HOLD_MS                   1000U
#define SOLDER_HOLD_MS                 1000U

/* =========================================================
 * RANGE
 * ========================================================= */

#define VSET_MIN                       1.00f
#define VSET_MAX                       30.00f

#define ISET_MIN                       0.10f
#define ISET_MAX                       10.00f


/* =========================================================
 * OUTPUT PROTECTION
 * ========================================================= */

/*
 * Default thresholds.
 *
 * OVP is intentionally slightly above the normal 30 V maximum
 * so operating at 30.0 V does not nuisance-trip due to ADC noise.
 */
#define PROTECT_OVP_DEFAULT             31.0f
#define PROTECT_OCP_DEFAULT              8.5f
#define PROTECT_OPP_DEFAULT            200.0f

#define PROTECT_OVP_MIN                  5.0f
#define PROTECT_OVP_MAX                 35.0f
#define PROTECT_OVP_STEP                 0.5f

#define PROTECT_OCP_MIN                  0.5f
#define PROTECT_OCP_MAX                 12.0f
#define PROTECT_OCP_STEP                 0.1f

#define PROTECT_OPP_MIN                 10.0f
#define PROTECT_OPP_MAX                300.0f
#define PROTECT_OPP_STEP                10.0f

/*
 * Software protection is a secondary layer.
 *
 * Startup blank prevents false trips while the converter / ADC settles.
 * Trip debounce rejects very short switching spikes.
 */
#define PROTECT_STARTUP_BLANK_MS        250U
#define PROTECT_TRIP_DEBOUNCE_MS         50U

typedef enum
{
    PROTECT_FAULT_NONE = 0,
    PROTECT_FAULT_OVP,
    PROTECT_FAULT_OCP,
    PROTECT_FAULT_OPP,
    PROTECT_FAULT_EXTERNAL
} ProtectFault_t;

static float protect_ovp = PROTECT_OVP_DEFAULT;
static float protect_ocp = PROTECT_OCP_DEFAULT;
static float protect_opp = PROTECT_OPP_DEFAULT;

/* Temporary menu values; applied only when PB9 confirms. */
static float menu_ovp = PROTECT_OVP_DEFAULT;
static float menu_ocp = PROTECT_OCP_DEFAULT;
static float menu_opp = PROTECT_OPP_DEFAULT;

static ProtectFault_t protect_fault = PROTECT_FAULT_NONE;
static uint8_t protect_fault_latched = 0U;

static uint32_t protect_enable_tick = 0U;
static uint32_t protect_violation_tick = 0U;
static ProtectFault_t protect_pending_fault = PROTECT_FAULT_NONE;
static uint8_t protect_prev_enable = 0U;

/*
 * Forward declarations.
 *
 * These functions are defined later in this file, but SOLDER / OUT-button
 * handlers call some of them earlier. ARMClang/C99 requires declarations
 * before first use.
 */
static void clear_protection_fault(void);
static void protection_trip(ProtectFault_t fault);
static void protection_arm(void);
static void protection_task(void);


/* =========================================================
 * BUZZER
 * ========================================================= */

/*
 * Fault pattern:
 *    BEEP 150 ms
 *    OFF  150 ms
 *    BEEP 150 ms
 *    OFF  150 ms
 *    BEEP 150 ms
 *
 * Completely non-blocking.
 */
#define BUZZER_BEEP_MS                  150U
#define BUZZER_GAP_MS                   150U
#define BUZZER_BEEP_COUNT                 3U

static uint8_t buzzer_pattern_active = 0U;
static uint8_t buzzer_phase_on = 0U;
static uint8_t buzzer_beep_count = 0U;
static uint32_t buzzer_tick = 0U;

static void buzzer_write(uint8_t on)
{
    HAL_GPIO_WritePin(
        BUZZER_PORT,
        BUZZER_PIN,
        on ? BUZZER_ACTIVE_STATE : BUZZER_IDLE_STATE
    );
}

static void buzzer_stop(void)
{
    buzzer_pattern_active = 0U;
    buzzer_phase_on = 0U;
    buzzer_beep_count = 0U;

    buzzer_write(0U);
}

static void buzzer_fault_start(void)
{
    buzzer_pattern_active = 1U;
    buzzer_phase_on = 1U;
    buzzer_beep_count = 0U;
    buzzer_tick = HAL_GetTick();

    buzzer_write(1U);
}

static void buzzer_task(void)
{
    if(!buzzer_pattern_active)
        return;

    uint32_t now = HAL_GetTick();

    if(buzzer_phase_on)
    {
        if((uint32_t)(now - buzzer_tick) >= BUZZER_BEEP_MS)
        {
            buzzer_tick = now;
            buzzer_phase_on = 0U;
            buzzer_beep_count++;

            buzzer_write(0U);

            if(buzzer_beep_count >= BUZZER_BEEP_COUNT)
            {
                buzzer_pattern_active = 0U;
            }
        }
    }
    else
    {
        if((uint32_t)(now - buzzer_tick) >= BUZZER_GAP_MS)
        {
            buzzer_tick = now;
            buzzer_phase_on = 1U;

            buzzer_write(1U);
        }
    }
}


/* =========================================================
 * FLASH MEMORY - SAVE VSET / ISET + PROTECTION
 *
 * STM32F103C8T6:
 *   Flash base     = 0x08000000
 *   Official size  = 64 KB
 *   Page size      = 1 KB
 *   Last page      = 0x0800FC00
 *
 * IMPORTANT:
 * Reserve the last 1 KB of Flash for settings.
 * Application code must NOT occupy this page.
 * ========================================================= */

#define UI_FLASH_PAGE_ADDR              0x0800FC00U
#define UI_FLASH_MAGIC                  0x42554242U   /* "BUBB" */

#define UI_FLASH_VERSION_V1             0x00010001U
#define UI_FLASH_VERSION                0x00010002U

/*
 * Previous firmware format.
 * Kept so old saved VSET/ISET can still be restored.
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t vset_x100;
    uint32_t iset_x100;
    uint32_t checksum;
} UIFlashDataV1_t;

/*
 * Current format.
 */
typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t vset_x100;
    uint32_t iset_x100;

    uint32_t ovp_x100;
    uint32_t ocp_x100;
    uint32_t opp_x10;

    uint32_t checksum;
} UIFlashData_t;

static uint32_t ui_flash_checksum_v1(const UIFlashDataV1_t *d)
{
    return d->magic ^
           d->version ^
           d->vset_x100 ^
           d->iset_x100 ^
           0x5A5AA5A5U;
}

static uint32_t ui_flash_checksum(const UIFlashData_t *d)
{
    return d->magic ^
           d->version ^
           d->vset_x100 ^
           d->iset_x100 ^
           d->ovp_x100 ^
           d->ocp_x100 ^
           d->opp_x10 ^
           0x5A5AA5A5U;
}

static uint8_t ui_flash_read(float *vset, float *iset)
{
    const uint32_t *raw =
        (const uint32_t *)UI_FLASH_PAGE_ADDR;

    if(raw[0] != UI_FLASH_MAGIC)
        return 0U;

    /*
     * Backward compatibility:
     * old firmware saved VSET/ISET only.
     */
    if(raw[1] == UI_FLASH_VERSION_V1)
    {
        const UIFlashDataV1_t *d =
            (const UIFlashDataV1_t *)UI_FLASH_PAGE_ADDR;

        if(d->checksum != ui_flash_checksum_v1(d))
            return 0U;

        float v = (float)d->vset_x100 / 100.0f;
        float i = (float)d->iset_x100 / 100.0f;

        if(v < VSET_MIN || v > VSET_MAX)
            return 0U;

        if(i < ISET_MIN || i > ISET_MAX)
            return 0U;

        *vset = v;
        *iset = i;

        /*
         * Protection values keep their defaults.
         */
        return 1U;
    }

    if(raw[1] != UI_FLASH_VERSION)
        return 0U;

    const UIFlashData_t *d =
        (const UIFlashData_t *)UI_FLASH_PAGE_ADDR;

    if(d->checksum != ui_flash_checksum(d))
        return 0U;

    float v = (float)d->vset_x100 / 100.0f;
    float i = (float)d->iset_x100 / 100.0f;

    float ovp = (float)d->ovp_x100 / 100.0f;
    float ocp = (float)d->ocp_x100 / 100.0f;
    float opp = (float)d->opp_x10 / 10.0f;

    if(v < VSET_MIN || v > VSET_MAX)
        return 0U;

    if(i < ISET_MIN || i > ISET_MAX)
        return 0U;

    if(ovp < PROTECT_OVP_MIN || ovp > PROTECT_OVP_MAX)
        return 0U;

    if(ocp < PROTECT_OCP_MIN || ocp > PROTECT_OCP_MAX)
        return 0U;

    if(opp < PROTECT_OPP_MIN || opp > PROTECT_OPP_MAX)
        return 0U;

    *vset = v;
    *iset = i;

    protect_ovp = ovp;
    protect_ocp = ocp;
    protect_opp = opp;

    return 1U;
}

static uint8_t ui_flash_save(float vset, float iset)
{
    UIFlashData_t new_data;

    new_data.magic = UI_FLASH_MAGIC;
    new_data.version = UI_FLASH_VERSION;

    new_data.vset_x100 =
        (uint32_t)(vset * 100.0f + 0.5f);

    new_data.iset_x100 =
        (uint32_t)(iset * 100.0f + 0.5f);

    new_data.ovp_x100 =
        (uint32_t)(protect_ovp * 100.0f + 0.5f);

    new_data.ocp_x100 =
        (uint32_t)(protect_ocp * 100.0f + 0.5f);

    new_data.opp_x10 =
        (uint32_t)(protect_opp * 10.0f + 0.5f);

    new_data.checksum =
        ui_flash_checksum(&new_data);

    /*
     * Avoid erase/write if nothing changed.
     */
    const UIFlashData_t *old_data =
        (const UIFlashData_t *)UI_FLASH_PAGE_ADDR;

    if(old_data->magic == new_data.magic &&
       old_data->version == new_data.version &&
       old_data->vset_x100 == new_data.vset_x100 &&
       old_data->iset_x100 == new_data.iset_x100 &&
       old_data->ovp_x100 == new_data.ovp_x100 &&
       old_data->ocp_x100 == new_data.ocp_x100 &&
       old_data->opp_x10 == new_data.opp_x10 &&
       old_data->checksum == new_data.checksum)
    {
        return 1U;
    }

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = UI_FLASH_PAGE_ADDR;
    erase.NbPages = 1U;

    if(HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0U;
    }

    const uint16_t *src =
        (const uint16_t *)&new_data;

    uint32_t addr = UI_FLASH_PAGE_ADDR;

    for(uint32_t k = 0U;
        k < sizeof(UIFlashData_t) / 2U;
        k++)
    {
        if(HAL_FLASH_Program(
               FLASH_TYPEPROGRAM_HALFWORD,
               addr,
               src[k]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 0U;
        }

        addr += 2U;
    }

    HAL_FLASH_Lock();

    return 1U;
}

/* =========================================================
 * DISPLAY / UPDATE
 * ========================================================= */

#define MEAS_FILTER_MS                 80U
#define MEAS_REFRESH_MS                100U
#define DISP_ALPHA                     0.30f

#define EDIT_ACTIVE_MS                 2000U
#define BLINK_PERIOD_MS                220U

#define C_BG                           ST7789_COLOR_BLACK
#define C_WHITE                        ST7789_COLOR_WHITE
#define C_MUTED                        ST7789_COLOR_LIGHTGREY
#define C_GRID                         ST7789_Color_GetFromRGB(45, 50, 58)

#define C_VOLT                         ST7789_COLOR_CYAN
#define C_CURR                         ST7789_COLOR_YELLOW
#define C_POWER                        ST7789_COLOR_ORANGE

#define C_ON                           ST7789_COLOR_GREEN
#define C_OFF                          ST7789_COLOR_DARKGREY
#define C_FAULT                        ST7789_COLOR_RED

/* =========================================================
 * SCREEN / DISPLAY MODE
 * ========================================================= */

typedef enum
{
    UI_MODE_NUMBER = 0,
    UI_MODE_GRAPH
} UIMode_t;

typedef enum
{
    SCREEN_NUMBER = 0,
    SCREEN_GRAPH,
    SCREEN_MENU,
    SCREEN_SOLDER
} Screen_t;

/*
 * Menu is row/value based.
 *
 * VSET encoder  -> move between rows
 * ISET encoder  -> change selected row value
 * PB9           -> confirm + exit
 *
 * More rows can be added later without changing the interaction model.
 */
typedef enum
{
    MENU_ITEM_UI = 0,
    MENU_ITEM_OVP,
    MENU_ITEM_OCP,
    MENU_ITEM_OPP,
    MENU_ITEM_COUNT
} MenuItem_t;

static UIMode_t ui_mode = UI_MODE_NUMBER;
static UIMode_t menu_ui_mode = UI_MODE_NUMBER;

static Screen_t screen = SCREEN_NUMBER;
static MenuItem_t menu_item = MENU_ITEM_UI;

/* =========================================================
 * SOLDER MODE STATE
 * ========================================================= */

static UIMode_t solder_return_ui_mode = UI_MODE_NUMBER;

static float solder_backup_vset = 12.0f;
static float solder_backup_iset = 2.0f;
static uint8_t solder_backup_enable = 0U;

static GPIO_PinState solder_detect_last = GPIO_PIN_SET;
static uint32_t solder_detect_tick = 0U;

/*
 * PB9 is now short/long aware:
 *
 * normal UI:
 *   short -> output ON/OFF
 *   hold 1 s -> enter SOLDER
 *
 * solder:
 *   short -> preset #3 (400 C)
 *   hold 1 s -> exit SOLDER
 */
static uint8_t outbtn_press_active = 0U;
static uint8_t outbtn_long_handled = 0U;
static uint32_t outbtn_press_tick = 0U;

/* =========================================================
 * DIGIT MODE
 * ========================================================= */

typedef enum
{
    DIGIT_1 = 0,
    DIGIT_01,
    DIGIT_001
} DigitMode_t;

static DigitMode_t v_digit = DIGIT_01;
static DigitMode_t i_digit = DIGIT_01;

static float digit_step(DigitMode_t mode)
{
    switch(mode)
    {
        case DIGIT_1:   return 1.00f;
        case DIGIT_001: return 0.01f;
        case DIGIT_01:
        default:        return 0.10f;
    }
}

static DigitMode_t digit_next(DigitMode_t mode)
{
    if(mode == DIGIT_1)   return DIGIT_01;
    if(mode == DIGIT_01)  return DIGIT_001;
    return DIGIT_1;
}

/* =========================================================
 * INTERNAL STATE
 * ========================================================= */

static BBUI_Data_t *ui = NULL;

static TIM_HandleTypeDef *tim_v = NULL;
static TIM_HandleTypeDef *tim_i = NULL;

static int16_t enc_v_last = 0;
static int16_t enc_i_last = 0;

static int32_t enc_v_acc = 0;
static int32_t enc_i_acc = 0;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState raw_last;
    GPIO_PinState stable;
    uint32_t changed_at;
} Button_t;

static Button_t btn_v =
{
    .port = BTN_V_PORT,
    .pin = BTN_V_PIN,
    .raw_last = GPIO_PIN_SET,
    .stable = GPIO_PIN_SET
};

static Button_t btn_i =
{
    .port = BTN_I_PORT,
    .pin = BTN_I_PIN,
    .raw_last = GPIO_PIN_SET,
    .stable = GPIO_PIN_SET
};

static Button_t btn_out =
{
    .port = BTN_OUT_PORT,
    .pin = BTN_OUT_PIN,
    .raw_last = GPIO_PIN_SET,
    .stable = GPIO_PIN_SET
};

static uint8_t filter_init = 0U;

static float disp_vout = 0.0f;
static float disp_iout = 0.0f;
static float disp_vin  = 0.0f;
static float disp_temp = 0.0f;

static uint32_t filter_tick = 0U;
static uint32_t meas_tick = 0U;

static uint8_t vset_dirty = 1U;
static uint8_t iset_dirty = 1U;
static uint8_t status_dirty = 1U;

static uint32_t v_edit_until = 0U;
static uint32_t i_edit_until = 0U;

static uint32_t blink_tick = 0U;
static uint8_t blink_on = 1U;

static char old_vout[24] = "";
static char old_iout[24] = "";
static char old_power[24] = "";
static char old_info[64] = "";
static char old_state[16] = "";
static char old_out[16] = "";

/*
 * Fine-grained SET redraw cache.
 *
 * Each element stores the character that is currently visible
 * at that character cell. '\0' means the cell is blank.
 *
 * Therefore blink only touches ONE character cell.
 */
static char rendered_vset[5] = {0};
static char rendered_iset[5] = {0};

static int8_t rendered_v_underline = -1;
static int8_t rendered_i_underline = -1;

static uint8_t rendered_v_unit = 0U;
static uint8_t rendered_i_unit = 0U;

/* Graph top-line cache */
static char old_graph_live[48] = "";

/* =========================================================
 * RUN TIME
 *
 * OFF -> ON : reset to 00:00:00 and start
 * ON        : count
 * ON -> OFF : freeze
 * next ON   : reset again
 * ========================================================= */

static uint8_t runtime_prev_enable = 0U;
static uint32_t runtime_last_tick = 0U;
static uint32_t runtime_elapsed_sec = 0U;
static uint32_t runtime_ms_remainder = 0U;
static uint8_t runtime_dirty = 1U;

static char old_runtime_number[20] = "";
static char old_runtime_graph[20] = "";

/*
 * VSET encoder push behavior:
 *
 * short press (< MENU_HOLD_MS):
 *      change VSET digit
 *
 * long press (>= MENU_HOLD_MS):
 *      enter MENU
 *
 * We detect the action on the debounced stable button state,
 * so a long press never also triggers a short-press digit change.
 */
static uint8_t vbtn_press_active = 0U;
static uint8_t vbtn_long_handled = 0U;
static uint32_t vbtn_press_tick = 0U;

/* =========================================================
 * GRAPH
 * ========================================================= */

#define GRAPH_X0                       20
#define GRAPH_X1                       315
#define GRAPH_Y0                       28
#define GRAPH_Y1                       172

#define GRAPH_W                        (GRAPH_X1 - GRAPH_X0 + 1)
#define GRAPH_H                        (GRAPH_Y1 - GRAPH_Y0 + 1)

#define GRAPH_SAMPLE_MS                250U
#define GRAPH_VMAX                     30.0f
#define GRAPH_IMAX                     10.0f

static uint16_t graph_head = 0U;
static int16_t graph_last_x = -1;
static int16_t graph_last_yv = -1;
static int16_t graph_last_yi = -1;
static uint8_t graph_has_last = 0U;
static uint32_t graph_tick = 0U;

/* =========================================================
 * HELPERS
 * ========================================================= */

static float clampf_ui(float x, float lo, float hi)
{
    if(x < lo) return lo;
    if(x > hi) return hi;
    return x;
}

static uint8_t is_live_screen(void)
{
    return (screen == SCREEN_NUMBER || screen == SCREEN_GRAPH) ? 1U : 0U;
}


/*
 * Draw only character cells whose content actually changed.
 *
 * ST7789_PutString() font cell is approximately:
 *     width  = 6 * scale
 *     height = 8 * scale
 *
 * This is much cheaper than clearing and redrawing the whole number.
 */
static void draw_text_diff(uint16_t x,
                           uint16_t y,
                           uint8_t scale,
                           uint16_t fg,
                           const char *text,
                           char *cache,
                           uint16_t cache_size)
{
    if(text == NULL || cache == NULL || cache_size < 2U)
        return;

    size_t new_len = strlen(text);
    size_t old_len = strlen(cache);

    size_t count =
        (new_len > old_len) ? new_len : old_len;

    if(count > (size_t)(cache_size - 1U))
        count = (size_t)(cache_size - 1U);

    const uint16_t cell_w =
        (uint16_t)(6U * scale);

    const uint16_t cell_h =
        (uint16_t)(8U * scale);

    for(size_t i = 0U; i < count; i++)
    {
        char new_c =
            (i < new_len) ? text[i] : '\0';

        char old_c =
            (i < old_len) ? cache[i] : '\0';

        if(new_c == old_c)
            continue;

        uint16_t cx =
            (uint16_t)(x + i * cell_w);

        /*
         * Clear only this character cell.
         */
        ST7789_DrawFilledRectangle(
            cx,
            y,
            cell_w,
            cell_h,
            C_BG
        );

        if(new_c != '\0')
        {
            char ch[2] =
            {
                new_c,
                '\0'
            };

            ST7789_PutString(
                cx,
                y,
                ch,
                scale,
                fg,
                C_BG
            );
        }
    }

    strncpy(cache, text, cache_size - 1U);
    cache[cache_size - 1U] = '\0';
}

static const char *state_text(BBUI_State_t state)
{
    switch(state)
    {
        case BBUI_STATE_CV:
            return "CV";

        case BBUI_STATE_CC:
            return "CC";

        case BBUI_STATE_FAULT:
            switch(protect_fault)
            {
                case PROTECT_FAULT_OVP: return "OVP";
                case PROTECT_FAULT_OCP: return "OCP";
                case PROTECT_FAULT_OPP: return "OPP";
                default:                return "FAULT";
            }

        default:
            return "OFF";
    }
}

static uint16_t state_color(BBUI_State_t state)
{
    switch(state)
    {
        case BBUI_STATE_CV:    return ST7789_COLOR_GREEN;
        case BBUI_STATE_CC:    return ST7789_COLOR_YELLOW;
        case BBUI_STATE_FAULT: return ST7789_COLOR_RED;
        default:               return ST7789_COLOR_DARKGREY;
    }
}

static uint8_t button_pressed_event(Button_t *b)
{
    uint32_t now = HAL_GetTick();
    GPIO_PinState raw = HAL_GPIO_ReadPin(b->port, b->pin);

    if(raw != b->raw_last)
    {
        b->raw_last = raw;
        b->changed_at = now;
    }

    if((uint32_t)(now - b->changed_at) >= BTN_DEBOUNCE_MS)
    {
        if(raw != b->stable)
        {
            b->stable = raw;

            if(b->stable == BTN_ACTIVE)
                return 1U;
        }
    }

    return 0U;
}

static void activate_v_edit(void)
{
    v_edit_until = HAL_GetTick() + EDIT_ACTIVE_MS;
    blink_on = 1U;
    blink_tick = HAL_GetTick();
    vset_dirty = 1U;
}

static void activate_i_edit(void)
{
    i_edit_until = HAL_GetTick() + EDIT_ACTIVE_MS;
    blink_on = 1U;
    blink_tick = HAL_GetTick();
    iset_dirty = 1U;
}

static void invalidate_main_cache(void)
{
    old_vout[0] = '\0';
    old_iout[0] = '\0';
    old_power[0] = '\0';
    old_info[0] = '\0';
    old_state[0] = '\0';
    old_out[0] = '\0';
    old_graph_live[0] = '\0';

    old_runtime_number[0] = '\0';
    old_runtime_graph[0] = '\0';
    runtime_dirty = 1U;

    memset(rendered_vset, 0, sizeof(rendered_vset));
    memset(rendered_iset, 0, sizeof(rendered_iset));

    rendered_v_underline = -1;
    rendered_i_underline = -1;

    rendered_v_unit = 0U;
    rendered_i_unit = 0U;

    vset_dirty = 1U;
    iset_dirty = 1U;
    status_dirty = 1U;
}

/* =========================================================
 * FILTER
 * ========================================================= */

static void filter_task(void)
{
    if(ui == NULL)
        return;

    uint32_t now = HAL_GetTick();

    if((uint32_t)(now - filter_tick) < MEAS_FILTER_MS)
        return;

    filter_tick = now;

    if(!filter_init)
    {
        disp_vout = ui->vout;
        disp_iout = ui->current;
        disp_vin = ui->vin;
        disp_temp = ui->temp;
        filter_init = 1U;
        return;
    }

    disp_vout += DISP_ALPHA * (ui->vout - disp_vout);
    disp_iout += DISP_ALPHA * (ui->current - disp_iout);
    disp_vin  += DISP_ALPHA * (ui->vin - disp_vin);
    disp_temp += DISP_ALPHA * (ui->temp - disp_temp);
}

/* =========================================================
 * RUN TIME
 * ========================================================= */

static void runtime_task(void)
{
    if(ui == NULL)
        return;

    uint32_t now = HAL_GetTick();
    uint8_t enabled = (ui->enable != 0U) ? 1U : 0U;

    /*
     * New output session.
     */
    if((runtime_prev_enable == 0U) &&
       (enabled != 0U))
    {
        runtime_elapsed_sec = 0U;
        runtime_ms_remainder = 0U;
        runtime_last_tick = now;
        runtime_dirty = 1U;
    }
    /*
     * Continue current session.
     *
     * Unsigned subtraction handles HAL_GetTick() wrap-around.
     */
    else if((runtime_prev_enable != 0U) &&
            (enabled != 0U))
    {
        uint32_t delta =
            (uint32_t)(now - runtime_last_tick);

        runtime_last_tick = now;

        uint32_t total_ms =
            runtime_ms_remainder + delta;

        if(total_ms >= 1000U)
        {
            runtime_elapsed_sec +=
                total_ms / 1000U;

            runtime_ms_remainder =
                total_ms % 1000U;

            runtime_dirty = 1U;
        }
        else
        {
            runtime_ms_remainder = total_ms;
        }
    }
    /*
     * Output turned OFF / protection tripped:
     * keep the final elapsed value frozen.
     */
    else if((runtime_prev_enable != 0U) &&
            (enabled == 0U))
    {
        uint32_t delta =
            (uint32_t)(now - runtime_last_tick);

        uint32_t total_ms =
            runtime_ms_remainder + delta;

        runtime_elapsed_sec +=
            total_ms / 1000U;

        runtime_ms_remainder =
            total_ms % 1000U;

        runtime_last_tick = now;
        runtime_dirty = 1U;
    }
    else
    {
        runtime_last_tick = now;
    }

    runtime_prev_enable = enabled;
}

static void runtime_format(char *buf,
                           uint16_t size)
{
    uint32_t total = runtime_elapsed_sec;

    uint32_t hours =
        total / 3600U;

    uint32_t minutes =
        (total / 60U) % 60U;

    uint32_t seconds =
        total % 60U;

    snprintf(
        buf,
        size,
        "%02lu:%02lu:%02lu",
        (unsigned long)hours,
        (unsigned long)minutes,
        (unsigned long)seconds
    );
}

static void draw_runtime_number(void)
{
    char buf[20];

    runtime_format(buf, sizeof(buf));

    /*
     * Reuse the old CV/CC + ON/OFF area.
     */
    draw_text_diff(
        218,
        180,
        2,
        C_WHITE,
        buf,
        old_runtime_number,
        sizeof(old_runtime_number)
    );
}

static void draw_runtime_graph(void)
{
    char buf[20];

    runtime_format(buf, sizeof(buf));

    /*
     * Same location as NUMBER: old CV/CC + ON/OFF area.
     */
    draw_text_diff(
        218,
        180,
        2,
        C_WHITE,
        buf,
        old_runtime_graph,
        sizeof(old_runtime_graph)
    );
}


/* =========================================================
 * NUMBER UI
 * ========================================================= */

static void draw_number_static(void)
{
    ST7789_FillScreen(C_BG);

    ST7789_DrawFilledRectangle(0, 60, 320, 1, C_GRID);
    ST7789_DrawFilledRectangle(0, 121, 320, 1, C_GRID);
    ST7789_DrawFilledRectangle(0, 193, 320, 1, C_GRID);

    ST7789_PutString(4, 8,   "V", 2, C_VOLT, C_BG);
    ST7789_PutString(4, 69,  "A", 2, C_CURR, C_BG);
    ST7789_PutString(4, 130, "W", 2, C_POWER, C_BG);

    /* Units on the right are static: draw once, not every measurement update. */
    ST7789_PutString(286, 31,  "V", 2, C_VOLT, C_BG);
    ST7789_PutString(286, 92,  "A", 2, C_CURR, C_BG);
    ST7789_PutString(286, 149, "W", 2, C_POWER, C_BG);

    ST7789_PutString(4,   210, "VSET", 2, C_VOLT, C_BG);
    ST7789_PutString(164, 210, "ISET", 2, C_CURR, C_BG);

    /* Runtime replaces CV/CC + ON/OFF. */
    ST7789_PutString(218, 170, "RUN", 1, C_MUTED, C_BG);

    invalidate_main_cache();
}

static void draw_number_measurements(void)
{
    char buf[64];

    /*
     * VOUT
     * Old version cleared a 250x51 rectangle every time.
     * New version updates only changed character cells.
     */
    snprintf(buf, sizeof(buf), "%05.2f", disp_vout);

    draw_text_diff(
        34,
        4,
        6,
        C_VOLT,
        buf,
        old_vout,
        sizeof(old_vout)
    );

    /*
     * IOUT
     */
    snprintf(buf, sizeof(buf), "%05.2f", disp_iout);

    draw_text_diff(
        34,
        65,
        6,
        C_CURR,
        buf,
        old_iout,
        sizeof(old_iout)
    );

    /*
     * POWER
     */
    float power = disp_vout * disp_iout;

    snprintf(buf, sizeof(buf), "%06.1f", power);

    draw_text_diff(
        42,
        126,
        5,
        C_POWER,
        buf,
        old_power,
        sizeof(old_power)
    );

    /*
     * Vin / Temperature:
     * also update only changed characters.
     */
    snprintf(buf, sizeof(buf),
             "Vin %.1fV  T %.0fC",
             disp_vin,
             disp_temp);

    draw_text_diff(
        5,
        180,
        2,
        C_MUTED,
        buf,
        old_info,
        sizeof(old_info)
    );
}

static void draw_number_status(void)
{
    /*
     * CV / CC / FAULT and ON / OFF are intentionally hidden.
     * Their former display area is used by RUN TIME.
     */
    status_dirty = 0U;
}

/* =========================================================
 * SET FIELD - shared by NUMBER and GRAPH
 * ========================================================= */

static uint8_t selected_char_index(DigitMode_t mode)
{
    switch(mode)
    {
        case DIGIT_1:   return 1U;
        case DIGIT_001: return 4U;
        case DIGIT_01:
        default:        return 3U;
    }
}

static void draw_set_value_at(uint8_t is_voltage,
                              uint16_t base_x,
                              uint16_t value_y,
                              uint16_t underline_y,
                              uint16_t clear_y,
                              uint16_t clear_h)
{
    (void)clear_y;
    (void)clear_h;

    if(ui == NULL)
        return;

    const uint16_t label_color =
        is_voltage ? C_VOLT : C_CURR;

    const float value =
        is_voltage ? ui->vset : ui->iset;

    const DigitMode_t mode =
        is_voltage ? v_digit : i_digit;

    const uint32_t edit_until =
        is_voltage ? v_edit_until : i_edit_until;

    char *rendered =
        is_voltage ? rendered_vset : rendered_iset;

    int8_t *old_underline =
        is_voltage
        ? &rendered_v_underline
        : &rendered_i_underline;

    uint8_t *unit_drawn =
        is_voltage
        ? &rendered_v_unit
        : &rendered_i_unit;

    uint32_t now = HAL_GetTick();

    uint8_t editing =
        ((int32_t)(edit_until - now) > 0)
        ? 1U
        : 0U;

    uint8_t selected =
        selected_char_index(mode);

    char buf[16];
    snprintf(buf, sizeof(buf), "%05.2f", value);

    /*
     * One character cell at scale 2 = about 12 x 16 px.
     *
     * Compare every displayed digit with what is currently on screen.
     * Only changed cells are transferred over SPI.
     */
    for(uint8_t i = 0U; i < 5U; i++)
    {
        char desired = buf[i];

        /*
         * Blink OFF:
         * only selected digit becomes blank.
         */
        if(editing &&
           i == selected &&
           blink_on == 0U)
        {
            desired = '\0';
        }

        if(rendered[i] == desired)
            continue;

        uint16_t x =
            (uint16_t)(base_x + i * 12U);

        ST7789_DrawFilledRectangle(
            x,
            value_y,
            12U,
            16U,
            C_BG
        );

        if(desired != '\0')
        {
            char ch[2] =
            {
                desired,
                '\0'
            };

            ST7789_PutString(
                x,
                value_y,
                ch,
                2,
                C_WHITE,
                C_BG
            );
        }

        rendered[i] = desired;
    }

    /*
     * Unit is static for this field.
     * Draw only once after each full-screen transition.
     */
    if(!(*unit_drawn))
    {
        ST7789_PutString(
            (uint16_t)(base_x + 64U),
            value_y,
            is_voltage ? "V" : "A",
            2,
            label_color,
            C_BG
        );

        *unit_drawn = 1U;
    }

    /*
     * Underline:
     * only erase the OLD underline and draw the NEW one
     * if the selected digit changed / edit mode ended.
     */
    int8_t new_underline =
        editing ? (int8_t)selected : -1;

    if(*old_underline != new_underline)
    {
        if(*old_underline >= 0)
        {
            uint16_t old_x =
                (uint16_t)(
                    base_x +
                    (uint16_t)(*old_underline) * 12U
                );

            ST7789_DrawFilledRectangle(
                old_x,
                underline_y,
                10U,
                2U,
                C_BG
            );
        }

        if(new_underline >= 0)
        {
            uint16_t new_x =
                (uint16_t)(
                    base_x +
                    (uint16_t)new_underline * 12U
                );

            ST7789_DrawFilledRectangle(
                new_x,
                underline_y,
                10U,
                2U,
                label_color
            );
        }

        *old_underline = new_underline;
    }
}

static void draw_number_set_field(uint8_t is_voltage)
{
    const uint16_t base_x =
        is_voltage ? 55U : 215U;

    draw_set_value_at(
        is_voltage,
        base_x,
        208,
        229,
        199,
        40
    );
}

/* =========================================================
 * GRAPH UI
 * ========================================================= */

static void draw_line_fast(int x0,
                           int y0,
                           int x1,
                           int y1,
                           uint16_t color)
{
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int sx = (x0 < x1) ? 1 : -1;

    int dy = (y1 > y0) ? (y0 - y1) : (y1 - y0);
    int sy = (y0 < y1) ? 1 : -1;

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

static int graph_map_v(float v)
{
    v = clampf_ui(v, 0.0f, GRAPH_VMAX);

    return GRAPH_Y1 -
        (int)((v / GRAPH_VMAX) *
        (float)(GRAPH_H - 1));
}

static int graph_map_i(float i)
{
    i = clampf_ui(i, 0.0f, GRAPH_IMAX);

    return GRAPH_Y1 -
        (int)((i / GRAPH_IMAX) *
        (float)(GRAPH_H - 1));
}

static void graph_restore_grid_column(int x)
{
    int y1 = GRAPH_Y0 + GRAPH_H / 4;
    int y2 = GRAPH_Y0 + GRAPH_H / 2;
    int y3 = GRAPH_Y0 + 3 * GRAPH_H / 4;

    ST7789_DrawPixel((uint16_t)x, (uint16_t)y1, C_GRID);
    ST7789_DrawPixel((uint16_t)x, (uint16_t)y2, C_GRID);
    ST7789_DrawPixel((uint16_t)x, (uint16_t)y3, C_GRID);
}

static void graph_clear_column(int x)
{
    if(x < GRAPH_X0 || x > GRAPH_X1)
        return;

    ST7789_DrawFilledRectangle(
        (uint16_t)x,
        GRAPH_Y0,
        1U,
        GRAPH_H,
        C_BG
    );

    graph_restore_grid_column(x);
}

static void draw_graph_static(void)
{
    ST7789_FillScreen(C_BG);

    ST7789_PutString(5, 5, "V", 2, C_VOLT, C_BG);
    ST7789_PutString(23, 5, "VOUT", 1, C_VOLT, C_BG);

    ST7789_PutString(82, 5, "A", 2, C_CURR, C_BG);
    ST7789_PutString(100, 5, "IOUT", 1, C_CURR, C_BG);

    /* plot frame */
    ST7789_DrawFilledRectangle(GRAPH_X0, GRAPH_Y0, GRAPH_W, 1, C_GRID);
    ST7789_DrawFilledRectangle(GRAPH_X0, GRAPH_Y1, GRAPH_W, 1, C_GRID);
    ST7789_DrawFilledRectangle(GRAPH_X0, GRAPH_Y0, 1, GRAPH_H, C_GRID);
    ST7789_DrawFilledRectangle(GRAPH_X1, GRAPH_Y0, 1, GRAPH_H, C_GRID);

    for(uint8_t k = 1U; k < 4U; k++)
    {
        uint16_t y =
            (uint16_t)(GRAPH_Y0 + (GRAPH_H * k) / 4U);

        for(uint16_t x = GRAPH_X0;
            x <= GRAPH_X1;
            x += 4U)
        {
            ST7789_DrawPixel(x, y, C_GRID);
        }
    }

    /*
     * Same control/status area as NUMBER:
     * Vin/Temp, CV/CC, ON/OFF, VSET, ISET.
     */
    ST7789_DrawFilledRectangle(0, 176, 320, 1, C_GRID);

    /* Runtime replaces CV/CC + ON/OFF. */
    ST7789_PutString(218, 170, "RUN", 1, C_MUTED, C_BG);

    ST7789_PutString(4,   210, "VSET", 2, C_VOLT, C_BG);
    ST7789_PutString(164, 210, "ISET", 2, C_CURR, C_BG);

    graph_head = 0U;
    graph_has_last = 0U;
    graph_last_x = -1;
    graph_tick = HAL_GetTick();

    invalidate_main_cache();
}

static void draw_graph_live_info(void)
{
    if(ui == NULL)
        return;

    char buf[64];

    snprintf(buf, sizeof(buf),
             "%.2fV %.2fA %.0fW",
             disp_vout,
             disp_iout,
             disp_vout * disp_iout);

    draw_text_diff(
        160,
        5,
        1,
        C_WHITE,
        buf,
        old_graph_live,
        sizeof(old_graph_live)
    );

    snprintf(buf, sizeof(buf),
             "Vin %.1fV  T %.0fC",
             disp_vin,
             disp_temp);

    draw_text_diff(
        5,
        180,
        2,
        C_MUTED,
        buf,
        old_info,
        sizeof(old_info)
    );

    /*
     * CV/CC/FAULT and ON/OFF are no longer drawn.
     * RUN TIME occupies that area.
     */
    status_dirty = 0U;
}

static void draw_graph_set_field(uint8_t is_voltage)
{
    const uint16_t base_x =
        is_voltage ? 55U : 215U;

    draw_set_value_at(
        is_voltage,
        base_x,
        208,
        229,
        199,
        40
    );
}

static void graph_sample_task(void)
{
    uint32_t now = HAL_GetTick();

    if((uint32_t)(now - graph_tick) < GRAPH_SAMPLE_MS)
        return;

    graph_tick = now;

    int x = GRAPH_X0 + (int)graph_head;
    int yv = graph_map_v(disp_vout);
    int yi = graph_map_i(disp_iout);

    graph_clear_column(x);

    if(x < GRAPH_X1)
        graph_clear_column(x + 1);

    if(graph_has_last &&
       graph_last_x >= GRAPH_X0 &&
       x > graph_last_x)
    {
        draw_line_fast(
            graph_last_x,
            graph_last_yv,
            x,
            yv,
            C_VOLT
        );

        draw_line_fast(
            graph_last_x,
            graph_last_yi,
            x,
            yi,
            C_CURR
        );
    }
    else
    {
        ST7789_DrawPixel((uint16_t)x, (uint16_t)yv, C_VOLT);
        ST7789_DrawPixel((uint16_t)x, (uint16_t)yi, C_CURR);
    }

    graph_last_x = (int16_t)x;
    graph_last_yv = (int16_t)yv;
    graph_last_yi = (int16_t)yi;
    graph_has_last = 1U;

    graph_head++;

    if(graph_head >= GRAPH_W)
    {
        graph_head = 0U;
        graph_has_last = 0U;
    }

    draw_graph_live_info();
}

/* =========================================================
 * MENU
 * ========================================================= */

static const char *ui_mode_text(UIMode_t mode)
{
    return (mode == UI_MODE_GRAPH) ? "GRAPH" : "NUMBER";
}

static void draw_menu_row(uint8_t row,
                          const char *name,
                          const char *value)
{
    uint16_t y =
        (uint16_t)(58U + row * 37U);

    uint8_t selected =
        ((uint8_t)menu_item == row)
        ? 1U
        : 0U;

    uint16_t fg =
        selected ? C_BG : C_WHITE;

    uint16_t bg =
        selected ? C_VOLT : C_BG;

    ST7789_DrawFilledRectangle(
        20,
        (uint16_t)(y - 5U),
        280,
        30,
        bg
    );

    ST7789_PutString(
        34,
        y,
        name,
        2,
        fg,
        bg
    );

    ST7789_PutString(
        156,
        y,
        value,
        2,
        fg,
        bg
    );
}

static void draw_menu(void)
{
    ST7789_FillScreen(C_BG);

    ST7789_PutString(
        100,
        8,
        "MENU",
        3,
        C_WHITE,
        C_BG
    );

    ST7789_DrawFilledRectangle(
        14,
        42,
        292,
        1,
        C_GRID
    );

    char b_ovp[20];
    char b_ocp[20];
    char b_opp[20];

    snprintf(b_ovp, sizeof(b_ovp),
             "%.1f V",
             menu_ovp);

    snprintf(b_ocp, sizeof(b_ocp),
             "%.1f A",
             menu_ocp);

    snprintf(b_opp, sizeof(b_opp),
             "%.0f W",
             menu_opp);

    draw_menu_row(
        MENU_ITEM_UI,
        "UI",
        ui_mode_text(menu_ui_mode)
    );

    draw_menu_row(
        MENU_ITEM_OVP,
        "OVP",
        b_ovp
    );

    draw_menu_row(
        MENU_ITEM_OCP,
        "OCP",
        b_ocp
    );

    draw_menu_row(
        MENU_ITEM_OPP,
        "OPP",
        b_opp
    );

    ST7789_DrawFilledRectangle(
        10,
        211,
        300,
        1,
        C_GRID
    );

    ST7789_PutString(
        10,
        220,
        "V:ITEM  I:VALUE  OUT:OK",
        1,
        C_MUTED,
        C_BG
    );
}

/* =========================================================
 * SCREEN CONTROL
 * ========================================================= */

static void enter_live_mode(UIMode_t mode)
{
    ui_mode = mode;

    if(mode == UI_MODE_GRAPH)
    {
        screen = SCREEN_GRAPH;

        draw_graph_static();
        draw_graph_live_info();
        draw_runtime_graph();
        draw_graph_set_field(1U);
        draw_graph_set_field(0U);
    }
    else
    {
        screen = SCREEN_NUMBER;

        draw_number_static();
        draw_number_measurements();
        draw_number_status();
        draw_runtime_number();
        draw_number_set_field(1U);
        draw_number_set_field(0U);
    }

    vset_dirty = 0U;
    iset_dirty = 0U;
}

static void enter_menu(void)
{
    /*
     * Work on temporary values.
     * Nothing is applied until PB9 confirms.
     */
    menu_ui_mode = ui_mode;

    menu_ovp = protect_ovp;
    menu_ocp = protect_ocp;
    menu_opp = protect_opp;

    menu_item = MENU_ITEM_UI;

    screen = SCREEN_MENU;
    draw_menu();
}

/* =========================================================
 * SOLDER MODE CONTROL
 * ========================================================= */

static void enter_solder_mode(void)
{
    if(ui == NULL || screen == SCREEN_SOLDER)
        return;

    /*
     * Remember which normal UI should return after SOLDER.
     * If called from MENU, ui_mode still contains the active NUMBER/GRAPH mode.
     */
    solder_return_ui_mode = ui_mode;

    /*
     * Backup power-supply settings.
     * These values are intentionally NOT written to Flash.
     */
    solder_backup_vset = ui->vset;
    solder_backup_iset = ui->iset;
    solder_backup_enable = ui->enable;

    /*
     * Same supply setup as the old solder mode.
     */
    ui->vset = SOLDER_BB_VSET;
    ui->iset = SOLDER_BB_ISET;
    ui->enable = 1U;
    ui->state = BBUI_STATE_CV;

    protect_fault = PROTECT_FAULT_NONE;
    protect_fault_latched = 0U;
    protection_arm();

#if SOLDER_ROUTE_ENABLE
    HAL_GPIO_WritePin(SOLDER_ROUTE_PORT,
                      SOLDER_ROUTE_PIN,
                      GPIO_PIN_RESET);
#endif

    UI_Solider_Enter();

    screen = SCREEN_SOLDER;

    /*
     * Reset encoder accumulators so the first solder adjustment
     * does not inherit an unfinished detent from NUMBER/GRAPH.
     */
    enc_v_acc = 0;
    enc_i_acc = 0;

    UI_Solider_Task(1U);
}

static void exit_solder_mode(void)
{
    if(ui == NULL || screen != SCREEN_SOLDER)
        return;

    UI_Solider_Exit();

    /*
     * Restore the supply exactly as it was before SOLDER.
     */
    ui->vset = solder_backup_vset;
    ui->iset = solder_backup_iset;
    ui->enable = solder_backup_enable;

    if(ui->enable)
        ui->state = BBUI_STATE_CV;
    else
        ui->state = BBUI_STATE_OFF;

#if SOLDER_ROUTE_ENABLE
    HAL_GPIO_WritePin(SOLDER_ROUTE_PORT,
                      SOLDER_ROUTE_PIN,
                      GPIO_PIN_SET);
#endif

    enter_live_mode(solder_return_ui_mode);
}

/*
 * PB11 falling-edge detector.
 *
 * HIGH -> LOW enters SOLDER.
 * LOW -> HIGH does nothing; leaving SOLDER is done by holding PB9 for 1 s.
 */
static void solder_detect_task(void)
{
    GPIO_PinState now =
        HAL_GPIO_ReadPin(SOLDER_DETECT_PORT,
                         SOLDER_DETECT_PIN);

    uint32_t tick = HAL_GetTick();

    if(solder_detect_last == GPIO_PIN_SET &&
       now == GPIO_PIN_RESET)
    {
        /*
         * Small edge lockout for switch/contact bounce.
         */
        if((uint32_t)(tick - solder_detect_tick) >= 100U)
        {
            solder_detect_tick = tick;

            if(screen != SCREEN_SOLDER)
                enter_solder_mode();
        }
    }

    solder_detect_last = now;
}

/*
 * PB9 short / long press handler.
 *
 * The short action happens on RELEASE. This is required so a 1-second
 * hold can be distinguished without first toggling the normal output.
 */
static void out_button_task(void)
{
    uint32_t now = HAL_GetTick();

    uint8_t pressed =
        (btn_out.stable == BTN_ACTIVE)
        ? 1U
        : 0U;

    if(pressed)
    {
        if(!outbtn_press_active)
        {
            outbtn_press_active = 1U;
            outbtn_long_handled = 0U;
            outbtn_press_tick = now;
        }

        if(!outbtn_long_handled &&
           (uint32_t)(now - outbtn_press_tick) >= SOLDER_HOLD_MS)
        {
            outbtn_long_handled = 1U;

            if(screen == SCREEN_SOLDER)
                exit_solder_mode();
            else
                enter_solder_mode();
        }
    }
    else
    {
        if(outbtn_press_active)
        {
            /*
             * Short press action only if the hold action never fired.
             */
            if(!outbtn_long_handled)
            {
                if(screen == SCREEN_SOLDER)
                {
                    /*
                     * Old solder behavior:
                     * OUT button selects preset #3.
                     */
                    UI_Solider_SelectPreset(2U);
                }
                else if(is_live_screen())
                {
                    /*
                     * If a protection fault is latched:
                     * first short press only ACK/CLEARs the fault.
                     * A second short press is then required to turn ON.
                     */
                    if(protect_fault_latched ||
                       ui->state == BBUI_STATE_FAULT)
                    {
                        clear_protection_fault();
                    }
                    else
                    {
                        uint8_t was_enabled = ui->enable;

                        ui->enable ^= 1U;

                        if((was_enabled == 0U) &&
                           (ui->enable != 0U))
                        {
                            protect_fault = PROTECT_FAULT_NONE;
                            protect_fault_latched = 0U;

                            protection_arm();

                            /*
                             * Save VSET/ISET and current protection limits.
                             */
                            (void)ui_flash_save(
                                ui->vset,
                                ui->iset
                            );
                        }

                        if(ui->enable == 0U)
                            ui->state = BBUI_STATE_OFF;
                        else if(ui->state == BBUI_STATE_OFF)
                            ui->state = BBUI_STATE_CV;

                        status_dirty = 1U;
                    }
                }
                else if(screen == SCREEN_MENU)
                {
                    /*
                     * Apply menu values only when PB9 confirms.
                     */
                    protect_ovp = menu_ovp;
                    protect_ocp = menu_ocp;
                    protect_opp = menu_opp;

                    /*
                     * Persist menu settings as well.
                     * Flash writer skips erase/write if nothing changed.
                     */
                    (void)ui_flash_save(
                        ui->vset,
                        ui->iset
                    );

                    enter_live_mode(menu_ui_mode);
                }
            }

            outbtn_press_active = 0U;
            outbtn_long_handled = 0U;
        }
    }
}

/* =========================================================
 * PROTECTION / FAULT
 * ========================================================= */

static void clear_protection_fault(void)
{
    protect_fault_latched = 0U;
    protect_fault = PROTECT_FAULT_NONE;

    protect_pending_fault = PROTECT_FAULT_NONE;
    protect_violation_tick = 0U;

    if(ui != NULL)
    {
        ui->enable = 0U;
        ui->state = BBUI_STATE_OFF;
    }

    protect_prev_enable = 0U;

    buzzer_stop();

    status_dirty = 1U;
}

static void abort_solder_on_fault(void)
{
    if(screen != SCREEN_SOLDER)
        return;

    UI_Solider_Exit();

    /*
     * Restore normal POWER setpoints, but NEVER restore output enable
     * after a protection event.
     */
    ui->vset = solder_backup_vset;
    ui->iset = solder_backup_iset;

    ui->enable = 0U;
    ui->state = BBUI_STATE_FAULT;

#if SOLDER_ROUTE_ENABLE
    HAL_GPIO_WritePin(
        SOLDER_ROUTE_PORT,
        SOLDER_ROUTE_PIN,
        GPIO_PIN_SET
    );
#endif

    enter_live_mode(solder_return_ui_mode);

    /*
     * enter_live_mode only changes screen rendering;
     * retain FAULT state.
     */
    ui->enable = 0U;
    ui->state = BBUI_STATE_FAULT;
    status_dirty = 1U;
}

static void protection_trip(ProtectFault_t fault)
{
    if(ui == NULL)
        return;

    protect_fault = fault;
    protect_fault_latched = 1U;

    ui->enable = 0U;
    ui->state = BBUI_STATE_FAULT;
    protect_prev_enable = 0U;

    protect_pending_fault = PROTECT_FAULT_NONE;
    protect_violation_tick = 0U;

    buzzer_fault_start();

    if(screen == SCREEN_SOLDER)
        abort_solder_on_fault();

    status_dirty = 1U;
}

static void protection_arm(void)
{
    protect_enable_tick = HAL_GetTick();

    protect_pending_fault = PROTECT_FAULT_NONE;
    protect_violation_tick = 0U;
}

static void protection_task(void)
{
    if(ui == NULL)
        return;

    /*
     * Automatically detect OFF -> ON even if another part of the
     * firmware changes ui->enable directly.
     */
    if((protect_prev_enable == 0U) &&
       (ui->enable != 0U))
    {
        protection_arm();
    }

    protect_prev_enable = ui->enable;

    /*
     * If another part of the power-stage firmware reports a FAULT,
     * still sound the buzzer once.
     */
    if(ui->state == BBUI_STATE_FAULT &&
       !protect_fault_latched)
    {
        protect_fault = PROTECT_FAULT_EXTERNAL;
        protect_fault_latched = 1U;

        buzzer_fault_start();
        status_dirty = 1U;
    }

    if(ui->enable == 0U ||
       ui->state == BBUI_STATE_FAULT)
    {
        protect_pending_fault = PROTECT_FAULT_NONE;
        protect_violation_tick = 0U;
        return;
    }

    uint32_t now = HAL_GetTick();

    if((uint32_t)(now - protect_enable_tick) <
       PROTECT_STARTUP_BLANK_MS)
    {
        protect_pending_fault = PROTECT_FAULT_NONE;
        protect_violation_tick = 0U;
        return;
    }

    float v = ui->vout;
    float i = ui->current;
    float p = v * i;

    ProtectFault_t violation = PROTECT_FAULT_NONE;

    /*
     * Priority if several limits are exceeded simultaneously:
     * OVP -> OCP -> OPP
     */
    if(v >= protect_ovp)
        violation = PROTECT_FAULT_OVP;
    else if(i >= protect_ocp)
        violation = PROTECT_FAULT_OCP;
    else if(p >= protect_opp)
        violation = PROTECT_FAULT_OPP;

    if(violation == PROTECT_FAULT_NONE)
    {
        protect_pending_fault = PROTECT_FAULT_NONE;
        protect_violation_tick = 0U;
        return;
    }

    if(protect_pending_fault != violation)
    {
        protect_pending_fault = violation;
        protect_violation_tick = now;
        return;
    }

    if((uint32_t)(now - protect_violation_tick) >=
       PROTECT_TRIP_DEBOUNCE_MS)
    {
        protection_trip(violation);
    }
}

/* =========================================================
 * VSET BUTTON: SHORT PRESS / LONG PRESS
 * ========================================================= */

static void vset_button_task(void)
{
    uint32_t now = HAL_GetTick();

    /*
     * btn_v.stable is updated by button_pressed_event()
     * before this function is called.
     */
    uint8_t pressed =
        (btn_v.stable == BTN_ACTIVE)
        ? 1U
        : 0U;

    if(pressed)
    {
        if(!vbtn_press_active)
        {
            vbtn_press_active = 1U;
            vbtn_long_handled = 0U;
            vbtn_press_tick = now;
        }

        /*
         * Long press only has meaning on a live screen.
         */
        if(!vbtn_long_handled &&
           is_live_screen() &&
           (uint32_t)(now - vbtn_press_tick) >= MENU_HOLD_MS)
        {
            vbtn_long_handled = 1U;
            enter_menu();
        }
    }
    else
    {
        if(vbtn_press_active)
        {
            /*
             * Released before reaching the long-press threshold:
             * normal VSET digit change.
             *
             * Do NOT do this after a long press.
             */
            if(!vbtn_long_handled)
            {
                if(is_live_screen())
                {
                    v_digit = digit_next(v_digit);
                    activate_v_edit();
                }
                else if(screen == SCREEN_SOLDER)
                {
                    /*
                     * Old solder behavior:
                     * VSET encoder button selects preset #1.
                     */
                    UI_Solider_SelectPreset(0U);
                }
            }

            vbtn_press_active = 0U;
            vbtn_long_handled = 0U;
        }
    }
}

/* =========================================================
 * ENCODERS
 * ========================================================= */

static uint8_t apply_encoder_delta(int32_t *acc,
                                   int16_t delta,
                                   float *value,
                                   float step,
                                   float lo,
                                   float hi)
{
    float old = *value;

    *acc += delta;

    while(*acc >= ENC_COUNTS_PER_DETENT)
    {
        *value += step;
        *acc -= ENC_COUNTS_PER_DETENT;
    }

    while(*acc <= -ENC_COUNTS_PER_DETENT)
    {
        *value -= step;
        *acc += ENC_COUNTS_PER_DETENT;
    }

    *value = clampf_ui(*value, lo, hi);

    return (*value != old) ? 1U : 0U;
}

static int8_t menu_encoder_step(int32_t *acc,
                                int16_t delta)
{
    *acc += delta;

    if(*acc >= ENC_COUNTS_PER_DETENT)
    {
        *acc -= ENC_COUNTS_PER_DETENT;
        return +1;
    }

    if(*acc <= -ENC_COUNTS_PER_DETENT)
    {
        *acc += ENC_COUNTS_PER_DETENT;
        return -1;
    }

    return 0;
}

static void encoder_task(void)
{
    if(ui == NULL)
        return;

    /* -----------------------------------------------------
     * VSET encoder
     *
     * LIVE:
     *     adjust VSET
     *
     * MENU:
     *     move between menu rows
     * ----------------------------------------------------- */
    if(tim_v != NULL)
    {
        int16_t now =
            (int16_t)__HAL_TIM_GET_COUNTER(tim_v);

        int16_t delta =
            (int16_t)(now - enc_v_last);

        enc_v_last = now;

        if(delta != 0)
        {
            if(is_live_screen())
            {
                if(apply_encoder_delta(
                    &enc_v_acc,
                    delta,
                    &ui->vset,
                    digit_step(v_digit),
                    VSET_MIN,
                    VSET_MAX))
                {
                    activate_v_edit();
                }
            }
            else if(screen == SCREEN_SOLDER)
            {
                int8_t dir =
                    menu_encoder_step(
                        &enc_v_acc,
                        delta
                    );

                if(dir != 0)
                    UI_Solider_EncoderAdjust(dir);
            }
            else if(screen == SCREEN_MENU)
            {
                int8_t dir =
                    menu_encoder_step(
                        &enc_v_acc,
                        delta
                    );

                if(dir != 0)
                {
                    int item =
                        (int)menu_item + dir;

                    if(item < 0)
                        item = MENU_ITEM_COUNT - 1;

                    if(item >= MENU_ITEM_COUNT)
                        item = 0;

                    menu_item =
                        (MenuItem_t)item;

                    draw_menu();
                }
            }
        }
    }

    /* -----------------------------------------------------
     * ISET encoder
     *
     * LIVE:
     *     adjust ISET
     *
     * MENU:
     *     change the VALUE of selected row
     *
     * Current row:
     *     UI : NUMBER <-> GRAPH
     * ----------------------------------------------------- */
    if(tim_i != NULL)
    {
        int16_t now =
            (int16_t)__HAL_TIM_GET_COUNTER(tim_i);

        int16_t delta =
            (int16_t)(now - enc_i_last);

        enc_i_last = now;

        if(delta != 0)
        {
            if(is_live_screen())
            {
                if(apply_encoder_delta(
                    &enc_i_acc,
                    delta,
                    &ui->iset,
                    digit_step(i_digit),
                    ISET_MIN,
                    ISET_MAX))
                {
                    activate_i_edit();
                }
            }
            else if(screen == SCREEN_MENU)
            {
                int8_t dir =
                    menu_encoder_step(
                        &enc_i_acc,
                        delta
                    );

                if(dir != 0)
                {
                    if(menu_item == MENU_ITEM_UI)
                    {
                        menu_ui_mode =
                            (menu_ui_mode == UI_MODE_NUMBER)
                            ? UI_MODE_GRAPH
                            : UI_MODE_NUMBER;
                    }
                    else if(menu_item == MENU_ITEM_OVP)
                    {
                        menu_ovp +=
                            (float)dir * PROTECT_OVP_STEP;

                        menu_ovp =
                            clampf_ui(
                                menu_ovp,
                                PROTECT_OVP_MIN,
                                PROTECT_OVP_MAX
                            );
                    }
                    else if(menu_item == MENU_ITEM_OCP)
                    {
                        menu_ocp +=
                            (float)dir * PROTECT_OCP_STEP;

                        menu_ocp =
                            clampf_ui(
                                menu_ocp,
                                PROTECT_OCP_MIN,
                                PROTECT_OCP_MAX
                            );
                    }
                    else if(menu_item == MENU_ITEM_OPP)
                    {
                        menu_opp +=
                            (float)dir * PROTECT_OPP_STEP;

                        menu_opp =
                            clampf_ui(
                                menu_opp,
                                PROTECT_OPP_MIN,
                                PROTECT_OPP_MAX
                            );
                    }

                    draw_menu();
                }
            }
        }
    }
}

/* =========================================================
 * BUTTONS
 * ========================================================= */

static void button_task(void)
{
    if(ui == NULL)
        return;

    /*
     * Update debounced stable state for all three buttons.
     *
     * VSET and OUT need stable press/release timing because both have
     * long-press behavior.
     */
    (void)button_pressed_event(&btn_v);

    uint8_t pi =
        button_pressed_event(&btn_i);

    (void)button_pressed_event(&btn_out);

    /*
     * VSET:
     * normal short press / normal long menu / solder preset #1.
     */
    vset_button_task();

    /*
     * OUT:
     * normal short ON/OFF / long SOLDER / solder preset #3 / long exit.
     */
    out_button_task();

    /*
     * ISET button remains an immediate short-press event.
     */
    if(pi)
    {
        if(is_live_screen())
        {
            i_digit = digit_next(i_digit);
            activate_i_edit();
        }
        else if(screen == SCREEN_SOLDER)
        {
            /*
             * Old solder behavior:
             * ISET encoder button selects preset #2.
             */
            UI_Solider_SelectPreset(1U);
        }
        else if(screen == SCREEN_MENU)
        {
            /*
             * No action in menu. Menu values are changed by rotating ISET.
             */
        }
    }
}

/* =========================================================
 * BLINK
 * ========================================================= */

static void blink_task(void)
{
    if(!is_live_screen())
        return;

    uint32_t now = HAL_GetTick();

    uint8_t any_edit =
        (((int32_t)(v_edit_until - now) > 0) ||
         ((int32_t)(i_edit_until - now) > 0))
        ? 1U : 0U;

    if(!any_edit)
    {
        if(blink_on == 0U)
        {
            blink_on = 1U;
            vset_dirty = 1U;
            iset_dirty = 1U;
        }

        return;
    }

    if((uint32_t)(now - blink_tick) >= BLINK_PERIOD_MS)
    {
        blink_tick = now;
        blink_on ^= 1U;

        if((int32_t)(v_edit_until - now) > 0)
            vset_dirty = 1U;

        if((int32_t)(i_edit_until - now) > 0)
            iset_dirty = 1U;
    }
}

/* =========================================================
 * PUBLIC
 * ========================================================= */

void BBUI_Init(BBUI_Data_t *data,
               TIM_HandleTypeDef *htim_vset,
               TIM_HandleTypeDef *htim_iset)
{
    ui = data;
    tim_v = htim_vset;
    tim_i = htim_iset;

    if(ui != NULL)
    {
        float saved_vset = 0.0f;
        float saved_iset = 0.0f;

        /*
         * Restore the most recently saved setpoints.
         * If Flash is empty/invalid, keep the values supplied by main.c.
         */
        if(ui_flash_read(
               &saved_vset,
               &saved_iset))
        {
            ui->vset = saved_vset;
            ui->iset = saved_iset;
        }

        ui->vset =
            clampf_ui(ui->vset, VSET_MIN, VSET_MAX);

        ui->iset =
            clampf_ui(ui->iset, ISET_MIN, ISET_MAX);

        /*
         * Always start with output OFF for safety.
         * Only the setpoints are remembered.
         */
        ui->enable = 0U;
        ui->state = BBUI_STATE_OFF;
    }

    if(tim_v != NULL)
    {
        HAL_TIM_Encoder_Start(tim_v, TIM_CHANNEL_ALL);

        enc_v_last =
            (int16_t)__HAL_TIM_GET_COUNTER(tim_v);
    }

    if(tim_i != NULL)
    {
        HAL_TIM_Encoder_Start(tim_i, TIM_CHANNEL_ALL);

        enc_i_last =
            (int16_t)__HAL_TIM_GET_COUNTER(tim_i);
    }

    btn_v.raw_last =
        HAL_GPIO_ReadPin(btn_v.port, btn_v.pin);

    btn_v.stable = btn_v.raw_last;
    btn_v.changed_at = HAL_GetTick();

    btn_i.raw_last =
        HAL_GPIO_ReadPin(btn_i.port, btn_i.pin);

    btn_i.stable = btn_i.raw_last;
    btn_i.changed_at = HAL_GetTick();

    btn_out.raw_last =
        HAL_GPIO_ReadPin(btn_out.port, btn_out.pin);

    btn_out.stable = btn_out.raw_last;
    btn_out.changed_at = HAL_GetTick();

    ST7789_Init();
    ST7789_SetRotation(1U);

    /*
     * GPIO must already be configured as OUTPUT_PP in MX_GPIO_Init().
     */
    buzzer_stop();

    UI_Solider_Init();

    solder_detect_last =
        HAL_GPIO_ReadPin(
            SOLDER_DETECT_PORT,
            SOLDER_DETECT_PIN
        );

    solder_detect_tick = HAL_GetTick();

#if SOLDER_ROUTE_ENABLE
    /* Normal power route at startup. */
    HAL_GPIO_WritePin(SOLDER_ROUTE_PORT,
                      SOLDER_ROUTE_PIN,
                      GPIO_PIN_SET);
#endif

    if(ui != NULL)
    {
        disp_vout = ui->vout;
        disp_iout = ui->current;
        disp_vin = ui->vin;
        disp_temp = ui->temp;
        filter_init = 1U;
    }

    filter_tick = HAL_GetTick();
    meas_tick = HAL_GetTick();
    blink_tick = HAL_GetTick();

    protect_fault = PROTECT_FAULT_NONE;
    protect_fault_latched = 0U;
    protect_pending_fault = PROTECT_FAULT_NONE;
    protect_enable_tick = HAL_GetTick();
    protect_prev_enable = 0U;

    runtime_prev_enable = 0U;
    runtime_last_tick = HAL_GetTick();
    runtime_elapsed_sec = 0U;
    runtime_ms_remainder = 0U;
    runtime_dirty = 1U;

    enter_live_mode(UI_MODE_NUMBER);
}

void BBUI_Task(void)
{
    uint32_t now = HAL_GetTick();

    filter_task();

    /*
     * PB11 HIGH -> LOW may enter SOLDER from NUMBER, GRAPH or MENU.
     */
    solder_detect_task();

    encoder_task();
    button_task();

    /*
     * Protection is independent of the selected NUMBER/GRAPH/SOLDER view.
     */
    protection_task();
    buzzer_task();

    /*
     * Count actual output run time after button/protection logic has
     * finalized ui->enable for this loop.
     */
    runtime_task();

    blink_task();

    if(screen == SCREEN_NUMBER)
    {
        if((uint32_t)(now - meas_tick) >= MEAS_REFRESH_MS)
        {
            meas_tick = now;

            draw_number_measurements();
            draw_number_status();
        }

        if(vset_dirty)
        {
            draw_number_set_field(1U);
            vset_dirty = 0U;
        }

        if(iset_dirty)
        {
            draw_number_set_field(0U);
            iset_dirty = 0U;
        }

        if(status_dirty)
            draw_number_status();

        if(runtime_dirty)
        {
            draw_runtime_number();
            runtime_dirty = 0U;
        }
    }
    else if(screen == SCREEN_GRAPH)
    {
        graph_sample_task();

        /*
         * Same interaction feedback as NUMBER.
         */
        if(vset_dirty)
        {
            draw_graph_set_field(1U);
            vset_dirty = 0U;
        }

        if(iset_dirty)
        {
            draw_graph_set_field(0U);
            iset_dirty = 0U;
        }

        if(status_dirty)
        {
            draw_graph_live_info();
        }

        if(runtime_dirty)
        {
            draw_runtime_graph();
            runtime_dirty = 0U;
        }
    }
    else if(screen == SCREEN_SOLDER)
    {
        UI_Solider_Task(0U);
    }
}

void BBUI_ForceRefresh(void)
{
    if(screen == SCREEN_SOLDER)
        UI_Solider_Task(1U);
    else
        enter_live_mode(ui_mode);
}

void BBUI_ButtonIRQ(void)
{
    /* Polling + debounce is used. */
}
