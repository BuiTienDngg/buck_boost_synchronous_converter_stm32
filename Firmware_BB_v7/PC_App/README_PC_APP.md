# SmartPSU PC App v9.4

The tuner lives inside `Firmware_BB_v7/PC_App`.

## Installing / repairing pip

1. Double-click `setup_windows.bat`. It runs `setup_app.py`.
2. If `.venv/Scripts/python.exe` exists without the pip module, the installer
   calls `.venv/Scripts/python.exe -m ensurepip --upgrade` and checks it.
3. If ensurepip is unavailable, it tries Python's system pip with the `--python`
   option to bootstrap into the *existing* `.venv`.
4. Dependencies `pyserial` and `matplotlib` are installed. Python's `tkinter`
   is also checked.
5. Double-click `start_windows.bat` to open the app. It automatically invokes
   setup when required imports are missing.

You may also run from PowerShell inside `PC_App`:

```powershell
py -3 .\setup_app.py
.\start_windows.bat
```

If both pip bootstrapping options fail, repair/install a standard Python 3 from
python.org with pip and Tk enabled, then rerun setup. The installer does not
remove `.venv` or affect MCU firmware.

## USB diagnostics

Select the STM32's USB CDC COM port. `COM open` means the port was opened;
`STM32 telemetry active` means valid telemetry was received.

If the plot is empty, close the app and use (replace COM7):

```powershell
.\.venv\Scripts\python.exe .\usb_probe.py COM7
```

Expected replies include `OK,PONG,PSU_V9`, `OK,STATUS`, and `T,...` lines.
