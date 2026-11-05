# BBV6.51 - Buck/boost PWM 20 kHz
TIM1 power PWM: 72 MHz / (PSC 0 + 1) / (ARR 3599 + 1) = 20 kHz, period 50 us. Duty fraction mapping is unchanged; e.g. 90% = 3240 counts.
TIM4_CH4 heater remains 10 kHz (ARR 7199), with the configured thermal PID/duty cap and 50 ms frame / 15 ms quiet. TIM3 CC/CV remains 1 kHz. Explicit heater period separates it from the source carrier.
Discharge OPM period is now 50 us, while its programmed pulse duration remains 2..20 us because the timer clock stays 72 MHz. PID gains, ADC conversion rate and settings Flash format remain unchanged.
Tests: application/OPM and actuator checks 110/110; manual DUTY 20/20; thermal configuration 17/17; hold mode 11/11; foreground/recovery 13/13. Keil build passed. Not flashed or verified on hardware. This frequency change does not establish that LCD noise is fixed.
