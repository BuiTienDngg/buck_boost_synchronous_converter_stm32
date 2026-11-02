# BBV6.65 — automatic solder entry, no duty ramp, target-reached beep

Edited Firmware_BB_V6 in place. Build only; not flashed; no power/heater commands sent to hardware.

Changes:
- Long ON/OFF mode switch into SOLDER no longer issues the extra STOP after selecting the mode. SOLDER automatically requests ROUTE/PRECHARGE/RUN; entering through the menu uses the same selection path. LAB still enters OFF.
- Existing fault/config errors prevent arming on a mode switch. PB11 activity and fault clearing cannot automatically re-arm a stopped heater.
- Disabled the five-second thermal duty ramp (PSU_SOLDER_RAMP_MS=0), including manual restart, sleep/wake and source recovery. Thermal PID applies its demand after a valid quiet reading. The 80% hard cap and configured lower duty caps remain.
- Source CV/CC voltage ramp, relay sequencing, precharge and ADC/rail readiness checks remain. No direct bypass into heater PWM was added.
- Once the normal target is within +/-5 C for 800 ms of independent 50-ms quiet readings, emit a single target-reached event.
- Foreground consumes the event and drives active-HIGH PC14 buzzer for 150 ms without blocking. LCD transfer progress also services the buzzer so screen work cannot extend the pulse significantly.
- One shot per startup, changed active target, or wake. Normal thermal oscillation does not re-arm it. Sleep is silent. STOP/fault cancels the target tone; existing periodic fault alarm has priority.
- Existing PB11 three-LOW / one-HIGH sleep logic, 50-ms cycle with 15-ms quiet window, display, NTC 50k, power-stage frequency and PC15 mode routing retained.

Verification:
- Target notification / physical buzzer / automatic entry: 39/39; includes stability-window reset, no repeated beep after cooling/reheating, new target, silent sleep, wake, bad ADC, exact 150-ms pulse and STOP cancellation.
- Sleep: 48/48; thermal slew: 33/33; solder settings: 22/22; physical hold-mode behavior: 14/14.
- Application/ADC/buttons: 112/112; USB protocol: 115/115.
- Keil: 0 errors, 0 warnings. Flash 56032/63488 bytes; RAM including configured stack/heap 18616/20480 bytes.

HEX SHA256: ad800e44e2dd750eb93f8c1049a3c52c48c4d8371c4df454b8a6cd215b369021
