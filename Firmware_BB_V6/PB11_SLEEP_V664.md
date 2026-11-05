# BBV6.64 — PB11 sleep policy ported from V5

Source inspected:
- firmwareV5/MDK-ARM/ST7735.uvprojx references ../Src/UI_Solider.c.
- firmwareV5/Src/UI_Solider.c: UI_Solider_SetSleep(), Solider_CheckSleepDuringON(), and the actual call after Solider_ProcessTemperatureMeasurement(). Some nearby historical comments describe different sampling points; the implemented call sequence was used.
- firmwareV5/Src/main.c configures PB11 input with an internal pull-up.

Firmware_BB_V6 was edited in place. V5 sources were only read.

Behavior:
- PB11 LOW = iron in holder, HIGH/open = lifted. Input remains GPIO input + pull-up in main.c and CubeMX project.
- Sample PB11 once at the end of the 15 ms quiet temperature-sensing window, before thermal PID. V6 formerly read PB11 every 1 ms and confirmed the held state every 5 ms for about 305 ms.
- Three consecutive frame samples LOW enter sleep; one HIGH at the next scheduled sample wakes. HIGH clears the pending LOW count.
- Sleep uses sleep_target (default 200 C). Wake restores the user's normal target; neither state overwrites that target or presets.
- A transition resets PID/integrator/derivative/slew history, keeps the valid temperature, restarts a quiet frame, and restarts the existing 5 s conservative duty ramp. Source enable, heater request, relay routing and operating time remain intact.
- STOP/fault cannot be re-armed by PB11. The existing ten-minute continuous-sleep auto-OFF remains; lifting after that requires ON again.
- Screen displays SLEEP and the sleep setpoint; the regular preset highlight returns on wake. Incremental rendering remains.
- 50 ms frame, 15 ms quiet ADC window, PB9 TIM4_CH4 10 kHz active HIGH, 80% hard cap, source PWM 25 kHz, protections and PC15 mode routing retained.

Verification:
- New sleep fixture: 48/48, including real application GPIO/IRQ/actuator simulation, no PB11 reads while heater PWM is active, LOW noise history, wake ramp, equal targets, STOP, OVP and ten-minute shutdown.
- Thermal slew: 33/33; solder configuration: 22/22; hold mode: 14/14.
- Application: 112/112; USB protocol: 115/115.
- 60 actual C display scenarios, including sleep setpoint/preset rendering, passed.
- Keil: 0 errors, 0 warnings. Flash 55760/63488 bytes; RAM including configured stack/heap 18584/20480 bytes.

Not flashed; no physical output or heater test performed.
HEX SHA256: c35d5d833d06c7c869b054dc88b9fe5fd16cbaaaa0237a73e32209d02e0d0efe
