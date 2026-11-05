# UI/UX v6.68

Research and implementation, 2026-11-02.

## Sources
- JBC: automatic Sleep/Hibernation, temperature limits and usage counters: https://us.jbctools.com/top10.html
- RIGOL DP800: numerical display, presets, timer and graphical output monitoring: https://www.rigol.com/intl/products/dc-power-supply/DP800.html
- Keysight BenchVue: primary voltage/current controls, advanced protection options and selectable traces: https://helpfiles.keysight.com/BenchVueSoftware_HDML5HelpFiles/PSApp/English/Content/GUI/Instrument%20Settings%20Tab.htm

## Applied
- POWER: voltage/current stay primary; existing presets, CV/CC status, logging and configuration remain available.
- SOLDER: primary cards become actual tip temperature and active temperature target. Secondary cards show heater PID command and measured current versus its configured limit. Source PWM switching no longer displaces these primary operating values.
- SOLDER chart shows actual temperature and target with distinct solid/dashed lines, on the same temperature scale. POWER chart shows V/A. Mode changes discard the previous mode's chart history.
- Chart redraw is bounded at 10 Hz. Existing trace objects are updated in place; background/grid are recreated only for resize, mode or quantized scale changes.
- SOLDER status distinguishes routing/rail wait, supply recovery, sleep, heating, ready within 5 C, cooling, stopped and fault. LCD no longer labels zero heater command as AT TEMP below target; it shows PID HOLD.
- Existing LCD field/digit caching is retained.

## Validation
- Python compilation and application DEMO/configuration smoke checks passed.
- SOLDER actual/target cards and READY/COOL/HEAT transitions checked with synthetic telemetry.
- Canvas object IDs remain identical across 20 consecutive chart updates at the same dimensions/scales.
- POWER DEMO and SOLDER preview images generated under build/preview.
- Keil build: 0 errors, 0 warnings; Flash/RAM boundary checks passed.
- Windows EXE packaged under PC_App/dist/BB_V6_Studio_v6_68.exe.
- No board flashed and no physical output commands sent. Physical LCD performance remains unmeasured.

Temperature conversion still uses the provisional linear slope. Room calibration shifts the reference point only and is RAM-only. Existing hardware protections are not changed by this UI work.
