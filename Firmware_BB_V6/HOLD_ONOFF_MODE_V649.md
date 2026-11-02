# BBV6.49 - Hold ON/OFF to toggle mode

BT1 PB13 short press: ON/OFF. When running, OFF is posted immediately after 25 ms debounce; releasing does not restart output. Hold for 1000 ms after debounce: toggle LAB supply <-> SOLDER, leave the new mode OFF. Press briefly again to start. A continuous long hold toggles only once. V/A long hold no longer switches mode. Menu mode selection remains available.

With an existing source fault, a long hold retains the explicit OFF/CLEAR action and does not switch mode or automatically restart. Native fault-clear eligibility still applies. PC15 diagnostic latch was removed at the user's request: the relay now follows the solder route and may release on OFF/fault/mode change.

PID/duty app settings, default 80% maximum duty, 5 s startup ramp, 50 ms frame with 15 ms quiet remain unchanged. App v6.48 remains compatible.

Tests: hold/release/run/OFF/two-way mode/old shortcut/fault action 11/11; USB protocol 115/115; application 110/110. Keil build passed. Not flashed or tested on the physical board for this version.
