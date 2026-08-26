#!/usr/bin/env python3
"""Parse HA YAML and compile the firmware against host fakes."""

from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"
CPP = FIRMWARE / "src" / "sewer-backup-detector.cpp"
HA = ROOT / "home-assistant"


def fail(msg: str) -> None:
    raise SystemExit(msg)


def check_yaml() -> None:
    try:
        import yaml
    except ImportError:
        fail("pip install pyyaml")

    files = sorted(HA.glob("*.yaml"))
    if not files:
        fail(f"no YAML files in {HA}")
    for path in files:
        list(yaml.safe_load_all(path.read_text()))
        print(f"yaml ok  {path.relative_to(ROOT)}")


def check_host_firmware() -> None:
    sys.stdout.flush()
    out = ROOT / "test" / "firmware_test"
    cmd = [
        "g++",
        "-std=c++17",
        "-Wall",
        "-Wextra",
        "-Werror",
        "-I",
        str(ROOT / "test" / "fakes"),
        "-I",
        str(FIRMWARE / "src"),
        str(ROOT / "test" / "test_firmware.cpp"),
        str(CPP),
        "-o",
        str(out),
    ]
    subprocess.check_call(cmd)
    subprocess.check_call([str(out)])


def main() -> None:
    check_yaml()
    check_host_firmware()


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)
    except Exception as e:
        print(e, file=sys.stderr)
        sys.exit(1)
