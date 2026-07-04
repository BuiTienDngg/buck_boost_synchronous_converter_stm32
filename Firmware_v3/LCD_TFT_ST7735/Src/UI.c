#include "UI.h"
#include "ST7735_SPI.h"
#include <stdio.h>
#include <string.h>

#define BBUI_LCD_PERIOD_MS          100
#define BBUI_EDIT_TIMEOUT_MS        1500
#define BBUI_BLINK_MS               300

#define BBUI_ENC_COUNT_PER_STEP     4

#define BBUI_VSET_MIN               1.0f
#define BBUI_VSET_MAX               30.0f
#define BBUI_VSET_STEP              0.1f

#define BBUI_ISET_MIN               0.1f
#define BBUI_ISET_MAX               10.0f
#define BBUI_ISET_STEP              0.1f

static BBUI_Data_t *ui_data = 0;
static TIM_HandleTypeDef *ui_htim_enc = 0;

static BBUI_Field_t ui_field = BBUI_FIELD_VSET;
static BBUI_Field_t ui_last_field = BBUI_FIELD_COUNT;

static BBUI_Field_t ui_edit_field = BBUI_FIELD_COUNT;
static uint32_t ui_edit_until = 0;

static int16_t enc_last = 0;
static int32_t enc_acc = 0;

static uint8_t ui_force = 1;
static uint8_t ui_dirty = 1;
static uint32_t t_lcd = 0;

static char cache_header_state[16] = "";
static char cache_v_line[24] = "";
static char cache_i_line[24] = "";
static char cache_t_line[24] = "";
static char cache_footer[32] = "";
static char cache_cursor[8] = "";

static float BBUI_Clamp(float x, float min, float max)
{
    if(x < min) return min;
    if(x > max) return max;
    return x;
}

static const char* BBUI_StateText(BBUI_State_t state)
{
    switch(state)
    {
        case BBUI_STATE_OFF:
            return "OFF";

        case BBUI_STATE_CV:
            return "CV";

        case BBUI_STATE_CC:
            return "CC";

        case BBUI_STATE_FAULT:
            return "FLT";

        default:
            return "---";
    }
}

static uint16_t BBUI_StateColor(BBUI_State_t state)
{
    switch(state)
    {
        case BBUI_STATE_OFF:
            return ST7735_WHITE;

        case BBUI_STATE_CV:
            return ST7735_GREEN;

        case BBUI_STATE_CC:
            return ST7735_YELLOW;

        case BBUI_STATE_FAULT:
            return ST7735_RED;

        default:
            return ST7735_WHITE;
    }
}

static void BBUI_FmtFloat(char *buf, float value, uint8_t dec, const char *unit)
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

static void BBUI_ClearCache(void)
{
    cache_header_state[0] = 0;
    cache_v_line[0] = 0;
    cache_i_line[0] = 0;
    cache_t_line[0] = 0;
    cache_footer[0] = 0;
    cache_cursor[0] = 0;

    ui_last_field = BBUI_FIELD_COUNT;
}

static void BBUI_WriteCached(char *cache,
                             uint16_t x,
                             uint16_t y,
                             uint16_t w,
                             const char *str,
                             uint16_t color,
                             uint16_t bg,
                             uint8_t force)
{
    if(force || strcmp(cache, str) != 0)
    {
        ST7735_FillRectangle(x, y, w, 12, bg);
        ST7735_WriteString(x, y, str, Font_7x10, color, bg);

        strncpy(cache, str, 23);
        cache[23] = 0;
    }
}

static void BBUI_DrawBase(void)
{
    ST7735_FillScreen(ST7735_BLACK);

    ST7735_WriteString(4, 2, "BUCK-BOOST", Font_7x10, ST7735_YELLOW, ST7735_BLACK);

    drawHline(0, 15, 160, ST7735_BLUE);

    ST7735_WriteString(18, 26, "V", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 48, "I", Font_7x10, ST7735_WHITE, ST7735_BLACK);
    ST7735_WriteString(18, 70, "T", Font_7x10, ST7735_WHITE, ST7735_BLACK);

    drawHline(0, 91, 160, ST7735_BLUE);

    ST7735_WriteString(4, 98, "SET", Font_7x10, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(62, 98, "LIM", Font_7x10, ST7735_CYAN, ST7735_BLACK);
    ST7735_WriteString(122, 98, "OUT", Font_7x10, ST7735_CYAN, ST7735_BLACK);

    drawHline(0, 115, 160, ST7735_BLUE);

    ST7735_WriteString(6, 118, "BTN:NEXT ENC:ADJ", Font_7x10, ST7735_BLUE, ST7735_BLACK);

    BBUI_ClearCache();
}

static void BBUI_UpdateCursor(uint8_t force)
{
    if(!force && ui_field == ui_last_field)
        return;

    ST7735_FillRectangle(2, 24, 12, 60, ST7735_BLACK);
    ST7735_FillRectangle(112, 96, 8, 14, ST7735_BLACK);

    if(ui_field == BBUI_FIELD_VSET)
        ST7735_WriteString(4, 26, ">", Font_7x10, ST7735_YELLOW, ST7735_BLACK);
    else if(ui_field == BBUI_FIELD_ISET)
        ST7735_WriteString(4, 48, ">", Font_7x10, ST7735_YELLOW, ST7735_BLACK);
    else if(ui_field == BBUI_FIELD_OUT)
        ST7735_WriteString(112, 98, ">", Font_7x10, ST7735_YELLOW, ST7735_BLACK);

    ui_last_field = ui_field;

    cache_v_line[0] = 0;
    cache_i_line[0] = 0;
    cache_footer[0] = 0;
}

static uint8_t BBUI_IsEditingField(BBUI_Field_t field)
{
    uint32_t now = HAL_GetTick();

    if(ui_edit_field == field && now < ui_edit_until)
        return 1;

    return 0;
}

static uint8_t BBUI_BlinkOn(void)
{
    return ((HAL_GetTick() / BBUI_BLINK_MS) & 1) ? 1 : 0;
}

static void BBUI_UpdateHeader(uint8_t force)
{
    char buf[16];

    sprintf(buf, "%s", BBUI_StateText(ui_data->state));

    BBUI_WriteCached(cache_header_state,
                     120, 2, 38,
                     buf,
                     BBUI_StateColor(ui_data->state),
                     ST7735_BLACK,
                     force);
}

static void BBUI_UpdateMainValues(uint8_t force)
{
    char buf[24];
    uint8_t blink = BBUI_BlinkOn();

    uint8_t edit_v = BBUI_IsEditingField(BBUI_FIELD_VSET);
    uint8_t edit_i = BBUI_IsEditingField(BBUI_FIELD_ISET);

    if(edit_v)
    {
        if(blink)
            BBUI_FmtFloat(buf, ui_data->vset, 2, " V");
        else
            sprintf(buf, "        ");

        BBUI_WriteCached(cache_v_line,
                         48, 26, 100,
                         buf,
                         ST7735_YELLOW,
                         ST7735_BLACK,
                         force || 1);
    }
    else
    {
        BBUI_FmtFloat(buf, ui_data->vout, 2, " V");

        BBUI_WriteCached(cache_v_line,
                         48, 26, 100,
                         buf,
                         ST7735_GREEN,
                         ST7735_BLACK,
                         force);
    }

    if(edit_i)
    {
        if(blink)
            BBUI_FmtFloat(buf, ui_data->iset, 2, " A");
        else
            sprintf(buf, "        ");

        BBUI_WriteCached(cache_i_line,
                         48, 48, 100,
                         buf,
                         ST7735_YELLOW,
                         ST7735_BLACK,
                         force || 1);
    }
    else
    {
        BBUI_FmtFloat(buf, ui_data->current, 2, " A");

        BBUI_WriteCached(cache_i_line,
                         48, 48, 100,
                         buf,
                         ST7735_YELLOW,
                         ST7735_BLACK,
                         force);
    }

    BBUI_FmtFloat(buf, ui_data->temp, 1, " C");

    BBUI_WriteCached(cache_t_line,
                     48, 70, 100,
                     buf,
                     ui_data->temp > 70.0f ? ST7735_RED : ST7735_MAGENTA,
                     ST7735_BLACK,
                     force);
}

static void BBUI_UpdateFooter(uint8_t force)
{
    char vbuf[12];
    char ibuf[12];
    char obuf[8];
    char line[32];

    BBUI_FmtFloat(vbuf, ui_data->vset, 1, "V");
    BBUI_FmtFloat(ibuf, ui_data->iset, 1, "A");

    if(ui_data->enable)
        sprintf(obuf, "ON");
    else
        sprintf(obuf, "OFF");

    sprintf(line, "%s  %s  %s", vbuf, ibuf, obuf);

    BBUI_WriteCached(cache_footer,
                     34, 98, 124,
                     line,
                     ST7735_WHITE,
                     ST7735_BLACK,
                     force);
}

static void BBUI_UpdateLCD(uint8_t force)
{
    if(ui_data == 0)
        return;

    BBUI_UpdateCursor(force);
    BBUI_UpdateHeader(force);
    BBUI_UpdateMainValues(force);
    BBUI_UpdateFooter(force);
}

static void BBUI_StartEdit(BBUI_Field_t field)
{
    ui_edit_field = field;
    ui_edit_until = HAL_GetTick() + BBUI_EDIT_TIMEOUT_MS;

    if(field == BBUI_FIELD_VSET)
        cache_v_line[0] = 0;
    else if(field == BBUI_FIELD_ISET)
        cache_i_line[0] = 0;

    cache_footer[0] = 0;

    ui_dirty = 1;
}

static void BBUI_Adjust(int8_t dir)
{
    if(ui_data == 0)
        return;

    if(dir == 0)
        return;

    switch(ui_field)
    {
        case BBUI_FIELD_VSET:
            ui_data->vset += dir * BBUI_VSET_STEP;
            ui_data->vset = BBUI_Clamp(ui_data->vset, BBUI_VSET_MIN, BBUI_VSET_MAX);
            BBUI_StartEdit(BBUI_FIELD_VSET);
            break;

        case BBUI_FIELD_ISET:
            ui_data->iset += dir * BBUI_ISET_STEP;
            ui_data->iset = BBUI_Clamp(ui_data->iset, BBUI_ISET_MIN, BBUI_ISET_MAX);
            BBUI_StartEdit(BBUI_FIELD_ISET);
            break;

        case BBUI_FIELD_OUT:
            if(dir > 0)
            {
                ui_data->enable = 1;
                ui_data->state = BBUI_STATE_CV;
            }
            else
            {
                ui_data->enable = 0;
                ui_data->state = BBUI_STATE_OFF;
            }

            cache_header_state[0] = 0;
            cache_footer[0] = 0;
            ui_dirty = 1;
            break;

        default:
            break;
    }
}

static void BBUI_EncoderTask(void)
{
    if(ui_htim_enc == 0)
        return;

    int16_t enc_now = (int16_t)__HAL_TIM_GET_COUNTER(ui_htim_enc);
    int16_t diff = enc_now - enc_last;

    enc_last = enc_now;
    enc_acc += diff;

    while(enc_acc >= BBUI_ENC_COUNT_PER_STEP)
    {
        enc_acc -= BBUI_ENC_COUNT_PER_STEP;
        BBUI_Adjust(+1);
    }

    while(enc_acc <= -BBUI_ENC_COUNT_PER_STEP)
    {
        enc_acc += BBUI_ENC_COUNT_PER_STEP;
        BBUI_Adjust(-1);
    }
}

void BBUI_Init(BBUI_Data_t *data, TIM_HandleTypeDef *htim_encoder)
{
    ui_data = data;
    ui_htim_enc = htim_encoder;

    if(ui_data != 0)
    {
        if(ui_data->vset <= 0.0f)
            ui_data->vset = 12.0f;

        if(ui_data->iset <= 0.0f)
            ui_data->iset = 2.0f;

        ui_data->enable = 0;
        ui_data->state = BBUI_STATE_OFF;
    }

    if(ui_htim_enc != 0)
    {
        HAL_TIM_Encoder_Start(ui_htim_enc, TIM_CHANNEL_ALL);
        enc_last = (int16_t)__HAL_TIM_GET_COUNTER(ui_htim_enc);
    }

    ST7735_Init();

    BBUI_DrawBase();

    ui_force = 1;
    ui_dirty = 1;

    BBUI_UpdateLCD(1);
}

void BBUI_Task(void)
{
    BBUI_EncoderTask();

    if(ui_force)
    {
        ui_force = 0;
        ui_dirty = 0;

        BBUI_DrawBase();
        BBUI_UpdateLCD(1);

        return;
    }

    if(ui_edit_field != BBUI_FIELD_COUNT && HAL_GetTick() > ui_edit_until)
    {
        ui_edit_field = BBUI_FIELD_COUNT;
        cache_v_line[0] = 0;
        cache_i_line[0] = 0;
        ui_dirty = 1;
    }

    if(ui_dirty || HAL_GetTick() - t_lcd >= BBUI_LCD_PERIOD_MS)
    {
        t_lcd = HAL_GetTick();
        ui_dirty = 0;

        BBUI_UpdateLCD(0);
    }
}

void BBUI_ButtonIRQ(void)
{
    static uint32_t last_press = 0;

    uint32_t now = HAL_GetTick();

    if(now - last_press < 180)
        return;

    last_press = now;

    ui_field++;

    if(ui_field >= BBUI_FIELD_COUNT)
        ui_field = BBUI_FIELD_VSET;

    ui_edit_field = BBUI_FIELD_COUNT;

    BBUI_UpdateCursor(1);

    ui_dirty = 1;
}

void BBUI_ForceRefresh(void)
{
    ui_force = 1;
}

BBUI_Field_t BBUI_GetField(void)
{
    return ui_field;
}