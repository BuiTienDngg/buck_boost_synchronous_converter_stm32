# BBV6.62 — clean solder screen

Updated Firmware_BB_V6 in place. Build only; not flashed, no power/heater commands sent.

- Header: tip type at left and temperature setpoint at right.
- Current temperature: three large 8x glyphs in the center, with a separate C unit. Invalid sensor shows --- in red.
- Current: horizontal 294-pixel fill inside a border, with measured amps / configured solder current limit. Full scale follows settings.solder_current; over-limit fill is red. Only changed bar spans are redrawn, or the occupied span when color changes.
- Runtime: HH:MM:SS. State: OFF, ROUTING, RAIL, NO TIP, TIMER ERR, STANDBY, HEATING, AT TEMP, ERROR or PROTECT.
- Errors remain visible above the quick presets.
- Three full-width-row quick temperature buttons: 250, 300 and 350 C. Selected target is highlighted green; others pink. Existing physical preset-button actions remain intact.
- Removed engineering labels, PWM duty text, rail voltage and the former heat toggle tile from the solder screen.
- Black / red / green / pink palette and character-level cache retained. No full-page redraw during normal solder updates.

Validation:
- Native Keil: 0 errors, 0 warnings; Flash 55632 / 63488 bytes; RAM including configured stack/heap 18584 / 20480 bytes.
- 60 actual C display scenarios rendered with SPI-window bounds checks and incremental transfer assertions.
- Solder unchanged update: 0 bytes. Duty-only changes with same state: 0 bytes. Runtime one-second tick: 347 bytes.
- Verified half-current, over-limit red bar, empty bar, changed current limit, moved preset highlight, OFF and PROTECT states.
- Power control, PID, NTC conversion, protections, button handlers and settings format unchanged from v6.61.

SHA256: 5bad724784738d396bc06a82f6d8a7450054b7b5f847e8cc0c1a02cc903469b2
