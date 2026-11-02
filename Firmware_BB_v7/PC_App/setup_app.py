#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Repair/install local PC_App/.venv dependencies for SmartPSU.

A previous v9.3 setup skipped recovery when .venv/Scripts/python.exe existed
but its pip module had not been installed. This script repairs such environments
without replacing the virtual environment or changing firmware files.
"""
from __future__ import annotations

from pathlib import Path
import subprocess
import sys
import venv


APP_DIR = Path(__file__).resolve().parent


def interpreter(venv_path: Path) -> Path:
    return venv_path / ("Scripts/python.exe" if sys.platform == "win32" else "bin/python")


def run(args: list[str], *, check: bool = False) -> bool:
    shown = " ".join(str(x) for x in args)
    print("  >", shown, flush=True)
    try:
        result = subprocess.run(args, check=False)
    except (OSError, ValueError) as exc:
        print("  Command could not start:", exc, flush=True)
        return False
    if check and result.returncode != 0:
        raise RuntimeError(f"Command failed (exit code {result.returncode}): {shown}")
    return result.returncode == 0


def has_pip(py: Path) -> bool:
    # Running pip -V is safe and does not require network access.
    try:
        return subprocess.run([str(py), "-m", "pip", "--version"],
                              stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, check=False).returncode == 0
    except OSError:
        return False


def restore_pip(py: Path, env_dir: Path) -> None:
    if has_pip(py):
        return
    print("pip is missing in PC_App/.venv. Attempting repair.", flush=True)

    # Best path: use the Python standard library. Works with python.org installs.
    if run([str(py), "-m", "ensurepip", "--upgrade"]) and has_pip(py):
        print("pip repaired using ensurepip.", flush=True)
        return

    # Fallback for venvs without ensurepip: pip >= 22.3 on the parent Python
    # can manage/install pip into a *different* environment via --python.
    print("Trying the parent Python pip as a fallback...", flush=True)
    if run([sys.executable, "-m", "pip", "--python", str(env_dir),
            "install", "--upgrade", "pip"]) and has_pip(py):
        print("pip repaired through the parent interpreter.", flush=True)
        return

    raise RuntimeError(
        "No working pip inside PC_App/.venv. In Windows, repair/install Python "
        "from python.org with the pip and venv components enabled, then run "
        "setup_windows.bat again. Your PC_App files and .venv have been preserved."
    )


def setup_app(app_dir: Path = APP_DIR, *, verify_imports: tuple[str, ...] =
              ("serial", "matplotlib", "tkinter")) -> Path:
    env_dir = app_dir / ".venv"
    py = interpreter(env_dir)
    requirements = app_dir / "requirements.txt"
    if not requirements.is_file():
        raise RuntimeError(f"Cannot find requirements: {requirements}")
    if not py.is_file():
        print("Creating isolated Python environment:", env_dir, flush=True)
        # Python's venv tries to seed pip. If it fails, we will detect and
        # provide a clear error on the next step.
        try:
            venv.EnvBuilder(with_pip=False).create(str(env_dir))
        except (OSError, RuntimeError) as exc:
            raise RuntimeError(f"Cannot create .venv: {exc}") from exc
    if not py.is_file():
        raise RuntimeError(f"Virtual environment Python was not created: {py}")

    restore_pip(py, env_dir)
    print("Installing PC_App requirements...", flush=True)
    run([str(py), "-m", "pip", "install", "--disable-pip-version-check",
         "-r", str(requirements)], check=True)

    if verify_imports:
        modules = ", ".join(verify_imports)
        print("Checking required modules:", modules, flush=True)
        run([str(py), "-c", "; ".join(f"import {m}" for m in verify_imports)],
            check=True)
    print("Success. Run start_windows.bat to start SmartPSU PC App.", flush=True)
    return py


def main() -> int:
    try:
        setup_app()
        return 0
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print("\nSetup error:", exc, file=sys.stderr, flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
