#include "power_stage.h"
#include "input_buttons.h"
#include "input_encoder.h"
#include "analog_monitor.h"

static TIM_HandleTypeDef *s_pwm;
static BB_Status s_bb;
static uint32_t s_last_ramp_ms;
static uint8_t s_ready;

/* Same TIM1 counter drives both complementary half-bridge drivers.
 * IR2184 generates HO/LO and its internal deadtime; STM32 CHxN is unused.
 * BUCK: PA8 HS duty = command, PA9 HS = 97% (3% LS refresh).
 * BOOST: PA8 HS = 97% (3% LS refresh), PA9 HS = 100%-command.
 * Thus duty_request_pct means buck HS duty in BUCK mode, and boost LS
 * duty in BOOST mode. Approximate open-loop relations in CCM:
 * Vout/Vin ~= D_PA8 / D_PA9. There is NO voltage regulation here.
 */
static uint32_t compare_pct(uint8_t pct)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(s_pwm) + 1U;
    return (period * (uint32_t)pct + 50U) / 100U;
}

static void put_pwm(uint8_t buck_hs, uint8_t boost_hs)
{
    __HAL_TIM_SET_COMPARE(s_pwm, TIM_CHANNEL_1, compare_pct(buck_hs));
    __HAL_TIM_SET_COMPARE(s_pwm, TIM_CHANNEL_2, compare_pct(boost_hs));
    s_bb.buck_input_pct = buck_hs;
    s_bb.boost_input_pct = boost_hs;
}

static void set_pwm_for_mode(uint8_t duty)
{
    if (s_bb.mode == BB_MODE_BUCK) put_pwm(duty, 97U);
    else put_pwm(97U, (uint8_t)(100U - duty));
}

void PowerStage_OffNow(void)
{
    /* PA10 low is the immediate, shared hardware shutdown. */
    HAL_GPIO_WritePin(BB_SD_GPIO_PORT, BB_SD_GPIO_PIN, GPIO_PIN_RESET);
    if (s_bb.state == BB_STATE_RUNNING) s_bb.state = BB_STATE_OFF;
}

static void trip(BB_Fault fault)
{
    PowerStage_OffNow();
    s_bb.fault = fault;
    s_bb.state = BB_STATE_FAULT;
}

static BB_Fault check_measurements(const AnalogMonitor_Data *adc, uint32_t now)
{
    if (!adc->adc1_valid ||
        (uint32_t)(now - adc->adc1_updated_ms) > BB_ADC_STALE_MS)
        return BB_FAULT_ADC_STALE;
    if (!(adc->vin_scaled >= BB_VIN_MIN_V && adc->vin_scaled <= BB_VIN_MAX_V))
        return BB_FAULT_INPUT_RANGE;
    if (!(adc->vout_v >= 0.0f && adc->vout_v < BB_VOUT_MAX_V))
        return BB_FAULT_OUTPUT_OVERVOLT;
    if (!(adc->current_a >= -0.5f && adc->current_a < BB_IOUT_MAX_A))
        return BB_FAULT_OUTPUT_OVERCURRENT;
    if (s_bb.mode == BB_MODE_BOOST &&
        adc->vin_scaled >= BB_VOUT_MAX_V - BB_BOOST_INPUT_MARGIN_V)
        return BB_FAULT_BOOST_INPUT_TOO_HIGH;
    return BB_FAULT_NONE;
}

HAL_StatusTypeDef PowerStage_Init(TIM_HandleTypeDef *htim1)
{
    GPIO_InitTypeDef gpio = {0};
    RCC_ClkInitTypeDef clk = {0};
    uint32_t latency, timer_clk, period, frequency;
    s_ready = 0U;
    s_pwm = 0;
    s_bb.state = BB_STATE_OFF;
    s_bb.mode = BB_MODE_BUCK;
    s_bb.fault = BB_FAULT_NONE;
    s_bb.duty_request_pct = BB_DUTY_DEFAULT_PCT;
    s_bb.duty_applied_pct = BB_DUTY_START_PCT;
    s_bb.buck_input_pct = 0U;
    s_bb.boost_input_pct = 0U;
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(BB_SD_GPIO_PORT, BB_SD_GPIO_PIN, GPIO_PIN_RESET);
    gpio.Pin = BB_SD_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BB_SD_GPIO_PORT, &gpio);
    /* HAL init and PWM outputs must have been configured by CubeMX. */
    if (htim1 == 0 || htim1->Instance != TIM1 ||
        htim1->Init.CounterMode != TIM_COUNTERMODE_UP) {
        s_bb.fault = BB_FAULT_PWM_INIT;
        s_bb.state = BB_STATE_FAULT;
        return HAL_ERROR;
    }
    HAL_RCC_GetClockConfig(&clk, &latency);
    timer_clk = HAL_RCC_GetPCLK2Freq();
    if (clk.APB2CLKDivider != RCC_HCLK_DIV1) timer_clk *= 2U;
    period = (htim1->Instance->PSC + 1U) * (htim1->Instance->ARR + 1U);
    frequency = period ? (timer_clk / period) : 0U;
    if (frequency < 19800U || frequency > 20200U ||
        (htim1->Instance->CCER & (TIM_CCER_CC1P | TIM_CCER_CC2P)) != 0U ||
        (htim1->Instance->CCMR1 & TIM_CCMR1_OC1M) != TIM_OCMODE_PWM1 ||
        (htim1->Instance->CCMR1 & TIM_CCMR1_OC2M) != (TIM_OCMODE_PWM1 << 8U)) {
        s_bb.fault = BB_FAULT_PWM_INIT;
        s_bb.state = BB_STATE_FAULT;
        return HAL_ERROR;
    }
    s_pwm = htim1;
    /* Program both duty registers BEFORE setting MOE / enabling PWM. */
    set_pwm_for_mode(BB_DUTY_START_PCT);
    if (HAL_TIM_PWM_Start(s_pwm, TIM_CHANNEL_1) != HAL_OK) {
        trip(BB_FAULT_PWM_INIT);
        s_pwm = 0;
        return HAL_ERROR;
    }
    if (HAL_TIM_PWM_Start(s_pwm, TIM_CHANNEL_2) != HAL_OK) {
        (void)HAL_TIM_PWM_Stop(s_pwm, TIM_CHANNEL_1);
        trip(BB_FAULT_PWM_INIT);
        s_pwm = 0;
        return HAL_ERROR;
    }
    s_last_ramp_ms = HAL_GetTick();
    s_ready = 1U;
    /* PWM counter runs, but PA10 stays LOW until BTN1 passes all checks. */
    return HAL_OK;
}

void PowerStage_Handle(void)
{
    AnalogMonitor_Data adc;
    BB_Fault error;
    uint32_t now;
    int32_t increment, requested;

    if (!s_ready) return;
    now = HAL_GetTick();
    /* Reverse the EC11 direction as requested. */
    increment = Encoder_TakeDelta();
    requested = (int32_t)s_bb.duty_request_pct - increment;
    if (requested < (int32_t)BB_DUTY_MIN_PCT) requested = BB_DUTY_MIN_PCT;
    if (requested > (int32_t)BB_DUTY_MAX_PCT) requested = BB_DUTY_MAX_PCT;
    s_bb.duty_request_pct = (uint8_t)requested;

    /* BTN3 clears a latched error, only after PA10 is disabled. */
    if (Button_TakePressedEvent(BUTTON_3) && s_bb.state == BB_STATE_FAULT) {
        PowerStage_OffNow();
        s_bb.fault = BB_FAULT_NONE;
        s_bb.state = BB_STATE_OFF;
    }
    /* BTN2 selects Buck / Boost only when the power stage is OFF. */
    if (Button_TakePressedEvent(BUTTON_2) && s_bb.state == BB_STATE_OFF) {
        s_bb.mode = (s_bb.mode == BB_MODE_BUCK) ? BB_MODE_BOOST : BB_MODE_BUCK;
        s_bb.duty_applied_pct = BB_DUTY_START_PCT;
        set_pwm_for_mode(s_bb.duty_applied_pct);
    }

    AnalogMonitor_GetSnapshot(&adc);
    if (s_bb.state == BB_STATE_RUNNING) {
        error = check_measurements(&adc, now);
        if (error != BB_FAULT_NONE) trip(error);
    }
    if (Button_TakePressedEvent(BUTTON_1)) {
        if (s_bb.state == BB_STATE_RUNNING) {
            PowerStage_OffNow();
        } else if (s_bb.state == BB_STATE_OFF) {
            error = check_measurements(&adc, now);
            if (error == BB_FAULT_NONE) {
                s_bb.duty_applied_pct = BB_DUTY_START_PCT;
                set_pwm_for_mode(s_bb.duty_applied_pct);
                s_last_ramp_ms = now;
                s_bb.state = BB_STATE_RUNNING;
                HAL_GPIO_WritePin(BB_SD_GPIO_PORT, BB_SD_GPIO_PIN, GPIO_PIN_SET);
            } else {
                trip(error);
            }
        }
    }
    if (s_bb.state == BB_STATE_RUNNING &&
        (uint32_t)(now - s_last_ramp_ms) >= BB_RAMP_INTERVAL_MS) {
        s_last_ramp_ms = now;
        if (s_bb.duty_applied_pct < s_bb.duty_request_pct) ++s_bb.duty_applied_pct;
        else if (s_bb.duty_applied_pct > s_bb.duty_request_pct) --s_bb.duty_applied_pct;
        set_pwm_for_mode(s_bb.duty_applied_pct);
    }
}

BB_Status PowerStage_GetStatus(void)
{
    return s_bb;
}
