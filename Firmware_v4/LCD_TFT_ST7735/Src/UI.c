#include "UI.h"
#include "ST7735_SPI.h"
#include <stdio.h>
#include <string.h>
#include "UI_Solider.h"
#include "main.h"
#define UI_FONT_SMALL              Font_7x10
#define UI_FONT_BIG                Font_16x26
#define UI_FONT_MEDIUM             Font_11x18

#define UI_DIGIT_BLINK_MS					 600
#define UI_LCD_PERIOD_MS           60
#define UI_ENC_STEP                4

#define VSET_MIN                   1.0f
#define VSET_MAX                   30.0f
#define VSET_STEP                  0.1f

#define ISET_MIN                   0.1f
#define ISET_MAX                   10.0f
#define ISET_STEP                  0.1f

#define UI_FLASH_PAGE_ADDR         0x0800FC00U
#define UI_FLASH_MAGIC             0xBABA2026U
#define UI_FLASH_VERSION           3U

#define BTN_ACTIVE                 GPIO_PIN_RESET
#define BTN_DEBOUNCE_MS            35

#define BTN_VSET_PORT              GPIOB
#define BTN_VSET_PIN               GPIO_PIN_9

#define BTN_ISET_PORT              GPIOB
#define BTN_ISET_PIN               GPIO_PIN_8

#define BTN_OUT_PORT               GPIOB
#define BTN_OUT_PIN                GPIO_PIN_7

#define BTN_MODE_PORT              GPIOB
#define BTN_MODE_PIN               GPIO_PIN_6

#define BTN_ENC_PORT               GPIOB
#define BTN_ENC_PIN                GPIO_PIN_4

#define DISP_FILTER_MS             150

#define VOUT_DEADBAND              0.05f
#define IOUT_DEADBAND              0.05f
#define VIN_DEADBAND               0.08f
#define TEMP_DEADBAND              0.3f
#define POWER_DEADBAND             0.3f

#define DISP_ALPHA                 0.70f

#define SWITCH_PIN GPIO_PIN_15
#define SWITCH_PORT GPIOB

#define SOLDER_BB_VSET          24.0f
#define SOLDER_BB_ISET          7.0f

#define UI_ENCODER_LONG_PRESS_MS    1000U

#define MAX_INPUT_POWER_MIN         20.0f
#define MAX_INPUT_POWER_MAX         500.0f
#define MAX_INPUT_POWER_STEP        5.0f

/* Main-screen trend graph (portrait LCD: 128 x 160). */
#define GRAPH_X                     1U
#define GRAPH_Y                     15U
#define GRAPH_W                     126U
#define GRAPH_H                     89U
#define GRAPH_SAMPLE_MS             1U
#define GRAPH_POINTS                (GRAPH_W - 2U)
typedef enum
{
    UI_SCREEN_POWER = 0,
    UI_SCREEN_SOLDER,
		UI_SCREEN_SETTINGS
} UI_Screen_t;
typedef enum
{
    UI_SELECT_NONE = 0,
    UI_SELECT_VSET,
    UI_SELECT_ISET
} UI_Select_t;
typedef enum
{
    SETTINGS_ITEM_MAX_POWER = 0,
    SETTINGS_ITEM_START_MODE,
    SETTINGS_ITEM_MAIN_STYLE,
    SETTINGS_ITEM_COUNT
} UI_SettingsItem_t;
typedef enum
{
    UI_MAIN_NUMERIC = 0,
    UI_MAIN_GRAPH
} UI_MainStyle_t;
typedef struct
{
    uint32_t magic;
    uint32_t version;

    float vset;
    float iset;

    float max_input_power;
    uint32_t start_mode;
    uint32_t main_style;

    uint32_t checksum;
} UI_FlashData_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    float vset;
    float iset;
    float max_input_power;
    uint32_t start_mode;
    uint32_t checksum;
} UI_FlashDataV2_t;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t last_state;
    uint32_t last_tick;
} UI_Button_t;
static UI_Screen_t ui_screen = UI_SCREEN_POWER;
static BBUI_Data_t *ui = 0;
static TIM_HandleTypeDef *enc_tim = 0;
static UI_SettingsItem_t settings_item = SETTINGS_ITEM_MAX_POWER;
static uint8_t settings_editing = 0;
static UI_MainStyle_t main_style = UI_MAIN_GRAPH;

static UI_Screen_t settings_return_screen = UI_SCREEN_POWER;
static UI_Select_t ui_select = UI_SELECT_NONE;

static int16_t enc_last = 0;
static int32_t enc_acc = 0;

static uint8_t force_redraw = 1;
static uint8_t dirty = 1;
static uint32_t t_lcd = 0;

static uint8_t v_digit = 1;
static uint8_t i_digit = 1;
static uint8_t enc_button_down = 0;
static uint8_t enc_long_handled = 0;
static uint32_t enc_button_down_tick = 0;
static const float v_step_table[] =
{
    0.01f,
    0.10f,
    1.00f,
    10.0f
};

static const float i_step_table[] =
{
    0.01f,
    0.10f,
    1.00f
};

static const char *v_step_text[] =
{
    ".01",
    "0.1",
    "1",
    "10"
};

static const char *i_step_text[] =
{
    ".01",
    "0.1",
    "1"
};
static char c_digit[32] = "";
static char c_u[32] = "";
static char c_i[32] = "";
static char c_p[32] = "";
static char c_vin[32] = "";
static char c_iset[32] = "";
static char c_temp[32] = "";
static char c_out[32] = "";
static char c_set_tag[32] = "";
static char c_run_cv[32] = "";
static char c_run_cc[32] = "";
static char c_unit_p[32] = "";
static char c_save[32] = "";

static uint16_t graph_head = 0;
static uint32_t graph_last_sample = 0;
static int16_t graph_last_x = -1;
static int16_t graph_last_yv = -1;
static int16_t graph_last_yi = -1;
static uint8_t graph_has_last = 0;

static float disp_vout = 0.0f;
static float disp_iout = 0.0f;
static float disp_vin = 0.0f;
static float disp_temp = 0.0f;
static float disp_power = 0.0f;

static float iron_set_temp = 320.0f;
static float iron_temp = 25.0f;
static float iron_pwm = 0.0f;
static uint8_t iron_enable = 0;

static char c_iron_set[32] = "";
static char c_iron_temp[32] = "";
static char c_iron_pwm[32] = "";
static char c_iron_out[16] = "";

static float power_vset_backup = 12.0f;
static float power_iset_backup = 2.0f;
static uint8_t power_enable_backup = 0;
static uint8_t disp_init = 0;
static uint32_t t_disp_filter = 0;

static uint32_t save_msg_until = 0;

static UI_Button_t btn_vset =
{
    .port = BTN_VSET_PORT,
    .pin = BTN_VSET_PIN,
    .last_state = 1,
    .last_tick = 0
};

static UI_Button_t btn_iset =
{
    .port = BTN_ISET_PORT,
    .pin = BTN_ISET_PIN,
    .last_state = 1,
    .last_tick = 0
};

static UI_Button_t btn_out =
{
    .port = BTN_OUT_PORT,
    .pin = BTN_OUT_PIN,
    .last_state = 1,
    .last_tick = 0
};

static UI_Button_t btn_mode =
{
    .port = BTN_MODE_PORT,
    .pin = BTN_MODE_PIN,
    .last_state = 1,
    .last_tick = 0
};
static UI_Button_t btn_enc =
{
    .port = BTN_ENC_PORT,
    .pin = BTN_ENC_PIN,
    .last_state = 1,
    .last_tick = 0
};
static float clampf(float x, float min, float max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

static float absf_local(float x)
{
    return x < 0.0f ? -x : x;
}

static void ClearCache(void)
{
    c_u[0] = 0;
    c_i[0] = 0;
    c_p[0] = 0;
    c_vin[0] = 0;
    c_iset[0] = 0;
    c_temp[0] = 0;
    c_out[0] = 0;
    c_set_tag[0] = 0;
    c_run_cv[0] = 0;
    c_run_cc[0] = 0;
    c_unit_p[0] = 0;
    c_save[0] = 0;
		c_digit[0] = 0;
	
		c_iron_set[0] = 0;
		c_iron_temp[0] = 0;
		c_iron_pwm[0] = 0;
		c_iron_out[0] = 0;
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
            sprintf(buf, "-%ld", v);
        else if(dec == 1)
            sprintf(buf, "-%ld.%01ld", v / scale, v % scale);
        else
            sprintf(buf, "-%ld.%02ld", v / scale, v % scale);
    }
    else
    {
        if(dec == 0)
            sprintf(buf, "%02ld", v);
        else if(dec == 1)
            sprintf(buf, "%02ld.%01ld", v / scale, v % scale);
        else
            sprintf(buf, "%02ld.%02ld", v / scale, v % scale);
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

static float DisplayFollow(float shown, float real, float deadband)
{
    float err = real - shown;

    if(absf_local(err) < deadband)
        return shown;

    return shown + err * DISP_ALPHA;
}

static void DisplayFilterTask(void)
{
    if(ui == 0)
        return;

    if(disp_init == 0)
    {
        disp_init = 1;

        disp_vout = ui->vout;
        disp_iout = ui->current;
        disp_vin = ui->vin;
        disp_temp = ui->temp;
        disp_power = ui->vout * ui->current;

        return;
    }

    if(HAL_GetTick() - t_disp_filter < DISP_FILTER_MS)
        return;

    t_disp_filter = HAL_GetTick();

    disp_vout = DisplayFollow(disp_vout, ui->vout, VOUT_DEADBAND);
    disp_iout = DisplayFollow(disp_iout, ui->current, IOUT_DEADBAND);
    disp_vin = DisplayFollow(disp_vin, ui->vin, VIN_DEADBAND);
    disp_temp = DisplayFollow(disp_temp, ui->temp, TEMP_DEADBAND);

    disp_power = DisplayFollow(disp_power,
                               ui->vout * ui->current,
                               POWER_DEADBAND);
}

static uint32_t UI_Checksum32(const uint32_t *data, uint32_t words)
{
    uint32_t sum = 0x12345678U;

    for(uint32_t i = 0; i < words; i++)
    {
        sum ^= data[i] + 0x9E3779B9U + (sum << 6) + (sum >> 2);
    }

    return sum;
}

void BBUI_LoadFromFlash(void)
{
    if(ui == 0)
        return;

    UI_FlashData_t *fd = (UI_FlashData_t*)UI_FLASH_PAGE_ADDR;

    if(fd->magic != UI_FLASH_MAGIC)
        return;

    if(fd->version == 2U)
    {
        UI_FlashDataV2_t *old = (UI_FlashDataV2_t*)UI_FLASH_PAGE_ADDR;
        uint32_t old_words = (sizeof(UI_FlashDataV2_t) - sizeof(uint32_t)) / 4U;

        if(UI_Checksum32((uint32_t*)old, old_words) != old->checksum)
            return;

        ui->vset = clampf(old->vset, VSET_MIN, VSET_MAX);
        ui->iset = clampf(old->iset, ISET_MIN, ISET_MAX);
        ui->max_input_power = clampf(old->max_input_power,
                                     MAX_INPUT_POWER_MIN,
                                     MAX_INPUT_POWER_MAX);
        ui->start_mode = old->start_mode == BB_START_HARD
                       ? BB_START_HARD : BB_START_SOFT;
        main_style = UI_MAIN_GRAPH;
    }
    else
    {
        uint32_t words;
        uint32_t checksum;

        if(fd->version != UI_FLASH_VERSION)
            return;

        words = (sizeof(UI_FlashData_t) - sizeof(uint32_t)) / 4U;
        checksum = UI_Checksum32((uint32_t*)fd, words);
        if(checksum != fd->checksum)
            return;

        ui->vset = clampf(fd->vset, VSET_MIN, VSET_MAX);
        ui->iset = clampf(fd->iset, ISET_MIN, ISET_MAX);
        ui->max_input_power = clampf(fd->max_input_power,
                                     MAX_INPUT_POWER_MIN,
                                     MAX_INPUT_POWER_MAX);
        ui->start_mode = fd->start_mode == BB_START_HARD
                       ? BB_START_HARD : BB_START_SOFT;
        main_style = fd->main_style == UI_MAIN_NUMERIC
                   ? UI_MAIN_NUMERIC : UI_MAIN_GRAPH;
    }

    ui->enable = 0;
    ui->state = BBUI_STATE_OFF;

    ClearCache();
    dirty = 1;
}

void BBUI_SaveToFlash(void)
{
    if(ui == 0)
        return;

    UI_FlashData_t fd;

    fd.magic = UI_FLASH_MAGIC;
    fd.version = UI_FLASH_VERSION;
    fd.vset = ui->vset;
    fd.iset = ui->iset;
		fd.max_input_power = ui->max_input_power;
		fd.start_mode = ui->start_mode;
    fd.main_style = main_style;
    uint32_t words = (sizeof(UI_FlashData_t) - sizeof(uint32_t)) / 4;
    fd.checksum = UI_Checksum32((uint32_t*)&fd, words);

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase;
    uint32_t page_error = 0;

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = UI_FLASH_PAGE_ADDR;
    erase.NbPages = 1;

    HAL_FLASHEx_Erase(&erase, &page_error);

    uint16_t *p = (uint16_t*)&fd;

    for(uint32_t i = 0; i < sizeof(UI_FlashData_t) / 2; i++)
    {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                          UI_FLASH_PAGE_ADDR + i * 2,
                          p[i]);
    }

    HAL_FLASH_Lock();

    save_msg_until = HAL_GetTick() + 800;
    c_save[0] = 0;
    dirty = 1;
}

static void SetDefaultConfig(void)
{
    if(ui == 0)
        return;

    ui->vset = 12.0f;
    ui->iset = 2.0f;
    ui->enable = 0;
    ui->state = BBUI_STATE_OFF;
}

static void DrawMainGraphBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    ST7735_WriteString(0, 2, "U:", UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
    ST7735_WriteString(66, 2, "I:", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);

    drawRect(GRAPH_X, GRAPH_Y, GRAPH_W, GRAPH_H, ST7735_BLUE);

    drawHline(GRAPH_X + 1U,
              GRAPH_Y + (GRAPH_H - 1U) / 4U,
              GRAPH_W - 2U, ST7735_COLOR565(16, 28, 38));
    drawHline(GRAPH_X + 1U,
              GRAPH_Y + (GRAPH_H - 1U) / 2U,
              GRAPH_W - 2U, ST7735_COLOR565(16, 28, 38));
    drawHline(GRAPH_X + 1U,
              GRAPH_Y + 3U * (GRAPH_H - 1U) / 4U,
              GRAPH_W - 2U, ST7735_COLOR565(16, 28, 38));

    graph_head = 0;
    graph_last_sample = 0;
    graph_last_x = -1;
    graph_last_yv = -1;
    graph_last_yi = -1;
    graph_has_last = 0;

    ClearCache();
}

static void DrawMainNumericBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);
    ST7735_WriteString(110, 13, "U", UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
    ST7735_WriteString(110, 43, "I", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
    ST7735_WriteString(110, 73, "P", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
    ST7735_WriteString(0, 60, "SET", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
    ST7735_WriteString(0, 98, "Vin:", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(85, 98, "V", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(0, 118, "Ilim:", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(92, 118, "A", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(0, 138, "Temp:", UI_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(85, 138, "*C", UI_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    drawHline(25, 25, 78, ST7735_BLUE);
    drawHline(25, 55, 78, ST7735_BLUE);
    drawHline(25, 85, 78, ST7735_BLUE);
    ClearCache();
}

static void DrawMainBase(void)
{
    if(main_style == UI_MAIN_NUMERIC)
        DrawMainNumericBase();
    else
        DrawMainGraphBase();
}
static void DrawSolderBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    ST7735_WriteString(4, 2, "C245 SOLDER", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);

    drawHline(0, 15, 160, ST7735_BLUE);

    ST7735_WriteString(4, 25, "SET", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(4, 55, "TMP", UI_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(4, 85, "PWM", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);

    ST7735_WriteString(110, 25, "*C", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(110, 55, "*C", UI_FONT_SMALL, ST7735_RED, ST7735_BLACK);
    ST7735_WriteString(110, 85, "%",  UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);

    drawHline(0, 115, 160, ST7735_BLUE);

    ST7735_WriteString(4, 118, "PB6:POWER", UI_FONT_SMALL, ST7735_BLUE, ST7735_BLACK);

    ClearCache();
}

static void UpdateSolder(uint8_t force)
{
    char buf[32];

    FmtNumber(buf, iron_set_temp, 0);
    WriteCached(c_iron_set,
                38, 20, 70,
                buf,
                UI_FONT_BIG,
                ST7735_CYAN,
                ST7735_BLACK,
                force);

    FmtNumber(buf, iron_temp, 0);
    WriteCached(c_iron_temp,
                38, 50, 70,
                buf,
                UI_FONT_BIG,
                ST7735_RED,
                ST7735_BLACK,
                force);

    FmtNumber(buf, iron_pwm, 0);
    WriteCached(c_iron_pwm,
                38, 80, 70,
                buf,
                UI_FONT_BIG,
                ST7735_MAGENTA,
                ST7735_BLACK,
                force);

    if(iron_enable)
    {
        WriteCached(c_iron_out,
                    118, 118, 36,
                    "ON",
                    UI_FONT_SMALL,
                    ST7735_GREEN,
                    ST7735_BLACK,
                    force);
    }
    else
    {
        WriteCached(c_iron_out,
                    118, 118, 36,
                    "OFF",
                    UI_FONT_SMALL,
                    ST7735_RED,
                    ST7735_BLACK,
                    force);
    }
}
static void UpdateSelectTag(uint8_t force)
{
    if(ui_select == UI_SELECT_VSET)
    {
        WriteCached(c_set_tag,
                    0, 70, 18,
                    "CV",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_GREEN,
                    force);
    }
    else if(ui_select == UI_SELECT_ISET)
    {
        WriteCached(c_set_tag,
                    0, 70, 18,
                    "CC",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_YELLOW,
                    force);
    }
    else
    {
        WriteCached(c_set_tag,
                    0, 70, 18,
                    "  ",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_BLACK,
                    force);

        WriteCached(c_digit,
                    0, 80, 25,
                    "   ",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_BLACK,
                    force);
    }
}

static void UpdateRunModeTag(uint8_t force)
{
    if(ui == 0)
        return;

    if(ui->state == BBUI_STATE_CV)
    {
        WriteCached(c_run_cv,
                    110, 0, 18,
                    "CV",
                    UI_FONT_SMALL,
                    ST7735_WHITE,
                    ST7735_RED,
                    force);

        WriteCached(c_run_cc,
                    110, 30, 18,
                    "   ",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_BLACK,
                    force);
    }
    else if(ui->state == BBUI_STATE_CC)
    {
        WriteCached(c_run_cc,
                    110, 30, 18,
                    "CC",
                    UI_FONT_SMALL,
                    ST7735_WHITE,
                    ST7735_RED,
                    force);

        WriteCached(c_run_cv,
                    110, 0, 18,
                    "   ",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_BLACK,
                    force);
    }
    else
    {
        WriteCached(c_run_cv,
                    110, 0, 18,
                    "  ",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_BLACK,
                    force);

        WriteCached(c_run_cc,
                    110, 30, 18,
                    "  ",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_BLACK,
                    force);
    }
}
static uint8_t BlinkOn(void)
{
    return ((HAL_GetTick() / UI_DIGIT_BLINK_MS) & 1) ? 1 : 0;
}

static uint8_t GetVDigitIndex(void)
{
    /*
        Format: 12.34
        index : 01234

        v_digit:
        0 -> 0.01 -> index 4
        1 -> 0.10 -> index 3
        2 -> 1.00 -> index 1
        3 -> 10.0 -> index 0
    */

    switch(v_digit)
    {
        case 0: return 4;
        case 1: return 3;
        case 2: return 1;
        case 3: return 0;
        default: return 3;
    }
}

static uint8_t GetIDigitIndex(void)
{
    /*
        Format: 02.50
        index : 01234

        i_digit:
        0 -> 0.01 -> index 4
        1 -> 0.10 -> index 3
        2 -> 1.00 -> index 1
    */

    switch(i_digit)
    {
        case 0: return 4;
        case 1: return 3;
        case 2: return 1;
        default: return 3;
    }
}

static void ApplyBlinkDigit(char *buf, uint8_t digit_index)
{
    if(BlinkOn())
        return;

    if(digit_index >= strlen(buf))
        return;

    if(buf[digit_index] == '.')
        return;

    buf[digit_index] = ' ';
}

static uint8_t GraphScale(float value, float maximum)
{
    value = clampf(value, 0.0f, maximum);
    return (uint8_t)(value * (float)(GRAPH_H - 3U) / maximum + 0.5f);
}

static void GraphLine(int16_t x0, int16_t y0,
                      int16_t x1, int16_t y1,
                      uint16_t color)
{
    int16_t dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int16_t sx = x0 < x1 ? 1 : -1;
    int16_t dy_abs = y1 > y0 ? y1 - y0 : y0 - y1;
    int16_t dy = -dy_abs;
    int16_t sy = y0 < y1 ? 1 : -1;
    int16_t err = dx + dy;

    while(1)
    {
        ST7735_DrawPixel((uint16_t)x0, (uint16_t)y0, color);
        if(x0 == x1 && y0 == y1)
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

static void UpdateGraph(uint8_t force)
{
    uint32_t now = HAL_GetTick();
    uint16_t grid = ST7735_COLOR565(16, 28, 38);
    int16_t x;
    int16_t yv;
    int16_t yi;

    if(!force && (uint32_t)(now - graph_last_sample) < GRAPH_SAMPLE_MS)
        return;

    graph_last_sample = now;
    x = (int16_t)(GRAPH_X + 1U + graph_head);
    yv = (int16_t)(GRAPH_Y + GRAPH_H - 2U -
                   GraphScale(disp_vout, VSET_MAX));
    yi = (int16_t)(GRAPH_Y + GRAPH_H - 2U -
                   GraphScale(disp_iout, ISET_MAX));

    /* Clear only the write cursor and one look-ahead column. */
    for(uint8_t column = 0; column < 2U; column++)
    {
        int16_t clear_x = (int16_t)(x + column);

        if(clear_x <= (int16_t)(GRAPH_X + GRAPH_W - 2U))
        {
            ST7735_FillRectangle((uint16_t)clear_x, GRAPH_Y + 1U,
                                 1U, GRAPH_H - 2U, ST7735_BLACK);

            ST7735_DrawPixel((uint16_t)clear_x,
                             GRAPH_Y + (GRAPH_H - 1U) / 4U, grid);
            ST7735_DrawPixel((uint16_t)clear_x,
                             GRAPH_Y + (GRAPH_H - 1U) / 2U, grid);
            ST7735_DrawPixel((uint16_t)clear_x,
                             GRAPH_Y + 3U * (GRAPH_H - 1U) / 4U, grid);
        }
    }

    if(graph_has_last && x > graph_last_x)
    {
        GraphLine(graph_last_x, graph_last_yv, x, yv, ST7735_GREEN);
        GraphLine(graph_last_x, graph_last_yi, x, yi, ST7735_YELLOW);
    }
    else
    {
        ST7735_DrawPixel((uint16_t)x, (uint16_t)yv, ST7735_GREEN);
        ST7735_DrawPixel((uint16_t)x, (uint16_t)yi, ST7735_YELLOW);
    }

    graph_last_x = x;
    graph_last_yv = yv;
    graph_last_yi = yi;
    graph_has_last = 1;

    graph_head++;
    if(graph_head >= GRAPH_POINTS)
    {
        graph_head = 0;
        graph_has_last = 0;
    }
}

static void UpdateMainGraph(uint8_t force)
{
    char buf[32];

    if(ui == 0)
        return;

    DisplayFilterTask();

    FmtNumber(buf, disp_vout, 2);
    WriteCached(c_u,
                14, 2, 48,
                buf,
                UI_FONT_SMALL,
                ST7735_GREEN,
                ST7735_BLACK,
                force);

    FmtNumber(buf, disp_iout, 2);
    WriteCached(c_i,
                80, 2, 48,
                buf,
                UI_FONT_SMALL,
                ST7735_YELLOW,
                ST7735_BLACK,
                force);

    UpdateGraph(force);

    FmtNumber(buf, disp_power, disp_power < 100.0f ? 1 : 0);
    {
        char line[32];
        char vin[16];
        FmtNumber(vin, disp_vin, 1);
        sprintf(line, "P:%sW Vin:%sV", buf, vin);
        WriteCached(c_p, 0, 106, 128, line, UI_FONT_SMALL,
                    ST7735_CYAN, ST7735_BLACK, force);
    }

    {
        char line[32];
        char vset[16];
        char iset[16];
        FmtNumber(vset, ui->vset, 2);
        FmtNumber(iset, ui->iset, 2);
        if(ui_select == UI_SELECT_VSET)
            ApplyBlinkDigit(vset, GetVDigitIndex());
        if(ui_select == UI_SELECT_ISET)
            ApplyBlinkDigit(iset, GetIDigitIndex());
        sprintf(line, "Set:%sV %sA", vset, iset);
        WriteCached(c_iset, 0, 118, 128, line, UI_FONT_SMALL,
                    ST7735_MAGENTA, ST7735_BLACK, force);
    }

    {
        char line[32];
        char temp[16];
        FmtNumber(temp, disp_temp, 1);
        sprintf(line, "T:%sC OUT:%s", temp, ui->enable ? "ON" : "OFF");
        WriteCached(c_temp, 0, 130, 128, line, UI_FONT_SMALL,
                    ui->enable ? ST7735_GREEN : ST7735_RED,
                    ST7735_BLACK, force);
    }

    {
        char line[32];
        const char *mode = ui->state == BBUI_STATE_CV ? "CV" :
                           ui->state == BBUI_STATE_CC ? "CC" :
                           ui->state == BBUI_STATE_FAULT ? "FAULT" : "OFF";
        if(save_msg_until > HAL_GetTick())
            sprintf(line, "%s  SAVED", mode);
        else if(ui_select == UI_SELECT_VSET)
            sprintf(line, "%s  EDIT V %s", mode, v_step_text[v_digit]);
        else if(ui_select == UI_SELECT_ISET)
            sprintf(line, "%s  EDIT I %s", mode, i_step_text[i_digit]);
        else
            sprintf(line, "%s", mode);
        WriteCached(c_run_cv, 0, 144, 128, line, UI_FONT_SMALL,
                    ST7735_WHITE, ST7735_BLACK, force);
    }
}
static void UpdateMainNumeric(uint8_t force)
{
    char buf[32];

    if(ui == 0)
        return;

    DisplayFilterTask();

    FmtNumber(buf, disp_vout, 2);
    WriteCached(c_u, 25, 0, 78, buf, UI_FONT_BIG,
                ST7735_GREEN, ST7735_BLACK, force);
    FmtNumber(buf, disp_iout, 2);
    WriteCached(c_i, 25, 30, 78, buf, UI_FONT_BIG,
                ST7735_YELLOW, ST7735_BLACK, force);

    if(ui_select == UI_SELECT_VSET)
    {
        FmtNumber(buf, ui->vset, 2);
        ApplyBlinkDigit(buf, GetVDigitIndex());
        WriteCached(c_unit_p, 110, 73, 12, "V", UI_FONT_SMALL,
                    ST7735_MAGENTA, ST7735_BLACK, force);
    }
    else if(ui_select == UI_SELECT_ISET)
    {
        FmtNumber(buf, ui->iset, 2);
        ApplyBlinkDigit(buf, GetIDigitIndex());
        WriteCached(c_unit_p, 110, 73, 12, "I", UI_FONT_SMALL,
                    ST7735_MAGENTA, ST7735_BLACK, force);
    }
    else
    {
        FmtNumber(buf, disp_power, disp_power < 100.0f ? 2 : 1);
        WriteCached(c_unit_p, 110, 73, 12, "P", UI_FONT_SMALL,
                    ST7735_MAGENTA, ST7735_BLACK, force);
    }
    WriteCached(c_p, 25, 60, 78, buf, UI_FONT_BIG,
                ST7735_MAGENTA, ST7735_BLACK, force);

    WriteCached(c_out, 0, 40, 24, ui->enable ? "ON " : "OFF",
                UI_FONT_SMALL, ui->enable ? ST7735_GREEN : ST7735_RED,
                ST7735_BLACK, force);
    UpdateSelectTag(force);
    UpdateRunModeTag(force);

    FmtNumber(buf, disp_vin, 2);
    WriteCached(c_vin, 28, 91, 54, buf, UI_FONT_MEDIUM,
                ST7735_GREEN, ST7735_BLACK, force);
    FmtNumber(buf, ui->iset, 2);
    WriteCached(c_iset, 37, 111, 52, buf, UI_FONT_MEDIUM,
                ST7735_YELLOW, ST7735_BLACK, force);
    FmtNumber(buf, disp_temp, 1);
    WriteCached(c_temp, 37, 131, 46, buf, UI_FONT_MEDIUM,
                ST7735_RED, ST7735_BLACK, force);
    WriteCached(c_save, 98, 148, 30,
                save_msg_until > HAL_GetTick() ? "SAVE" : "    ",
                UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK, force);
}

static void UpdateMain(uint8_t force)
{
    if(main_style == UI_MAIN_NUMERIC)
        UpdateMainNumeric(force);
    else
        UpdateMainGraph(force);
}

static void DrawSettingsBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    ST7735_WriteString(31,
                       4,
                       "SETTINGS",
                       UI_FONT_MEDIUM,
                       ST7735_CYAN,
                       ST7735_BLACK);

    drawHline(0, 25, 127, ST7735_BLUE);

    ST7735_WriteString(12,
                       31,
                       "MAX INPUT POWER",
                       UI_FONT_SMALL,
                       ST7735_WHITE,
                       ST7735_BLACK);

    ST7735_WriteString(12,
                       72,
                       "START MODE",
                       UI_FONT_SMALL,
                       ST7735_WHITE,
                       ST7735_BLACK);

    ST7735_WriteString(12,
                       108,
                       "MAIN DISPLAY",
                       UI_FONT_SMALL,
                       ST7735_WHITE,
                       ST7735_BLACK);

    ST7735_WriteString(0,
                       149,
                       "HOLD:EXIT PUSH:SET",
                       UI_FONT_SMALL,
                       ST7735_BLUE,
                       ST7735_BLACK);
}
static void UpdateSettings(uint8_t force)
{
    char buf[32];

    uint16_t power_color =
        settings_item == SETTINGS_ITEM_MAX_POWER
        ? ST7735_RED
        : ST7735_WHITE;

    uint16_t start_color =
        settings_item == SETTINGS_ITEM_START_MODE
        ? ST7735_RED
        : ST7735_WHITE;

    uint16_t style_color =
        settings_item == SETTINGS_ITEM_MAIN_STYLE
        ? ST7735_RED
        : ST7735_WHITE;

    /*
     * X?a v? v? con tr?.
     */
    ST7735_FillRectangle(0, 28, 10, 118, ST7735_BLACK);

    if(settings_item == SETTINGS_ITEM_MAX_POWER)
        ST7735_WriteString(2, 47, ">", UI_FONT_SMALL,
                           ST7735_RED, ST7735_BLACK);
    else if(settings_item == SETTINGS_ITEM_START_MODE)
        ST7735_WriteString(2, 86, ">", UI_FONT_SMALL,
                           ST7735_RED, ST7735_BLACK);
    else
        ST7735_WriteString(2, 123, ">", UI_FONT_SMALL,
                           ST7735_RED, ST7735_BLACK);

    sprintf(buf, "%03ld W",
            (long)(ui->max_input_power + 0.5f));

    ST7735_FillRectangle(28, 43, 80, 20, ST7735_BLACK);

    ST7735_WriteString(28,
                       43,
                       buf,
                       UI_FONT_MEDIUM,
                       power_color,
                       ST7735_BLACK);

    ST7735_FillRectangle(35, 82, 65, 20, ST7735_BLACK);

    if(ui->start_mode == BB_START_SOFT)
    {
        ST7735_WriteString(35,
                           82,
                           "SOFT",
                           UI_FONT_MEDIUM,
                           start_color,
                           ST7735_BLACK);
    }
    else
    {
        ST7735_WriteString(35,
                           82,
                           "HARD",
                           UI_FONT_MEDIUM,
                           start_color,
                           ST7735_BLACK);
    }

    ST7735_FillRectangle(25, 119, 88, 20, ST7735_BLACK);
    ST7735_WriteString(25, 119,
                       main_style == UI_MAIN_GRAPH ? "GRAPH" : "NUMERIC",
                       UI_FONT_MEDIUM, style_color, ST7735_BLACK);

    /*
     * Khi dang ch?nh, th?m d?u ngo?c.
     */
    if(settings_editing)
    {
        if(settings_item == SETTINGS_ITEM_MAX_POWER)
        {
            ST7735_WriteString(18, 43, "[",
                               UI_FONT_MEDIUM,
                               ST7735_YELLOW,
                               ST7735_BLACK);

            ST7735_WriteString(105, 43, "]",
                               UI_FONT_MEDIUM,
                               ST7735_YELLOW,
                               ST7735_BLACK);
        }
        else if(settings_item == SETTINGS_ITEM_START_MODE)
        {
            ST7735_WriteString(25, 82, "[",
                               UI_FONT_MEDIUM,
                               ST7735_YELLOW,
                               ST7735_BLACK);

            ST7735_WriteString(85, 82, "]",
                               UI_FONT_MEDIUM,
                               ST7735_YELLOW,
                               ST7735_BLACK);
        }
        else
        {
            ST7735_WriteString(15, 119, "[", UI_FONT_MEDIUM,
                               ST7735_YELLOW, ST7735_BLACK);
            ST7735_WriteString(113, 119, "]", UI_FONT_MEDIUM,
                               ST7735_YELLOW, ST7735_BLACK);
        }
    }
}
static void EncoderButtonPress(void)
{
    if(ui_screen == UI_SCREEN_SETTINGS)
    {
        /*
         * Nh?n ng?n: v?o/tho?t ch? d? ch?nh m?c hi?n t?i.
         */
        settings_editing = !settings_editing;

        force_redraw = 1;
        dirty = 1;
        return;
    }

    if(UI_Solider_IsActive())
    {
        /*
         * Gi? logic hi?n t?i c?a Solder n?u c?.
         */
        return;
    }

    /*
     * Logic ch?nh ch? s? Vset/Iset hi?n t?i.
     */
    if(ui_select == UI_SELECT_VSET)
    {
        v_digit++;

        if(v_digit >= sizeof(v_step_table) / sizeof(v_step_table[0]))
            v_digit = 0;
    }
    else if(ui_select == UI_SELECT_ISET)
    {
        i_digit++;

        if(i_digit >= sizeof(i_step_table) / sizeof(i_step_table[0]))
            i_digit = 0;
    }

    dirty = 1;
}
static uint8_t ButtonPressedEvent(UI_Button_t *btn)
{
    uint8_t now_state;
    uint32_t now;

    now_state = HAL_GPIO_ReadPin(btn->port, btn->pin) == BTN_ACTIVE ? 0 : 1;
    now = HAL_GetTick();

    if(now_state != btn->last_state)
    {
        if(now - btn->last_tick < BTN_DEBOUNCE_MS)
            return 0;

        btn->last_tick = now;
        btn->last_state = now_state;

        if(now_state == 0)
            return 1;
    }

    return 0;
}

static void ButtonTask(void)
{
    if(ui == 0)
        return;

    if(ButtonPressedEvent(&btn_vset))
		{
				if(UI_Solider_IsActive())
				{
						UI_Solider_SelectPreset(0);
				}
				else
				{
						if(ui_select == UI_SELECT_VSET)
								ui_select = UI_SELECT_NONE;
						else
								ui_select = UI_SELECT_VSET;

						ClearCache();
						dirty = 1;
				}
		}

		if(ButtonPressedEvent(&btn_iset))
		{
				if(UI_Solider_IsActive())
				{
						UI_Solider_SelectPreset(1);
				}
				else
				{
						if(ui_select == UI_SELECT_ISET)
								ui_select = UI_SELECT_NONE;
						else
								ui_select = UI_SELECT_ISET;

						ClearCache();
						dirty = 1;
				}
		}

		if(ButtonPressedEvent(&btn_out))
		{
				if(UI_Solider_IsActive())
				{
						UI_Solider_SelectPreset(2);
				}
				else
				{
                        if(ui->enable == 0U)
                        {
                            /* Default relay path must be POWER before starting BB. */
                            HAL_GPIO_WritePin(SWITCH_PORT, SWITCH_PIN, GPIO_PIN_SET);
                        }

                        ui->enable = !ui->enable;

						if(ui->enable)
						{
								if(ui->state == BBUI_STATE_OFF)
										ui->state = BBUI_STATE_CV;
						}
						else
						{
								ui->state = BBUI_STATE_OFF;
						}


						ClearCache();
						dirty = 1;
				}
		}

//    if(ButtonPressedEvent(&btn_out))
//		{
//				if(ui_screen == UI_SCREEN_POWER)
//				{
//						ui->enable = !ui->enable;

//						if(ui->enable)
//						{
//								if(ui->state == BBUI_STATE_OFF)
//										ui->state = BBUI_STATE_CV;
//						}
//						else
//						{
//								ui->state = BBUI_STATE_OFF;
//						}

//						BBUI_SaveToFlash();

//						ClearCache();
//						dirty = 1;
//				}
//		}
			if(ButtonPressedEvent(&btn_mode))
			{
					/*
					 * Khi dang trong menu Settings, n?t MODE kh?ng d?i m?n h?nh.
					 * Gi? encoder d? tho?t Settings.
					 */
					if(ui_screen == UI_SCREEN_SETTINGS)
							return;

					/*
					 * =====================================================
					 * SOLDER -> POWER
					 * =====================================================
					 */
					if(ui_screen == UI_SCREEN_SOLDER)
					{
							UI_Solider_Exit();

							/*
							 * Kh?i ph?c c?u h?nh ngu?n tru?c khi v?o Solder.
							 */
							ui->vset = power_vset_backup;
							ui->iset = power_iset_backup;
							ui->enable = power_enable_backup;

							if(ui->enable)
							{
									ui->state = BBUI_STATE_CV;
							}
							else
							{
									ui->state = BBUI_STATE_OFF;
							}

							/*
							 * Chuy?n relay/m?ch c?ng su?t v? ch? d? ngu?n.
							 */
							HAL_GPIO_WritePin(SWITCH_PORT,
																SWITCH_PIN,
																GPIO_PIN_SET);

							ui_screen = UI_SCREEN_POWER;
					}

					/*
					 * =====================================================
					 * POWER -> SOLDER
					 * =====================================================
					 */
					else
					{
							/*
							 * Luu c?u h?nh ngu?n hi?n t?i d? kh?i ph?c
							 * khi tho?t ch? d? Solder.
							 */
							power_vset_backup = ui->vset;
							power_iset_backup = ui->iset;
							power_enable_backup = ui->enable;

							/*
							 * C?u h?nh buck-boost cho m? h?n:
							 * 24 V, gi?i h?n d?ng 7 A.
							 */
							ui->vset = 24.0f;
							ui->iset = 7.0f;
							ui->enable = 1;
							ui->state = BBUI_STATE_CV;

							/*
							 * Chuy?n relay/m?ch c?ng su?t sang m? h?n.
							 */
							HAL_GPIO_WritePin(SWITCH_PORT,
																SWITCH_PIN,
																GPIO_PIN_RESET);

							UI_Solider_Enter();

							ui_screen = UI_SCREEN_SOLDER;
					}

					ClearCache();

					force_redraw = 1;
					dirty = 1;
			}
//		if(ButtonPressedEvent(&btn_enc))
//		{
//				EncoderButtonPress();
//		}
}

static void EncoderAdjust(int8_t dir)
{
    if(ui == 0 || dir == 0)
        return;
		if(ui_screen == UI_SCREEN_SETTINGS)
    {
        if(settings_editing == 0)
        {
            int item = (int)settings_item + dir;

            if(item < 0)
                item = SETTINGS_ITEM_COUNT - 1;

            if(item >= SETTINGS_ITEM_COUNT)
                item = 0;

            settings_item = (UI_SettingsItem_t)item;
        }
        else
        {
            if(settings_item == SETTINGS_ITEM_MAX_POWER)
            {
                ui->max_input_power +=
                    dir * MAX_INPUT_POWER_STEP;

                ui->max_input_power =
                    clampf(ui->max_input_power,
                           MAX_INPUT_POWER_MIN,
                           MAX_INPUT_POWER_MAX);
            }
            else if(settings_item == SETTINGS_ITEM_START_MODE)
            {
                if(dir != 0)
                {
                    ui->start_mode =
                        ui->start_mode == BB_START_SOFT
                        ? BB_START_HARD
                        : BB_START_SOFT;
                }
            }
            else if(settings_item == SETTINGS_ITEM_MAIN_STYLE)
            {
                main_style = main_style == UI_MAIN_GRAPH
                           ? UI_MAIN_NUMERIC : UI_MAIN_GRAPH;
            }
        }

        dirty = 1;
        return;
    }
		if(UI_Solider_IsActive())
		{
				UI_Solider_EncoderAdjust(dir);
				return;
		}
    if(ui_select == UI_SELECT_VSET)
    {
        float step = v_step_table[v_digit];

        ui->vset += dir * step;
        ui->vset = clampf(ui->vset, VSET_MIN, VSET_MAX);

        c_p[0] = 0;
        c_set_tag[0] = 0;
        dirty = 1;
    }
    else if(ui_select == UI_SELECT_ISET)
    {
        float step = i_step_table[i_digit];

        ui->iset += dir * step;
        ui->iset = clampf(ui->iset, ISET_MIN, ISET_MAX);

        c_p[0] = 0;
        c_iset[0] = 0;
        c_set_tag[0] = 0;
        dirty = 1;
    }else if(ui_screen == UI_SCREEN_SOLDER)
		{
				iron_set_temp += dir * 5.0f;

				if(iron_set_temp < 150.0f)
						iron_set_temp = 150.0f;

				if(iron_set_temp > 450.0f)
						iron_set_temp = 450.0f;

				c_iron_set[0] = 0;
				dirty = 1;

				return;
		}
}
static void EncoderButtonTask(void)
{
    uint8_t pressed =
        HAL_GPIO_ReadPin(BTN_ENC_PORT, BTN_ENC_PIN) == BTN_ACTIVE;

    uint32_t now = HAL_GetTick();

    if(pressed)
    {
        if(enc_button_down == 0)
        {
            enc_button_down = 1;
            enc_long_handled = 0;
            enc_button_down_tick = now;
        }
        else if(enc_long_handled == 0 &&
                now - enc_button_down_tick >= UI_ENCODER_LONG_PRESS_MS)
        {
            enc_long_handled = 1;

            if(ui_screen == UI_SCREEN_SETTINGS)
            {
                /*
                 * Tho?t menu v? luu c?u h?nh.
                 */
                BBUI_SaveToFlash();

                ui_screen = settings_return_screen;

                if(ui_screen == UI_SCREEN_SOLDER)
                    UI_Solider_Enter();

                force_redraw = 1;
                dirty = 1;
            }
            else
            {
                /*
                 * Nh? m?n h?nh tru?c d?.
                 */
                settings_return_screen =
                    UI_Solider_IsActive()
                    ? UI_SCREEN_SOLDER
                    : UI_SCREEN_POWER;

                /*
                 * T?m tho?t UI Solder d? BBUI_Task
                 * kh?ng return tru?c khi v? Settings.
                 */
                if(UI_Solider_IsActive())
                    UI_Solider_Exit();

                ui_screen = UI_SCREEN_SETTINGS;
                settings_item = SETTINGS_ITEM_MAX_POWER;
                settings_editing = 0;

                force_redraw = 1;
                dirty = 1;
            }
        }
    }
    else
    {
        if(enc_button_down)
        {
            /*
             * Ch? x? l? nh?n ng?n n?u chua k?ch ho?t nh?n gi?.
             */
            if(enc_long_handled == 0)
            {
                EncoderButtonPress();
            }

            enc_button_down = 0;
            enc_long_handled = 0;
        }
    }
}
static void EncoderTask(void)
{
    if(enc_tim == 0)
        return;

    int16_t enc_now = (int16_t)__HAL_TIM_GET_COUNTER(enc_tim);
    int16_t diff = enc_now - enc_last;

    enc_last = enc_now;
    enc_acc += diff;

    while(enc_acc >= UI_ENC_STEP)
    {
        enc_acc -= UI_ENC_STEP;
        EncoderAdjust(+1);
    }

    while(enc_acc <= -UI_ENC_STEP)
    {
        enc_acc += UI_ENC_STEP;
        EncoderAdjust(-1);
    }
}

static void ApplyOutputState(void)
{
    if(ui == 0)
        return;

    if(ui->enable == 0)
    {
        ui->state = BBUI_STATE_OFF;
    }
    else
    {
        if(ui->state == BBUI_STATE_OFF)
            ui->state = BBUI_STATE_CV;
    }
}

void BBUI_Init(BBUI_Data_t *data, TIM_HandleTypeDef *htim_encoder)
{
    ui = data;
    enc_tim = htim_encoder;

    if(ui != 0)
    {
        if(ui->vset <= 0.0f || ui->iset <= 0.0f)
            SetDefaultConfig();

        ui->vset = clampf(ui->vset, VSET_MIN, VSET_MAX);
        ui->iset = clampf(ui->iset, ISET_MIN, ISET_MAX);

        ui->enable = 0;
        ui->state = BBUI_STATE_OFF;

        BBUI_LoadFromFlash();

        ui->enable = 0;
        ui->state = BBUI_STATE_OFF;
    }

    if(enc_tim != 0)
    {
        HAL_TIM_Encoder_Start(enc_tim, TIM_CHANNEL_ALL);
        enc_last = (int16_t)__HAL_TIM_GET_COUNTER(enc_tim);
    }

    ST7735_Init();

        ui_screen = UI_SCREEN_POWER;
        ui_select = UI_SELECT_NONE;

        /* At boot, connect the output to the normal BB path. */
        HAL_GPIO_WritePin(SWITCH_PORT, SWITCH_PIN, GPIO_PIN_SET);

		force_redraw = 1;
		dirty = 1;

		DrawMainBase();
		UI_Solider_Init();
		UpdateMain(1);
}

void BBUI_Task(void)
{
    uint32_t now = HAL_GetTick();

    /*
     * X? l? nh?n gi?/nh?n ng?n encoder.
     *
     * EncoderButtonTask() ph?i thay th? ph?n:
     * if(ButtonPressedEvent(&btn_enc))
     *     EncoderButtonPress();
     *
     * trong ButtonTask(), d? nh?n gi? kh?ng d?ng th?i
     * b? nh?n th?nh m?t l?n nh?n ng?n.
     */
    EncoderButtonTask();

    /*
     * X? l? c?c n?t VSET, ISET, OUT, MODE.
     */
    ButtonTask();

    /*
     * ??c encoder xoay.
     * EncoderAdjust() ph?i c? nh?nh x? l? SETTINGS.
     */
    EncoderTask();

    /*
     * ??ng b? tr?ng th?i ON/OFF c?a ngu?n.
     * Ngu?n v?n ti?p t?c ho?t d?ng khi dang m? menu Settings.
     */
    ApplyOutputState();

    /*
     * =====================================================
     * SCREEN: SETTINGS
     * =====================================================
     *
     * Ph?i ki?m tra SETTINGS tru?c SOLDER.
     * N?u kh?ng, m?n h?nh Solder c? th? chi?m quy?n c?p nh?t LCD.
     */
    if(ui_screen == UI_SCREEN_SETTINGS)
    {
        if(force_redraw)
        {
            force_redraw = 0;
            dirty = 0;
            t_lcd = now;

            ClearCache();
            DrawSettingsBase();
            UpdateSettings(1);

            return;
        }

        if(dirty ||
           (uint32_t)(now - t_lcd) >= UI_LCD_PERIOD_MS)
        {
            t_lcd = now;
            dirty = 0;

            UpdateSettings(0);
        }

        return;
    }

    /*
     * =====================================================
     * SCREEN: SOLDER
     * =====================================================
     */
    if(ui_screen == UI_SCREEN_SOLDER)
    {
        /*
         * B?o d?m module UI Solder dang active.
         * Th?ng thu?ng UI_Solider_Enter() d? du?c g?i
         * khi chuy?n m?n h?nh.
         */
        if(!UI_Solider_IsActive())
        {
            UI_Solider_Enter();
            force_redraw = 1;
        }

        UI_Solider_Task(force_redraw);

        if(force_redraw)
            force_redraw = 0;

        return;
    }

    /*
     * N?u dang ? Power nhung module Solder v?n active
     * do chuy?n tr?ng th?i chua d?ng b?, t?t n? t?i d?y.
     */
    if(UI_Solider_IsActive())
    {
        UI_Solider_Exit();
        force_redraw = 1;
    }

    /*
     * =====================================================
     * SCREEN: POWER
     * =====================================================
     */
    if(force_redraw)
    {
        force_redraw = 0;
        dirty = 0;
        t_lcd = now;

        ClearCache();
        DrawMainBase();
        UpdateMain(1);

        return;
    }

    if(dirty ||
       (uint32_t)(now - t_lcd) >= UI_LCD_PERIOD_MS)
    {
        t_lcd = now;
        dirty = 0;

        UpdateMain(0);
    }
}

void handleUI(void)
{
    BBUI_Task();
}

void BBUI_ButtonIRQ(void)
{
}

void BBUI_ForceRefresh(void)
{
    force_redraw = 1;
}
