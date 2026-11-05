# Restored BBV6.49

Restored to before the LCD recovery request at 23:25 Friday 2026-10-23 (+07). Rebuilt HEX is byte-identical to the original BBV6_49_HOLD_ONOFF_MODE_Keil.hex (SHA256 ad7be652bcdbf435f9f88b89af8b10d7531d8f63a6f267a1348cce4e7cd791a0).

Large display; no ST7735 test or V/A LCD reset. Buck/boost ratio control 90%, 10 kHz. Heater PB9 TIM4_CH4 10 kHz; PB13 ON/OFF hold mode toggle; thermal PID app config default duty 80%, 50 ms frame/15 ms quiet; 5 s startup ramp. Relay follows mode. App v6.48 remains compatible. Existing Flash layout V5 retained.

Current sources were backed up under _backups\before_RESTORE_V649_20261027_215205. No hardware flashing or Git push performed.
