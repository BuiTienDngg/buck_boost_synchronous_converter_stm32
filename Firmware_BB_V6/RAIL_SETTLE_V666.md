# BBV6.66 — settle BB before heater startup

Changed the existing solder PRECHARGE qualification from 100 ms to 500 ms of continuously ready output, using PSU_SOLDER_RAIL_SETTLE_MS.

Sequence: select SOLDER -> automatic source routing/start -> BB reaches the configured solder voltage -> keep PB9 heater OFF for 500 consecutive ready control ticks -> begin RUN with its normal 15-ms quiet temperature window -> apply thermal PID duty. No five-second duty ramp is restored.

Ready qualification remains VOUT >=95% of the solder voltage and <=setpoint+0.5V, source reference >=setpoint-0.1V, and no precharge hold. A failing condition resets the 500-ms count. Faults/ADC loss/STOP remain effective while waiting. Sleep/wake keeps the existing quiet-frame behavior; the new hold applies to startup/precharge, not each sleep transition.

Validation: new settle fixture 26/26, sleep 48/48, target beep 39/39, thermal slew 33/33, settings 22/22, hold-mode 14/14, health diagnostics 26/26, app 112/112 and USB 115/115. Keil: 0 errors, 0 warnings; Flash 56024/63488 bytes; RAM including configured stack/heap 18616/20480 bytes.

Build only, not flashed. No physical output/heater commands sent.
HEX SHA256: c950f132de19cf2e8a90cbb57a034165c06dee3fc49bc0a0f534f7f8db5527e4
