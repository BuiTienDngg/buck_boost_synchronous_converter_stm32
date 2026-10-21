"""Rebuild with the installed Keil and verify the native C8 memory boundaries.

Usage: python tools/build_keil.py --uv4 D:/KeilC/UV4/UV4.exe
Does not flash a device or modify the two settings pages.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import struct
import subprocess
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--uv4', default=os.environ.get('KEIL_UV4'))
args = parser.parse_args()
candidates = [Path(args.uv4)] if args.uv4 else [
    Path('D:/KeilC/UV4/UV4.exe'), Path('C:/Keil_v5/UV4/UV4.exe')]
uv4 = next((p.resolve() for p in candidates if p.is_file()), None)
if uv4 is None:
    raise SystemExit('Keil not found; pass --uv4 <path to UV4.exe>')
project = ROOT / 'MDK-ARM/Firmware_BB_V6.uvprojx'
xml = ET.parse(project)
assert xml.find('.//Cads/Optim').text == '6', 'Select ArmClang -Os'
assert xml.find('.//ArmAdsMisc/useUlib').text == '1', 'Enable MicroLIB'
assert xml.find('.//Cads/v6Lto').text == '1', 'Enable ArmClang LTO for the 62 KiB program region'
output = ROOT / 'build/keil'
output.mkdir(parents=True, exist_ok=True)
report = output / 'verification.json'
# A failed new build must not leave a success report from an older build.
report.write_text(json.dumps({'status': 'building'}, indent=2))
log = output / 'rebuild.log'
startup = subprocess.STARTUPINFO()
startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
startup.wShowWindow = 0
result = subprocess.run([str(uv4), '-r', str(project), '-j0', '-o', str(log)],
                        cwd=project.parent, startupinfo=startup, timeout=120)
text = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
print(text)
assert result.returncode == 0 and re.search(
    r'\b0 Error\(s\), 0 Warning\(s\)', text), 'Native Keil rebuild failed; see rebuild.log'
dep = (project.parent / 'Firmware_BB_V6/Firmware_BB_V6_Firmware_BB_V6.dep').read_text()
assert '-Oz' in dep and '-D__MICROLIB' in dep
assert '-Ofast' not in dep and '-ffast-math' not in dep
hexfile = project.parent / 'Firmware_BB_V6/Firmware_BB_V6.hex'
memory = {}
base = 0
eof = False
for line in hexfile.read_text().splitlines():
    assert line.startswith(':')
    record = bytes.fromhex(line[1:])
    assert len(record) == record[0] + 5 and sum(record) % 256 == 0
    count, address, kind = record[0], int.from_bytes(record[1:3], 'big'), record[3]
    data = record[4:4 + count]
    assert not eof, 'Records after EOF'
    if kind == 0:
        for offset, value in enumerate(data):
            absolute = base + address + offset
            assert 0x08000000 <= absolute < 0x0800F800, 'HEX enters settings Flash'
            assert absolute not in memory or memory[absolute] == value
            memory[absolute] = value
    elif kind == 4:
        assert count == 2
        base = int.from_bytes(data, 'big') << 16
    elif kind == 1:
        assert count == 0
        eof = True
    else:
        assert kind == 5, 'Unexpected HEX record'
assert eof and memory
stack, reset = struct.unpack('<II', bytes(memory[0x08000000+i] for i in range(8)))
assert 0x20000C00 <= stack <= 0x20005000 and stack % 8 == 0
assert reset & 1 and 0x08000000 <= reset < 0x0800F800
size = re.search(r'Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)', text)
assert size, 'Keil did not report memory usage'
code, ro, rw, zi = map(int, size.groups())
# ArmClang may compress initialized RW data; Code+RO+RW overcounts its load image.
# Validate every HEX address above and the actual on-device span here.
assert max(memory)+1-0x08000000 <= 62*1024 and rw + zi <= 20*1024
summary = {
    'status': 'passed', 'utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
    'compiler': dep.splitlines()[1], 'optimization': '-Oz', 'library': 'MicroLIB',
    'lto': xml.find('.//Cads/v6Lto').text == '1',
    'errors': 0, 'warnings': 0, 'code': code, 'ro_data': ro, 'rw_data': rw, 'zi_data': zi,
    'ram_bytes_including_stack_heap': rw+zi,
    'hex_bytes': len(memory), 'flash_span_bytes': max(memory)+1-0x08000000,
    'flash_limit_bytes': 62*1024, 'settings_base': '0x0800F800',
    'initial_sp': hex(stack), 'reset_vector': hex(reset),
    'hex_sha256': hashlib.sha256(hexfile.read_bytes()).hexdigest(),
    'project_sha256': hashlib.sha256(project.read_bytes()).hexdigest(),
    'hardware': 'Build only; not flashed; no output commands sent',
}
report.write_text(json.dumps(summary, indent=2))
print(json.dumps(summary, indent=2))
