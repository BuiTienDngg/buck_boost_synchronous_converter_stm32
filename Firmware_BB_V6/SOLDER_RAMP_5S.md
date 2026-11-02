# BBV6.43 - solder startup duty ramp

After the solder supply rail reaches RUN, ramp the permitted heater duty from 0 to 95% over 5000 ms. PID can request less than the ceiling at any time; reductions and faults remain immediate. Thermal PID integration is held when its command exceeds the ramp ceiling to avoid windup. The ramp restarts after rail recovery and every new heater start. It does not alter supply voltage ramp or the open-loop DUTY mode.

PB9 TIM4_CH4 10 kHz active HIGH; frame 50 ms with 10 ms quiet sampling. Configure PSU_SOLDER_RAMP_MS in Core/Inc/psu_modes.h (default 5000 ms).

Tests: ramp/start/recovery/downward target/fault 21/21; existing PWM regression with ramp explicitly disabled 80/80 for each profile; DUTY 20/20. Keil build passed. Not flashed or bench validated.
