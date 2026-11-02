# BBV6.42 - DUTY OPEN LOOP

Select MENU > DUTY OPEN LOOP while output is OFF. BT1 PB13 enables at zero duty. Each encoder detent changes buck duty by 0.5%, range 0..90%. The boost high-side duty is 90% whenever buck duty is nonzero; both PWM channels and SD are OFF at zero duty or OFF. Stopping and restarting always clears the duty command.

VIN, VOUT and output current remain displayed. The status line shows commanded buck duty, boost duty and VIN. Existing voltage/current settings are protection limits for this diagnostic mode; they do not regulate the output. Current above current_set trips rather than running CC. OVP, fast peak current, continuous current, VIN, ADC and temperature protections remain enabled. Power/input-current limits are checked from measured output power and configured efficiency, not a direct input-current measurement.

No CV/CC integrator or voltage ramp is used for the manual duty command. This is a buck-range diagnostic, not independently adjustable duty on both bridges and not a boost-range sweep.

Tests: DUTY core/menu/encoder/zero-duty SD/OFF/restart/faults 20/20; application 110/110; solder PWM 80/80. Keil build passed. Not flashed or powered on for this change. Existing PB9 TIM4_CH4 heater, PB13 EXTI13 BT1, hardware SPI1 and TIM3 1 kHz PID mapping retained.
