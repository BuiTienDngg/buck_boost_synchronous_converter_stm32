#include "button.h"


static uint8_t button_state[BUTTON_NUM];
static uint8_t button_event[BUTTON_NUM];

static uint32_t button_tick[BUTTON_NUM];


void Button_Init(void)
{

    for(uint8_t i=0;i<BUTTON_NUM;i++)
    {
        button_state[i]=0;
        button_event[i]=0;
        button_tick[i]=0;
    }

}


static void Button_Update(uint8_t index,GPIO_TypeDef *GPIOx,uint16_t GPIO_Pin)
{

    uint32_t tick = HAL_GetTick();

    if((tick - button_tick[index]) < DEBOUNCE_TIME)
        return;

    button_tick[index] = tick;


    if(HAL_GPIO_ReadPin(GPIOx,GPIO_Pin)==GPIO_PIN_RESET)
    {
        if(button_state[index]==0)
        {
            button_state[index]=1;
            button_event[index]=1;
        }
    }
    else
    {
        button_state[index]=0;
    }

}



void Button_EXTI_Callback(uint16_t GPIO_Pin)
{

    switch(GPIO_Pin)
    {

        case GPIO_PIN_5:
            Button_Update(BTN_PB5,GPIOB,GPIO_PIN_5);
            break;


        case GPIO_PIN_6:
            Button_Update(BTN_PB6,GPIOB,GPIO_PIN_6);
            break;


        case GPIO_PIN_7:
            Button_Update(BTN_PB7,GPIOB,GPIO_PIN_7);
            break;


        case GPIO_PIN_8:
            Button_Update(BTN_PB8,GPIOB,GPIO_PIN_8);
            break;


        case GPIO_PIN_9:
            Button_Update(BTN_PB9,GPIOB,GPIO_PIN_9);
            break;


        default:
            break;
    }

}


uint8_t Button_IsPressed(BUTTON_t button)
{
    return button_state[button];
}



uint8_t Button_GetEvent(BUTTON_t button)
{

    if(button_event[button])
    {
        button_event[button]=0;
        return 1;
    }

    return 0;

}
