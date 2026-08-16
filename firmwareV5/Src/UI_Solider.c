#include "UI_Solider.h"
#include "st7789.h"

#include <stdio.h>
#include <string.h>

/* =========================================================
 * SOLDER RANGE / DISPLAY
 * ========================================================= */

#define SOL_TEMP_MIN                  150.0f
#define SOL_TEMP_MAX                  450.0f

#define SOL_CURR_MIN                  0.0f
#define SOL_CURR_MAX                  10.0f

#define SOL_SET_MIN                   150.0f
#define SOL_SET_MAX                   450.0f
#define SOL_SET_STEP                  5.0f

#define SOL_SAMPLE_MS                 100U
#define SOL_LCD_MS                    120U

#define SOL_BG                        ST7789_COLOR_BLACK
#define SOL_TEMP_COLOR                ST7789_COLOR_RED
#define SOL_CURR_COLOR                ST7789_COLOR_CYAN
#define SOL_SET_COLOR                 ST7789_COLOR_CYAN
#define SOL_TEXT_COLOR                ST7789_COLOR_WHITE
#define SOL_MUTED_COLOR               ST7789_COLOR_LIGHTGREY
#define SOL_PWR_COLOR                 ST7789_Color_GetFromRGB(255, 0, 255)
#define SOL_GRID_COLOR                ST7789_Color_GetFromRGB(42, 48, 60)

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
 * INTERNAL
 * ========================================================= */

static UI_Solider_Data_t sol;

static uint8_t sol_active = 0U;
static uint8_t force_redraw = 1U;
static uint8_t values_dirty = 1U;

static uint16_t graph_head = 0U;
static int16_t graph_last_x = -1;
static int16_t graph_last_yt = -1;
static int16_t graph_last_yi = -1;
static uint8_t graph_has_last = 0U;

static uint32_t sample_tick = 0U;
static uint32_t lcd_tick = 0U;

/* caches */
static char c_tip[16] = "";
static char c_set[16] = "";
static char c_pwr[16] = "";
static char c_current[16] = "";
static char c_preset0[8] = "";
static char c_preset1[8] = "";
static char c_preset2[8] = "";

static uint8_t old_active_preset = 0xFFU;

/* =========================================================
 * HELPERS
 * ========================================================= */

static float clampf_sol(float x, float lo, float hi)
{
    if(x < lo) return lo;
    if(x > hi) return hi;
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
            ST7789_DrawPixel((uint16_t)x0,
                             (uint16_t)y0,
                             color);
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

static void graph_restore_grid_column(int x)
{
    int y1 = SOL_GRAPH_Y0 + SOL_GRAPH_H / 4;
    int y2 = SOL_GRAPH_Y0 + SOL_GRAPH_H / 2;
    int y3 = SOL_GRAPH_Y0 + 3 * SOL_GRAPH_H / 4;

    ST7789_DrawPixel((uint16_t)x, (uint16_t)y1, SOL_GRID_COLOR);
    ST7789_DrawPixel((uint16_t)x, (uint16_t)y2, SOL_GRID_COLOR);
    ST7789_DrawPixel((uint16_t)x, (uint16_t)y3, SOL_GRID_COLOR);
}

static void graph_clear_column(int x)
{
    if(x < SOL_PLOT_X0 || x > SOL_PLOT_X1)
        return;

    ST7789_DrawFilledRectangle((uint16_t)x,
                               SOL_PLOT_Y0,
                               1U,
                               SOL_PLOT_H,
                               SOL_BG);

    graph_restore_grid_column(x);
}

static void draw_graph_static(void)
{
    /* legend */
    ST7789_PutString(7, 3,
                     "T 150-450C",
                     1,
                     SOL_TEMP_COLOR,
                     SOL_BG);

    ST7789_PutString(110, 3,
                     "I 0-10A",
                     1,
                     SOL_CURR_COLOR,
                     SOL_BG);

    ST7789_PutString(230, 3,
                     "SOLDER",
                     1,
                     ST7789_COLOR_YELLOW,
                     SOL_BG);

    /* border */
    draw_rect_outline(SOL_GRAPH_X0,
                      SOL_GRAPH_Y0,
                      SOL_GRAPH_W,
                      SOL_GRAPH_H,
                      SOL_TEXT_COLOR);

    /* dotted horizontal grid */
    for(uint8_t k = 1U; k < 4U; k++)
    {
        uint16_t y =
            (uint16_t)(SOL_GRAPH_Y0 +
                       (SOL_GRAPH_H * k) / 4U);

        for(uint16_t x = SOL_GRAPH_X0 + 1U;
            x < SOL_GRAPH_X1;
            x += 4U)
        {
            ST7789_DrawPixel(x, y, SOL_GRID_COLOR);
        }
    }

    graph_head = 0U;
    graph_last_x = -1;
    graph_last_yt = -1;
    graph_last_yi = -1;
    graph_has_last = 0U;

    sample_tick = HAL_GetTick();
}

static void graph_push(float temp, float current)
{
    int x = SOL_PLOT_X0 + (int)graph_head;

    int yt = map_y(temp,
                   SOL_TEMP_MIN,
                   SOL_TEMP_MAX);

    int yi = map_y(current,
                   SOL_CURR_MIN,
                   SOL_CURR_MAX);

    graph_clear_column(x);

    if(x + 1 <= SOL_PLOT_X1)
        graph_clear_column(x + 1);

    if(graph_has_last &&
       x > graph_last_x)
    {
        draw_line_fast(graph_last_x,
                       graph_last_yt,
                       x,
                       yt,
                       SOL_TEMP_COLOR);

        draw_line_fast(graph_last_x,
                       graph_last_yi,
                       x,
                       yi,
                       SOL_CURR_COLOR);
    }
    else
    {
        ST7789_DrawPixel((uint16_t)x,
                         (uint16_t)yt,
                         SOL_TEMP_COLOR);

        ST7789_DrawPixel((uint16_t)x,
                         (uint16_t)yi,
                         SOL_CURR_COLOR);
    }

    graph_last_x = (int16_t)x;
    graph_last_yt = (int16_t)yt;
    graph_last_yi = (int16_t)yi;
    graph_has_last = 1U;

    graph_head++;

    if(graph_head >= SOL_PLOT_W)
    {
        graph_head = 0U;
        graph_has_last = 0U;
    }
}

static void graph_task(void)
{
    uint32_t now = HAL_GetTick();

    if((uint32_t)(now - sample_tick) < SOL_SAMPLE_MS)
        return;

    sample_tick = now;

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
 * PUBLIC API
 * ========================================================= */

void UI_Solider_Init(void)
{
    memset(&sol, 0, sizeof(sol));

    sol.tip_temp = 25.0f;
    sol.current = 0.0f;
    sol.fet_temp = 25.0f;
    sol.power = 0.0f;

    sol.preset[0] = 300.0f;
    sol.preset[1] = 350.0f;
    sol.preset[2] = 400.0f;

    sol.active_preset = 1U;
    sol.set_temp = sol.preset[1];

    sol_active = 0U;
    force_redraw = 1U;
    values_dirty = 1U;

    graph_head = 0U;
    graph_has_last = 0U;

    sample_tick = HAL_GetTick();
    lcd_tick = HAL_GetTick();

    clear_caches();
}

void UI_Solider_Enter(void)
{
    sol_active = 1U;
    force_redraw = 1U;
    values_dirty = 1U;

    graph_head = 0U;
    graph_has_last = 0U;
    graph_last_x = -1;

    sample_tick = HAL_GetTick();
    lcd_tick = HAL_GetTick();

    clear_caches();
}

void UI_Solider_Exit(void)
{
    sol_active = 0U;
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
    sol.fet_temp = fet_temp;
    sol.power = power;

    values_dirty = 1U;
}

float UI_Solider_GetSetTemp(void)
{
    return sol.set_temp;
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
        sol.set_temp = temp;

    values_dirty = 1U;
}

void UI_Solider_SelectPreset(uint8_t id)
{
    if(id > 2U)
        return;

    sol.active_preset = id;
    sol.set_temp = sol.preset[id];

    values_dirty = 1U;
}

void UI_Solider_EncoderAdjust(int8_t dir)
{
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

    values_dirty = 1U;
}

void UI_Solider_Task(uint8_t force)
{
    if(!sol_active)
        return;

    if(force || force_redraw)
    {
        force_redraw = 0U;
        values_dirty = 0U;

        draw_base();
        update_values(1U);

        return;
    }

    graph_task();

    uint32_t now = HAL_GetTick();

    if(values_dirty ||
       (uint32_t)(now - lcd_tick) >= SOL_LCD_MS)
    {
        lcd_tick = now;
        values_dirty = 0U;

        update_values(0U);
    }
}
