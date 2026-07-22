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
typedef enum
{
	SET_VOLTAGE = 0,
	SET_CURRENT,
	RUN_CC_MODE,
	RUN_CV_MODE
} UI_State_t;

UI_State_t ui_state;
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

BBUI_Data_t *ui = 0;
static TIM_HandleTypeDef *enc_tim = 0;

static UI_Page_t page = UI_PAGE_MAIN;
static UI_Page_t prev_page = UI_PAGE_MENU;

UI_MainField_t main_field = MAIN_VSET;
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
            sprintf(buf, "%02ld", v);
        else if(dec == 1)
            sprintf(buf, "%02ld.%01ld", v / scale, v % scale);
        else
            sprintf(buf, "%02ld.%02ld", v / scale, v % scale);
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



static void DrawMainBase(void)
{
		ST7735_FillScreen(ST7735_BLACK);
    ST7735_WriteString(110, 13,  "U", UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
    ST7735_WriteString(110, 43, "I", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
    ST7735_WriteString(110, 73,  "P", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
		
	
		ST7735_WriteString(0, 60,  "SET", UI_FONT_SMALL, ST7735_MAGENTA, ST7735_BLACK);
		ST7735_WriteString(0, 70,  "CC", UI_FONT_SMALL, ST7735_BLACK, ST7735_YELLOW);
		//ST7735_WriteString(5, 70,  "CV", UI_FONT_SMALL, ST7735_BLACK, ST7735_GREEN);
		
		
		
		
		
	
		ST7735_WriteString(0, 98,  "Vin:", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
		ST7735_WriteString(85, 98,  "V", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
		
		ST7735_WriteString(0, 118,  "Ilim:", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
		ST7735_WriteString(92, 118,  "A", UI_FONT_SMALL, ST7735_CYAN, ST7735_BLACK);
		
		ST7735_WriteString(110, 105,  "*C", UI_FONT_SMALL, ST7735_RED, ST7735_BLACK);
		
		drawHline(25, 25,78, ST7735_BLUE);
		drawHline(25, 55,78, ST7735_BLUE);
		drawHline(25, 85,78, ST7735_BLUE);
		
    ClearCache();
}


static void UpdateMain(uint8_t force)
{
    char buf[32];
    if(ui == 0)
        return;
    FmtNumber(buf, ui->vout, 2);
		WriteCached(c_u,25, 0, 50,buf,UI_FONT_BIG,ST7735_GREEN,ST7735_BLACK,force);
		FmtNumber(buf, ui->current, 2);
		WriteCached(c_i,25, 30, 50,buf,UI_FONT_BIG,ST7735_YELLOW,ST7735_BLACK,force);

		
		
		
		
		if(main_field == MAIN_VSET)
		{
			ST7735_WriteString(0, 70,  "CV", UI_FONT_SMALL, ST7735_BLACK, ST7735_GREEN);
			FmtNumber(buf, ui->vset, 2);
			WriteCached(c_p,25, 60, 50,buf,UI_FONT_BIG,ST7735_MAGENTA,ST7735_BLACK,force);
			
		}else if(main_field == MAIN_ISET)
		{
			FmtNumber(buf, ui->iset, 2);
			ST7735_WriteString(0, 70,  "CC", UI_FONT_SMALL, ST7735_BLACK, ST7735_YELLOW);
			WriteCached(c_p,25, 60, 50,buf,UI_FONT_BIG,ST7735_MAGENTA,ST7735_BLACK,force);
		}
		
		
		if(main_field == MAIN_OUT){
			
			ST7735_WriteString(0, 40,  "ON ", UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
			// POWER
			ST7735_WriteString(0, 70,  "  ", UI_FONT_SMALL, ST7735_BLACK, ST7735_BLACK);
			
			float power = ui->vout * ui->current;
			if(power < 100.0f) FmtNumber(buf, power, 2);
			else FmtNumber(buf, power, 1);
			WriteCached(c_p,25, 60, 50,buf,UI_FONT_BIG,ST7735_MAGENTA,ST7735_BLACK,force);
		}else if(main_field == MAIN_OUT_OFF)
		{
			ST7735_WriteString(0, 40,  "OFF", UI_FONT_SMALL, ST7735_RED, ST7735_BLACK);
			ui->state = BBUI_STATE_OFF;
		}
		
		
		if(ui->state == BBUI_STATE_CV){
			ST7735_WriteString(110, 0,  "CV", UI_FONT_SMALL, ST7735_YELLOW, ST7735_BLACK);
			ST7735_WriteString(110, 30,  "  ", UI_FONT_SMALL, ST7735_BLACK, ST7735_BLACK);
		}else if(ui->state == BBUI_STATE_CC){
			ST7735_WriteString(110, 30,  "CC", UI_FONT_SMALL, ST7735_GREEN, ST7735_BLACK);
			ST7735_WriteString(110, 0,  "  ", UI_FONT_SMALL, ST7735_BLACK, ST7735_BLACK);
		}else {
			ST7735_WriteString(110, 0,  "  ", UI_FONT_SMALL, ST7735_BLACK, ST7735_BLACK);
			ST7735_WriteString(110, 30,  "  ", UI_FONT_SMALL, ST7735_BLACK, ST7735_BLACK);
		}
		
		
		FmtNumber(buf, ui->vin, 2);
    WriteCached(c_vin,28, 91, 27, buf, UI_FONT_MEDIUM, ST7735_GREEN, ST7735_BLACK,force);
		
		
		FmtNumber(buf, ui->iset, 2);
    WriteCached(c_lim,37, 111, 27, buf, UI_FONT_MEDIUM, ST7735_GREEN, ST7735_BLACK,force);
		
		FmtNumber(buf, ui->temp, 1);
    WriteCached(c_temp,98, 90, 27, buf, UI_FONT_SMALL, ST7735_RED, ST7735_BLACK,force);
		
}
static void EncoderMainAdjust(int8_t dir)
{
//    if(ui == 0 || dir == 0)
//        return;

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
   
		ClearCache();
    dirty = 1;
}

static void EncoderMove(int8_t dir)
{
//    if(ui == 0 || dir == 0)
//        return;
		EncoderMainAdjust(dir);
//    if(page == UI_PAGE_MAIN)
//    {
//        
//        return;
//    }

//    if(page == UI_PAGE_EDIT_V)
//    {
//        edit_value += dir * VSET_STEP;
//        edit_value = clampf(edit_value, VSET_MIN, VSET_MAX);
//        dirty = 1;
//        return;
//    }

//    if(page == UI_PAGE_EDIT_I)
//    {
//        edit_value += dir * ISET_STEP;
//        edit_value = clampf(edit_value, ISET_MIN, ISET_MAX);
//        dirty = 1;
//        return;
//    }

//    uint8_t count = PageCount();

//    if(count == 0)
//        return;

//    if(dir > 0)
//    {
//        menu_index++;

//        if(menu_index >= count)
//            menu_index = 0;
//    }
//    else
//    {
//        if(menu_index == 0)
//            menu_index = count - 1;
//        else
//            menu_index--;
//    }

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
void handleButton()
{
	if(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9)){
		HAL_Delay(30);
		while(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9));
		force_redraw = 1;
		if( main_field == MAIN_VSET) main_field = MAIN_OUT_OFF;
		else main_field = MAIN_VSET;
	}else if(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8)){
		HAL_Delay(30);
		while(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_8));
		ui_state = SET_CURRENT;
		force_redraw = 1;
		if( main_field == MAIN_ISET) main_field = MAIN_OUT_OFF;
		else main_field = MAIN_ISET;
	}else if(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7)){
		HAL_Delay(30);
		while(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7));
		force_redraw = 1;
		if( main_field == MAIN_OUT) main_field = MAIN_OUT_OFF;
		else main_field = MAIN_OUT;
	}else if(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6)){
		HAL_Delay(30);
		while(!HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_6));
		force_redraw = 1;
		
	}
	
}
void handleUI()
{
		EncoderTask();
		handleButton();
	
	if(main_field == MAIN_OUT)
    {
            ui->enable = 1;
						//ui->state = BBUI_STATE_CV;
			
		}
    else if(main_field == MAIN_OUT_OFF)
		{
				ui->enable = 0;
				ui->state = BBUI_STATE_OFF;
		}
    if(force_redraw || dirty)
    {
        force_redraw = 0;
        dirty = 0;
        
		}
		if(HAL_GetTick() - t_lcd >= UI_LCD_PERIOD_MS)
    {
        t_lcd = HAL_GetTick();
        dirty = 0;
				UpdateMain(1);
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
//                HandleLongPress();
            }
        }
    }
    else
    {
        if(btn_pressed)
        {
            btn_pressed = 0;

//            if(btn_long_done == 0)
//                HandleShortPress();
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
		DrawMainBase();
}

//void BBUI_Task(void)
//{
//#if UI_USE_POLLING_BUTTON
//    ButtonTask();
//#endif

//    EncoderTask();

//    if(force_redraw)
//    {
//        force_redraw = 0;
//        dirty = 0;

//        switch(page)
//        {
//            case UI_PAGE_MAIN:
//                DrawMainBase();
//                UpdateMain(1);
//                break;

//            case UI_PAGE_MENU:
//                DrawMenuBase();
//                UpdateMenu(1);
//                break;

//            case UI_PAGE_PRESET:
//                DrawPresetBase();
//                UpdatePreset(1);
//                break;

//            case UI_PAGE_BATTERY:
//                DrawBatteryBase();
//                UpdateBattery(1);
//                break;

//            case UI_PAGE_MQTT:
//                DrawMqttBase();
//                UpdateMqtt(1);
//                break;

//            case UI_PAGE_SYSTEM:
//                DrawSystemBase();
//                UpdateSystem(1);
//                break;

//            case UI_PAGE_EDIT_V:
//                DrawEditBase("EDIT VSET");
//                UpdateEditVoltage(1);
//                break;

//            case UI_PAGE_EDIT_I:
//                DrawEditBase("EDIT ILIM");
//                UpdateEditCurrent(1);
//                break;

//            default:
//                DrawMainBase();
//                UpdateMain(1);
//                break;
//        }

//        return;
//    }

//    if(dirty || HAL_GetTick() - t_lcd >= UI_LCD_PERIOD_MS)
//    {
//        t_lcd = HAL_GetTick();
//        dirty = 0;

//        switch(page)
//        {
//            case UI_PAGE_MAIN:
//                UpdateMain(0);
//                break;

//            case UI_PAGE_MENU:
//                UpdateMenu(0);
//                break;

//            case UI_PAGE_PRESET:
//                UpdatePreset(0);
//                break;

//            case UI_PAGE_BATTERY:
//                UpdateBattery(0);
//                break;

//            case UI_PAGE_MQTT:
//                UpdateMqtt(0);
//                break;

//            case UI_PAGE_SYSTEM:
//                UpdateSystem(0);
//                break;

//            case UI_PAGE_EDIT_V:
//                UpdateEditVoltage(0);
//                break;

//            case UI_PAGE_EDIT_I:
//                UpdateEditCurrent(0);
//                break;

//            default:
//                UpdateMain(0);
//                break;
//        }
//    }
//}

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