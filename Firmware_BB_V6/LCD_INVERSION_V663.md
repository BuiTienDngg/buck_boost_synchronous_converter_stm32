# BBV6.63 — LCD display inversion correction

User reported green appearing magenta, magenta appearing green, and black appearing white. The driver explicitly sent INVON (0x21). Changed panel initialization to INVOFF (0x20), configurable with PSU_LCD_INVERT=0 in psu_config.h. RGB565, orientation, BGR selection, palette and UI layout remain unchanged.

The former software preview ignored display inversion. It now tracks INVON/INVOFF, models complemented RGB565 pixels, checks normal display polarity after initialization, and verifies COLMOD=0x55 and MADCTL=0x68.

Green/magenta and black/white reversal are consistent with inversion. The reported red-to-black mapping is not explained by inversion alone; verify it on the physical panel after loading this correction. No physical color verification has been performed.

Keil: 0 errors, 0 warnings. No board flash or power/heater commands performed.
HEX SHA256: aaf8f5a09878230ca5b803b8d33810c419186c82ae36c5a5b653b5568e98c816
