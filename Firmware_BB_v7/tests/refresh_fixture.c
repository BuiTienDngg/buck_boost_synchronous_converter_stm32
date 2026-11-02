/* Execute the production control code and the bundled ST TIM drivers
 * under an ARM instruction emulator. Peripheral/ADC/input stubs are explicit;
 * these are register-model tests, not measurements of a physical gate driver. */
#include "stm32f1xx_hal.h"
#include "power_stage.h"
#include "input_buttons.h"
#include <stddef.h>

static uint32_t now_ms, adc_seq=1U, raw[4];
static uint8_t adc_ok=1U, pressed, flash_saves;
static TIM_HandleTypeDef test_pwm,test_ctl;
volatile uint32_t result_count,failures,failed_lines[128],test_completed;
volatile uint32_t ripple_worst_vticks[3];
#define CHECK(x) do {++result_count;if (!(x)) failed_lines[failures++]=__LINE__;} while (0)

void *memset(void *p,int c,size_t n) {unsigned char *q=p;while(n--) *q++=(unsigned char)c;return p;}
void *memcpy(void *p,const void *v,size_t n) {unsigned char *q=p;const unsigned char *s=v;while(n--) *q++=*s++;return p;}
int strcmp(const char *a,const char *b) {while (*a && *a==*b) {++a;++b;}return (unsigned char)*a-(unsigned char)*b;}
uint32_t HAL_GetTick(void) {return now_ms;}
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *c,uint32_t *lat) {
    memset(c,0,sizeof(*c));c->APB1CLKDivider=RCC_HCLK_DIV2;c->APB2CLKDivider=RCC_HCLK_DIV1;*lat=2U;
}
uint32_t HAL_RCC_GetPCLK1Freq(void) {return 36000000U;}
uint32_t HAL_RCC_GetPCLK2Freq(void) {return 72000000U;}
void HAL_GPIO_Init(GPIO_TypeDef *p,GPIO_InitTypeDef *g) {(void)p;(void)g;}
void HAL_GPIO_WritePin(GPIO_TypeDef *p,uint16_t pin,GPIO_PinState on) {
    if (on==GPIO_PIN_SET) p->BSRR=pin;else p->BRR=pin;
}
void HAL_NVIC_SetPriority(IRQn_Type n,uint32_t a,uint32_t b) {(void)n;(void)a;(void)b;}
void HAL_NVIC_EnableIRQ(IRQn_Type n) {(void)n;}
uint8_t PSU_Flash_Load(PSU_Tuning *t,uint32_t *v,uint32_t *i) {(void)t;(void)v;(void)i;return 0U;}
uint8_t PSU_Flash_Save(const PSU_Tuning *t,uint32_t v,uint32_t i) {(void)t;(void)v;(void)i;++flash_saves;return 1U;}
uint8_t AnalogMonitor_GetFastRaw(uint32_t sums[4],uint32_t *seq) {memcpy(sums,raw,sizeof(raw));*seq=adc_seq;return adc_ok;}
uint8_t Button_TakePressedEvent(ButtonId b) {if (b==BUTTON_1 && pressed) {pressed=0U;return 1U;}return 0U;}
int32_t Encoder_TakeDelta(void) {return 0;}

#ifdef BASELINE
#include "baseline_power_stage.c"
#else
#include "../Core/Src/power_stage.c"
#endif

static void prepare(void) {
    TIM_OC_InitTypeDef oc={0};
    memset(TIM1,0,sizeof(*TIM1));memset(TIM3,0,sizeof(*TIM3));
    memset(DMA1,0,sizeof(*DMA1));memset(DMA1_Channel5,0,sizeof(*DMA1_Channel5));
    memset(GPIOA,0,sizeof(*GPIOA));memset(GPIOC,0,sizeof(*GPIOC));
    memset(&test_pwm,0,sizeof(test_pwm));memset(&test_ctl,0,sizeof(test_ctl));
    test_pwm.Instance=TIM1;test_pwm.Init.Period=3599U;
    test_pwm.Init.CounterMode=TIM_COUNTERMODE_UP;
    test_pwm.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_ENABLE;
    CHECK(HAL_TIM_Base_Init(&test_pwm)==HAL_OK);
    CHECK(HAL_TIM_PWM_Init(&test_pwm)==HAL_OK);
    oc.OCMode=TIM_OCMODE_PWM1;oc.OCPolarity=TIM_OCPOLARITY_HIGH;
    CHECK(HAL_TIM_PWM_ConfigChannel(&test_pwm,&oc,TIM_CHANNEL_1)==HAL_OK);
    CHECK(HAL_TIM_PWM_ConfigChannel(&test_pwm,&oc,TIM_CHANNEL_2)==HAL_OK);
    test_ctl.Instance=TIM3;test_ctl.Init.Prescaler=71U;test_ctl.Init.Period=499U;
    test_ctl.Init.AutoReloadPreload=TIM_AUTORELOAD_PRELOAD_ENABLE;
    CHECK(HAL_TIM_Base_Init(&test_ctl)==HAL_OK);
    now_ms=0U;pressed=0U;adc_ok=1U;adc_seq=1U;
    raw[0]=raw[1]=raw[2]=0U;raw[3]=20306U;
}
#ifndef BASELINE
static float absf(float x) {return x<0.0f?-x:x;}
static void drive_tests(void) {
    float a,b,q;uint32_t k;
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    CHECK(s_diag.result==PSU_INIT_OK && s_diag.pwm_hz==20000U && s_diag.control_hz==2000U);
    CHECK((GPIOA->ODR & GPIO_PIN_10)==0U);
    CHECK(s_period==3600U && s_min_ccr==216U && s_max_ccr==3384U);
    CHECK(!(TIM1->DIER & TIM_DIER_UDE));
    CHECK(!(DMA1_Channel5->CCR & DMA_CCR_EN));
    CHECK(paired_duties(100.0f/3.0f,94.0f,&a,&b,&q));
    CHECK(absf(a+b-100.0f)<0.0001f && absf(a/b-0.5f)<0.0001f);
    CHECK(paired_duties(200.0f/3.0f,94.0f,&a,&b,&q));
    CHECK(absf(a+b-100.0f)<0.0001f && absf(a/b-2.0f)<0.0001f);
    CHECK(paired_duties(50.0f,94.0f,&a,&b,&q) && a==50.0f && b==50.0f);
    CHECK(paired_duties(55.0f,40.0f,&a,&b,&q) && a==40.0f && b==60.0f && q==40.0f);
    CHECK(!paired_duties(5.0f,94.0f,&a,&b,&q));
    CHECK(paired_duties(99.0f,94.0f,&a,&b,&q) && a==94.0f && b==6.0f);
    CHECK(mm_mode(PSU_BUCK,24.0f,21.9f)==PSU_BUCK);
    CHECK(mm_mode(PSU_BUCK,24.0f,22.2f)==PSU_BB);
    CHECK(mm_mode(PSU_BB,24.0f,21.9f)==PSU_BB);
    CHECK(mm_mode(PSU_BB,24.0f,20.0f)==PSU_BUCK);
    CHECK(mm_mode(PSU_BOOST,12.0f,13.1f)==PSU_BOOST);
    CHECK(mm_mode(PSU_BOOST,12.0f,12.8f)==PSU_BB);
    s_ui.state=PSU_ON;pwm_write(92.0f,8.0f);
    CHECK(s_drive_on && !s_cmd_valid && s_precharge_ticks==4U);
    CHECK(GPIOA->ODR & GPIO_PIN_10);
    CHECK(TIM1->CCR1==0U && TIM1->CCR2==0U);
    for(k=0U;k<4U;k++) {pwm_write(92.0f,8.0f);CHECK(!s_cmd_valid);}
    pwm_write(92.0f,8.0f);CHECK(s_cmd_valid);
    CHECK(TIM1->CCR1==3312U && TIM1->CCR2==288U);
    CHECK(!(TIM1->CR1 & TIM_CR1_UDIS));
    for(k=0U;k<40U;k++) {
        pwm_write(92.0f,8.0f);
        CHECK(TIM1->CCR1==3312U && TIM1->CCR2==288U);
    }
    pwm_write(12.3456f,87.6544f);
    CHECK(TIM1->CCR1+TIM1->CCR2==3600U);
    CHECK(TIM1->CCR1>=216U && TIM1->CCR2>=216U);
    PowerStage_OffNow();CHECK(!(GPIOA->ODR & GPIO_PIN_10) && !s_cmd_valid && s_ui.state==PSU_OFF);
    s_ui.state=PSU_ON;pwm_write(25.0f,75.0f);
    CHECK(!s_cmd_valid && s_precharge_ticks==4U && TIM1->CCR1==0U && TIM1->CCR2==0U);
    PowerStage_OffNow();CHECK(!(GPIOA->ODR & GPIO_PIN_10));
}
static void fault_tests(void) {
    prepare();TIM3->ARR=999U;
    CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR && s_diag.result==PSU_INIT_FREQUENCY);
    CHECK(s_diag.control_hz==1000U && !(GPIOA->ODR & GPIO_PIN_10));
    CHECK(__get_PRIMASK()==0U);
    now_ms=300U;PowerStage_Handle();CHECK(!(GPIOC->ODR & GPIO_PIN_14));
    now_ms=500U;PowerStage_Handle();CHECK(GPIOC->ODR & GPIO_PIN_14);
    pressed=1U;PowerStage_Handle();CHECK(s_ui.state==PSU_FAULT && !(GPIOC->ODR & GPIO_PIN_14));
    now_ms=1000U;PowerStage_Handle();CHECK(!(GPIOC->ODR & GPIO_PIN_14));
    pressed=1U;PowerStage_Handle();CHECK(s_ui.state==PSU_FAULT && !(GPIOA->ODR & GPIO_PIN_10));
    prepare();TIM1->CCMR1=(TIM1->CCMR1 & ~TIM_CCMR1_OC2M)|(TIM_OCMODE_PWM2<<8U);
    CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR && s_diag.result==PSU_INIT_PWM_MODE);
    prepare();TIM1->CCER|=TIM_CCER_CC2P;
    CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR && s_diag.result==PSU_INIT_TIMER_CONFIG);
    prepare();TIM3->CR1|=TIM_COUNTERMODE_CENTERALIGNED1;
    CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR && s_diag.result==PSU_INIT_TIMER_CONFIG);
    prepare();test_pwm.ChannelState[0]=HAL_TIM_CHANNEL_STATE_BUSY;
    CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR && s_diag.result==PSU_INIT_PWM_CH1);
    prepare();test_pwm.ChannelState[1]=HAL_TIM_CHANNEL_STATE_BUSY;
    CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR && s_diag.result==PSU_INIT_PWM_CH2);
    CHECK(!(TIM1->BDTR & TIM_BDTR_MOE) && !(GPIOA->ODR & GPIO_PIN_10));
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    s_ui.state=PSU_ON;s_vref_ramped=12.0f;raw[0]=52456U;adc_seq=2U;
    PowerStage_TIM3_Callback(&test_ctl);CHECK(s_ui.state==PSU_FAULT && s_ui.error==PSU_ERR_OVP);
    CHECK(!(GPIOA->ODR & GPIO_PIN_10));
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    s_ui.state=PSU_ON;s_first=1U;s_v_filter=0.0f;s_i_filter=0.0f;s_vin_filter=12.0f;
    s_vref_ramped=12.0f;raw[1]=22709U;adc_seq=2U; /* 6.1A at 1mOhm x100 */
    PowerStage_TIM3_Callback(&test_ctl);
    CHECK(s_ui.state==PSU_FAULT && s_ui.error==PSU_ERR_OCP && s_i_filter<6.0f);
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    s_ui.state=PSU_ON;adc_ok=0U;PowerStage_TIM3_Callback(&test_ctl);
    CHECK(s_ui.error==PSU_ERR_ADC && !(GPIOA->ODR & GPIO_PIN_10));
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    s_ui.state=PSU_ON;PowerStage_TIM3_Callback(&test_ctl);
    {uint32_t k;for(k=0U;k<=PSU_ADC_STALE_TICKS;k++) PowerStage_TIM3_Callback(&test_ctl);}
    CHECK(s_ui.state==PSU_FAULT && s_ui.error==PSU_ERR_ADC);
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    PowerStage_ControlStartFailed();CHECK(s_diag.result==PSU_INIT_CONTROL_START && !s_hw_ok);
}
static void control_tests(void) {
    uint32_t k;float before,cc_debt;
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    PowerStage_TIM3_Callback(&test_ctl);pressed=1U;PowerStage_Handle();
    CHECK(s_ui.state==PSU_ON && !(GPIOA->ODR & GPIO_PIN_10));
    for (k=1U;k<=2000U;k++) {
        ++adc_seq;PowerStage_TIM3_Callback(&test_ctl);
    }
    CHECK(s_ui.state==PSU_ON && absf(s_vref_ramped-2.0f)<0.0001f);
    CHECK(s_drive_on && s_cmd_valid && s_ui.running_seconds==1U);
    before=s_vref_ramped;
    CHECK(PowerStage_SetParam("VSET_MV",8000U));
    CHECK(s_vref_ramped==before);
    s_vref_ramped=5.0f;s_outer_count=0U;
    CHECK(PowerStage_SetParam("VSET_MV",3000U));
    for (k=1U;k<=200U;k++) {
        ++adc_seq;PowerStage_TIM3_Callback(&test_ctl);
    }
    CHECK(absf(s_vref_ramped-4.2f)<0.0001f);
    /* Controlled boundary crossing: retain virtual duty and CC integral. */
    s_ui.mode=PSU_BUCK;s_vin_filter=12.0f;s_v_filter=11.1f;s_i_filter=1.0f;
    s_vref_ramped=11.5f;s_outer_count=0U;s_duty=40.0f;s_cc_int=5.0f;
    s_drive_on=1U;s_cmd_valid=1U;s_precharge_ticks=0U;
    s_cv_target=40.0f-100.0f*11.5f/23.5f+5.0f;
    before=s_duty;cc_debt=s_cc_int;
    PowerStage_TIM3_Callback(&test_ctl);
    CHECK(s_ui.mode==PSU_BB && absf(s_duty-before)<0.0001f);
    CHECK(s_cc_int==cc_debt && s_cmd_valid);
    CHECK(PowerStage_SetParam("CV_KP",2000U));
    CHECK(absf(s_cv_int-(s_duty-s_last_ff+s_last_cc-0.8f))<0.0001f && s_cc_int==cc_debt);
    s_i_filter=2.0f;before=s_duty;PowerStage_TIM3_Callback(&test_ctl);
    CHECK(s_duty<before-1.0f && s_cc_int>cc_debt);
    s_v_filter=13.0f;PowerStage_TIM3_Callback(&test_ctl);
    CHECK(!s_drive_on && !(GPIOA->ODR & GPIO_PIN_10));
    /* Flash saves remain deferred while ON, then commit once after OFF. */
    flash_saves=0U;PowerStage_RequestSave();PowerStage_StoragePoll();CHECK(flash_saves==0U);
    PowerStage_OffNow();PowerStage_StoragePoll();CHECK(flash_saves==1U);
    CHECK(PowerStage_TakeSaveEvent()==1U && PowerStage_TakeSaveEvent()==0U);

}
static void current_scale_tests(void) {
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    CHECK(ANALOG_SHUNT_OHMS==0.001f && ANALOG_CURRENT_AMP_GAIN==100.0f);
    raw[1]=3723U;adc_seq=2U;PowerStage_TIM3_Callback(&test_ctl);
    CHECK(absf(PowerStage_GetStatus().iout_a-1.0f)<0.001f);
    s_first=0U;raw[1]=18614U;adc_seq=3U;PowerStage_TIM3_Callback(&test_ctl);
    CHECK(absf(PowerStage_GetStatus().iout_a-5.0f)<0.001f);
    /* ADC sums bracket 6A by one count: preserve the physical OCP limit. */
    s_ui.state=PSU_ON;s_first=0U;raw[1]=22336U;adc_seq=4U;
    PowerStage_TIM3_Callback(&test_ctl);
    CHECK(s_ui.state==PSU_ON && s_i_filter<6.0f && s_i_filter>5.9f);
    raw[1]=22337U;adc_seq=5U;PowerStage_TIM3_Callback(&test_ctl);
    CHECK(s_ui.state==PSU_FAULT && s_ui.error==PSU_ERR_OCP);
    CHECK(!(GPIOA->ODR & GPIO_PIN_10));
}
static void ripple_tests(void) {
    static const uint16_t inputs[3]={24U,12U,24U};
    static const uint16_t outputs[3]={12U,24U,22U};
    uint32_t point,k;
    float a,b,q;
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    s_ui.state=PSU_ON;s_drive_on=1U;s_cmd_valid=1U;s_precharge_ticks=0U;
    for(point=0U;point<3U;point++) {
        uint32_t worst=0U;uint16_t first_a=0U,first_b=0U;
        q=100.0f*outputs[point]/(inputs[point]+outputs[point]);
        CHECK(paired_duties(q,94.0f,&a,&b,&q));
        for(k=0U;k<40U;k++) {
            int32_t e;uint32_t magnitude;
            pwm_write(a,b);
            if(k==0U) {first_a=(uint16_t)TIM1->CCR1;first_b=(uint16_t)TIM1->CCR2;}
            CHECK(TIM1->CCR1==first_a && TIM1->CCR2==first_b);
            CHECK(TIM1->CCR1+TIM1->CCR2==s_period);
            e=(int32_t)inputs[point]*TIM1->CCR1-(int32_t)outputs[point]*TIM1->CCR2;
            magnitude=(uint32_t)(e<0?-e:e);if(magnitude>worst) worst=magnitude;
        }
        ripple_worst_vticks[point]=worst;
        CHECK(worst<=2U*(inputs[point]+outputs[point]));
    }
}

#endif
int main(void) {
#ifdef BASELINE
    prepare();CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_OK);
    prepare();TIM3->ARR=999U;CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR);
    prepare();TIM1->CCMR1=(TIM1->CCMR1 & ~TIM_CCMR1_OC2M)|(TIM_OCMODE_PWM2<<8U);
    CHECK(PowerStage_Init(&test_pwm,&test_ctl)==HAL_ERROR);
#else
    drive_tests();fault_tests();control_tests();current_scale_tests();ripple_tests();
#endif
    test_completed=0x50535554U;
    __asm volatile("bkpt #0");
    for (;;) {}
}
