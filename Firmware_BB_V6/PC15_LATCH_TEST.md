# BBV6.45 - PC15 latch diagnostic
Once the solder route asserts PC15 HIGH, every subsequent software request to turn it OFF retains HIGH until MCU reset or power loss. OFF, protection, ADC errors and emergency shutdown still stop TIM1 power PWM and TIM4 heater PWM. This diagnostic does not bypass electrical protections or preserve GPIO across reset/power loss.

Runtime telemetry change reads PC15 ODR. After fault/OFF expect change=1, enabled=0, heater=0, power compares=0, heater compare=0. A reset restarts uptime and releases the software latch. High GPIO alone does not prove relay contacts or coil supply remain energized.

Relay latch tests 10/10 (OFF, emergency, ADC current fault and continued OFF ticks). Keil build passed. Not flashed or bench tested. This is a diagnostic behavior; do not treat its relay as load isolation on OFF.
