/* PA6 hardware PWM, quiet ADC, TIM4 dispatch and independent shutdown. */
#define TEST_FACTORY_CONFIG 1
#define test_main application_tests_not_run
#include "app_fixture.c"
#undef test_main
static PsuControl p;
static PsuModes m;
static uint32_t clock_ms;
static uint16_t raw_tip=1200;
static void step(void){
  ++clock_ms;p.adc_age_ms=p.temp_age_ms=0;
  if(m.solder.duty==0 && clock_ms-m.solder.last_heat_ms>=2)PsuSolder_Sample(&m,raw_tip,clock_ms);
  PsuModes_Tick(&m,&p,clock_ms);
}
static void steps(unsigned n){while(n--)step();}
static void setup(uint16_t raw){
  Psu_Init(&p);PsuModes_Init(&m);p.initialized=p.temp_valid=1;p.ready_ms=1000;
  p.adc_age_ms=p.temp_age_ms=0;p.vin=p.vin_raw=24;p.temp_c=30;clock_ms=100;raw_tip=raw;
  CHECK(PsuModes_Select(&m,&p,PSU_MODE_SOLDER,clock_ms));
  steps(300);CHECK(p.enabled && m.solder.phase==PSU_SOL_PRECHARGE);
  p.vout=p.vout_raw=p.voltage_set;p.reference_v=p.voltage_set;steps(PSU_SOLDER_RAIL_SETTLE_MS);
  CHECK(m.solder.phase==PSU_SOL_RUN && !m.solder.duty);
}
int test_main(void){
  /* Logical acceptance is once per frame, after fresh quiet temperature. */
  setup(1635);m.solder.stand=1;steps(14);
  CHECK(!m.solder.sleeping && !m.solder.stand_count);
  steps(1);CHECK(m.solder.stand_count==1 && !m.solder.sleeping);
  steps(49);CHECK(m.solder.stand_count==1 && !m.solder.sleeping);
  steps(1);CHECK(m.solder.stand_count==2 && !m.solder.sleeping);
  steps(49);CHECK(!m.solder.sleeping);steps(1);
  CHECK(m.solder.sleeping && m.solder.active_target==200 && m.solder.target==300);
  CHECK(p.enabled && m.solder_request && m.solder_on && !m.solder.duty && !m.solder.command);
  CHECK(!m.solder.integral && !m.solder.derivative && !m.solder.pid_ready && !m.solder.ramp_ms);
  CHECK(m.solder.valid && m.solder.temperature>240 && !p.fault);
  uint32_t sleep_since=m.solder.stand_ms;steps(15);
  CHECK(m.solder.sleeping && !m.solder.duty && m.solder.stand_ms==sleep_since);
  /* One HIGH wakes, at next quiet-boundary sample; wake never interrupts PWM. */
  m.solder.stand=0;steps(49);CHECK(m.solder.sleeping);steps(1);
  CHECK(!m.solder.sleeping && m.solder.active_target==300 && !m.solder.stand_count);
  CHECK(!m.solder.command && !m.solder.duty && !m.solder.ramp_ms);
  steps(14);CHECK(!m.solder.duty);steps(1);
  CHECK(m.solder.command>0.49f && m.solder.command<=.51f && p.enabled);
  /* A HIGH between LOW samples cancels their history. */
  setup(1635);m.solder.stand=1;steps(15);CHECK(m.solder.stand_count==1);
  m.solder.stand=0;steps(50);CHECK(!m.solder.stand_count && !m.solder.sleeping);
  m.solder.stand=1;steps(100);CHECK(m.solder.stand_count==2 && !m.solder.sleeping);
  steps(50);CHECK(m.solder.sleeping);
  /* Both directions reset PID even for equal active/sleep targets. */
  setup(1635);m.solder.sleep_target=300;m.solder.stand=1;steps(115);
  CHECK(m.solder.sleeping && m.solder.active_target==300 && !m.solder.pid_ready && !m.solder.duty);
  m.solder.stand=0;steps(15);
  CHECK(!m.solder.sleeping && !m.solder.pid_ready && !m.solder.duty);
  /* Holder activity cannot restart STOP, fault or another mode. */
  PsuModes_Stop(&m,&p);m.solder.stand=1;steps(200);m.solder.stand=0;steps(200);
  CHECK(!p.enabled && !m.solder_request && !m.solder.duty);
  setup(1635);m.solder.stand=1;steps(115);p.fault=PSU_F_OVP;steps(1);
  m.solder.stand=0;steps(100);CHECK(!p.enabled && !m.solder_request && p.fault==PSU_F_OVP);
  /* Existing ten-minute sleep shutdown remains; lifting does not auto-arm. */
  setup(1635);m.solder.stand=1;steps(115);
  m.solder.stand_ms=clock_ms-600000u;steps(1);
  CHECK(!p.enabled && !m.solder_request && !m.solder.error);
  /* Actual application: PB11 goes through GPIO, source IRQ and heater driver. */
  hadc1.Instance=ADC1;hadc2.Instance=ADC2;htim1.Instance=TIM1;htim2.Instance=TIM2;htim3.Instance=TIM3;htim4.Instance=TIM4;
  TIM1->ARR=PSU_PWM_PERIOD;TIM1->CCMR1=TIM_CCMR1_OC1PE|TIM_CCMR1_OC2PE;
  hadc1.Init.ExternalTrigConv=ADC_SOFTWARE_START;hadc1.Init.ContinuousConvMode=ENABLE;
  tip_code=1635;PsuApp_Init();run(0,160);
  run(GPIO_PIN_13,1026);run(0,60);vout_code=971;run(0,1000);
  PsuView view;PsuApp_Snapshot(&view);
  CHECK(view.modes.mode==PSU_MODE_SOLDER && view.modes.solder.phase==PSU_SOL_RUN && view.control.enabled);
  uint32_t old_reads=sleep_pin_reads;
  run(GPIO_PIN_11,200);PsuApp_Snapshot(&view);
  CHECK(view.modes.solder.sleeping && view.modes.solder.active_target==200 && view.modes.solder.target==300);
  CHECK(view.control.enabled && view.modes.solder_request && (GPIOC->ODR&GPIO_PIN_15));
  CHECK(!view.hardware.heater && !TIM4->CCR4 && !view.control.fault);
  CHECK(sleep_pin_reads>old_reads && sleep_pin_reads-old_reads<=5 && !sleep_pin_reads_heating);
  run(0,65);PsuApp_Snapshot(&view);
  CHECK(!view.modes.solder.sleeping && view.modes.solder.active_target==300 && view.control.enabled);
  CHECK(!view.modes.solder.ramp_ms && view.modes.solder.ramp_limit_pm==800);
  tap(GPIO_PIN_13);run(GPIO_PIN_11,160);run(0,160);PsuApp_Snapshot(&view);
  CHECK(!view.control.enabled && !view.modes.solder_request && !view.hardware.heater && (GPIOC->ODR&GPIO_PIN_15));
  CHECK(!sleep_pin_reads_heating);
  return 0;
}
