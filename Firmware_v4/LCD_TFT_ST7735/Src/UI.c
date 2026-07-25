#include "UI.h"
#include "ST7735_SPI.h"
#include <stdio.h>
#include <string.h>
#include "UI_Solider.h"

#define UI_FONT_SMALL              Font_7x10
#define UI_FONT_BIG                Font_16x26
#define UI_FONT_MEDIUM             Font_11x18

#define UI_DIGIT_BLINK_MS					 400
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
#define UI_FLASH_VERSION           1U

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

#define DISP_FILTER_MS             200

#define VOUT_DEADBAND              0.05f
#define IOUT_DEADBAND              0.05f
#define VIN_DEADBAND               0.08f
#define TEMP_DEADBAND              0.3f
#define POWER_DEADBAND             0.3f

#define DISP_ALPHA                 0.25f
typedef enum
{
    UI_SCREEN_POWER = 0,
    UI_SCREEN_SOLDER
} UI_Screen_t;
typedef enum
{
    UI_SELECT_NONE = 0,
    UI_SELECT_VSET,
    UI_SELECT_ISET
} UI_Select_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;

    float vset;
    float iset;

    uint32_t checksum;
} UI_FlashData_t;

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

static UI_Select_t ui_select = UI_SELECT_NONE;

static int16_t enc_last = 0;
static int32_t enc_acc = 0;

static uint8_t force_redraw = 1;
static uint8_t dirty = 1;
static uint32_t t_lcd = 0;

static uint8_t v_digit = 1;
static uint8_t i_digit = 1;

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
static char c_digit[16] = "";
static char c_u[32] = "";
static char c_i[32] = "";
static char c_p[32] = "";
static char c_vin[32] = "";
static char c_iset[32] = "";
static char c_temp[32] = "";
static char c_out[16] = "";
static char c_set_tag[16] = "";
static char c_run_cv[16] = "";
static char c_run_cc[16] = "";
static char c_unit_p[16] = "";
static char c_save[16] = "";

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

    if(fd->version != UI_FLASH_VERSION)
        return;

    uint32_t words = (sizeof(UI_FlashData_t) - sizeof(uint32_t)) / 4;
    uint32_t checksum = UI_Checksum32((uint32_t*)fd, words);

    if(checksum != fd->checksum)
        return;

    ui->vset = clampf(fd->vset, VSET_MIN, VSET_MAX);
    ui->iset = clampf(fd->iset, ISET_MIN, ISET_MAX);

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

static void DrawMainBase(void)
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
static void UpdateMain(uint8_t force)
{
    char buf[32];

    if(ui == 0)
        return;

    DisplayFilterTask();

    FmtNumber(buf, disp_vout, 2);
    WriteCached(c_u,
                25, 0, 78,
                buf,
                UI_FONT_BIG,
                ST7735_GREEN,
                ST7735_BLACK,
                force);

    FmtNumber(buf, disp_iout, 2);
    WriteCached(c_i,
                25, 30, 78,
                buf,
                UI_FONT_BIG,
                ST7735_YELLOW,
                ST7735_BLACK,
                force);

		if(ui_select == UI_SELECT_VSET)
		{
				FmtNumber(buf, ui->vset, 2);
				ApplyBlinkDigit(buf, GetVDigitIndex());

				WriteCached(c_p,
										25, 60, 78,
										buf,
										UI_FONT_BIG,
										ST7735_MAGENTA,
										ST7735_BLACK,
										force);

				WriteCached(c_unit_p,
										110, 73, 12,
										"V",
										UI_FONT_SMALL,
										ST7735_MAGENTA,
										ST7735_BLACK,
										force);
		}
		else if(ui_select == UI_SELECT_ISET)
		{
				FmtNumber(buf, ui->iset, 2);
				ApplyBlinkDigit(buf, GetIDigitIndex());

				WriteCached(c_p,
										25, 60, 78,
										buf,
										UI_FONT_BIG,
										ST7735_MAGENTA,
										ST7735_BLACK,
										force);

				WriteCached(c_unit_p,
										110, 73, 12,
										"I",
										UI_FONT_SMALL,
										ST7735_MAGENTA,
										ST7735_BLACK,
										force);
		}
    else
    {
        if(disp_power < 100.0f)
            FmtNumber(buf, disp_power, 2);
        else
            FmtNumber(buf, disp_power, 1);

        WriteCached(c_p,
                    25, 60, 78,
                    buf,
                    UI_FONT_BIG,
                    ST7735_MAGENTA,
                    ST7735_BLACK,
                    force);

        WriteCached(c_unit_p,
                    110, 73, 12,
                    "P",
                    UI_FONT_SMALL,
                    ST7735_MAGENTA,
                    ST7735_BLACK,
                    force);
    }

    if(ui->enable)
    {
        WriteCached(c_out,
                    0, 40, 24,
                    "ON ",
                    UI_FONT_SMALL,
                    ST7735_GREEN,
                    ST7735_BLACK,
                    force);
    }
    else
    {
        WriteCached(c_out,
                    0, 40, 24,
                    "OFF",
                    UI_FONT_SMALL,
                    ST7735_RED,
                    ST7735_BLACK,
                    force);
    }

    UpdateSelectTag(force);
    UpdateRunModeTag(force);

    FmtNumber(buf, disp_vin, 2);
    WriteCached(c_vin,
                28, 91, 54,
                buf,
                UI_FONT_MEDIUM,
                ST7735_GREEN,
                ST7735_BLACK,
                force);

    FmtNumber(buf, ui->iset, 2);
    WriteCached(c_iset,
                37, 111, 52,
                buf,
                UI_FONT_MEDIUM,
                ST7735_YELLOW,
                ST7735_BLACK,
                force);

    FmtNumber(buf, disp_temp, 1);
    WriteCached(c_temp,
                37, 131, 46,
                buf,
                UI_FONT_MEDIUM,
                ST7735_RED,
                ST7735_BLACK,
                force);

    if(save_msg_until > HAL_GetTick())
    {
        WriteCached(c_save,
                    118, 138, 40,
                    "SAVE",
                    UI_FONT_SMALL,
                    ST7735_GREEN,
                    ST7735_BLACK,
                    force);
    }
    else
    {
        WriteCached(c_save,
                    118, 138, 40,
                    "    ",
                    UI_FONT_SMALL,
                    ST7735_BLACK,
                    ST7735_BLACK,
                    force);
    }
}
static void EncoderButtonPress(void)
{
    if(ui_select == UI_SELECT_VSET)
    {
        v_digit++;

        if(v_digit >= 4)
            v_digit = 0;

        c_p[0] = 0;
        c_set_tag[0] = 0;
        dirty = 1;
    }
    else if(ui_select == UI_SELECT_ISET)
    {
        i_digit++;

        if(i_digit >= 3)
            i_digit = 0;

        c_p[0] = 0;
        c_iset[0] = 0;
        c_set_tag[0] = 0;
        dirty = 1;
    }
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

						BBUI_SaveToFlash();

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
				if(UI_Solider_IsActive())
				{
						UI_Solider_Exit();
						force_redraw = 1;
						dirty = 1;
				}
				else
				{
						UI_Solider_Enter();
						force_redraw = 1;
						dirty = 1;
				}
		}
		if(ButtonPressedEvent(&btn_enc))
		{
				EncoderButtonPress();
		}
}

static void EncoderAdjust(int8_t dir)
{
    if(ui == 0 || dir == 0)
        return;
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

		force_redraw = 1;
		dirty = 1;

		DrawMainBase();
		UI_Solider_Init();
		UpdateMain(1);
}

void BBUI_Task(void)
{
    ButtonTask();
    EncoderTask();
		if(UI_Solider_IsActive())
		{
				UI_Solider_Task(force_redraw);

				if(force_redraw)
						force_redraw = 0;

				return;
		}
    ApplyOutputState();

    if(force_redraw)
		{
				force_redraw = 0;
				dirty = 0;

				if(ui_screen == UI_SCREEN_POWER)
				{
						DrawMainBase();
						UpdateMain(1);
				}
				else
				{
						DrawSolderBase();
						UpdateSolder(1);
				}

				return;
		}

    if(dirty || HAL_GetTick() - t_lcd >= UI_LCD_PERIOD_MS)
		{
				t_lcd = HAL_GetTick();
				dirty = 0;

				if(ui_screen == UI_SCREEN_POWER)
						UpdateMain(0);
				else
						UpdateSolder(0);
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