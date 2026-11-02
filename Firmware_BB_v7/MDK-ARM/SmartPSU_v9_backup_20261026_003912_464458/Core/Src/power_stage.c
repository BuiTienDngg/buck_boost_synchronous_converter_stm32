#include "power_stage.h"
#include "analog_monitor.h"
#include "input_buttons.h"
#include "input_encoder.h"
#include <string.h>

static TIM_HandleTypeDef *s_pwm;
static TIM_HandleTypeDef *s_t3;
static volatile PSU_Status s_ui;
static volatile uint8_t s_ready;
static volatile uint32_t s_ticks;
static uint32_t s_last_adc_seq, s_adc_age_ticks;
static float s_v_filter, s_i_filter, s_vin_filter;
static uint8_t s_first;
static float s_cv_int, s_cc_int, s_duty;
static float s_cv_target;
static uint16_t s_outer_count;
static uint32_t s_run_ticks;

static float clampf(float x, float lo, float hi) { return x<lo?lo:(x>hi?hi:x); }
static uint16_t clampi(int32_t x, uint16_t lo, uint16_t hi) {
    return (uint16_t)(x<(int32_t)lo?lo:(x>(int32_t)hi?hi:x));
}
static uint32_t ccr(float percent) {
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(s_pwm) + 1U;
    float c = (float)period * clampf(percent, 0.0f, 97.0f) * 0.01f;
    return (uint32_t)(c + 0.5f);
}
static void pwm_write(float a, float b) {
    __HAL_TIM_SET_COMPARE(s_pwm, TIM_CHANNEL_1, ccr(a));
    __HAL_TIM_SET_COMPARE(s_pwm, TIM_CHANNEL_2, ccr(b));
    s_ui.duty_buck_pct=a;
    s_ui.duty_boost_pct=b;
}
static void pwm_zero(void) {
    __HAL_TIM_SET_COMPARE(s_pwm, TIM_CHANNEL_1, 0U);
    __HAL_TIM_SET_COMPARE(s_pwm, TIM_CHANNEL_2, 0U);
    s_ui.duty_buck_pct=0.0f;
    s_ui.duty_boost_pct=0.0f;
    s_ui.duty_applied_pct=0.0f;
}
static void reset_pi(void) {
    s_cv_int=0.0f; s_cv_target=PSU_DUTY_MIN;
    s_cc_int=0.0f; s_duty=0.0f;
    s_outer_count=0U; s_first=0U;
}
void PowerStage_OffNow(void) {
    if (!s_ready) return;
    pwm_zero();
    if (s_ui.state==PSU_ON) s_ui.state=PSU_OFF;
    reset_pi();
    /* PA10 deliberately remains HIGH per hardware requirement. */
}
static void trip(PSU_Error e) {
    pwm_zero();
    reset_pi();
    s_ui.error=e;
    s_ui.state=PSU_FAULT;
}
static uint32_t timer_hz(TIM_HandleTypeDef *t, uint8_t apb2) {
    RCC_ClkInitTypeDef cfg;
    uint32_t lat, clk, div;
    HAL_RCC_GetClockConfig(&cfg,&lat);
    clk=apb2 ? HAL_RCC_GetPCLK2Freq() : HAL_RCC_GetPCLK1Freq();
    if (apb2 ? cfg.APB2CLKDivider != RCC_HCLK_DIV1 : cfg.APB1CLKDivider != RCC_HCLK_DIV1) clk*=2U;
    div=(t->Instance->PSC+1U)*(t->Instance->ARR+1U);
    return div ? clk/div : 0U;
}
HAL_StatusTypeDef PowerStage_Init(TIM_HandleTypeDef *pwm,TIM_HandleTypeDef *timer) {
    GPIO_InitTypeDef g={0};
    s_ready=0U;
    memset((void*)&s_ui,0,sizeof(s_ui));
    s_ui.state=PSU_OFF;
    s_ui.vset_cv=500U;
    s_ui.iset_ca=100U;
    s_ui.digit=2U; /* 0.1V */
    s_ui.editing=PSU_EDIT_V;
    if (!pwm || !timer || pwm->Instance!=TIM1 || timer->Instance!=TIM3 ||
        timer_hz(pwm,1U)!=PSU_PWM_HZ || timer_hz(timer,0U)!=PSU_CONTROL_HZ ||
        pwm->Init.CounterMode!=TIM_COUNTERMODE_UP ||
        (pwm->Instance->CCMR1 & TIM_CCMR1_OC1M)!=TIM_OCMODE_PWM1 ||
        (pwm->Instance->CCMR1 & TIM_CCMR1_OC2M)!=(TIM_OCMODE_PWM1<<8U)) {
        s_ui.state=PSU_FAULT; s_ui.error=PSU_ERR_TIMERS;
        return HAL_ERROR;
    }
    s_pwm=pwm; s_t3=timer;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,GPIO_PIN_RESET);
    g.Pin=GPIO_PIN_10; g.Mode=GPIO_MODE_OUTPUT_PP;
    g.Speed=GPIO_SPEED_FREQ_HIGH; g.Pull=GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA,&g);
    pwm_zero(); reset_pi();
    s_ticks=0U; s_last_adc_seq=0U; s_adc_age_ticks=PSU_ADC_STALE_TICKS+1U;
    s_run_ticks=0U; s_first=0U;
    if (HAL_TIM_PWM_Start(s_pwm,TIM_CHANNEL_1)!=HAL_OK ||
        HAL_TIM_PWM_Start(s_pwm,TIM_CHANNEL_2)!=HAL_OK) {
        HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,GPIO_PIN_RESET);
        s_ui.state=PSU_FAULT; s_ui.error=PSU_ERR_TIMERS;
        return HAL_ERROR;
    }
    /* /SD stays HIGH from this point onward, even for OFF and FAULT.
       With IR2184, this actively drives LO when IN=LOW. */
    HAL_GPIO_WritePin(GPIOA,GPIO_PIN_10,GPIO_PIN_SET);
    s_ready=1U;
    return HAL_OK;
}

/* Called exclusively by TIM3 update IRQ @ 2kHz. No HAL polling, printf,
 * drawing, or HAL_Delay. Reads the latest completed DMA 30-scan averages.
 * Fast filter settles in a few milliseconds; main's slower IIR is for UI.
 */
void PowerStage_TIM3_Callback(TIM_HandleTypeDef *htim) {
    uint32_t sums[4],seq;
    float v,i,vin,vset,iset,ff,delta,base,err,cc_over,cc_reduction,command;
    PSU_Mode mode;
    if (!s_ready || htim!=s_t3 || htim->Instance!=TIM3) return;
    ++s_ticks;
    if (!AnalogMonitor_GetFastRaw(sums,&seq)) { if (s_ui.state==PSU_ON) trip(PSU_ERR_ADC); return; }
    if (seq!=s_last_adc_seq) {
        s_last_adc_seq=seq; s_adc_age_ticks=0U;
        /* 3.3 / (4095 * 30) => raw ADC count sums converted to volts.
           1mOhm shunt and gain200 => 0.2V per amp. */
        v=(float)sums[0]*(3.3f/(4095.0f*30.0f))*22.0f;
        i=(float)sums[1]*(3.3f/(4095.0f*30.0f))/0.2f;
        vin=(float)sums[3]*(3.3f/(4095.0f*30.0f))*22.0f;
        if (!s_first) {s_v_filter=v; s_i_filter=i; s_vin_filter=vin; s_first=1U;}
        else {
            s_v_filter+=0.15f*(v-s_v_filter);
            s_i_filter+=0.15f*(i-s_i_filter);
            s_vin_filter+=0.15f*(vin-s_vin_filter);
        }
        s_ui.vout_v=s_v_filter;
        s_ui.iout_a=s_i_filter;
        s_ui.vin_v=s_vin_filter;
    } else if (s_adc_age_ticks<0xFFFFFFFFU) ++s_adc_age_ticks;
    if (s_ui.state!=PSU_ON) return;
    ++s_run_ticks;
    if ((s_run_ticks%PSU_CONTROL_HZ)==0U) ++s_ui.running_seconds;
    if (s_adc_age_ticks>PSU_ADC_STALE_TICKS) {trip(PSU_ERR_ADC);return;}
    if (!(s_vin_filter>=PSU_VIN_MIN_V && s_vin_filter<=PSU_VIN_MAX_V)) {trip(PSU_ERR_VIN);return;}
    if (!(s_v_filter>=0.0f && s_v_filter<PSU_OVP_V)) {trip(PSU_ERR_OVP);return;}
    if (!(s_i_filter>=-0.5f && s_i_filter<PSU_OCP_A)) {trip(PSU_ERR_OCP);return;}
    vset=(float)s_ui.vset_cv*0.01f;
    iset=(float)s_ui.iset_ca*0.01f;
    mode=s_ui.mode;
    if (mode==PSU_BUCK && vset>s_vin_filter+0.8f) mode=PSU_BOOST;
    else if (mode==PSU_BOOST && vset<s_vin_filter-0.4f) mode=PSU_BUCK;
    if (mode!=s_ui.mode) { s_ui.mode=mode; reset_pi(); pwm_zero(); }
    if (++s_outer_count>=10U) { /* 200 Hz voltage loop */
        s_outer_count=0U;
        if (mode==PSU_BUCK) ff=97.0f*vset/clampf(s_vin_filter,1.0f,100.0f);
        else ff=100.0f-97.0f*s_vin_filter/clampf(vset,1.0f,100.0f);
        err=vset-s_v_filter;
        base=ff+PSU_CV_KP*err;
        /* Freeze positive CV integral when current loop is active.
         * Otherwise voltage PI would wind up against CC regulation. */
        if (s_cc_int<0.01f || err<0.0f)
            s_cv_int=clampf(s_cv_int+PSU_CV_KI*0.005f*err,-30.0f,30.0f);
        s_cv_target=clampf(base+s_cv_int,PSU_DUTY_MIN,PSU_DUTY_MAX);
        /* simple conditional integrator anti-windup at CV saturation */
        if ((base+s_cv_int>=PSU_DUTY_MAX && err>0.0f) ||
            (base+s_cv_int<=PSU_DUTY_MIN && err<0.0f))
            s_cv_int=clampf(s_cv_int-PSU_CV_KI*0.005f*err,-30.0f,30.0f);
    }
    /* Fast current PI removes duty from CV request as current passes Iset.
     * Below Iset the penalty integral unwinds toward zero. */
    cc_over=s_i_filter-iset;
    s_cc_int=clampf(s_cc_int+PSU_CC_KI*0.0005f*cc_over,
                    0.0f,PSU_DUTY_MAX);
    cc_reduction=clampf(PSU_CC_KP*(cc_over>0.0f?cc_over:0.0f)+s_cc_int,
                        0.0f,PSU_DUTY_MAX);
    command=clampf(s_cv_target-cc_reduction,0.0f,PSU_DUTY_MAX);
    /* Since only IOUT (not inductor current) is measured, under a very
     * light load the voltage loop must be able to stop energy injection.
     * This is an additional CV clamp, not a hardware OVP substitute. */
    if (s_v_filter > vset + 0.25f) {
        pwm_zero(); s_duty=0.0f;
        return;
    }
    /* Soft ramp UP only. Ramp down immediately on CV/CC limit. */
    delta=PSU_DUTY_RAMP_PCT_PER_S/PSU_CONTROL_HZ;
    if (command > s_duty+delta) s_duty+=delta;
    else s_duty=command;
    s_duty=clampf(s_duty,0.0f,PSU_DUTY_MAX);
    if (s_duty < PSU_DUTY_MIN) {
        pwm_zero(); /* no energy injection below 3%, in BOTH modes */
    } else {
        if (mode==PSU_BUCK) pwm_write(s_duty,97.0f);
        else pwm_write(97.0f,100.0f-s_duty);
        s_ui.duty_applied_pct=s_duty;
    }
}

void PowerStage_Handle(void) {
    static const uint16_t vst[4]={1000U,100U,10U,1U};
    static const uint16_t ist[3]={100U,10U,1U};
    int32_t change,step,next;
    uint32_t mask;
    if (!s_ready) return;
    if (Button_TakePressedEvent(BUTTON_1)) {
        mask=__get_PRIMASK();__disable_irq();
        if (s_ui.state==PSU_ON) PowerStage_OffNow();
        else if (s_ui.state==PSU_OFF) {
            if (s_adc_age_ticks<PSU_ADC_STALE_TICKS &&
                s_vin_filter>=PSU_VIN_MIN_V && s_vin_filter<=PSU_VIN_MAX_V &&
                s_v_filter>=0.0f && s_v_filter<PSU_OVP_V &&
                s_i_filter>=-0.5f && s_i_filter<PSU_OCP_A) {
                reset_pi(); pwm_zero(); s_ui.state=PSU_ON;
            }
        }
        if (mask==0U) __enable_irq();
    }
    if (Button_TakePressedEvent(BUTTON_2)) {
        s_ui.editing=(s_ui.editing==PSU_EDIT_V)?PSU_EDIT_I:PSU_EDIT_V;
        s_ui.digit=(s_ui.editing==PSU_EDIT_V)?2U:1U;
    }
    if (Button_TakePressedEvent(BUTTON_ENCODER_SWITCH)) {
        if (s_ui.editing==PSU_EDIT_V) s_ui.digit=(uint8_t)((s_ui.digit+1U)%4U);
        else s_ui.digit=(uint8_t)((s_ui.digit+1U)%3U);
    }
    change=-Encoder_TakeDelta(); /* reverse current EC-11 direction */
    if (change!=0) {
        step=(int32_t)((s_ui.editing==PSU_EDIT_V)?vst[s_ui.digit]:ist[s_ui.digit]);
        mask=__get_PRIMASK();__disable_irq();
        if (s_ui.editing==PSU_EDIT_V) {
            next=(int32_t)s_ui.vset_cv + change*step;
            s_ui.vset_cv=clampi(next,PSU_VSET_MIN_CV,PSU_VSET_MAX_CV);
        } else {
            next=(int32_t)s_ui.iset_ca + change*step;
            s_ui.iset_ca=clampi(next,PSU_ISET_MIN_CA,PSU_ISET_MAX_CA);
        }
        if (mask==0U) __enable_irq();
    }
    /* BTN3 clears latched error, must press BTN1 separately to restart. */
    if (Button_TakePressedEvent(BUTTON_3) && s_ui.state==PSU_FAULT) {
        mask=__get_PRIMASK();__disable_irq();
        pwm_zero();reset_pi();s_ui.error=PSU_ERR_NONE;s_ui.state=PSU_OFF;
        if (mask==0U) __enable_irq();
    }
}
PSU_Status PowerStage_GetStatus(void) {
    PSU_Status r;
    uint32_t primask=__get_PRIMASK();
    __disable_irq();
    r.state=s_ui.state;r.mode=s_ui.mode;r.editing=s_ui.editing;
    r.error=s_ui.error;r.vset_cv=s_ui.vset_cv;r.iset_ca=s_ui.iset_ca;
    r.digit=s_ui.digit;r.duty_buck_pct=s_ui.duty_buck_pct;
    r.duty_boost_pct=s_ui.duty_boost_pct;r.duty_applied_pct=s_ui.duty_applied_pct;
    r.vout_v=s_ui.vout_v;r.iout_a=s_ui.iout_a;r.vin_v=s_ui.vin_v;
    r.running_seconds=s_ui.running_seconds;
    if (primask==0U) __enable_irq();
    return r;
}
