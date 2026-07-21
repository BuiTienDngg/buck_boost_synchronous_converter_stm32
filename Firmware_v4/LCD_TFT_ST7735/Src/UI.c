#include "UI.h"
#include "ST7735_SPI.h"
#include <stdio.h>
#include <string.h>

#define UI_FONT_SMALL              Font_7x10
#define UI_FONT_BIG                Font_16x26
#define UI_FONT_MEDIUM						 Font_11x18
#define UI_LCD_PERIOD_MS           120
#define UI_ENC_STEP                4

#define UI_USE_POLLING_BUTTON      1
#define UI_BTN_GPIO_Port           GPIOB
#define UI_BTN_Pin                 GPIO_PIN_4
#define UI_BTN_ACTIVE_LEVEL        GPIO_PIN_RESET
#define UI_BTN_DEBOUNCE_MS         40
#define UI_BTN_LONG_MS             700

#define VSET_MIN                   1.0f
#define VSET_MAX                   30.0f
#define VSET_STEP                  0.1f

#define ISET_MIN                   0.1f
#define ISET_MAX                   10.0f
#define ISET_STEP                  0.1f

#define UI_FLASH_PAGE_ADDR         0x0800FC00U
#define UI_FLASH_MAGIC             0xBABA2026U
#define UI_FLASH_VERSION           1U

typedef enum
{
    UI_PAGE_MAIN = 0,
    UI_PAGE_MENU,
    UI_PAGE_PRESET,
    UI_PAGE_BATTERY,
    UI_PAGE_MQTT,
    UI_PAGE_SYSTEM,
    UI_PAGE_EDIT_V,
    UI_PAGE_EDIT_I
} UI_Page_t;

typedef enum
{
    MAIN_VSET = 0,
    MAIN_ISET,
    MAIN_PRESET,
    MAIN_OUT,
    MAIN_COUNT
} UI_MainField_t;

typedef enum
{
    MENU_PRESET = 0,
    MENU_BATTERY,
    MENU_MQTT,
    MENU_SYSTEM,
    MENU_BACK,
    MENU_COUNT
} UI_MenuItem_t;

typedef enum
{
    PRESET_SAVE_M1 = 0,
    PRESET_SAVE_M2,
    PRESET_SAVE_M3,
    PRESET_BACK,
    PRESET_COUNT
} UI_PresetItem_t;

typedef enum
{
    BATT_1S = 0,
    BATT_2S,
    BATT_3S,
    BATT_4S,
    BATT_5S,
    BATT_6S,
    BATT_BACK,
    BATT_COUNT
} UI_BattItem_t;

typedef enum
{
    MQTT_ENABLE = 0,
    MQTT_BACK,
    MQTT_COUNT
} UI_MqttItem_t;

typedef enum
{
    SYSTEM_SAVE_FLASH = 0,
    SYSTEM_LOAD_FLASH,
    SYSTEM_DEFAULT,
    SYSTEM_BACK,
    SYSTEM_COUNT
} UI_SystemItem_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;

    float vset;
    float iset;

    uint8_t batt_cells;
    uint8_t active_preset;
    uint8_t mqtt_enable;
    uint8_t reserved0;

    BBUI_Preset_t preset[3];

    uint32_t checksum;
} UI_FlashData_t;

static BBUI_Data_t *ui = 0;
static TIM_HandleTypeDef *enc_tim = 0;

static UI_Page_t page = UI_PAGE_MAIN;
static UI_Page_t prev_page = UI_PAGE_MENU;

static UI_MainField_t main_field = MAIN_VSET;
static uint8_t menu_index = 0;

static int16_t enc_last = 0;
static int32_t enc_acc = 0;

static uint8_t force_redraw = 1;
static uint8_t dirty = 1;
static uint32_t t_lcd = 0;

static float edit_value = 0.0f;

static char c_state[32] = "";
static char c_u[32] = "";
static char c_i[32] = "";
static char c_p[32] = "";
static char c_vin[32] = "";
static char c_temp[32] = "";
static char c_set[32] = "";
static char c_lim[32] = "";
static char c_mem[32] = "";
static char c_out[32] = "";
static char c_hint[32] = "";

#if UI_USE_POLLING_BUTTON
static uint8_t btn_last = 1;
static uint8_t btn_pressed = 0;
static uint8_t btn_long_done = 0;
static uint32_t btn_down_tick = 0;
static uint32_t btn_last_change = 0;
#endif

static float clampf(float x, float min, float max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

static void ClearCache(void)
{
    c_state[0] = 0;
    c_u[0] = 0;
    c_i[0] = 0;
    c_p[0] = 0;
    c_vin[0] = 0;
    c_temp[0] = 0;
    c_set[0] = 0;
    c_lim[0] = 0;
    c_mem[0] = 0;
    c_out[0] = 0;
    c_hint[0] = 0;
}

static const char* StateText(BBUI_State_t s)
{
    switch(s)
    {
        case BBUI_STATE_OFF:   return "OFF";
        case BBUI_STATE_CV:    return "CV";
        case BBUI_STATE_CC:    return "CC";
        case BBUI_STATE_FAULT: return "FLT";
        default:               return "---";
    }
}

static uint16_t StateColor(BBUI_State_t s)
{
    switch(s)
    {
        case BBUI_STATE_OFF:   return ST7735_WHITE;
        case BBUI_STATE_CV:    return ST7735_GREEN;
        case BBUI_STATE_CC:    return ST7735_YELLOW;
        case BBUI_STATE_FAULT: return ST7735_RED;
        default:               return ST7735_WHITE;
    }
}
static void FmtNumber(char *buf, float value, uint8_t dec)
{
    int32_t scale = 1;

    for(uint8_t i = 0; i < dec; i++)
        scale *= 10;

    int32_t v = (int32_t)(value * scale + (value >= 0 ? 0.5f : -0.5f));

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
            sprintf(buf, "%ld", v);
        else if(dec == 1)
            sprintf(buf, "%ld.%01ld", v / scale, v % scale);
        else
            sprintf(buf, "%ld.%02ld", v / scale, v % scale);
    }
}
static void FmtFloat(char *buf, float value, uint8_t dec, const char *unit)
{
    int32_t scale = 1;

    for(uint8_t i = 0; i < dec; i++)
        scale *= 10;

    int32_t v = (int32_t)(value * scale + (value >= 0 ? 0.5f : -0.5f));

    if(v < 0)
    {
        v = -v;

        if(dec == 1)
            sprintf(buf, "-%ld.%01ld%s", v / scale, v % scale, unit);
        else
            sprintf(buf, "-%ld.%02ld%s", v / scale, v % scale, unit);
    }
    else
    {
        if(dec == 1)
            sprintf(buf, "%ld.%01ld%s", v / scale, v % scale, unit);
        else
            sprintf(buf, "%ld.%02ld%s", v / scale, v % scale, unit);
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
        ST7735_FillRectangle(x, y, w, font.height, bg);
        ST7735_WriteString(x, y, str, font, color, bg);

        strncpy(cache, str, 31);
        cache[31] = 0;
    }
}

static uint32_t UI_Checksum32(const uint32_t *data, uint32_t words)
{
    uint32_t sum = 0x12345678U;

    for(uint32_t i = 0; i < words; i++)
        sum ^= data[i] + 0x9E3779B9U + (sum << 6) + (sum >> 2);

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

    ui->vset = fd->vset;
    ui->iset = fd->iset;

    ui->batt_cells = fd->batt_cells;
    ui->active_preset = fd->active_preset;
    ui->mqtt_enable = fd->mqtt_enable;

    if(ui->batt_cells < 1 || ui->batt_cells > 6)
        ui->batt_cells = 3;

    if(ui->active_preset > 2)
        ui->active_preset = 0;

    ui->preset[0] = fd->preset[0];
    ui->preset[1] = fd->preset[1];
    ui->preset[2] = fd->preset[2];
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

    fd.batt_cells = ui->batt_cells;
    fd.active_preset = ui->active_preset;
    fd.mqtt_enable = ui->mqtt_enable;
    fd.reserved0 = 0;

    fd.preset[0] = ui->preset[0];
    fd.preset[1] = ui->preset[1];
    fd.preset[2] = ui->preset[2];

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
}

static void SetDefaultConfig(void)
{
    if(ui == 0)
        return;

    ui->vset = 12.0f;
    ui->iset = 2.0f;

    ui->batt_cells = 3;
    ui->active_preset = 0;
    ui->mqtt_enable = 0;

    ui->preset[0].vset = 12.0f;
    ui->preset[0].iset = 5.0f;

    ui->preset[1].vset = 16.8f;
    ui->preset[1].iset = 3.0f;

    ui->preset[2].vset = 20.0f;
    ui->preset[2].iset = 2.5f;
}

static void SavePreset(uint8_t id)
{
    if(ui == 0 || id > 2)
        return;

    ui->preset[id].vset = ui->vset;
    ui->preset[id].iset = ui->iset;
    ui->active_preset = id;
}

static void LoadPreset(uint8_t id)
{
    if(ui == 0 || id > 2)
        return;

    ui->active_preset = id;
    ui->vset = ui->preset[id].vset;
    ui->iset = ui->preset[id].iset;

    ClearCache();
    dirty = 1;
}

static void SelectBattery(uint8_t cells)
{
    if(ui == 0)
        return;

    if(cells < 1) cells = 1;
    if(cells > 6) cells = 6;

    ui->batt_cells = cells;
    ui->vset = 4.20f * (float)cells;

    ClearCache();
    dirty = 1;
}

static void ChangePage(UI_Page_t next)
{
    page = next;
    menu_index = 0;
    force_redraw = 1;
    dirty = 1;
}

static void DrawTitle(const char *title)
{
    ST7735_FillScreen(ST7735_BLACK);
    ST7735_WriteString(4, 2, title, UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
    drawHline(0, 15, 160, ST7735_BLUE);

    ClearCache();
}

static void DrawListCursor(uint8_t index, uint8_t count)
{
    ST7735_FillRectangle(2, 20, 12, 96, ST7735_BLACK);

    if(index >= count)
        return;

    ST7735_WriteString(4, 22 + index * 14, ">", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
}

static void DrawMainBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    ST7735_WriteString(4, 2, "BUCK-BOOST", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
    drawHline(0, 15, 160, ST7735_BLUE);

    //drawVline(98, 16, 99, ST7735_BLUE);

    ST7735_WriteString(0, 20, "U", UI_FONT_MEDIUM, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(0, 50, "I", UI_FONT_MEDIUM, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(0, 80, "P", UI_FONT_MEDIUM, ST7735_YELLOW, ST7735_BLACK);

    ST7735_WriteString(103 - 30, 20, "IN",  UI_FONT_SMALL, ST7735_BLUE, ST7735_BLACK);
		ST7735_WriteString(103 - 30, 34, "TMP", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
		ST7735_WriteString(103 - 30, 48, "SET", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
		ST7735_WriteString(103 - 30, 62, "LIM", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
		ST7735_WriteString(103 - 30, 76, "MEM", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
		ST7735_WriteString(103 - 30, 90, "OUT", UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);

    drawHline(0, 115, 160, ST7735_BLUE);
    ST7735_WriteString(4, 118, "BTN:SEL HOLD:MENU", UI_FONT_SMALL, ST7735_BLUE, ST7735_BLACK);

    ClearCache();
}

static void DrawMainCursor(void)
{
    ST7735_FillRectangle(94 - 30, 12, 8, 100, ST7735_BLACK);

    if(main_field == MAIN_VSET)
        ST7735_WriteString(95 - 30, 48, ">", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
    else if(main_field == MAIN_ISET)
        ST7735_WriteString(95 - 30, 62, ">", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
    else if(main_field == MAIN_PRESET)
        ST7735_WriteString(95 - 30, 76, ">", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
    else if(main_field == MAIN_OUT)
        ST7735_WriteString(95 - 30, 90, ">", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
}

static void UpdateMain(uint8_t force)
{
    char buf[32];

    if(ui == 0)
        return;

    DrawMainCursor();

    sprintf(buf, "%s", StateText(ui->state));
    WriteCached(c_state,
                100, 2, 28,
                buf,
                UI_FONT_SMALL,
                StateColor(ui->state),
                ST7735_BLACK,
                force);

    FmtNumber(buf, ui->vout, 2);
		WriteCached(c_u,
								10, 20, 50,
								buf,
								UI_FONT_MEDIUM,
								ST7735_CYAN,
								ST7735_BLACK,
								force);

		FmtNumber(buf, ui->current, 2);
		WriteCached(c_i,
								10, 50, 50,
								buf,
								UI_FONT_MEDIUM,
								ST7735_CYAN,
								ST7735_BLACK,
								force);

		float power = ui->vout * ui->current;

		if(power < 100.0f)
				FmtNumber(buf, power, 1);
		else
				FmtNumber(buf, power, 0);

		WriteCached(c_p,
								10, 80, 50,
								buf,
								UI_FONT_MEDIUM,
								ST7735_YELLOW,
								ST7735_BLACK,
								force);

    FmtNumber(buf, ui->vin, 1);
    WriteCached(c_vin,
                128 - 30, 20, 27,
                buf,
                UI_FONT_SMALL,
                ST7735_BLUE,
                ST7735_BLACK,
                force);

    FmtNumber(buf, ui->temp, 1);
    WriteCached(c_temp,
                128 - 30, 34, 27,
                buf,
                UI_FONT_SMALL,
                ui->temp > 70.0f ? ST7735_RED : ST7735_MAGENTA,
                ST7735_BLACK,
                force);

    FmtNumber(buf, ui->vset, 1);
    WriteCached(c_set,
                128 - 30, 48, 27,
                buf,
                UI_FONT_SMALL,
                main_field == MAIN_VSET ? ST7735_YELLOW : ST7735_WHITE,
                ST7735_BLACK,
                force || main_field == MAIN_VSET);

    FmtNumber(buf, ui->iset, 1);
    WriteCached(c_lim,
                128 - 30, 62, 27,
                buf,
                UI_FONT_SMALL,
                main_field == MAIN_ISET ? ST7735_YELLOW : ST7735_WHITE,
                ST7735_BLACK,
                force || main_field == MAIN_ISET);

    sprintf(buf, "M%d", ui->active_preset + 1);
    WriteCached(c_mem,
                128 - 30, 76, 27,
                buf,
                UI_FONT_SMALL,
                main_field == MAIN_PRESET ? ST7735_YELLOW : ST7735_MAGENTA,
                ST7735_BLACK,
                force || main_field == MAIN_PRESET);

    sprintf(buf, "%s", ui->enable ? "ON" : "OFF");
    WriteCached(c_out,
                128 - 30, 90, 27,
                buf,
                UI_FONT_SMALL,
                ui->enable ? ST7735_GREEN : ST7735_RED,
                ST7735_BLACK,
                force || main_field == MAIN_OUT);
}

static void DrawMenuBase(void)
{
    DrawTitle("MAIN MENU");

    ST7735_WriteString(18, 22, "SAVE PRESET", UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 36, "BATTERY",     UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 50, "MQTT",        UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 64, "SYSTEM",      UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 78, "BACK",        UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
}

static void UpdateMenu(uint8_t force)
{
    (void)force;
    DrawListCursor(menu_index, MENU_COUNT);
}

static void DrawPresetBase(void)
{
    DrawTitle("SAVE PRESET");

    ST7735_WriteString(18, 28, "SAVE M1", UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
    ST7735_WriteString(18, 46, "SAVE M2", UI_FONT_SMALL, ST7735_BLUE, ST7735_BLACK);
    ST7735_WriteString(18, 64, "SAVE M3", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
    ST7735_WriteString(18, 92, "BACK",    UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
}

static void UpdatePreset(uint8_t force)
{
    (void)force;
    DrawListCursor(menu_index, PRESET_COUNT);
}

static void DrawBatteryBase(void)
{
    DrawTitle("BATTERY CHARGE");

    ST7735_WriteString(18, 20, "1S  4.20V",  UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 34, "2S  8.40V",  UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 48, "3S 12.60V",  UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 62, "4S 16.80V",  UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 76, "5S 21.00V",  UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 90, "6S 25.20V",  UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 108, "BACK",      UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
}

static void UpdateBattery(uint8_t force)
{
    (void)force;

    DrawListCursor(menu_index, BATT_COUNT);

    char buf[12];
    sprintf(buf, "%dS", ui->batt_cells);

    ST7735_FillRectangle(130, 2, 28, 12, ST7735_BLACK);
    ST7735_WriteString(130, 2, buf, UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
}

static void DrawMqttBase(void)
{
    DrawTitle("MQTT");

    ST7735_WriteString(18, 34, "ENABLE", UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 52, "BACK",   UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
}

static void UpdateMqtt(uint8_t force)
{
    (void)force;

    DrawListCursor(menu_index, MQTT_COUNT);

    ST7735_FillRectangle(82, 34, 50, 12, ST7735_BLACK);

    if(ui->mqtt_enable)
        ST7735_WriteString(82, 34, "ON", UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
    else
        ST7735_WriteString(82, 34, "OFF", UI_FONT_SMALL, ST7735_RED, ST7735_BLACK);
}

static void DrawSystemBase(void)
{
    DrawTitle("SYSTEM");

    ST7735_WriteString(18, 24, "SAVE FLASH", UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 42, "LOAD FLASH", UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 60, "DEFAULT",    UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 88, "BACK",       UI_FONT_SMALL, ST7735_WHITE, ST7735_BLACK);
}

static void UpdateSystem(uint8_t force)
{
    (void)force;
    DrawListCursor(menu_index, SYSTEM_COUNT);
}

static void DrawEditBase(const char *title)
{
    DrawTitle(title);
    ST7735_WriteString(8, 104, "ROTATE:ADJ PRESS:OK", UI_FONT_SMALL, ST7735_BLUE, ST7735_BLACK);
}

static void UpdateEditVoltage(uint8_t force)
{
    char buf[24];

    FmtFloat(buf, edit_value, 2, "V");

    WriteCached(c_u,
                28, 48, 120,
                buf,
                UI_FONT_MEDIUM,
                ST7735_CYAN,
                ST7735_BLACK,
                force);
}

static void UpdateEditCurrent(uint8_t force)
{
    char buf[24];

    FmtFloat(buf, edit_value, 2, "A");

    WriteCached(c_i,
                28, 48, 120,
                buf,
                UI_FONT_MEDIUM,
                ST7735_YELLOW,
                ST7735_BLACK,
                force);
}

static uint8_t PageCount(void)
{
    switch(page)
    {
        case UI_PAGE_MENU:    return MENU_COUNT;
        case UI_PAGE_PRESET:  return PRESET_COUNT;
        case UI_PAGE_BATTERY: return BATT_COUNT;
        case UI_PAGE_MQTT:    return MQTT_COUNT;
        case UI_PAGE_SYSTEM:  return SYSTEM_COUNT;
        default:              return 0;
    }
}

static void EncoderMainAdjust(int8_t dir)
{
    if(ui == 0 || dir == 0)
        return;

    if(main_field == MAIN_VSET)
    {
        ui->vset += dir * VSET_STEP;
        ui->vset = clampf(ui->vset, VSET_MIN, VSET_MAX);
        ClearCache();
    }
    else if(main_field == MAIN_ISET)
    {
        ui->iset += dir * ISET_STEP;
        ui->iset = clampf(ui->iset, ISET_MIN, ISET_MAX);
        ClearCache();
    }
    else if(main_field == MAIN_PRESET)
    {
        uint8_t id = ui->active_preset;

        if(dir > 0)
            id = (id + 1) % 3;
        else
            id = (id == 0) ? 2 : id - 1;

        LoadPreset(id);
    }
    else if(main_field == MAIN_OUT)
    {
        if(dir > 0)
        {
            ui->enable = 1;

            if(ui->state == BBUI_STATE_OFF)
                ui->state = BBUI_STATE_CV;
        }
        else
        {
            ui->enable = 0;
            ui->state = BBUI_STATE_OFF;
        }

        ClearCache();
    }

    dirty = 1;
}

static void EncoderMove(int8_t dir)
{
    if(ui == 0 || dir == 0)
        return;

    if(page == UI_PAGE_MAIN)
    {
        EncoderMainAdjust(dir);
        return;
    }

    if(page == UI_PAGE_EDIT_V)
    {
        edit_value += dir * VSET_STEP;
        edit_value = clampf(edit_value, VSET_MIN, VSET_MAX);
        dirty = 1;
        return;
    }

    if(page == UI_PAGE_EDIT_I)
    {
        edit_value += dir * ISET_STEP;
        edit_value = clampf(edit_value, ISET_MIN, ISET_MAX);
        dirty = 1;
        return;
    }

    uint8_t count = PageCount();

    if(count == 0)
        return;

    if(dir > 0)
    {
        menu_index++;

        if(menu_index >= count)
            menu_index = 0;
    }
    else
    {
        if(menu_index == 0)
            menu_index = count - 1;
        else
            menu_index--;
    }

    dirty = 1;
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
        EncoderMove(+1);
    }

    while(enc_acc <= -UI_ENC_STEP)
    {
        enc_acc += UI_ENC_STEP;
        EncoderMove(-1);
    }
}

static void HandleShortPress(void)
{
    if(ui == 0)
        return;

    if(page == UI_PAGE_MAIN)
    {
        main_field++;

        if(main_field >= MAIN_COUNT)
            main_field = MAIN_VSET;

        ClearCache();
        dirty = 1;
        return;
    }

    if(page == UI_PAGE_MENU)
    {
        if(menu_index == MENU_PRESET)
            ChangePage(UI_PAGE_PRESET);
        else if(menu_index == MENU_BATTERY)
            ChangePage(UI_PAGE_BATTERY);
        else if(menu_index == MENU_MQTT)
            ChangePage(UI_PAGE_MQTT);
        else if(menu_index == MENU_SYSTEM)
            ChangePage(UI_PAGE_SYSTEM);
        else if(menu_index == MENU_BACK)
            ChangePage(UI_PAGE_MAIN);

        return;
    }

    if(page == UI_PAGE_PRESET)
    {
        if(menu_index == PRESET_SAVE_M1)
        {
            SavePreset(0);
            BBUI_SaveToFlash();
        }
        else if(menu_index == PRESET_SAVE_M2)
        {
            SavePreset(1);
            BBUI_SaveToFlash();
        }
        else if(menu_index == PRESET_SAVE_M3)
        {
            SavePreset(2);
            BBUI_SaveToFlash();
        }
        else if(menu_index == PRESET_BACK)
        {
            ChangePage(UI_PAGE_MENU);
            return;
        }

        dirty = 1;
        return;
    }

    if(page == UI_PAGE_BATTERY)
    {
        if(menu_index <= BATT_6S)
        {
            SelectBattery(menu_index + 1);
            BBUI_SaveToFlash();
            dirty = 1;
        }
        else
        {
            ChangePage(UI_PAGE_MENU);
        }

        return;
    }

    if(page == UI_PAGE_MQTT)
    {
        if(menu_index == MQTT_ENABLE)
        {
            ui->mqtt_enable = !ui->mqtt_enable;
            BBUI_SaveToFlash();
            dirty = 1;
        }
        else
        {
            ChangePage(UI_PAGE_MENU);
        }

        return;
    }

    if(page == UI_PAGE_SYSTEM)
    {
        if(menu_index == SYSTEM_SAVE_FLASH)
        {
            BBUI_SaveToFlash();
            dirty = 1;
        }
        else if(menu_index == SYSTEM_LOAD_FLASH)
        {
            BBUI_LoadFromFlash();
            dirty = 1;
        }
        else if(menu_index == SYSTEM_DEFAULT)
        {
            SetDefaultConfig();
            BBUI_SaveToFlash();
            dirty = 1;
        }
        else if(menu_index == SYSTEM_BACK)
        {
            ChangePage(UI_PAGE_MENU);
        }

        return;
    }

    if(page == UI_PAGE_EDIT_V)
    {
        ui->vset = edit_value;
        ChangePage(prev_page);
        return;
    }

    if(page == UI_PAGE_EDIT_I)
    {
        ui->iset = edit_value;
        ChangePage(prev_page);
        return;
    }
}

static void HandleLongPress(void)
{
    if(page == UI_PAGE_MAIN)
    {
        ChangePage(UI_PAGE_MENU);
    }
    else
    {
        ChangePage(UI_PAGE_MAIN);
    }
}

#if UI_USE_POLLING_BUTTON
static void ButtonTask(void)
{
    uint8_t raw = (HAL_GPIO_ReadPin(UI_BTN_GPIO_Port, UI_BTN_Pin) == UI_BTN_ACTIVE_LEVEL) ? 0 : 1;
    uint32_t now = HAL_GetTick();

    if(raw != btn_last)
    {
        btn_last = raw;
        btn_last_change = now;
    }

    if(now - btn_last_change < UI_BTN_DEBOUNCE_MS)
        return;

    if(raw == 0)
    {
        if(btn_pressed == 0)
        {
            btn_pressed = 1;
            btn_long_done = 0;
            btn_down_tick = now;
        }
        else
        {
            if(btn_long_done == 0 && now - btn_down_tick >= UI_BTN_LONG_MS)
            {
                btn_long_done = 1;
                HandleLongPress();
            }
        }
    }
    else
    {
        if(btn_pressed)
        {
            btn_pressed = 0;

            if(btn_long_done == 0)
                HandleShortPress();
        }
    }
}
#endif

void BBUI_Init(BBUI_Data_t *data, TIM_HandleTypeDef *htim_encoder)
{
    ui = data;
    enc_tim = htim_encoder;

    if(ui != 0)
    {
        if(ui->vset <= 0.0f) ui->vset = 12.0f;
        if(ui->iset <= 0.0f) ui->iset = 2.0f;

        ui->enable = 0;
        ui->state = BBUI_STATE_OFF;

        if(ui->batt_cells < 1 || ui->batt_cells > 6)
            ui->batt_cells = 3;

        if(ui->active_preset > 2)
            ui->active_preset = 0;

        if(ui->preset[0].vset <= 0.0f)
            SetDefaultConfig();

        BBUI_LoadFromFlash();
    }

    if(enc_tim != 0)
    {
        HAL_TIM_Encoder_Start(enc_tim, TIM_CHANNEL_ALL);
        enc_last = (int16_t)__HAL_TIM_GET_COUNTER(enc_tim);
    }

    ST7735_Init();

    page = UI_PAGE_MAIN;
    main_field = MAIN_VSET;
    menu_index = 0;

    force_redraw = 1;
    dirty = 1;
}

void BBUI_Task(void)
{
#if UI_USE_POLLING_BUTTON
    ButtonTask();
#endif

    EncoderTask();

    if(force_redraw)
    {
        force_redraw = 0;
        dirty = 0;

        switch(page)
        {
            case UI_PAGE_MAIN:
                DrawMainBase();
                UpdateMain(1);
                break;

            case UI_PAGE_MENU:
                DrawMenuBase();
                UpdateMenu(1);
                break;

            case UI_PAGE_PRESET:
                DrawPresetBase();
                UpdatePreset(1);
                break;

            case UI_PAGE_BATTERY:
                DrawBatteryBase();
                UpdateBattery(1);
                break;

            case UI_PAGE_MQTT:
                DrawMqttBase();
                UpdateMqtt(1);
                break;

            case UI_PAGE_SYSTEM:
                DrawSystemBase();
                UpdateSystem(1);
                break;

            case UI_PAGE_EDIT_V:
                DrawEditBase("EDIT VSET");
                UpdateEditVoltage(1);
                break;

            case UI_PAGE_EDIT_I:
                DrawEditBase("EDIT ILIM");
                UpdateEditCurrent(1);
                break;

            default:
                DrawMainBase();
                UpdateMain(1);
                break;
        }

        return;
    }

    if(dirty || HAL_GetTick() - t_lcd >= UI_LCD_PERIOD_MS)
    {
        t_lcd = HAL_GetTick();
        dirty = 0;

        switch(page)
        {
            case UI_PAGE_MAIN:
                UpdateMain(0);
                break;

            case UI_PAGE_MENU:
                UpdateMenu(0);
                break;

            case UI_PAGE_PRESET:
                UpdatePreset(0);
                break;

            case UI_PAGE_BATTERY:
                UpdateBattery(0);
                break;

            case UI_PAGE_MQTT:
                UpdateMqtt(0);
                break;

            case UI_PAGE_SYSTEM:
                UpdateSystem(0);
                break;

            case UI_PAGE_EDIT_V:
                UpdateEditVoltage(0);
                break;

            case UI_PAGE_EDIT_I:
                UpdateEditCurrent(0);
                break;

            default:
                UpdateMain(0);
                break;
        }
    }
}

void BBUI_ButtonIRQ(void)
{
#if UI_USE_POLLING_BUTTON
    /*
       N?u dùng polling button d? có nh?n gi?, không c?n g?i hàm này trong EXTI.
       Ð? tr?ng d? tránh b? double click.
    */
#else
    static uint32_t last_press = 0;

    uint32_t now = HAL_GetTick();

    if(now - last_press < 180)
        return;

    last_press = now;

    HandleShortPress();
#endif
}

void BBUI_ForceRefresh(void)
{
    force_redraw = 1;
}