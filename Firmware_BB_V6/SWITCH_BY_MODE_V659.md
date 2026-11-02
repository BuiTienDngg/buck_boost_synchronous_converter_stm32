# BBV6.59 - PC15 selects the operating mode
PC15 HIGH whenever the selected mode is SOLDER, independent of RUN/STOP/fault; LOW in LAB/CHARGE/DUTY. Heater and power PWM safety shutdown is unchanged. Hold BT1/PB13 for 1 second to switch LAB <-> SOLDER; switch mode only after stopping PWM and leave output OFF. A fault is preserved during mode changes; use app CLEAR when safe. Short ON/OFF never changes mode or switch level.
This replaces v6.45's permanent-until-reset latch: returning to LAB releases PC15. Bootstrap/PWM and startup interlocks remain. BB PWM 25 kHz, heater cap 80%, 15 ms quiet and 5 s ramp retained.
Tests: mode switch/STOP/ADC fault/PWM shutdown 14/14; USB 115/115. Keil build passed. Not flashed or bench tested.
