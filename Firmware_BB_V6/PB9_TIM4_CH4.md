# BBV6.41 - PB9 TIM4_CH4
Heater PB9: TIM4 CH4, 10 kHz active HIGH, LOW OFF. Thermal frame remains 50 ms / 10 ms quiet.
BT1 moves from PB9 to PB13 EXTI13 with pull-up and foreground debounce.
SPI1 hardware is restored on PA5/PA7. TIM3 now runs the 1 kHz PID interrupt; TIM4 is the heater carrier without PID interrupts.
Temperature remains 29 + (ADC - 1193) / 2 C. Update physical wiring before flashing.
Application checks 110/110; heater checks 80/80 for each profile; Keil build passed. Not flashed or measured on hardware.
The legacy PA6 integration tool is disabled for this pin-map migration. Preserve current sources when regenerating CubeMX.
