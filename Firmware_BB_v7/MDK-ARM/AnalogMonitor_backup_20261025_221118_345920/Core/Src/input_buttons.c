#include "input_buttons.h"

/* All switches are active LOW and require pull-ups configured in CubeMX. */
typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    volatile uint8_t candidate_down;
    volatile uint32_t changed_ms;
    uint8_t stable_down;
    uint8_t pressed_event;
    uint8_t released_event;
} ButtonState;

static ButtonState s_buttons[BUTTON_COUNT] = {
    {GPIOB, GPIO_PIN_13, 0, 0, 0, 0, 0},
    {GPIOB, GPIO_PIN_8,  0, 0, 0, 0, 0},
    {GPIOB, GPIO_PIN_7,  0, 0, 0, 0, 0},
    {GPIOB, GPIO_PIN_6,  0, 0, 0, 0, 0},
    {GPIOB, GPIO_PIN_5,  0, 0, 0, 0, 0},
    {GPIOB, GPIO_PIN_4,  0, 0, 0, 0, 0}
};

static uint8_t read_down(const ButtonState *button)
{
    return (HAL_GPIO_ReadPin(button->port, button->pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

void Button_Init(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t i;
    for (i = 0; i < (uint32_t)BUTTON_COUNT; i++) {
        ButtonState *b = &s_buttons[i];
        uint8_t down = read_down(b);
        b->candidate_down = down;
        b->stable_down = down;
        b->changed_ms = now;
        b->pressed_event = 0U;
        b->released_event = 0U;
    }
}

/* EXTI captures transitions; button_handle() validates the stable level. */
void Buttons_OnExti(uint16_t gpio_pin)
{
    uint32_t i;
    for (i = 0; i < (uint32_t)BUTTON_COUNT; i++) {
        ButtonState *b = &s_buttons[i];
        if (b->pin == gpio_pin) {
            uint8_t down = read_down(b);
            if (down != b->candidate_down) {
                b->candidate_down = down;
                b->changed_ms = HAL_GetTick();
            }
            break;
        }
    }
}

/* Call on every main-loop iteration (preferably every 1-5 ms). */
void button_handle(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t i;
    for (i = 0; i < (uint32_t)BUTTON_COUNT; i++) {
        ButtonState *b = &s_buttons[i];
        uint8_t sampled = read_down(b);
        if (sampled != b->candidate_down) {
            b->candidate_down = sampled;
            b->changed_ms = now;
        }
        if (b->stable_down != b->candidate_down &&
            (uint32_t)(now - b->changed_ms) >= BUTTON_DEBOUNCE_MS) {
            b->stable_down = b->candidate_down;
            if (b->stable_down) {
                b->pressed_event = 1U;
            } else {
                b->released_event = 1U;
            }
        }
    }
}

uint8_t Button_IsPressed(ButtonId id)
{
    return ((unsigned)id < (unsigned)BUTTON_COUNT) ? s_buttons[id].stable_down : 0U;
}

uint8_t Button_TakePressedEvent(ButtonId id)
{
    uint8_t flag;
    if ((unsigned)id >= (unsigned)BUTTON_COUNT) return 0U;
    flag = s_buttons[id].pressed_event;
    s_buttons[id].pressed_event = 0U;
    return flag;
}

uint8_t Button_TakeReleasedEvent(ButtonId id)
{
    uint8_t flag;
    if ((unsigned)id >= (unsigned)BUTTON_COUNT) return 0U;
    flag = s_buttons[id].released_event;
    s_buttons[id].released_event = 0U;
    return flag;
}
