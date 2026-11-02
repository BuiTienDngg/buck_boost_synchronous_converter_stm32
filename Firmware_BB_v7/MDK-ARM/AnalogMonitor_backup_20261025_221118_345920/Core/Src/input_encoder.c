#include "input_encoder.h"
#include "input_buttons.h"

#if ENCODER_COUNTS_PER_DETENT < 1
#error ENCODER_COUNTS_PER_DETENT must be >= 1
#endif

static TIM_HandleTypeDef *s_timer;
static uint16_t s_previous_counter;
static int32_t s_raw_ticks;
static int32_t s_position;
static int32_t s_pending_delta;
static int32_t s_tick_remainder;

HAL_StatusTypeDef Encoder_Init(TIM_HandleTypeDef *htim)
{
    HAL_StatusTypeDef result;
    if (htim == NULL) return HAL_ERROR;
    s_timer = htim;
    s_raw_ticks = 0;
    s_position = 0;
    s_pending_delta = 0;
    s_tick_remainder = 0;
    __HAL_TIM_SET_COUNTER(s_timer, 0U);
    s_previous_counter = 0U;
    result = HAL_TIM_Encoder_Start(s_timer, TIM_CHANNEL_ALL);
    if (result != HAL_OK) s_timer = NULL;
    return result;
}

void encoder_hander(void)
{
    uint16_t current;
    int16_t increment;
    if (s_timer == NULL) return;

    /* TIM2 period must be 65535. Subtraction handles 16-bit wraparound. */
    current = (uint16_t)__HAL_TIM_GET_COUNTER(s_timer);
    increment = (int16_t)(uint16_t)(current - s_previous_counter);
    s_previous_counter = current;
    s_raw_ticks += (int32_t)increment;
    s_tick_remainder += (int32_t)increment;

    while (s_tick_remainder >= ENCODER_COUNTS_PER_DETENT) {
        s_tick_remainder -= ENCODER_COUNTS_PER_DETENT;
        s_position++;
        s_pending_delta++;
    }
    while (s_tick_remainder <= -ENCODER_COUNTS_PER_DETENT) {
        s_tick_remainder += ENCODER_COUNTS_PER_DETENT;
        s_position--;
        s_pending_delta--;
    }
}

void encoder_handler(void) { encoder_hander(); }
int32_t Encoder_GetPosition(void) { return s_position; }
int32_t Encoder_GetRawTicks(void) { return s_raw_ticks; }
int32_t Encoder_TakeDelta(void)
{
    int32_t delta = s_pending_delta;
    s_pending_delta = 0;
    return delta;
}
uint8_t Encoder_IsSwitchPressed(void)
{
    return Button_IsPressed(BUTTON_ENCODER_SWITCH);
}
