#include "power_stage.h"
#include "analog_monitor.h"
#include "input_buttons.h"
#include "input_encoder.h"
#include "psu_flash.h"
#include <string.h>

static TIM_HandleTypeDef *s_pwm, *s_t3;
static volatile PSU_Status s_ui;
static volatile PSU_Diagnostics s_diag;
static volatile uint8_t s_ready, s_hw_ok, s_drive_on;
static volatile uint32_t s_ticks;
static uint32_t s_last_adc_seq, s_adc_age_ticks;
static float s_v_filter, s_i_filter, s_vin_filter;
static uint8_t s_first;
static float s_cv_int, s_cc_int, s_duty, s_cv_target, s_vref_ramped;
static float s_last_ff, s_last_cc;
static uint16_t s_outer_count, s_precharge_ticks;
static uint32_t s_run_ticks;
static volatile PSU_Tuning s_cfg;
static volatile uint8_t s_cfg_dirty, s_save_requested, s_save_event;
static uint32_t s_change_at;
#ifndef PSU_LED_ACTIVE_LOW
#define PSU_LED_ACTIVE_LOW 1U
#endif
#ifndef PSU_BUZZER_ACTIVE_HIGH
#define PSU_BUZZER_ACTIVE_HIGH 1U
#endif
#define PSU_ALARM_PERIOD_MS 500U
#define PSU_ALARM_ON_MS     200U
static volatile uint32_t s_fault_since_ms;
static uint8_t s_alarm_muted;

/* Paired high-side PWM: buck IN duty D, boost IN duty 1-D.
 * Both TIM1 channels use PWM1 at 20kHz; no periodic pass-leg refresh.
 * Preloaded CCR1/CCR2 latch together. UDIS protects only the two stores.
 */
#define PI_MAX 30.0f
#define PRECHARGE_TICKS (PSU_CONTROL_HZ * PSU_PRECHARGE_MS / 1000U)
static volatile uint16_t s_cmd_a, s_cmd_b;
static volatile uint8_t s_cmd_valid;
static uint16_t s_period, s_min_ccr, s_max_ccr;

static const PSU_Tuning s_defaults = {
    1000U, 6000U, 5000U, 120000U,
    30000U, 6000U, 7000U, 40000U,
    28000U, 5000U, 970U, 2000U, 8000U
};
static float clampf(float x,float lo,float hi) {return x<lo?lo:(x>hi?hi:x);}
static uint16_t clampi(int32_t x,uint16_t lo,uint16_t hi) {
    return (uint16_t)(x<(int32_t)lo?lo:(x>(int32_t)hi?hi:x));
}
static float maxf(float a,float b) { return a>b?a:b; }
static void psu_indicators_write(uint8_t on,uint8_t beep) {
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_13,
        (on ^ PSU_LED_ACTIVE_LOW)?GPIO_PIN_SET:GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC,GPIO_PIN_14,
        (beep == PSU_BUZZER_ACTIVE_HIGH)?GPIO_PIN_SET:GPIO_PIN_RESET);
}
static void psu_indicators_init(void) {
    GPIO_InitTypeDef io={0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    psu_indicators_write(0U,0U);
    io.Pin=GPIO_PIN_13|GPIO_PIN_14; io.Mode=GPIO_MODE_OUTPUT_PP;
    io.Pull=GPIO_NOPULL; io.Speed=GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC,&io);
}
void PowerStage_EmergencyDisable(void) {
    /* Independent gate override, also safe before power-stage Init. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIOA->BRR=GPIO_PIN_10;
    s_drive_on=0U; s_cmd_valid=0U;
}
static void reset_ramp(void) {s_vref_ramped=0.0f;s_ui.vref_ramped_v=0.0f;}
static void reset_pi(void) {
    s_cv_int=0.0f;s_cc_int=0.0f;s_cv_target=0.0f;s_duty=0.0f;
    s_last_ff=0.0f;s_last_cc=0.0f;s_outer_count=0U;
}
static void pwm_zero(void) {
    PowerStage_EmergencyDisable();
    s_cmd_a=s_cmd_b=0U; s_precharge_ticks=0U;
    s_ui.duty_buck_pct=0.0f;s_ui.duty_boost_pct=0.0f;
    s_ui.duty_applied_pct=0.0f;
}
void PowerStage_OffNow(void) {
    uint32_t mask=__get_PRIMASK(); __disable_irq();
    pwm_zero();
    if (s_ready) {
        if (s_ui.state==PSU_ON) s_ui.state=PSU_OFF;
        reset_pi();reset_ramp();psu_indicators_write(0U,0U);
    }
    if (!mask) __enable_irq();
}
static void trip(PSU_Error e) {
    pwm_zero();reset_pi();reset_ramp();
    s_ui.error=e;s_ui.state=PSU_FAULT;s_fault_since_ms=HAL_GetTick();s_alarm_muted=0U;
    psu_indicators_write(0U,1U);
}
static uint32_t timer_clock(uint8_t apb2) {
    RCC_ClkInitTypeDef c;uint32_t lat,clk;
    HAL_RCC_GetClockConfig(&c,&lat);
    clk=apb2?HAL_RCC_GetPCLK2Freq():HAL_RCC_GetPCLK1Freq();
    if ((apb2?c.APB2CLKDivider:c.APB1CLKDivider)!=RCC_HCLK_DIV1) clk*=2U;
    return clk;
}
static uint32_t timer_hz(TIM_HandleTypeDef *t,uint8_t apb2) {
    uint64_t div=(uint64_t)(t->Instance->PSC+1U)*(t->Instance->ARR+1U);
    return div?(uint32_t)(timer_clock(apb2)/div):0U;
}
static void diag_capture(void) {
    s_diag.tim1_ccmr1=TIM1->CCMR1;s_diag.tim1_cr1=TIM1->CR1;
    s_diag.tim1_psc=TIM1->PSC;s_diag.tim1_arr=TIM1->ARR;
    s_diag.tim3_psc=TIM3->PSC;s_diag.tim3_arr=TIM3->ARR;
    if (s_pwm && s_pwm->Instance==TIM1) {
        s_diag.pwm_hz=timer_hz(s_pwm,1U);
        s_diag.pwm_ch1_state=HAL_TIM_GetChannelState(s_pwm,TIM_CHANNEL_1);
        s_diag.pwm_ch2_state=HAL_TIM_GetChannelState(s_pwm,TIM_CHANNEL_2);
    }
    if (s_t3 && s_t3->Instance==TIM3) s_diag.control_hz=timer_hz(s_t3,0U);
}
static HAL_StatusTypeDef init_failed(PSU_InitResult why) {
    diag_capture();s_diag.result=why;s_hw_ok=0U;
    trip(why==PSU_INIT_DMA?PSU_ERR_DMA:PSU_ERR_TIMERS);
    return HAL_ERROR;
}
void PowerStage_GetDiagnostics(PSU_Diagnostics *out) {
    uint32_t mask;
    if (!out) return;
    mask=__get_PRIMASK();__disable_irq();*out=s_diag;
    if (!mask) __enable_irq();
}
void PowerStage_ControlStartFailed(void) {
    (void)init_failed(PSU_INIT_CONTROL_START);
}

/* Schmitt bands also leave enough headroom for the 3us minimum LOW time. */
static PSU_Mode mm_mode(PSU_Mode old,float vin,float vout) {
    float x=vin-vout;
    float inner=maxf(0.75f,0.08f*vin),outer=maxf(3.50f,0.12f*vin);
    if (old==PSU_BUCK) return x<inner?PSU_BB:PSU_BUCK;
    if (old==PSU_BOOST) return x>-inner?PSU_BB:PSU_BOOST;
    if (x>outer) return PSU_BUCK;
    if (x<-outer) return PSU_BOOST;
    return PSU_BB;
}
static uint16_t ccr(float pct) {
    return (uint16_t)((float)s_period*clampf(pct,0.0f,100.0f)*0.01f+0.5f);
}
static uint8_t paired_duties(float command,float limit,float *a,float *b,float *real_q) {
    float minimum=100.0f*(float)s_min_ccr/(float)s_period;
    /* The same D drives buck HS and boost LS energy transfer. Both high
     * sides and both LOW intervals retain the 3us hardware minimum. */
    *a=clampf(command,0.0f,limit);*b=100.0f-*a;*real_q=*a;
    return *a>=minimum && *b>=minimum;
}
static void pwm_pair(uint16_t a,uint16_t b) {
    uint32_t cr1=TIM1->CR1;
    TIM1->CR1=cr1|TIM_CR1_UDIS;
    TIM1->CCR1=a;TIM1->CCR2=b;
    __DMB();
    TIM1->CR1=cr1;
}
/* Nonblocking initial charge, once each time the gate driver is re-enabled.
 * This is a start sequence, not a repeating 500Hz refresh schedule. */
static void pwm_precharge(void) {
    TIM1->CR1&=~TIM_CR1_CEN;
    s_cmd_valid=0U;s_cmd_a=s_cmd_b=0U;
    pwm_pair(0U,0U);TIM1->CNT=0U;TIM1->EGR=TIM_EGR_UG;TIM1->SR=0U;
    s_precharge_ticks=PRECHARGE_TICKS;
    s_drive_on=1U;TIM1->CR1|=TIM_CR1_CEN;
    GPIOA->BSRR=GPIO_PIN_10;
}
static void pwm_write(float a,float b) {
    (void)b;
    if (!s_drive_on) {pwm_precharge();return;}
    if (s_precharge_ticks) {--s_precharge_ticks;return;}
    s_cmd_a=ccr(a);
    if (s_cmd_a<s_min_ccr) s_cmd_a=s_min_ccr;
    if (s_cmd_a>s_max_ccr) s_cmd_a=s_max_ccr;
    /* Round only D; obtain 1-D by integer subtraction, so the pair sums
     * to exactly one carrier period even at half-tick boundaries. */
    s_cmd_b=s_period-s_cmd_a;pwm_pair(s_cmd_a,s_cmd_b);s_cmd_valid=1U;
    s_ui.duty_buck_pct=100.0f*(float)s_cmd_a/(float)s_period;
    s_ui.duty_boost_pct=100.0f*(float)s_cmd_b/(float)s_period;
}
HAL_StatusTypeDef PowerStage_Init(TIM_HandleTypeDef *pwm,TIM_HandleTypeDef *timer) {
    GPIO_InitTypeDef g={0};uint32_t clk;
    PowerStage_EmergencyDisable();s_ready=0U;s_hw_ok=0U;
    s_pwm=pwm;s_t3=timer;
    g.Pin=GPIO_PIN_10;g.Mode=GPIO_MODE_OUTPUT_PP;
    g.Speed=GPIO_SPEED_FREQ_LOW;g.Pull=GPIO_NOPULL;HAL_GPIO_Init(GPIOA,&g);
    psu_indicators_init();
    memset((void*)&s_ui,0,sizeof(s_ui));memset((void*)&s_diag,0,sizeof(s_diag));
    s_ui.state=PSU_OFF;s_ui.mode=PSU_BB;
    s_ui.vset_cv=500U;s_ui.iset_ca=100U;s_ui.digit=2U;s_ui.editing=PSU_EDIT_V;
    s_cfg=s_defaults;
    {PSU_Tuning stored;uint32_t v,i;
        if (PSU_Flash_Load(&stored,&v,&i) && v>=3000U && v<=stored.vset_max_mv &&
            i>=100U && i<=stored.iset_max_ma && v%10U==0U && i%10U==0U) {
            s_cfg=stored;s_ui.vset_cv=(uint16_t)(v/10U);s_ui.iset_ca=(uint16_t)(i/10U);
        }
    }
    s_cfg_dirty=s_save_requested=s_save_event=0U;s_change_at=HAL_GetTick();
    s_ticks=0U;s_last_adc_seq=0U;s_adc_age_ticks=PSU_ADC_STALE_TICKS+1U;
    s_run_ticks=0U;s_first=0U;
    reset_pi();reset_ramp();pwm_zero();
    s_alarm_muted=0U;
    s_ready=1U; /* Diagnostics, buttons, alarm and USB work on init failure. */
    if (!pwm || !timer || pwm->Instance!=TIM1 || timer->Instance!=TIM3)
        return init_failed(PSU_INIT_HANDLES);
    diag_capture();clk=timer_clock(1U);
    if ((uint64_t)PSU_PWM_HZ*(TIM1->PSC+1U)*(TIM1->ARR+1U)!=clk ||
        (uint64_t)PSU_CONTROL_HZ*(TIM3->PSC+1U)*(TIM3->ARR+1U)!=timer_clock(0U))
        return init_failed(PSU_INIT_FREQUENCY);
    /* OC2M is in bits 14:12. Normalize before comparing; keep validation. */
    if ((TIM1->CCMR1 & (TIM_CCMR1_CC1S|TIM_CCMR1_CC2S)) ||
        (TIM1->CCMR1 & TIM_CCMR1_OC1M)!=TIM_OCMODE_PWM1 ||
        ((TIM1->CCMR1 & TIM_CCMR1_OC2M)>>8U)!=TIM_OCMODE_PWM1)
        return init_failed(PSU_INIT_PWM_MODE);
    if (TIM1->PSC!=0U || TIM1->ARR>=65535U || TIM1->RCR!=0U ||
        (TIM1->CR1 & (TIM_CR1_DIR|TIM_CR1_CMS|TIM_CR1_UDIS)) ||
        (TIM3->CR1 & (TIM_CR1_DIR|TIM_CR1_CMS)) ||
        (TIM1->SMCR & (TIM_SMCR_SMS|TIM_SMCR_ECE)) ||
        (TIM3->SMCR & (TIM_SMCR_SMS|TIM_SMCR_ECE)) ||
        (TIM1->CCER & (TIM_CCER_CC1P|TIM_CCER_CC2P|TIM_CCER_CC1NE|TIM_CCER_CC2NE)) ||
        (TIM1->CCMR1 & (TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE))!=(TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE))
        return init_failed(PSU_INIT_TIMER_CONFIG);
    s_period=(uint16_t)(TIM1->ARR+1U);
    s_min_ccr=(uint16_t)((clk/1000000U)*PSU_MIN_PULSE_US);
    s_max_ccr=s_period-s_min_ccr;
    if (s_min_ccr==0U || s_period<=2U*s_min_ccr)
        return init_failed(PSU_INIT_TIMER_CONFIG);
    TIM1->CCR1=TIM1->CCR2=0U;TIM1->EGR=TIM_EGR_UG;
    if (HAL_TIM_PWM_Start(pwm,TIM_CHANNEL_1)!=HAL_OK) return init_failed(PSU_INIT_PWM_CH1);
    if (HAL_TIM_PWM_Start(pwm,TIM_CHANNEL_2)!=HAL_OK) {
        (void)HAL_TIM_PWM_Stop(pwm,TIM_CHANNEL_1);return init_failed(PSU_INIT_PWM_CH2);
    }
    /* Refresh DMA is no longer used. Carrier PWM comes directly from TIM1. */
    TIM1->DIER&=~TIM_DIER_UDE;
    /* Remain /SD LOW until a separate explicit start, healthy ADC and precharge. */
    s_hw_ok=1U;s_diag.result=PSU_INIT_OK;
    return HAL_OK;
}

void PowerStage_TIM3_Callback(TIM_HandleTypeDef *htim) {
    uint32_t sums[4],seq;
    float v,i,vin,vset,iset,ff,err,base,cc_over,cc_reduction,command,delta;
    PSU_Mode mode;
    if (!s_ready || !s_hw_ok || htim!=s_t3 || htim->Instance!=TIM3) return;
    ++s_ticks;
    if (!AnalogMonitor_GetFastRaw(sums,&seq)) {if (s_ui.state==PSU_ON) trip(PSU_ERR_ADC);return;}
    if (seq!=s_last_adc_seq) {
        s_last_adc_seq=seq;s_adc_age_ticks=0U;
        v=(float)sums[0]*(3.3f/(4095.0f*30.0f))*22.0f;
        i=(float)sums[1]*(3.3f/(4095.0f*30.0f))/
          (ANALOG_SHUNT_OHMS * ANALOG_CURRENT_AMP_GAIN);
        vin=(float)sums[3]*(3.3f/(4095.0f*30.0f))*22.0f;
        if (!s_first) {s_v_filter=v;s_i_filter=i;s_vin_filter=vin;s_first=1U;}
        else {s_v_filter+=0.15f*(v-s_v_filter);s_i_filter+=0.15f*(i-s_i_filter);s_vin_filter+=0.15f*(vin-s_vin_filter);}
        s_ui.vout_v=s_v_filter;s_ui.iout_a=s_i_filter;s_ui.vin_v=s_vin_filter;
        /* Unfiltered averages also trip: UI filtering must not hide OVP/OCP. */
        if (s_ui.state==PSU_ON) {
            if (!(v>=0.0f && v<(float)s_cfg.ovp_mv*0.001f)) {trip(PSU_ERR_OVP);return;}
            if (!(i>=-0.5f && i<(float)s_cfg.ocp_ma*0.001f)) {trip(PSU_ERR_OCP);return;}
            if (!(vin>=(float)s_cfg.vin_min_mv*0.001f && vin<=(float)s_cfg.vin_max_mv*0.001f)) {trip(PSU_ERR_VIN);return;}
        }
    } else if (s_adc_age_ticks<0xFFFFFFFFU) ++s_adc_age_ticks;
    if (s_ui.state!=PSU_ON) return;
    ++s_run_ticks;if (s_run_ticks%PSU_CONTROL_HZ==0U) ++s_ui.running_seconds;
    if (s_adc_age_ticks>PSU_ADC_STALE_TICKS) {trip(PSU_ERR_ADC);return;}
    if (!(s_vin_filter>=(float)s_cfg.vin_min_mv*0.001f && s_vin_filter<=(float)s_cfg.vin_max_mv*0.001f)) {trip(PSU_ERR_VIN);return;}
    if (!(s_v_filter>=0.0f && s_v_filter<(float)s_cfg.ovp_mv*0.001f)) {trip(PSU_ERR_OVP);return;}
    if (!(s_i_filter>=-0.5f && s_i_filter<(float)s_cfg.ocp_ma*0.001f)) {trip(PSU_ERR_OCP);return;}
    if (++s_outer_count>=10U) {
        float wanted=(float)s_ui.vset_cv*0.01f;
        float rise=(float)s_cfg.v_rise_mv_s*0.000005f,fall=(float)s_cfg.v_fall_mv_s*0.000005f;
        s_outer_count=0U;
        if (s_vref_ramped<wanted) s_vref_ramped+=clampf(wanted-s_vref_ramped,0.0f,rise);
        else s_vref_ramped-=clampf(s_vref_ramped-wanted,0.0f,fall);
        s_ui.vref_ramped_v=s_vref_ramped;
        err=s_vref_ramped-s_v_filter;base=(float)s_cfg.cv_kp_milli*0.001f*err;
        if ((s_cc_int<0.01f || err<0.0f) &&
            !((s_cv_target>=PI_MAX && err>0.0f)||(s_cv_target<=-PI_MAX && err<0.0f)))
            s_cv_int=clampf(s_cv_int+(float)s_cfg.cv_ki_milli*0.001f*0.005f*err,-PI_MAX,PI_MAX);
        s_cv_target=clampf(base+s_cv_int,-PI_MAX,PI_MAX);
    }
    vset=s_vref_ramped;iset=(float)s_ui.iset_ca*0.01f;
    ff=100.0f*vset/(maxf(s_vin_filter,0.2f)+vset);
    cc_over=s_i_filter-iset;
    s_cc_int=clampf(s_cc_int+(float)s_cfg.cc_ki_milli*0.001f*0.0005f*cc_over,0.0f,100.0f);
    cc_reduction=clampf((float)s_cfg.cc_kp_milli*0.001f*maxf(cc_over,0.0f)+s_cc_int,0.0f,100.0f);
    mode=mm_mode(s_ui.mode,s_vin_filter,s_v_filter);
    /* Mode labels retain hysteresis for UI; the D/(1-D) mapping and PI
     * coordinate are identical across labels, so no integral reset is needed. */
    s_ui.mode=mode;
    s_last_ff=ff;s_last_cc=cc_reduction;
    command=clampf(ff+s_cv_target-cc_reduction,0.0f,99.0f);
    if (s_v_filter>vset+0.25f || vset<0.30f || command<=0.1f) {
        pwm_zero();s_duty=0.0f;return;
    }
    delta=PSU_DUTY_RAMP_PCT_PER_S/PSU_CONTROL_HZ;
    s_duty=command>s_duty+delta?s_duty+delta:command;
    {
        float a,b,real_q;
        float limit=clampf((float)s_cfg.duty_max_permille*0.1f,PSU_DUTY_MIN,
                           100.0f*(float)s_max_ccr/(float)s_period);
        if (!paired_duties(s_duty,limit,&a,&b,&real_q)) {pwm_zero();return;}
        /* Back-calculate saturation into the CV integral, preserve CC clamp. */
        if ((real_q<s_duty-0.01f && vset>s_v_filter) || (real_q>s_duty+0.01f && vset<s_v_filter)) {
            s_cv_target=clampf(real_q-ff+cc_reduction,-PI_MAX,PI_MAX);
            s_cv_int=clampf(s_cv_target-(float)s_cfg.cv_kp_milli*0.001f*(vset-s_v_filter),-PI_MAX,PI_MAX);
        }
        pwm_write(a,b);
        if (s_cmd_valid) s_ui.duty_applied_pct=s_ui.duty_buck_pct;
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
        if (s_ui.state==PSU_FAULT) {
            /* One BTN1 press: keep power OFF, clear latched fault and silence PC14.
             * A separate press is required to start again. */
            pwm_zero(); reset_pi(); reset_ramp();
            if (s_hw_ok) {s_ui.error=PSU_ERR_NONE;s_ui.state=PSU_OFF;}
            /* A hardware initialization failure cannot be acknowledged ON. */
            s_alarm_muted=1U;
            psu_indicators_write(0U, 0U);
        }
        else if (s_ui.state==PSU_ON) {
            PowerStage_OffNow();
            /* Any pending parameters can now be saved without stopping PI. */
        }
        else if (s_ui.state==PSU_OFF) {
            if (s_hw_ok && s_first && s_adc_age_ticks<PSU_ADC_STALE_TICKS &&
                s_vin_filter>=(float)s_cfg.vin_min_mv*0.001f && s_vin_filter<=(float)s_cfg.vin_max_mv*0.001f &&
                s_v_filter>=0.0f && s_v_filter<(float)s_cfg.ovp_mv*0.001f &&
                s_i_filter>=-0.5f && s_i_filter<(float)s_cfg.ocp_ma*0.001f) {
                reset_pi(); reset_ramp(); pwm_zero(); s_ui.state=PSU_ON;
                psu_indicators_write(1U, 0U);
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
            s_ui.vset_cv=clampi(next,PSU_VSET_MIN_CV,(uint16_t)(s_cfg.vset_max_mv/10U));
            PowerStage_MarkDirty();
        } else {
            next=(int32_t)s_ui.iset_ca + change*step;
            s_ui.iset_ca=clampi(next,PSU_ISET_MIN_CA,(uint16_t)(s_cfg.iset_max_ma/10U));
            PowerStage_MarkDirty();
        }
        if (mask==0U) __enable_irq();
    }
    /* Fault beep pattern: 200ms ON, 300ms OFF. HAL_GPIO_WritePin is safe
     * here in the foreground; no LCD/USB work runs inside TIM3 ISR. */
    if (s_ui.state==PSU_FAULT) {
        uint32_t phase=(uint32_t)(HAL_GetTick()-s_fault_since_ms) % PSU_ALARM_PERIOD_MS;
        psu_indicators_write(0U, !s_alarm_muted && phase < PSU_ALARM_ON_MS);
    }
    PowerStage_StoragePoll(); /* Flash writes only with power OFF. */
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
    r.vref_ramped_v=s_ui.vref_ramped_v;
    r.running_seconds=s_ui.running_seconds;
    if (primask==0U) __enable_irq();
    return r;
}

void PowerStage_GetTuning(PSU_Tuning *out) {
    uint32_t mask;
    if (!out) return;
    mask=__get_PRIMASK(); __disable_irq();
    *out=s_cfg;
    if (mask==0U) __enable_irq();
}

uint8_t PowerStage_SetParam(const char *name,uint32_t value) {
    PSU_Tuning next;
    uint32_t mask;
    uint8_t is_gain=0U;
    if (!name || !s_ready) return 0U;
    PowerStage_GetTuning(&next);
    if (!strcmp(name,"CV_KP") && value<=20000U) {next.cv_kp_milli=value;is_gain=1U;}
    else if (!strcmp(name,"CV_KI") && value<=100000U) {next.cv_ki_milli=value;is_gain=1U;}
    else if (!strcmp(name,"CC_KP") && value<=50000U) {next.cc_kp_milli=value;is_gain=1U;}
    else if (!strcmp(name,"CC_KI") && value<=1000000U) {next.cc_ki_milli=value;is_gain=1U;}
    else if (!strcmp(name,"OVP_MV") && value>=5000U && value<=30000U) next.ovp_mv=value;
    else if (!strcmp(name,"OCP_MA") && value>=500U && value<=6500U) next.ocp_ma=value;
    else if (!strcmp(name,"VIN_MIN_MV") && value>=5000U && value<=35000U) next.vin_min_mv=value;
    else if (!strcmp(name,"VIN_MAX_MV") && value>=10000U && value<=40000U) next.vin_max_mv=value;
    else if (!strcmp(name,"VSET_MAX_MV") && value>=3000U && value<=28000U && (value%10U)==0U) next.vset_max_mv=value;
    else if (!strcmp(name,"ISET_MAX_MA") && value>=100U && value<=5000U && (value%10U)==0U) next.iset_max_ma=value;
    else if (!strcmp(name,"DUTY_MAX_PM") && value>=100U && value<=970U) next.duty_max_permille=value;
    else if (!strcmp(name,"V_RISE_MV_S") && value>=100U && value<=30000U) next.v_rise_mv_s=value;
    else if (!strcmp(name,"V_FALL_MV_S") && value>=100U && value<=30000U) next.v_fall_mv_s=value;
    else if (!strcmp(name,"VSET_MV") && value>=3000U && value<=next.vset_max_mv && (value%10U)==0U) {
        mask=__get_PRIMASK();__disable_irq(); s_ui.vset_cv=(uint16_t)(value/10U);
        if (mask==0U) __enable_irq();
        PowerStage_MarkDirty();
        return 1U;
    } else if (!strcmp(name,"ISET_MA") && value>=100U && value<=next.iset_max_ma && (value%10U)==0U) {
        mask=__get_PRIMASK();__disable_irq(); s_ui.iset_ca=(uint16_t)(value/10U);
        if (mask==0U) __enable_irq();
        PowerStage_MarkDirty();
        return 1U;
    } else return 0U;
    /* Keep protective thresholds stricter than hardware-authorized setpoints.
       For OFF and ON, reject inconsistent settings atomically. */
    if (next.vset_max_mv+500U>next.ovp_mv ||
        next.iset_max_ma+200U>next.ocp_ma ||
        next.vin_min_mv+1000U>=next.vin_max_mv ||
        next.duty_max_permille<100U) return 0U;
    mask=__get_PRIMASK();__disable_irq();
    s_cfg=next;
    if (s_ui.vset_cv>next.vset_max_mv/10U) s_ui.vset_cv=(uint16_t)(next.vset_max_mv/10U);
    if (s_ui.iset_ca>next.iset_max_ma/10U) s_ui.iset_ca=(uint16_t)(next.iset_max_ma/10U);
    if (is_gain) {
        /* Track the old actuator with the NEW Kp. Do not clear CC debt. */
        s_cv_target=clampf(s_duty-s_last_ff+s_last_cc,-PI_MAX,PI_MAX);
        s_cv_int=clampf(s_cv_target-(float)next.cv_kp_milli*0.001f*
                       (s_vref_ramped-s_v_filter),-PI_MAX,PI_MAX);
    }
    if (mask==0U) __enable_irq();
    PowerStage_MarkDirty();
    return 1U;
}

void PowerStage_MarkDirty(void) {
    s_cfg_dirty=1U;
    s_change_at=HAL_GetTick();
}
void PowerStage_RequestSave(void) { s_save_requested=1U; }
uint8_t PowerStage_TakeSaveEvent(void) {
    uint8_t event=s_save_event;
    s_save_event=0U;
    return event;
}
void PowerStage_StoragePoll(void) {
    PSU_Tuning c;
    uint32_t vset, iset;
    if (!s_ready || s_ui.state!=PSU_OFF ||
        (!s_cfg_dirty && !s_save_requested)) return;
    /* Quiet for 5s before automatic storage; explicit SAVE is immediate OFF. */
    if (!s_save_requested && (uint32_t)(HAL_GetTick()-s_change_at)<5000U) return;
    PowerStage_GetTuning(&c);
    {
        PSU_Status st=PowerStage_GetStatus();
        vset=(uint32_t)st.vset_cv*10U;
        iset=(uint32_t)st.iset_ca*10U;
    }
    if (PSU_Flash_Save(&c,vset,iset)) {
        s_cfg_dirty=0U; s_save_requested=0U; s_save_event=1U;
    } else {
        /* No tight retry loop on HAL Flash failure; explicit SAVE can retry. */
        s_save_requested=0U; s_cfg_dirty=0U; s_save_event=2U;
    }
}
