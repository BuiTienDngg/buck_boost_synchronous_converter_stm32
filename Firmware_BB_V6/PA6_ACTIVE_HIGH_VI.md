# BBV6.39 - PA6 active HIGH

Q10/Q8/Q11 drive Q9 P-MOS: heater HIGH = ON; LOW = OFF.
TIM3_CH1 PA6 PWM1, polarity HIGH (CC1P=0), PSC=0, ARR=7199, clock 72 MHz, carrier 10 kHz. CCR1 counts HIGH/ON time; 50% = 3600, maximum 95% = 6840.
50 ms frame: first 10 ms LOW for PA3 temperature sampling; remaining 40 ms PWM from thermal PID.
Boot, OFF, faults, expired lease, zero duty and TIM4 watchdog force PA6 GPIO LOW. CubeMX configuration and integration use the same polarity.
Temperature remains T = 29 + (ADC - 1193) / 2 degrees C. SPI1 Master TX-only and TIM4 1 kHz CC/CV are unchanged. Telemetry active_low=0.
Tests: solder_pwm 80/80, solder_pwm_normal 80/80, foreground 11/11, bench 8/8, pin_test 9/9, remote 115/115, application 110/110. GCC and Keil builds passed. Not flashed; physical waveform not verified.
