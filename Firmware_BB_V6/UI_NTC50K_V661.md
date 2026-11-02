# BBV6.61 — incremental LCD and FET NTC 50k

Modified the existing Firmware_BB_V6 project in place. This image has been built and simulated; it has NOT been flashed, and no output/heater commands were sent to hardware.

## LCD
- Removed the upper-right status badge in both power and solder screens. Fault details and the bottom output button remain.
- Black background, green voltage/temperature digits, pink current/power, red editing/error indicators. No yellow digits or gray panels.
- Cache the rendered characters and their colors. Refresh only changed glyph cells, erase obsolete trailing characters, and update editing highlights separately.
- Split VIN, FET temperature, power, clock, solder temperature, target, and duty into independent cached fields.
- Solder duty changes no longer repaint the title or temperature panel. Menu selection changes update affected rows only.
- Full-page initialization is reserved for startup and page/layout transitions. The scrolling graph still updates its plot area as samples arrive.
- Added the missing percent glyph for PWM duty labels.

Measured final-update SPI traffic in the actual C renderer:
| Scenario | Bytes |
|---|---:|
| Unchanged screen | 0 |
| One voltage digit | 1355 |
| One heater duty digit | 95 |
| One solder temperature digit | 2111 |
| Shorter VIN text (clears stale trailing cells) | 1735 |
| Menu selection (two changed rows) | 9369 |

## FET NTC
- Factory R25 is now 50000 ohms.
- Divider resistor remains 10000 ohms, Beta remains provisionally 3950 K, NTC to ground.
- Product link confirms 50k / 1%, but does not specify Beta: https://linhkien.cxtvn.com/10263-dien-tro-nhiet-ntc-50k-1-5c.html
- Existing pre-revision-6 Flash records with the old default R25=10000 migrate to 50000 in RAM. Other settings, including pull resistance, Beta and PID, remain intact. A custom non-default R25 is preserved.
- SAVE writes journal revision 6; no automatic Flash erase/write at startup. Revision 6 retains explicitly configured R25 values on subsequent boots.
- With a 10k pull-up and 50k NTC, ADC about 3413 corresponds to 25 C. Beta must be confirmed or calibrated before relying on accuracy away from room temperature.
- Regenerated PC_App/settings_schema.json to match the new firmware defaults. Existing packaged app EXEs are not rebuilt by this task.

## Verification
- Keil: 0 errors, 0 warnings; Flash span 55040 / 63488 bytes; RAM including configured stack/heap 18576 / 20480 bytes.
- 52 actual C display scenarios rendered with SPI address bounds, absent-badge and incremental-traffic checks.
- Application/ADC/buttons: 112/112.
- USB protocol: 115/115.
- Flash journal and migration/power-loss: 739/739.
- Source PWM frequency, thermal PID, duty cap, PC15 mode routing, and OVP behavior were not changed in this revision.

HEX SHA256: 71fb4d4b2c15465f6125c8461ff43edf076f8d2dacbea4cd8621e7d9b7255c4f
