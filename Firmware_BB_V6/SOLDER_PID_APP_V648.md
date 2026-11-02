# BBV6.48 - Thermal PID settings and 15 ms quiet

Default solder PID: Kp=0.010000 duty/C, Ki=0.001000 duty/(C*s), Kd=0.002000 duty*s/C. Duty limit=80.0%, configurable 0..95% (0 disables heating). These are configurable versions of the previous fixed thermal gains, not a new measured tuning profile. D is filtered and acts on measured temperature; anti-windup uses the configured duty/ramp ceiling.

50 ms frame: first 15 ms completely OFF for temperature sampling, then 35 ms 10 kHz carrier PWM on PB9 TIM4_CH4. The 80% limit applies inside the 35 ms heat window (maximum frame-averaged on-time 56%). The existing 5 s startup ramp reaches the configured duty ceiling. The actuator compare also clamps to the configured ceiling.

Use BB_V6_Studio_v6_48.exe, connect to BBV6.48, open configuration > PID tay han. Stop heating before changing Kp/Ki/Kd/duty. Apply configuration, wait for CFG acknowledgement, then Save Flash if persistence is needed. Existing runtime safety protections, DUTY mode, HEALTH diagnostics and PC15 latch test behavior remain unchanged.

New wire fields: solder_kp, solder_ki, solder_kd scaled by 1000000; solder_duty_max scaled by 1000 (80%=80000). Flash V5 appends four floats after the existing 39 fields. Existing V1/V2/V3/V4 settings retain their saved fields; only absent thermal fields receive the defaults above. An older app does not expose these new controls; use the matching app.

Validation: thermal PID/duty/quiet-window 17/17; source PWM 80/80 per profile (legacy quiet duration explicitly selected for those regression fixtures); DUTY 20/20; OVP 14/14; relay latch 10/10; HEALTH 26/26; Flash journal 633/633 including V4-to-V5 power-loss migration; remote 115/115; application 110/110; Python app tests 21/21; actual bundled EXE smoke test passed with 43 settings and thermal CFG/SAVE. Keil build passed. No hardware flashing or live gain tuning performed for this version.
