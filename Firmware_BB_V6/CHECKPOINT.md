# Checkpoint: software SPI, run power, run solder

Firmware: BBV6.40-PA6-SOFTLCD. User confirmed this checkpoint works on 2026-10-22.

- Main application enabled; standalone pin test disabled.
- LCD software SPI uses existing PA5 SCK and PA7 MOSI wiring. Hardware SPI1 clock stays disabled to free PA6; its initialization function is retained.
- PA6 TIM3_CH1 PWM: 10 kHz, active HIGH. LOW is OFF.
- Solder frame: 50 ms, first 10 ms OFF for PA3 temperature sampling, next 40 ms PWM under thermal PID.
- Temperature calibration: T = 29 + (ADC - 1193) / 2 degrees C.
- TIM4 interrupt at 1 kHz runs CC/CV and application control. USB and runtime protections enabled.

Restore sources from tag `checkpoint-software-spi-run-power-run-solder`.
Verified Keil HEX and test/build reports: `releases/software-spi-run-power-run-solder/`.
Build with Keil project `MDK-ARM/Firmware_BB_V6.uvprojx` or `python tools/build.py` with Arm GNU Toolchain.
Source and build checks are recorded in the release reports. This checkpoint records the confirmed setup; PID gains and the temporary temperature formula are not a full-range calibration.
