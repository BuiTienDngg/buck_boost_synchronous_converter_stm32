#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Standalone STM32 CDC diagnostic. Close psu_tuner.py before using it."""
from __future__ import annotations
import argparse
import time
import serial
from serial.tools import list_ports


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('port', nargs='?', help='USB CDC COM port, for example COM7')
    ap.add_argument('--seconds', type=float, default=12.0)
    args = ap.parse_args()
    if not args.port:
        for p in list_ports.comports():
            print(f'{p.device}: {p.description} ({p.hwid})')
        print('Usage: py usb_probe.py COM7')
        return
    print('Opening', args.port, '(close PC tuner before probing)')
    frames = 0
    with serial.Serial(args.port, 115200, timeout=0.12, write_timeout=0.5) as sp:
        sp.reset_input_buffer()
        sp.write(b'PING\nGET CFG\nGET STATUS\n')
        end = time.monotonic() + args.seconds
        last_req = time.monotonic()
        while time.monotonic() < end:
            frame = sp.readline().decode('ascii', errors='replace').strip()
            if frame:
                print('<', frame)
                if frame.startswith('T,'):
                    frames += 1
            if time.monotonic() - last_req >= 2.0:
                sp.write(b'GET STATUS\n')
                last_req = time.monotonic()
    print('Received telemetry frames:', frames)
    if not frames:
        print('No T frames. If no OK/PONG either: verify CDC_Receive_FS(), '
              'USB transmission and PSU_Link_Poll() in main loop.')
        print('If OK/STATUS exists but T missing: inspect psu_link.c telemetry scheduling.')

if __name__ == '__main__':
    main()
