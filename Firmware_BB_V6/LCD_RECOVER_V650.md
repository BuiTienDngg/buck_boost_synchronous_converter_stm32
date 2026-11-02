# BBV6.50 LCD diagnostic mitigation
Hardware SPI1 speed reduced from APB2/2 (36 MHz) to /16 (4.5 MHz). Hold V/A for 1 second to request a foreground LCD hardware reset, command reinitialization and full redraw. ON/OFF mode toggle remains unchanged. LCD initialization waits service foreground USB/buttons and watchdog progress every millisecond while interrupts continue. Recovery never commands output ON or changes supply/heater targets.

This does not detect a white screen automatically (LCD is transmit-only), nor does it prove whether the cause is supply droop, EMI or bus integrity. Hardware SPI transfer failures still follow the existing Error_Handler path; this version adds manual display recovery and reduced speed only.

Tests: real renderer/control recovery simulation 13/13, including USB OFF during LCD reinitialization; buttons 11/11; application 110/110. Keil build passed. Not flashed or measured on hardware. App v6.48 remains compatible.
