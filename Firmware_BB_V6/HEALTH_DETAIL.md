# BBV6.46 - solder HEALTH diagnostics

Removed the aggregate solder_healthy stop and its independent solder_on reset. VIN excursions now follow the native power-stage protection (5 ms undervoltage confirmation; overvoltage protection retained). No automatic restart after a source stop. Source fault, configuration/uninitialized control, missing ADC, invalid/stale FET sensor and overtemperature still inhibit heating. Tip protection, OVP four consecutive raw samples, current protection, watchdogs, startup ramp and PC15 relay latch remain enabled.

reason=6 is still used for source fault, failed source start or unexpected source OFF, with concrete stop fields instead of an unexplained aggregate condition.

Telemetry solder.health_now: latest active health bitmask. health_seen: conditions seen since this start. stop_flags: snapshot at first stop. stop_fault: native fault mask at that stop. stop_block: source enable rejection mask. stop_ms: firmware uptime at first stop. Stop fields persist until the next deliberate solder start; OFF/lease cleanup do not erase them.

stop_flags bits (sum when multiple):
1 source fault; 2 config error; 4 control uninitialized; 8 ADC age >=3 ms; 16 invalid FET sensor; 32 FET sensor age >=200 ms; 64 FET overtemperature; 128 VIN below minimum; 256 VIN at/above maximum; 512 source already OFF at stop.

stop_fault uses existing native masks: 1 peak current, 2 OVP, 4 VIN overvoltage, 8 VIN undervoltage, 16 FET overtemperature, 32 sensor, 64 ADC, 128 continuous current, 256 timing, 512 short circuit.

Checks: HEALTH detail 26/26, relay latch 10/10, OVP 14/14, startup ramp 21/21, DUTY 20/20, USB protocol 115/115, application 110/110. Keil build passed. Not flashed or bench validated.
