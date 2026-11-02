# BBV6.67 — reduce repeated heater retries after supply sag

The user reports duty rising then repeatedly returning to zero under heavy thermal load. Current-only chart and verbal observation are consistent with the existing low-output recovery branch, but do not prove it. COM14 cannot be opened (access denied); the latest PC error journal contains no useful running telemetry. This image fixes the identified retry behavior and adds diagnosis; physical confirmation is still required.

## Changes
- Retain the existing immediate heater cut when raw VOUT is below 70% of the solder supply setting (12.6 V for 18 V).
- Latch rail recovery instead of resuming as soon as voltage crosses back above 70%. Require >=95% voltage and <=setpoint+0.5 V continuously for 20 ms with no precharge hold, then start a fresh 15-ms quiet temperature window.
- After a sag, halve the ceiling relative to the last applied heat command (and existing ceiling). Thus a prior 80% command resumes at at most 40%; a prior 17.5% command resumes at about 8.7%, rather than retrying that same load.
- Minimum recovery budget is 5% carrier duty, or the lower configured cap. If that minimum also collapses the rail, stop with the existing supply/rail error instead of endless retries. A recovery that takes 2 seconds also stops.
- Recover headroom by only 1 percentage point per 1 second of actual heater-on operation at >=95% voltage without CC. Unloaded waiting does not increase the cap.
- Apply the supply ceiling inside PID saturation and integral clamping even though the startup thermal ramp is disabled. Keep the configured duty limit and hard 80% cap.
- LCD shows BB WAIT during rail recovery. PC15 routing remains controlled by mode.
- Startup's 500-ms BB settling, automatic SOLDER entry, PB11 sleep, target beep, source control and current settings are retained.

## Telemetry
New fields inside solder:
- load_limit_pm: supply-related carrier ceiling in permille (400 = 40%).
- rail_recover: 1 while heater is off for BB recovery.
- rail_events: number of low-output events since the current start.
- zero_cause: 0 heater command ON; 1 stopped; 2 startup/rail preparation; 3 intentional quiet window; 4 thermal PID zero; 5 supply recovery; 6 sleep zero/transition; 7 fault.

Use these with vout_mV, state, ilim_mA, pid_demand_pm, heat_cmd_pm, active_C and stand to identify a real event. PID zero or unintended sleep would require a different correction from BB sag.

## Validation and limitations
- New backoff fixture: 64/64. Covers loaded-vs-unloaded recovery, CC preventing ceiling increase, timer hysteresis, configured caps, repeated severe overload stopping, STOP/OCP cancellation, reduction from partial actual duty, and actual app/IRQ/GPIO simulation (CCR4 5760 -> OFF -> 2880).
- Sleep 48/48; beep 39/39; settle 26/26; thermal slew 33/33; settings 22/22; hold-mode 14/14.
- App 112/112; USB 116/116; maximum observed wire frame 1723 bytes / 2304 buffer.
- 60 display scenarios rendered with address bounds and incremental checks before the final recovery-specific refinements.
- Keil successful with 0 errors and 0 warnings. See build/keil/verification.json for final memory and HEX hash.
- No board flash, output enable, or hardware power test performed. This is not measured hardware tuning.
- The working tree already had its raw output OVP trip block commented out in psu_control.c before this task. That file was left unchanged; these checks must not be interpreted as confirming active OVP.
