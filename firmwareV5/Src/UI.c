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

/*
 * SOLDER sleep sensor:
 *   PB11 LOW  = iron in holder  -> SLEEP
 *   PB11 HIGH = iron removed    -> WAKE
 * PB11 no longer enters SOLDER.
 */
#define SOLDER_SLEEP_PORT              GPIOB
#define SOLDER_SLEEP_PIN               GPIO_PIN_11
#define SOLDER_SLEEP_ACTIVE            GPIO_PIN_RESET

/*
 * PB11 is sampled only while the solder heater is fully OFF.
 *
 * Heater OFF in the current TIM3 implementation:
 *     solider_pwm_ccr == TIM3->ARR
 *
 * Wait 2 ms after entering OFF so gate/sense switching noise has
 * settled. This matches the temperature-sensing settle interval.
 */
#define SOLDER_SLEEP_OFF_SETTLE_MS     2U

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

/* Blue Pill PC13 onboard LED is active-low. */
#define STATUS_LED_PORT                GPIOC
#define STATUS_LED_PIN                 GPIO_PIN_13
#define STATUS_LED_ACTIVE_STATE        GPIO_PIN_SET
#define STATUS_LED_IDLE_STATE          GPIO_PIN_RESET

/* Optional old power-route / relay output. Set 1 if PB15 is still used. */
#define SOLDER_ROUTE_ENABLE            1U
#define SOLDER_ROUTE_PORT              GPIOB
#define SOLDER_ROUTE_PIN               GPIO_PIN_15

#define SOLDER_C245_BB_VSET            20.0f
#define SOLDER_C245_BB_ISET             6.0f
#define SOLDER_C210_BB_VSET            12.0f
#define SOLDER_C210_BB_ISET             5.0f

#define BTN_ACTIVE                     GPIO_PIN_RESET
#define BTN_DEBOUNCE_MS                35U

#define ENC_COUNTS_PER_DETENT          4
#define MENU_HOLD_MS                   1000U
#define SOLDER_HOLD_MS                 1000U

/*
 * POWER -> SOLDER transition dead time.
 *
 * Sequence:
 *   1. Disable BB output
 *   2. Wait 300 ms
 *   3. Switch PB15 to SOLDER route
 *   4. Apply C245/C210 BB setpoints and enable output
 */
#define SOLDER_POWER_SWITCH_DELAY_MS    300U

/*
 * After the BB converter is enabled with the SOLDER setpoint,
 * keep the heater OFF long enough for Vout/Cout to charge.
 *
 * 1000 ms is intentional:
 * with the current SOFT-start slope (~20 V/s), C245 at 18 V
 * needs about 0.9 s to reach its requested output voltage.
 */
#define SOLDER_CAP_PRECHARGE_DELAY_MS   1000U

/* 1 = boot directly to SOLDER, 0 = boot to NUMBER. */
#define BOOT_DEFAULT_SOLDER            0U

/* =========================================================
 * RANGE
 * ========================================================= */

#define VSET_MIN                       1.00f
#define VSET_MAX                       50.00f

#define ISET_MIN                       0.10f
#define ISET_MAX                       12.00f


/* =========================================================
 * USER SETTINGS
 * ========================================================= */

#define SLEEP_TEMP_DEFAULT             200.0f
#define SLEEP_TEMP_MIN                 150.0f
#define SLEEP_TEMP_MAX                 300.0f
#define SLEEP_TEMP_STEP                  5.0f

typedef enum
{
    UI_THEME_DARK = 0,
    UI_THEME_LIGHT
} UITheme_t;

typedef enum
{
    SOLDER_TIP_C245 = 0,
    SOLDER_TIP_C210
} SolderTip_t;

static float sleep_temp = SLEEP_TEMP_DEFAULT;
static uint8_t buzzer_button_enable = 1U;
static UITheme_t ui_theme = UI_THEME_DARK;
static SolderTip_t solder_tip = SOLDER_TIP_C245;

static float menu_sleep_temp = SLEEP_TEMP_DEFAULT;
static uint8_t menu_buzzer_button_enable = 1U;
static UITheme_t menu_theme = UI_THEME_DARK;
static SolderTip_t menu_solder_tip = SOLDER_TIP_C245;
static BBUI_StartMode_t menu_start_mode = BB_START_SOFT;
static BBUI_ResponseMode_t menu_response_mode = BB_RESPONSE_NORMAL;
static BBUI_ControlMode_t menu_control_mode = BB_CONTROL_CLOSED;

/*
 * Restored from Flash before the UI menu is used.
 * Older Flash versions fall back to SOFT + NORMAL.
 */
static BBUI_StartMode_t flash_start_mode = BB_START_SOFT;
static BBUI_ResponseMode_t flash_response_mode = BB_RESPONSE_NORMAL;
static BBUI_ControlMode_t flash_control_mode = BB_CONTROL_CLOSED;
static float flash_openloop_ratio = 0.50f;


/* =========================================================
 * SETPOINT FLASH AUTOSAVE
 *
 * Do NOT write Flash on every encoder detent.
 * Wait until the user has stopped changing VSET/ISET for 1.2 s.
 *
 * This gives:
 *   - latest setpoint survives power-cycle
 *   - much lower Flash erase/write count
 * ========================================================= */

#define UI_FLASH_AUTOSAVE_DELAY_MS       1200U

static uint8_t ui_flash_dirty = 0U;
static uint32_t ui_flash_dirty_tick = 0U;

/* Keil Watch helpers */
volatile uint8_t ui_flash_last_read_ok = 0U;
volatile uint8_t ui_flash_last_save_ok = 0U;


/*
 * Main UI data pointer.
 *
 * Declared here because Flash save/load helpers below also use it.
 */
static BBUI_Data_t *ui = NULL;


/* =========================================================
 * OUTPUT PROTECTION
 * ========================================================= */

/*
 * Default thresholds for 50-V / 12-A user range.
 *
 * Protection is intentionally set slightly above normal setpoint
 * maxima to avoid nuisance trips due to ripple/noise.
 */
#define PROTECT_OVP_DEFAULT             52.0f
#define PROTECT_OCP_DEFAULT             12.5f
#define PROTECT_OPP_DEFAULT            200.0f

#define PROTECT_OVP_MIN                  5.0f
#define PROTECT_OVP_MAX                 60.0f
#define PROTECT_OVP_STEP                 0.5f

#define PROTECT_OCP_MIN                  0.5f
#define PROTECT_OCP_MAX                 14.0f
#define PROTECT_OCP_STEP                 0.1f

/*
 * 50 V x 12 A = 600 W theoretical user-range product.
 * Keep default OPP at 200 W for safety, but allow menu adjustment
 * up to 600 W if the real hardware is designed for that power.
 */
#define PROTECT_OPP_MIN                 10.0f
#define PROTECT_OPP_MAX                600.0f
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


/*
 * Fault codes exported by main.c.
 * Keep these numeric values synchronized with main.c.
 */
#define POWER_FAULT_NONE          0U
#define POWER_FAULT_VIN_LOW       1U
#define POWER_FAULT_TEMP          2U
#define POWER_FAULT_HARD_CURRENT  3U

extern volatile uint8_t g_power_fault_code;

extern volatile float g_power_fault_vin;
extern volatile float g_power_fault_vout;
extern volatile float g_power_fault_current;
extern volatile float g_power_fault_temp;
extern volatile float g_power_fault_value;
extern volatile float g_power_fault_limit;

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


/* =========================================================
 * FULL-SCREEN FAULT SNAPSHOT
 * ========================================================= */

typedef struct
{
    ProtectFault_t fault;
    uint8_t power_fault_code;

    float vin;
    float vout;
    float current;
    float temp;
    float power;

    /*
     * Quantity that actually crossed the fault threshold.
     */
    float trip_value;
    float trip_limit;

    uint8_t valid;
} FaultSnapshot_t;

static FaultSnapshot_t fault_snapshot =
{
    .fault = PROTECT_FAULT_NONE,
    .power_fault_code = POWER_FAULT_NONE,
    .vin = 0.0f,
    .vout = 0.0f,
    .current = 0.0f,
    .temp = 0.0f,
    .power = 0.0f,
    .trip_value = 0.0f,
    .trip_limit = 0.0f,
    .valid = 0U
};

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

static void fault_capture_snapshot(ProtectFault_t fault);
static void draw_fault_screen(void);
static void enter_fault_screen(void);
static void fault_reset_and_restart(void);


/* =========================================================
 * BUZZER
 * ========================================================= */

/*
 * =========================================================
 * FAULT BUZZER CODES
 *
 * All beeps are 150 ms ON / 150 ms OFF:
 *
 *   1 beep = OVP
 *   2 beep = OCP
 *   3 beep = OPP
 *   4 beep = HARD CURRENT (main.c)
 *   5 beep = POWER-STAGE TEMPERATURE
 *   6 beep = VIN LOW / VIN COLLAPSE while running
 *   7 beep = UNKNOWN EXTERNAL FAULT
 *
 * Fault beeps ignore the normal BUZZER menu setting.
 * Completely non-blocking.
 * ========================================================= */
#define BUZZER_BEEP_MS                  150U
#define BUZZER_GAP_MS                   150U

#define BUZZER_FAULT_OVP_COUNT            1U
#define BUZZER_FAULT_OCP_COUNT            2U
#define BUZZER_FAULT_OPP_COUNT            3U
#define BUZZER_FAULT_HARD_CURRENT_COUNT   4U
#define BUZZER_FAULT_TEMP_COUNT           5U
#define BUZZER_FAULT_VIN_COUNT            6U
#define BUZZER_FAULT_UNKNOWN_COUNT        7U

static uint8_t buzzer_pattern_active = 0U;
static uint8_t buzzer_phase_on = 0U;
static uint8_t buzzer_beep_count = 0U;
static uint8_t buzzer_target_count = 1U;
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

static void buzzer_start(uint8_t beep_count)
{
    if(beep_count == 0U)
        return;

    buzzer_pattern_active = 1U;
    buzzer_phase_on = 1U;
    buzzer_beep_count = 0U;
    buzzer_target_count = beep_count;
    buzzer_tick = HAL_GetTick();

    buzzer_write(1U);
}

static void buzzer_button_start(void)
{
    if(buzzer_button_enable)
        buzzer_start(1U);
}

static void buzzer_fault_start(ProtectFault_t fault)
{
    uint8_t count =
        BUZZER_FAULT_UNKNOWN_COUNT;

    switch(fault)
    {
        case PROTECT_FAULT_OVP:
            count =
                BUZZER_FAULT_OVP_COUNT;
            break;

        case PROTECT_FAULT_OCP:
            count =
                BUZZER_FAULT_OCP_COUNT;
            break;

        case PROTECT_FAULT_OPP:
            count =
                BUZZER_FAULT_OPP_COUNT;
            break;

        case PROTECT_FAULT_EXTERNAL:
        {
            /*
             * Decode the more specific fault raised by main.c.
             */
            switch(g_power_fault_code)
            {
                case POWER_FAULT_HARD_CURRENT:
                    count =
                        BUZZER_FAULT_HARD_CURRENT_COUNT;
                    break;

                case POWER_FAULT_TEMP:
                    count =
                        BUZZER_FAULT_TEMP_COUNT;
                    break;

                case POWER_FAULT_VIN_LOW:
                    count =
                        BUZZER_FAULT_VIN_COUNT;
                    break;

                case POWER_FAULT_NONE:
                default:
                    count =
                        BUZZER_FAULT_UNKNOWN_COUNT;
                    break;
            }
        }
        break;

        case PROTECT_FAULT_NONE:
        default:
            count =
                BUZZER_FAULT_UNKNOWN_COUNT;
            break;
    }

    buzzer_start(
        count
    );
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

            if(buzzer_beep_count >= buzzer_target_count)
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


/*
 * Forward declaration.
 * ui_flash_save() below uses clampf_ui() before its full definition.
 */
static float clampf_ui(float x, float lo, float hi);

/*
 * Forward declaration required because ui_flash_autosave_task()
 * uses is_live_screen() before its full definition later.
 */
static uint8_t is_live_screen(void);

static void ui_flash_mark_dirty(void);
static void ui_flash_autosave_task(void);

/* =========================================================
 * COMPACT FLASH SETTINGS - PURE BUCK
 *
 * STM32F103C8T6 official Flash:
 *   0x08000000 .. 0x0800FFFF = 64 KB
 *
 * Reserve final 1 KB page:
 *   0x0800FC00 .. 0x0800FFFF
 *
 * Keil:
 *   IROM1 Start = 0x08000000
 *   IROM1 Size  = 0x0000FC00
 *
 * This implementation intentionally supports ONLY the current
 * settings format. Old V1..V6 migration code has been removed
 * to save program Flash.
 *
 * IMPORTANT:
 * Flash erase/program is never performed while POWER is ON.
 * ========================================================= */

#define UI_FLASH_PAGE_ADDR              0x0800FC00U
#define UI_FLASH_MAGIC                  0x42554250U   /* "BUBP" */
#define UI_FLASH_VERSION                0x00020001U

/*
 * Wait after the output becomes OFF / user stops editing before
 * doing a page erase. This lets the power stage settle first.
 */
#undef  UI_FLASH_AUTOSAVE_DELAY_MS
#define UI_FLASH_AUTOSAVE_DELAY_MS      500U


typedef struct
{
    uint32_t magic;
    uint32_t version;

    uint32_t vset_x100;
    uint32_t iset_x100;

    uint32_t ovp_x100;
    uint32_t ocp_x100;
    uint32_t opp_x10;

    uint32_t sleep_temp_x10;

    uint32_t buzzer_button_enable;
    uint32_t theme;
    uint32_t solder_tip;

    uint32_t start_mode;
    uint32_t response_mode;

    /*
     * OPEN LOOP is still useful in pure BUCK:
     * openloop_ratio is now direct BUCK duty.
     */
    uint32_t control_mode;
    uint32_t openloop_ratio_x100;

    uint32_t checksum;
} UIFlashData_t;


/* ---------------------------------------------------------
 * Small generic checksum.
 * checksum must remain the LAST uint32_t in UIFlashData_t.
 * --------------------------------------------------------- */
static uint32_t ui_flash_checksum(
    const UIFlashData_t *d)
{
    const uint32_t *p =
        (const uint32_t *)d;

    uint32_t x =
        0x5A5AA5A5U;

    uint32_t words =
        (uint32_t)(
            sizeof(UIFlashData_t) /
            sizeof(uint32_t)
        ) - 1U;

    for(uint32_t k = 0U;
        k < words;
        k++)
    {
        x ^= p[k];
    }

    return x;
}


/* ---------------------------------------------------------
 * Compare current Flash record with candidate.
 * Avoid erase/program if nothing changed.
 * --------------------------------------------------------- */
static uint8_t ui_flash_same(
    const UIFlashData_t *a,
    const UIFlashData_t *b)
{
    const uint32_t *pa =
        (const uint32_t *)a;

    const uint32_t *pb =
        (const uint32_t *)b;

    uint32_t words =
        (uint32_t)(
            sizeof(UIFlashData_t) /
            sizeof(uint32_t)
        );

    for(uint32_t k = 0U;
        k < words;
        k++)
    {
        if(pa[k] != pb[k])
        {
            return 0U;
        }
    }

    return 1U;
}


/* ---------------------------------------------------------
 * Load current settings format only.
 *
 * Invalid/old Flash simply falls back to defaults.
 * --------------------------------------------------------- */
static uint8_t ui_flash_read(
    float *vset,
    float *iset)
{
    flash_start_mode =
        BB_START_SOFT;

    flash_response_mode =
        BB_RESPONSE_NORMAL;

    flash_control_mode =
        BB_CONTROL_CLOSED;

    flash_openloop_ratio =
        0.50f;

    const UIFlashData_t *d =
        (const UIFlashData_t *)
        UI_FLASH_PAGE_ADDR;

    if(d->magic != UI_FLASH_MAGIC ||
       d->version != UI_FLASH_VERSION ||
       d->checksum != ui_flash_checksum(d))
    {
        return 0U;
    }

    float v =
        (float)d->vset_x100 /
        100.0f;

    float i =
        (float)d->iset_x100 /
        100.0f;

    /*
     * VSET/ISET are the critical fields.
     * Reject the record if they are impossible.
     */
    if(v < VSET_MIN ||
       v > VSET_MAX ||
       i < ISET_MIN ||
       i > ISET_MAX)
    {
        return 0U;
    }

    *vset = v;
    *iset = i;

    /*
     * Remaining settings are clamped/defaulted instead of having
     * a large legacy-validation tree.
     */
    protect_ovp =
        clampf_ui(
            (float)d->ovp_x100 / 100.0f,
            PROTECT_OVP_MIN,
            PROTECT_OVP_MAX
        );

    protect_ocp =
        clampf_ui(
            (float)d->ocp_x100 / 100.0f,
            PROTECT_OCP_MIN,
            PROTECT_OCP_MAX
        );

    protect_opp =
        clampf_ui(
            (float)d->opp_x10 / 10.0f,
            PROTECT_OPP_MIN,
            PROTECT_OPP_MAX
        );

    sleep_temp =
        clampf_ui(
            (float)d->sleep_temp_x10 / 10.0f,
            SLEEP_TEMP_MIN,
            SLEEP_TEMP_MAX
        );

    buzzer_button_enable =
        (d->buzzer_button_enable != 0U)
        ? 1U
        : 0U;

    ui_theme =
        (d->theme <= (uint32_t)UI_THEME_LIGHT)
        ? (UITheme_t)d->theme
        : UI_THEME_DARK;

    solder_tip =
        (d->solder_tip <= (uint32_t)SOLDER_TIP_C210)
        ? (SolderTip_t)d->solder_tip
        : SOLDER_TIP_C245;

    flash_start_mode =
        (d->start_mode <= (uint32_t)BB_START_HARD)
        ? (BBUI_StartMode_t)d->start_mode
        : BB_START_SOFT;

    flash_response_mode =
        (d->response_mode <= (uint32_t)BB_RESPONSE_SLOW)
        ? (BBUI_ResponseMode_t)d->response_mode
        : BB_RESPONSE_NORMAL;

    flash_control_mode =
        (d->control_mode <= (uint32_t)BB_CONTROL_OPEN)
        ? (BBUI_ControlMode_t)d->control_mode
        : BB_CONTROL_CLOSED;

    if((d->openloop_ratio_x100 >= 3U) &&
       (d->openloop_ratio_x100 <= 97U))
    {
        flash_openloop_ratio =
            (float)d->openloop_ratio_x100 /
            100.0f;
    }
    else
    {
        flash_openloop_ratio =
            0.50f;
    }

    return 1U;
}


/* ---------------------------------------------------------
 * Save one compact record.
 *
 * SAFETY:
 * Do not erase/program application Flash while converter is ON.
 * --------------------------------------------------------- */
static uint8_t ui_flash_save(
    float vset,
    float iset)
{
    if((ui != NULL) &&
       ((ui->enable != 0U) ||
        (ui->state != BBUI_STATE_OFF)))
    {
        /*
         * Defer until output is safely OFF.
         */
        ui_flash_dirty =
            1U;

        ui_flash_dirty_tick =
            HAL_GetTick();

        return 0U;
    }

    UIFlashData_t n = {0};

    n.magic =
        UI_FLASH_MAGIC;

    n.version =
        UI_FLASH_VERSION;

    n.vset_x100 =
        (uint32_t)(
            vset * 100.0f +
            0.5f
        );

    n.iset_x100 =
        (uint32_t)(
            iset * 100.0f +
            0.5f
        );

    n.ovp_x100 =
        (uint32_t)(
            protect_ovp * 100.0f +
            0.5f
        );

    n.ocp_x100 =
        (uint32_t)(
            protect_ocp * 100.0f +
            0.5f
        );

    n.opp_x10 =
        (uint32_t)(
            protect_opp * 10.0f +
            0.5f
        );

    n.sleep_temp_x10 =
        (uint32_t)(
            sleep_temp * 10.0f +
            0.5f
        );

    n.buzzer_button_enable =
        (uint32_t)
        buzzer_button_enable;

    n.theme =
        (uint32_t)
        ui_theme;

    n.solder_tip =
        (uint32_t)
        solder_tip;

    n.start_mode =
        (ui != NULL)
        ? (uint32_t)ui->start_mode
        : (uint32_t)BB_START_SOFT;

    n.response_mode =
        (ui != NULL)
        ? (uint32_t)ui->response_mode
        : (uint32_t)BB_RESPONSE_NORMAL;

    n.control_mode =
        (ui != NULL)
        ? (uint32_t)ui->control_mode
        : (uint32_t)BB_CONTROL_CLOSED;

    float ratio =
        (ui != NULL)
        ? ui->openloop_ratio
        : 0.50f;

    ratio =
        clampf_ui(
            ratio,
            0.03f,
            0.97f
        );

    n.openloop_ratio_x100 =
        (uint32_t)(
            ratio * 100.0f +
            0.5f
        );

    n.checksum =
        ui_flash_checksum(&n);

    const UIFlashData_t *old =
        (const UIFlashData_t *)
        UI_FLASH_PAGE_ADDR;

    if(ui_flash_same(
           old,
           &n) != 0U)
    {
        return 1U;
    }

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0U;

    erase.TypeErase =
        FLASH_TYPEERASE_PAGES;

    erase.PageAddress =
        UI_FLASH_PAGE_ADDR;

    erase.NbPages =
        1U;

    if(HAL_FLASHEx_Erase(
           &erase,
           &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 0U;
    }

    const uint16_t *src =
        (const uint16_t *)&n;

    uint32_t addr =
        UI_FLASH_PAGE_ADDR;

    uint32_t halfwords =
        (uint32_t)(
            sizeof(UIFlashData_t) /
            sizeof(uint16_t)
        );

    for(uint32_t k = 0U;
        k < halfwords;
        k++, addr += 2U)
    {
        if(HAL_FLASH_Program(
               FLASH_TYPEPROGRAM_HALFWORD,
               addr,
               src[k]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 0U;
        }
    }

    HAL_FLASH_Lock();

    /*
     * Verify from Flash after programming.
     */
    const UIFlashData_t *verify =
        (const UIFlashData_t *)
        UI_FLASH_PAGE_ADDR;

    if(verify->magic != UI_FLASH_MAGIC ||
       verify->version != UI_FLASH_VERSION ||
       verify->checksum !=
           ui_flash_checksum(verify))
    {
        return 0U;
    }

    return 1U;
}


/* =========================================================
 * DEFERRED FLASH SAVE
 * ========================================================= */

static void ui_flash_mark_dirty(void)
{
    ui_flash_dirty =
        1U;

    ui_flash_dirty_tick =
        HAL_GetTick();
}


static void ui_flash_autosave_task(void)
{
    if((ui == NULL) ||
       (ui_flash_dirty == 0U))
    {
        return;
    }

    /*
     * Never store temporary C245/C210 supply setpoints.
     */
    if(!is_live_screen())
    {
        return;
    }

    /*
     * Never stall the CC/CV loop with a Flash page erase.
     */
    if((ui->enable != 0U) ||
       (ui->state != BBUI_STATE_OFF))
    {
        return;
    }

    uint32_t now =
        HAL_GetTick();

    if((uint32_t)(
           now -
           ui_flash_dirty_tick
       ) <
       UI_FLASH_AUTOSAVE_DELAY_MS)
    {
        return;
    }

    ui_flash_last_save_ok =
        ui_flash_save(
            ui->vset,
            ui->iset
        );

    if(ui_flash_last_save_ok != 0U)
    {
        ui_flash_dirty =
            0U;
    }
}


/* =========================================================
 * DISPLAY / UPDATE
 * ========================================================= */

#define MEAS_FILTER_MS                 80U
#define MEAS_REFRESH_MS                100U
#define DISP_ALPHA                     0.30f

#define EDIT_ACTIVE_MS                 2000U
#define BLINK_PERIOD_MS                220U

#define C_BG                           ((ui_theme == UI_THEME_LIGHT) ? ST7789_COLOR_WHITE : ST7789_COLOR_BLACK)
#define C_WHITE                        ((ui_theme == UI_THEME_LIGHT) ? ST7789_COLOR_BLACK : ST7789_COLOR_WHITE)
#define C_MUTED                        ((ui_theme == UI_THEME_LIGHT) ? ST7789_Color_GetFromRGB(75, 75, 75) : ST7789_COLOR_LIGHTGREY)
#define C_GRID                         ((ui_theme == UI_THEME_LIGHT) ? ST7789_Color_GetFromRGB(205, 205, 205) : ST7789_Color_GetFromRGB(45, 50, 58))

#define C_VOLT                         ((ui_theme == UI_THEME_LIGHT) ? ST7789_COLOR_BLUE : ST7789_COLOR_CYAN)
#define C_CURR                         ((ui_theme == UI_THEME_LIGHT) ? ST7789_Color_GetFromRGB(175, 115, 0) : ST7789_COLOR_YELLOW)
#define C_POWER                        ((ui_theme == UI_THEME_LIGHT) ? ST7789_COLOR_RED : ST7789_COLOR_ORANGE)

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

/*
 * Forward declaration:
 * fault_reset_and_restart() calls this function before its
 * full definition later in the file.
 *
 * Must be declared AFTER UIMode_t is defined.
 */
static void enter_live_mode(UIMode_t mode);

typedef enum
{
    SCREEN_NUMBER = 0,
    SCREEN_GRAPH,
    SCREEN_MENU,
    SCREEN_SOLDER,
    SCREEN_FAULT
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
    MENU_ITEM_LOOP,
    MENU_ITEM_START,
    MENU_ITEM_SPEED,
    MENU_ITEM_OVP,
    MENU_ITEM_OCP,
    MENU_ITEM_OPP,
    MENU_ITEM_SLEEP_TEMP,
    MENU_ITEM_BUZZER,
    MENU_ITEM_SOLDER_TIP,
    MENU_ITEM_THEME,
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

static BBUI_ControlMode_t
solder_backup_control_mode =
    BB_CONTROL_CLOSED;

static float
solder_backup_openloop_ratio =
    0.50f;

/*
 * PB11 holder state is intentionally NOT debounced continuously.
 * It is sampled only during a quiet heater-OFF window.
 */
static GPIO_PinState sleep_raw_last = GPIO_PIN_SET;
static GPIO_PinState sleep_stable = GPIO_PIN_SET;

/* Start time of the current heater-OFF window. */
static uint32_t sleep_off_tick = 0U;
static uint8_t sleep_off_tracking = 0U;

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
#define GRAPH_VMAX                     50.0f
#define GRAPH_IMAX                     12.0f

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


static uint32_t response_meas_filter_ms(void)
{
    if(ui == NULL)
        return 80U;

    switch(ui->response_mode)
    {
        case BB_RESPONSE_FAST:   return 40U;
        case BB_RESPONSE_SLOW:   return 150U;
        case BB_RESPONSE_NORMAL:
        default:                 return 80U;
    }
}

static uint32_t response_meas_refresh_ms(void)
{
    if(ui == NULL)
        return 100U;

    switch(ui->response_mode)
    {
        case BB_RESPONSE_FAST:   return 50U;
        case BB_RESPONSE_SLOW:   return 200U;
        case BB_RESPONSE_NORMAL:
        default:                 return 100U;
    }
}

static uint32_t response_graph_sample_ms(void)
{
    if(ui == NULL)
        return 250U;

    switch(ui->response_mode)
    {
        case BB_RESPONSE_FAST:   return 100U;
        case BB_RESPONSE_SLOW:   return 400U;
        case BB_RESPONSE_NORMAL:
        default:                 return 250U;
    }
}

static float response_display_alpha(void)
{
    if(ui == NULL)
        return 0.30f;

    switch(ui->response_mode)
    {
        case BB_RESPONSE_FAST:   return 0.55f;
        case BB_RESPONSE_SLOW:   return 0.15f;
        case BB_RESPONSE_NORMAL:
        default:                 return 0.30f;
    }
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

    uint32_t filter_ms =
        response_meas_filter_ms();

    if((uint32_t)(now - filter_tick) < filter_ms)
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

    float alpha =
        response_display_alpha();

    disp_vout += alpha * (ui->vout - disp_vout);
    disp_iout += alpha * (ui->current - disp_iout);
    disp_vin  += alpha * (ui->vin - disp_vin);
    disp_temp += alpha * (ui->temp - disp_temp);
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

    if(ui != NULL &&
       ui->control_mode == BB_CONTROL_OPEN)
    {
        ST7789_PutString(
            4,
            210,
            "RATIO",
            2,
            C_VOLT,
            C_BG
        );
    }
    else
    {
        ST7789_PutString(
            4,
            210,
            "VSET",
            2,
            C_VOLT,
            C_BG
        );
    }

    ST7789_PutString(
        164,
        210,
        "ISET",
        2,
        C_CURR,
        C_BG
    );

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

    uint8_t ratio_field =
        (
            is_voltage &&
            ui->control_mode ==
            BB_CONTROL_OPEN
        )
        ? 1U
        : 0U;

    const float value =
        ratio_field
        ? ui->openloop_ratio
        : (
            is_voltage
            ? ui->vset
            : ui->iset
          );

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
        ratio_field
        ? 3U
        : selected_char_index(mode);

    char buf[16];

    if(ratio_field)
    {
        snprintf(
            buf,
            sizeof(buf),
            "%.2f",
            clampf_ui(
                value,
                0.03f,
                0.97f
            )
        );
    }
    else
    {
        snprintf(
            buf,
            sizeof(buf),
            "%05.2f",
            value
        );
    }

    /*
     * One character cell at scale 2 = about 12 x 16 px.
     *
     * Compare every displayed digit with what is currently on screen.
     * Only changed cells are transferred over SPI.
     */
    size_t value_len =
        strlen(buf);

    for(uint8_t i = 0U; i < 5U; i++)
    {
        char desired =
            (i < value_len)
            ? buf[i]
            : '\0';

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
        if(!ratio_field)
        {
            ST7789_PutString(
                (uint16_t)(base_x + 64U),
                value_y,
                is_voltage ? "V" : "A",
                2,
                label_color,
                C_BG
            );
        }

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
        is_voltage
        ? (
            (ui != NULL &&
             ui->control_mode == BB_CONTROL_OPEN)
            ? 70U
            : 55U
          )
        : 215U;

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

static void draw_line_fast(int x0, int y0, int x1, int y1, uint16_t color)
{
    int dx=(x1>x0)?(x1-x0):(x0-x1), sx=(x0<x1)?1:-1;
    int dy=(y1>y0)?(y0-y1):(y1-y0), sy=(y0<y1)?1:-1;
    int err=dx+dy;
    while(1)
    {
        if(x0>=0&&x0<320&&y0>=0&&y0<240)
        {
            ST7789_DrawPixel((uint16_t)x0,(uint16_t)y0,color);
            if(y0+1<240) ST7789_DrawPixel((uint16_t)x0,(uint16_t)(y0+1),color);
        }
        if(x0==x1&&y0==y1) break;
        int e2=2*err;
        if(e2>=dy){err+=dy;x0+=sx;}
        if(e2<=dx){err+=dx;y0+=sy;}
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

    if(ui != NULL &&
       ui->control_mode == BB_CONTROL_OPEN)
    {
        ST7789_PutString(
            4,
            210,
            "RATIO",
            2,
            C_VOLT,
            C_BG
        );
    }
    else
    {
        ST7789_PutString(
            4,
            210,
            "VSET",
            2,
            C_VOLT,
            C_BG
        );
    }

    ST7789_PutString(
        164,
        210,
        "ISET",
        2,
        C_CURR,
        C_BG
    );

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
        is_voltage
        ? (
            (ui != NULL &&
             ui->control_mode == BB_CONTROL_OPEN)
            ? 70U
            : 55U
          )
        : 215U;

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

    uint32_t graph_ms =
        response_graph_sample_ms();

    if((uint32_t)(now - graph_tick) < graph_ms)
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

static const char *theme_text(UITheme_t t){return (t==UI_THEME_LIGHT)?"LIGHT":"DARK";}
static const char *on_off_text(uint8_t on){return on?"ON":"OFF";}
static const char *solder_tip_text(SolderTip_t tip){return (tip==SOLDER_TIP_C210)?"C210":"C245";}

static const char *control_mode_text(BBUI_ControlMode_t mode)
{
    return (mode == BB_CONTROL_OPEN)
           ? "OPEN"
           : "CLOSED";
}

static const char *start_mode_text(BBUI_StartMode_t mode)
{
    return (mode == BB_START_HARD) ? "HARD" : "SOFT";
}

static const char *response_mode_text(BBUI_ResponseMode_t mode)
{
    switch(mode)
    {
        case BB_RESPONSE_FAST: return "FAST";
        case BB_RESPONSE_SLOW: return "SLOW";
        case BB_RESPONSE_NORMAL:
        default:               return "NORMAL";
    }
}

static void draw_menu_row(uint8_t row,const char *name,const char *value)
{
    uint16_t y=(uint16_t)(26U+row*17U);
    uint8_t sel=((uint8_t)menu_item==row)?1U:0U;
    uint16_t fg=sel?C_BG:C_WHITE, bg=sel?C_VOLT:C_BG;
    ST7789_DrawFilledRectangle(18,(uint16_t)(y-1U),284,17,bg);
    ST7789_PutString(30,y,name,2,fg,bg);
    ST7789_PutString(184,y,value,2,fg,bg);
}

static void draw_menu(void)
{
    ST7789_FillScreen(C_BG);

    ST7789_PutString(
        124,
        1,
        "MENU",
        2,
        C_WHITE,
        C_BG
    );

    ST7789_DrawFilledRectangle(
        12,
        21,
        296,
        1,
        C_GRID
    );

    char a[20];
    char b[20];
    char c[20];
    char d[20];

    snprintf(a,sizeof(a),"%.1fV",menu_ovp);
    snprintf(b,sizeof(b),"%.1fA",menu_ocp);
    snprintf(c,sizeof(c),"%.0fW",menu_opp);
    snprintf(d,sizeof(d),"%.0fC",menu_sleep_temp);

    draw_menu_row(
        MENU_ITEM_UI,
        "UI",
        ui_mode_text(menu_ui_mode)
    );

    draw_menu_row(
        MENU_ITEM_LOOP,
        "LOOP",
        control_mode_text(menu_control_mode)
    );

    draw_menu_row(
        MENU_ITEM_START,
        "START",
        start_mode_text(menu_start_mode)
    );

    draw_menu_row(
        MENU_ITEM_SPEED,
        "SPEED",
        response_mode_text(menu_response_mode)
    );

    draw_menu_row(
        MENU_ITEM_OVP,
        "OVP",
        a
    );

    draw_menu_row(
        MENU_ITEM_OCP,
        "OCP",
        b
    );

    draw_menu_row(
        MENU_ITEM_OPP,
        "OPP",
        c
    );

    draw_menu_row(
        MENU_ITEM_SLEEP_TEMP,
        "SLEEP",
        d
    );

    draw_menu_row(
        MENU_ITEM_BUZZER,
        "BUZZER",
        on_off_text(menu_buzzer_button_enable)
    );

    draw_menu_row(
        MENU_ITEM_SOLDER_TIP,
        "IRON",
        solder_tip_text(menu_solder_tip)
    );

    draw_menu_row(
        MENU_ITEM_THEME,
        "THEME",
        theme_text(menu_theme)
    );

    ST7789_DrawFilledRectangle(
        10,
        216,
        300,
        1,
        C_GRID
    );

    ST7789_PutString(
        10,
        222,
        "V:ITEM I:VALUE OUT:OK",
        1,
        C_MUTED,
        C_BG
    );
}

/* =========================================================
 * FULL-SCREEN FAULT MODE
 * ========================================================= */

static const char *fault_name(void)
{
    if(fault_snapshot.fault ==
       PROTECT_FAULT_OVP)
        return "OVP";

    if(fault_snapshot.fault ==
       PROTECT_FAULT_OCP)
        return "OCP";

    if(fault_snapshot.fault ==
       PROTECT_FAULT_OPP)
        return "OPP";

    if(fault_snapshot.fault ==
       PROTECT_FAULT_EXTERNAL)
    {
        switch(fault_snapshot.power_fault_code)
        {
            case POWER_FAULT_HARD_CURRENT:
                return "HARD CURRENT";

            case POWER_FAULT_TEMP:
                return "POWER TEMP";

            case POWER_FAULT_VIN_LOW:
                return "VIN LOW";

            default:
                return "EXTERNAL";
        }
    }

    return "UNKNOWN";
}


static void fault_capture_snapshot(ProtectFault_t fault)
{
    memset(
        &fault_snapshot,
        0,
        sizeof(fault_snapshot)
    );

    fault_snapshot.fault =
        fault;

    fault_snapshot.power_fault_code =
        g_power_fault_code;

    /*
     * Faults raised in main.c have a snapshot taken in the
     * 5-kHz ISR BEFORE MOE/output are disabled.
     */
    if((fault ==
        PROTECT_FAULT_EXTERNAL) &&
       (g_power_fault_code !=
        POWER_FAULT_NONE))
    {
        fault_snapshot.vin =
            g_power_fault_vin;

        fault_snapshot.vout =
            g_power_fault_vout;

        fault_snapshot.current =
            g_power_fault_current;

        fault_snapshot.temp =
            g_power_fault_temp;

        fault_snapshot.trip_value =
            g_power_fault_value;

        fault_snapshot.trip_limit =
            g_power_fault_limit;
    }
    else if(ui != NULL)
    {
        /*
         * UI OVP/OCP/OPP are captured immediately when the
         * debounced violation is committed.
         */
        fault_snapshot.vin =
            ui->vin;

        fault_snapshot.vout =
            ui->vout;

        fault_snapshot.current =
            ui->current;

        fault_snapshot.temp =
            ui->temp;

        switch(fault)
        {
            case PROTECT_FAULT_OVP:
                fault_snapshot.trip_value =
                    ui->vout;

                fault_snapshot.trip_limit =
                    protect_ovp;
                break;

            case PROTECT_FAULT_OCP:
                fault_snapshot.trip_value =
                    ui->current;

                fault_snapshot.trip_limit =
                    protect_ocp;
                break;

            case PROTECT_FAULT_OPP:
                fault_snapshot.trip_value =
                    ui->vout *
                    ui->current;

                fault_snapshot.trip_limit =
                    protect_opp;
                break;

            default:
                break;
        }
    }

    fault_snapshot.power =
        fault_snapshot.vout *
        fault_snapshot.current;

    fault_snapshot.valid =
        1U;
}


static void draw_fault_screen(void)
{
    char buf[64];

    /*
     * Fault screen deliberately ignores LIGHT/DARK theme.
     * Red background makes a latched protection state obvious.
     */
    ST7789_FillScreen(
        ST7789_COLOR_RED
    );

    ST7789_PutString(
        88,
        8,
        "FAULT",
        3,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );

    ST7789_DrawFilledRectangle(
        12,
        48,
        296,
        2,
        ST7789_COLOR_WHITE
    );

    ST7789_PutString(
        12,
        58,
        fault_name(),
        2,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );

    snprintf(
        buf,
        sizeof(buf),
        "VIN  %.2f V",
        fault_snapshot.vin
    );

    ST7789_PutString(
        12,
        92,
        buf,
        1,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );

    snprintf(
        buf,
        sizeof(buf),
        "VOUT %.2f V",
        fault_snapshot.vout
    );

    ST7789_PutString(
        166,
        92,
        buf,
        1,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );

    snprintf(
        buf,
        sizeof(buf),
        "IOUT %.2f A",
        fault_snapshot.current
    );

    ST7789_PutString(
        12,
        112,
        buf,
        1,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );

    snprintf(
        buf,
        sizeof(buf),
        "TEMP %.1f C",
        fault_snapshot.temp
    );

    ST7789_PutString(
        166,
        112,
        buf,
        1,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );

    snprintf(
        buf,
        sizeof(buf),
        "POWER %.1f W",
        fault_snapshot.power
    );

    ST7789_PutString(
        12,
        132,
        buf,
        1,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );

    /*
     * Cause-specific trip value / threshold.
     */
    if(fault_snapshot.fault ==
       PROTECT_FAULT_OVP)
    {
        snprintf(
            buf,
            sizeof(buf),
            "TRIP %.2fV > %.2fV",
            fault_snapshot.trip_value,
            fault_snapshot.trip_limit
        );
    }
    else if(fault_snapshot.fault ==
            PROTECT_FAULT_OCP)
    {
        snprintf(
            buf,
            sizeof(buf),
            "TRIP %.2fA > %.2fA",
            fault_snapshot.trip_value,
            fault_snapshot.trip_limit
        );
    }
    else if(fault_snapshot.fault ==
            PROTECT_FAULT_OPP)
    {
        snprintf(
            buf,
            sizeof(buf),
            "TRIP %.1fW > %.1fW",
            fault_snapshot.trip_value,
            fault_snapshot.trip_limit
        );
    }
    else if((fault_snapshot.fault ==
             PROTECT_FAULT_EXTERNAL) &&
            (fault_snapshot.power_fault_code ==
             POWER_FAULT_HARD_CURRENT))
    {
        snprintf(
            buf,
            sizeof(buf),
            "IFAST %.2fA > %.2fA",
            fault_snapshot.trip_value,
            fault_snapshot.trip_limit
        );
    }
    else if((fault_snapshot.fault ==
             PROTECT_FAULT_EXTERNAL) &&
            (fault_snapshot.power_fault_code ==
             POWER_FAULT_TEMP))
    {
        snprintf(
            buf,
            sizeof(buf),
            "TEMP %.1fC > %.1fC",
            fault_snapshot.trip_value,
            fault_snapshot.trip_limit
        );
    }
    else if((fault_snapshot.fault ==
             PROTECT_FAULT_EXTERNAL) &&
            (fault_snapshot.power_fault_code ==
             POWER_FAULT_VIN_LOW))
    {
        snprintf(
            buf,
            sizeof(buf),
            "VIN %.2fV < %.2fV",
            fault_snapshot.trip_value,
            fault_snapshot.trip_limit
        );
    }
    else
    {
        snprintf(
            buf,
            sizeof(buf),
            "FAULT CODE %u",
            (unsigned int)
            fault_snapshot.power_fault_code
        );
    }

    ST7789_PutString(
        12,
        158,
        buf,
        2,
        ST7789_COLOR_YELLOW,
        ST7789_COLOR_RED
    );

    ST7789_DrawFilledRectangle(
        12,
        194,
        296,
        2,
        ST7789_COLOR_WHITE
    );

    ST7789_PutString(
        34,
        207,
        "PB9: RESET + RESTART",
        1,
        ST7789_COLOR_WHITE,
        ST7789_COLOR_RED
    );
}


static void enter_fault_screen(void)
{
    screen =
        SCREEN_FAULT;

    draw_fault_screen();

    /*
     * Normal live-screen dirty flags are irrelevant while FAULT
     * owns the complete LCD.
     */
    vset_dirty =
        0U;

    iset_dirty =
        0U;

    status_dirty =
        0U;

    runtime_dirty =
        0U;
}


static void fault_reset_and_restart(void)
{
    if(ui == NULL)
        return;

    /*
     * Clear latched software/main fault state first.
     * This leaves output OFF.
     */
    clear_protection_fault();

#if SOLDER_ROUTE_ENABLE
    /*
     * A restart always returns to normal POWER mode.
     */
    HAL_GPIO_WritePin(
        SOLDER_ROUTE_PORT,
        SOLDER_ROUTE_PIN,
        GPIO_PIN_RESET
    );
#endif

    /*
     * Restart runtime/display history from zero.
     */
    runtime_elapsed_sec =
        0U;

    runtime_ms_remainder =
        0U;

    runtime_last_tick =
        HAL_GetTick();

    runtime_prev_enable =
        0U;

    runtime_dirty =
        1U;

    /*
     * Seed display filters from the latest measurements instead
     * of carrying old pre-fault filter history.
     */
    disp_vout =
        ui->vout;

    disp_iout =
        ui->current;

    disp_vin =
        ui->vin;

    disp_temp =
        ui->temp;

    filter_init =
        1U;

    /*
     * One PB9 press means:
     *
     *   clear fault
     *   -> main NUMBER screen
     *   -> output ON again
     *   -> main.c sees a fresh OFF->ON edge and resets CV/CC PI,
     *      input-droop state and soft-start exactly like a new run.
     */
    ui->enable =
        1U;

    ui->state =
        BBUI_STATE_CV;

    protect_prev_enable =
        0U;

    protection_arm();

    fault_snapshot.valid =
        0U;

    enter_live_mode(
        UI_MODE_NUMBER
    );

    status_dirty =
        1U;
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
    menu_sleep_temp = sleep_temp;
    menu_buzzer_button_enable = buzzer_button_enable;
    menu_theme = ui_theme;
    menu_solder_tip = solder_tip;

    if(ui != NULL)
    {
        menu_control_mode = ui->control_mode;
        menu_start_mode = ui->start_mode;
        menu_response_mode = ui->response_mode;
    }

    menu_item = MENU_ITEM_UI;

    screen = SCREEN_MENU;
    draw_menu();
}

/* =========================================================
 * SOLDER MODE CONTROL
 * ========================================================= */

static void solder_get_bb_setpoints(float *vset, float *iset)
{
    if(vset == NULL || iset == NULL)
        return;

    if(solder_tip == SOLDER_TIP_C210)
    {
        *vset = SOLDER_C210_BB_VSET;
        *iset = SOLDER_C210_BB_ISET;
    }
    else
    {
        *vset = SOLDER_C245_BB_VSET;
        *iset = SOLDER_C245_BB_ISET;
    }
}

static void enter_solder_mode(void)
{
    if(ui == NULL || screen == SCREEN_SOLDER)
        return;

    /*
     * Guarantee heater MOSFET is OFF throughout the complete
     * POWER -> SOLDER transition and capacitor precharge.
     */
    UI_Solider_Exit();

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

    solder_backup_control_mode =
        ui->control_mode;

    solder_backup_openloop_ratio =
        ui->openloop_ratio;

    /*
     * =====================================================
     * POWER -> SOLDER transition
     * =====================================================
     *
     * First turn the normal BB output OFF. TIM1 control IRQ
     * keeps running, so PowerStage_Stop() is executed almost
     * immediately by the power-stage service routine.
     */
    ui->enable =
        0U;

    ui->state =
        BBUI_STATE_OFF;

    /*
     * Dead time requested between POWER and SOLDER.
     *
     * This runs from the main UI task, not from an ISR.
     * TIM1/ADC interrupts continue running during HAL_Delay().
     */
    HAL_Delay(
        SOLDER_POWER_SWITCH_DELAY_MS
    );

#if SOLDER_ROUTE_ENABLE
    /*
     * Switch the physical route only while BB output is OFF.
     */
    HAL_GPIO_WritePin(
        SOLDER_ROUTE_PORT,
        SOLDER_ROUTE_PIN,
        GPIO_PIN_SET
    );
#endif

    /*
     * SOLDER always uses normal closed-loop regulation.
     */
    ui->control_mode =
        BB_CONTROL_CLOSED;

    /*
     * Handpiece dependent BB supply:
     *   C245 -> 18 V / 6 A
     *   C210 -> 12 V / 5 A
     */
    solder_get_bb_setpoints(
        &ui->vset,
        &ui->iset
    );

    protect_fault =
        PROTECT_FAULT_NONE;

    protect_fault_latched =
        0U;

    protection_arm();

    /*
     * Enable BB only AFTER the SOLDER route and setpoints are ready.
     */
    ui->enable =
        1U;

    ui->state =
        BBUI_STATE_CV;

    /*
     * =====================================================
     * SOLDER OUTPUT CAPACITOR PRECHARGE
     * =====================================================
     *
     * The heater is still OFF here because UI_Solider_Enter()
     * has not been called yet.
     *
     * TIM1/ADC interrupts continue running during HAL_Delay(),
     * therefore the BB CV loop can ramp Vout and charge Cout.
     */
    /*
     * Do not use one blocking HAL_Delay(1000) here.
     *
     * During precharge the TIM1 ISR can detect HARD CURRENT,
     * TEMP or VIN faults, while UI protection must also remain
     * alive to detect OVP/OCP/OPP.
     *
     * Run protection continuously while Cout is charging.
     */
    uint32_t precharge_start =
        HAL_GetTick();

    while((uint32_t)(
              HAL_GetTick() -
              precharge_start
          ) <
          SOLDER_CAP_PRECHARGE_DELAY_MS)
    {
        protection_task();
        buzzer_task();

        /*
         * A fault may originate either from UI protection
         * (OVP/OCP/OPP) or from main.c.
         *
         * Never start the heater after a failed precharge.
         */
        if((ui->state ==
            BBUI_STATE_FAULT) ||
           (ui->enable == 0U))
        {
#if SOLDER_ROUTE_ENABLE
            HAL_GPIO_WritePin(
                SOLDER_ROUTE_PORT,
                SOLDER_ROUTE_PIN,
                GPIO_PIN_RESET
            );
#endif

            status_dirty =
                1U;

            return;
        }

        HAL_Delay(1U);
    }

    /*
     * Final guard immediately before enabling the heater.
     */
    if((ui->state ==
        BBUI_STATE_FAULT) ||
       (ui->enable == 0U))
    {
#if SOLDER_ROUTE_ENABLE
        HAL_GPIO_WritePin(
            SOLDER_ROUTE_PORT,
            SOLDER_ROUTE_PIN,
            GPIO_PIN_RESET
        );
#endif
        return;
    }

    /*
     * Only after Cout/Vout has charged successfully do we start
     * the 45/5 or 5/45 heater burst controller.
     */
    UI_Solider_SetTheme(
        (ui_theme == UI_THEME_LIGHT)
        ? 1U
        : 0U
    );

    UI_Solider_Enter();

    screen = SCREEN_SOLDER;

    /*
     * Sleep detection now lives entirely inside UI_Solider.c.
     *
     * UI.c only passes the configured sleep temperature here.
     * UI_Solider_Enter() starts with sol_sleep = 0, therefore
     * this call updates sol_sleep_temp without detecting PB11.
     *
     * PB11 itself will be checked during the first quiet
     * heater-OFF sensing window, BEFORE the temperature ADC.
     */
    UI_Solider_SetSleep(
        UI_Solider_IsSleeping(),
        sleep_temp
    );

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

    ui->control_mode =
        solder_backup_control_mode;

    ui->openloop_ratio =
        solder_backup_openloop_ratio;

    if(ui->enable)
        ui->state = BBUI_STATE_CV;
    else
        ui->state = BBUI_STATE_OFF;

#if SOLDER_ROUTE_ENABLE
    /*
     * Return to normal POWER route.
     */
    HAL_GPIO_WritePin(SOLDER_ROUTE_PORT,
                      SOLDER_ROUTE_PIN,
                      GPIO_PIN_RESET);
#endif

    enter_live_mode(solder_return_ui_mode);
}

/* PB11 holder/sleep detector.
 *
 * Sleep detection has moved to UI_Solider.c so it occurs inside
 * the heater-OFF sensing window, immediately BEFORE ADC sampling.
 *
 * Keep this function as a no-op to avoid changing the surrounding
 * UI task structure.
 */
static void solder_sleep_task(void)
{
    /* intentionally empty */
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

    /*
     * Dedicated FAULT-screen PB9 behavior.
     *
     * No long-press SOLDER action is allowed while faulted.
     * One press+release clears the fault and starts a fresh POWER run.
     */
    if(screen ==
       SCREEN_FAULT)
    {
        if(pressed)
        {
            if(!outbtn_press_active)
            {
                outbtn_press_active =
                    1U;

                outbtn_long_handled =
                    0U;

                outbtn_press_tick =
                    now;
            }
        }
        else if(outbtn_press_active)
        {
            outbtn_press_active =
                0U;

            outbtn_long_handled =
                0U;

            fault_reset_and_restart();
        }

        return;
    }

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
                        uint8_t was_enabled =
                            ui->enable;

                        if(was_enabled == 0U)
                        {
                            /*
                             * Save while power stage is still OFF.
                             * This avoids Flash erase/program stalls
                             * after PWM has already started.
                             */
                            ui_flash_last_save_ok =
                                ui_flash_save(
                                    ui->vset,
                                    ui->iset
                                );

                            if(ui_flash_last_save_ok != 0U)
                            {
                                ui_flash_dirty =
                                    0U;
                            }

                            protect_fault =
                                PROTECT_FAULT_NONE;

                            protect_fault_latched =
                                0U;

                            protection_arm();

                            ui->enable =
                                1U;

                            if(ui->state ==
                               BBUI_STATE_OFF)
                            {
                                ui->state =
                                    BBUI_STATE_CV;
                            }
                        }
                        else
                        {
                            /*
                             * Stop first. Flash is saved later after
                             * the converter has been OFF for 500 ms.
                             */
                            ui->enable =
                                0U;

                            ui->state =
                                BBUI_STATE_OFF;

                            ui_flash_mark_dirty();
                        }

                        status_dirty = 1U;
                        buzzer_button_start();
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
                    sleep_temp = menu_sleep_temp;
                    buzzer_button_enable = menu_buzzer_button_enable;
                    ui_theme = menu_theme;
                    solder_tip = menu_solder_tip;

                    ui->control_mode =
                        menu_control_mode;

                    ui->start_mode =
                        menu_start_mode;

                    ui->response_mode =
                        menu_response_mode;

                    UI_Solider_SetTheme((ui_theme == UI_THEME_LIGHT) ? 1U : 0U);

                    ui_flash_last_save_ok =
                        ui_flash_save(
                            ui->vset,
                            ui->iset
                        );

                    if(ui_flash_last_save_ok != 0U)
                    {
                        ui_flash_dirty =
                            0U;
                    }
                    else
                    {
                        ui_flash_mark_dirty();
                    }

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

    /*
     * Clear the cause exported by main.c as part of the user's
     * fault acknowledgement.
     */
    g_power_fault_code =
        POWER_FAULT_NONE;

    protect_pending_fault = PROTECT_FAULT_NONE;
    protect_violation_tick = 0U;

    if(ui != NULL)
    {
        ui->enable = 0U;
        ui->state = BBUI_STATE_OFF;

        /*
         * Boot restore is not a user edit.
         */
        ui_flash_dirty =
            0U;
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
    /*
     * Fault must also leave the SOLDER power route.
     */
    HAL_GPIO_WritePin(
        SOLDER_ROUTE_PORT,
        SOLDER_ROUTE_PIN,
        GPIO_PIN_RESET
    );
#endif

    /*
     * Do not redraw NUMBER/GRAPH here.
     * The caller will immediately enter the dedicated FAULT screen.
     */
    ui_mode =
        solder_return_ui_mode;

    ui->enable = 0U;
    ui->state = BBUI_STATE_FAULT;
    status_dirty = 1U;
}

static void protection_trip(ProtectFault_t fault)
{
    if(ui == NULL)
        return;

    /*
     * Capture values before output collapse changes measurements.
     */
    fault_capture_snapshot(
        fault
    );

    protect_fault = fault;
    protect_fault_latched = 1U;

    ui->enable = 0U;
    ui->state = BBUI_STATE_FAULT;
    protect_prev_enable = 0U;

    protect_pending_fault = PROTECT_FAULT_NONE;
    protect_violation_tick = 0U;

    buzzer_fault_start(fault);

    if(screen == SCREEN_SOLDER)
        abort_solder_on_fault();

    enter_fault_screen();

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
        /*
         * main.c has already captured the exact pre-shutdown
         * values in g_power_fault_*.
         */
        fault_capture_snapshot(
            PROTECT_FAULT_EXTERNAL
        );

        protect_fault =
            PROTECT_FAULT_EXTERNAL;

        protect_fault_latched =
            1U;

        buzzer_fault_start(
            PROTECT_FAULT_EXTERNAL
        );

        if(screen ==
           SCREEN_SOLDER)
        {
            abort_solder_on_fault();
        }

        enter_fault_screen();

        status_dirty =
            1U;
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
                    if(ui->control_mode ==
                       BB_CONTROL_CLOSED)
                    {
                        v_digit =
                            digit_next(
                                v_digit
                            );

                        activate_v_edit();
                    }
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
                if(ui->control_mode ==
                   BB_CONTROL_OPEN)
                {
                    if(apply_encoder_delta(
                        &enc_v_acc,
                        delta,
                        &ui->openloop_ratio,
                        0.01f,
                        0.03f,
                        0.97f))
                    {
                        /*
                         * RATIO always has fixed step = 0.01.
                         */
                        activate_v_edit();

                        ui_flash_mark_dirty();
                    }
                }
                else
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

                        ui_flash_mark_dirty();
                    }
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

                    ui_flash_mark_dirty();
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
                    else if(menu_item == MENU_ITEM_LOOP)
                    {
                        menu_control_mode =
                            (menu_control_mode == BB_CONTROL_CLOSED)
                            ? BB_CONTROL_OPEN
                            : BB_CONTROL_CLOSED;
                    }
                    else if(menu_item == MENU_ITEM_START)
                    {
                        menu_start_mode =
                            (menu_start_mode == BB_START_SOFT)
                            ? BB_START_HARD
                            : BB_START_SOFT;
                    }
                    else if(menu_item == MENU_ITEM_SPEED)
                    {
                        int mode =
                            (int)menu_response_mode +
                            (int)dir;

                        if(mode < (int)BB_RESPONSE_FAST)
                            mode = (int)BB_RESPONSE_SLOW;

                        if(mode > (int)BB_RESPONSE_SLOW)
                            mode = (int)BB_RESPONSE_FAST;

                        menu_response_mode =
                            (BBUI_ResponseMode_t)mode;
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
                        menu_opp += (float)dir * PROTECT_OPP_STEP;
                        menu_opp = clampf_ui(menu_opp,PROTECT_OPP_MIN,PROTECT_OPP_MAX);
                    }
                    else if(menu_item == MENU_ITEM_SLEEP_TEMP)
                    {
                        menu_sleep_temp += (float)dir * SLEEP_TEMP_STEP;
                        menu_sleep_temp = clampf_ui(menu_sleep_temp,SLEEP_TEMP_MIN,SLEEP_TEMP_MAX);
                    }
                    else if(menu_item == MENU_ITEM_BUZZER)
                    {
                        menu_buzzer_button_enable ^= 1U;
                    }
                    else if(menu_item == MENU_ITEM_SOLDER_TIP)
                    {
                        menu_solder_tip =
                            (menu_solder_tip == SOLDER_TIP_C245)
                            ? SOLDER_TIP_C210
                            : SOLDER_TIP_C245;
                    }
                    else if(menu_item == MENU_ITEM_THEME)
                    {
                        menu_theme = (menu_theme == UI_THEME_DARK) ? UI_THEME_LIGHT : UI_THEME_DARK;
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
 * SOLDER BUZZER EVENTS
 * ========================================================= */

static void solder_buzzer_event_task(void)
{
    uint8_t events =
        UI_Solider_GetEvents();

    if(events ==
       UI_SOLDER_EVENT_NONE)
    {
        return;
    }

    /*
     * A normal SOLDER event must NEVER overwrite a fault pattern.
     *
     * This was the reason a real 4/5/6-beep main fault could sound
     * like only one beep immediately after entering SOLDER.
     */
    if((ui != NULL) &&
       ((ui->state ==
         BBUI_STATE_FAULT) ||
        (protect_fault_latched != 0U)))
    {
        return;
    }

    /*
     * BUZZER menu controls normal interaction/status beeps.
     * Fault patterns always sound regardless of this setting.
     */
    if(!buzzer_button_enable)
        return;

    if(events &
       (UI_SOLDER_EVENT_ENTER |
        UI_SOLDER_EVENT_SLEEP_ENTER |
        UI_SOLDER_EVENT_TARGET_REACHED))
    {
        buzzer_start(1U);
    }
}


/* =========================================================
 * STATUS LED PC13
 * ========================================================= */

static void status_led_write(uint8_t on)
{
    HAL_GPIO_WritePin(STATUS_LED_PORT,STATUS_LED_PIN,on?STATUS_LED_ACTIVE_STATE:STATUS_LED_IDLE_STATE);
}

static void status_led_task(void)
{
    uint8_t on=0U;
    if(screen==SCREEN_SOLDER) on=UI_Solider_IsSleeping()?0U:1U;
    else on=(ui!=NULL&&ui->enable!=0U&&ui->state!=BBUI_STATE_FAULT)?1U:0U;
    status_led_write(on);
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
        ui_flash_last_read_ok =
            ui_flash_read(
                &saved_vset,
                &saved_iset
            );

        if(ui_flash_last_read_ok != 0U)
        {
            ui->vset = saved_vset;
            ui->iset = saved_iset;

            ui->start_mode =
                flash_start_mode;

            ui->response_mode =
                flash_response_mode;

            ui->control_mode =
                flash_control_mode;

            ui->openloop_ratio =
                flash_openloop_ratio;
        }

        ui->vset =
            clampf_ui(ui->vset, VSET_MIN, VSET_MAX);

        ui->iset =
            clampf_ui(ui->iset, ISET_MIN, ISET_MAX);

        if(ui->start_mode != BB_START_SOFT &&
           ui->start_mode != BB_START_HARD)
        {
            ui->start_mode =
                BB_START_SOFT;
        }

        if(ui->response_mode < BB_RESPONSE_FAST ||
           ui->response_mode > BB_RESPONSE_SLOW)
        {
            ui->response_mode =
                BB_RESPONSE_NORMAL;
        }

        if(ui->control_mode != BB_CONTROL_CLOSED &&
           ui->control_mode != BB_CONTROL_OPEN)
        {
            ui->control_mode =
                BB_CONTROL_CLOSED;
        }

        ui->openloop_ratio =
            clampf_ui(
                ui->openloop_ratio,
                0.03f,
                0.97f
            );

        /*
         * Always start with output OFF for safety.
         * Only the setpoints are remembered.
         */
        ui->enable = 0U;
        ui->state = BBUI_STATE_OFF;
				/*
				 * Auto start Buck-Boost:
				 *     Vout = 10 V
				 *     Iout limit = 5 A
				 */
//				ui->vset = 10.0f;
//				ui->iset = 5.0f;

//				ui->enable = 1U;
//				ui->state = BBUI_STATE_CV;
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

    sleep_raw_last = HAL_GPIO_ReadPin(SOLDER_SLEEP_PORT,SOLDER_SLEEP_PIN);
    sleep_stable = sleep_raw_last;
    sleep_off_tick = HAL_GetTick();
    sleep_off_tracking = 0U;
    UI_Solider_SetTheme((ui_theme == UI_THEME_LIGHT) ? 1U : 0U);
    status_led_write(0U);

#if SOLDER_ROUTE_ENABLE
    /* Normal POWER route at startup. */
    HAL_GPIO_WritePin(SOLDER_ROUTE_PORT,
                      SOLDER_ROUTE_PIN,
                      GPIO_PIN_RESET);
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

    g_power_fault_code =
        POWER_FAULT_NONE;

    memset(
        &fault_snapshot,
        0,
        sizeof(fault_snapshot)
    );

    fault_snapshot.fault =
        PROTECT_FAULT_NONE;

    fault_snapshot.power_fault_code =
        POWER_FAULT_NONE;

    protect_enable_tick = HAL_GetTick();
    protect_prev_enable = 0U;

    runtime_prev_enable = 0U;
    runtime_last_tick = HAL_GetTick();
    runtime_elapsed_sec = 0U;
    runtime_ms_remainder = 0U;
    runtime_dirty = 1U;

    if(BOOT_DEFAULT_SOLDER) enter_solder_mode();
    else enter_live_mode(UI_MODE_NUMBER);
}

void BBUI_Task(void)
{
    uint32_t now = HAL_GetTick();

    filter_task();

    /* PB11 holder sensor controls SOLDER sleep/wake. */
    solder_sleep_task();

    encoder_task();
    button_task();

    /*
     * Deferred setpoint persistence.
     * Writes once after encoder activity has been idle for 1.2 s.
     */
    ui_flash_autosave_task();

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
    solder_buzzer_event_task();

    status_led_task();

    blink_task();

    if(screen == SCREEN_NUMBER)
    {
        if((uint32_t)(now - meas_tick) >=
           response_meas_refresh_ms())
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
    else if(screen == SCREEN_FAULT)
    {
        /*
         * Static snapshot screen.
         * Do not continuously redraw it; this avoids TFT flicker.
         */
    }
}

void BBUI_ForceRefresh(void)
{
    if(screen == SCREEN_SOLDER)
    {
        UI_Solider_Task(1U);
    }
    else if(screen == SCREEN_FAULT)
    {
        draw_fault_screen();
    }
    else
    {
        enter_live_mode(ui_mode);
    }
}

void BBUI_ButtonIRQ(void)
{
    /* Polling + debounce is used. */
}
