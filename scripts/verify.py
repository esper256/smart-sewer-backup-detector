#!/usr/bin/env python3
"""Checks that do not need a Photon 2 or Particle libraries."""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FIRMWARE = ROOT / "firmware"
CONFIG = FIRMWARE / "src" / "config_and_secrets.h"
CPP = FIRMWARE / "src" / "sewer-backup-detector.cpp"
PROPS = FIRMWARE / "project.properties"
HA = ROOT / "home-assistant"
HTTP_YAML = HA / "sewer-http.yaml"
MQTT_YAML = HA / "sewer-mqtt.yaml"


def fail(msg: str) -> None:
    raise SystemExit(msg)


def defined(text: str, name: str) -> bool:
    return bool(re.search(rf"^#define {name}\s*$", text, re.M))


def quoted(text: str, name: str) -> str:
    m = re.search(rf'{name} = "([^"]*)"', text)
    if not m:
        fail(f"missing {name}")
    return m.group(1)


def check_yaml() -> None:
    try:
        import yaml
    except ImportError:
        fail("pip install pyyaml")

    for path in (HTTP_YAML, MQTT_YAML):
        list(yaml.safe_load_all(path.read_text()))
        print(f"yaml ok  {path.relative_to(ROOT)}")


def check_firmware() -> None:
    config = CONFIG.read_text()
    cpp = CPP.read_text()
    http_yaml = HTTP_YAML.read_text()
    mqtt_yaml = MQTT_YAML.read_text()
    props = PROPS.read_text()

    http = defined(config, "SEWER_TRANSPORT_HTTP")
    mqtt = defined(config, "SEWER_TRANSPORT_MQTT")
    if http == mqtt:
        fail("define exactly one of SEWER_TRANSPORT_HTTP or SEWER_TRANSPORT_MQTT")
    if not http:
        fail("checked-in default must be HTTP")

    path = quoted(config, "HA_WEBHOOK_PATH")
    m = re.fullmatch(r"/api/webhook/([^/]+)", path)
    if not m:
        fail(f"HA_WEBHOOK_PATH should be /api/webhook/<id>, got {path!r}")
    webhook_id = m.group(1)
    if f"webhook_id: {webhook_id}" not in http_yaml:
        fail(f"sewer-http.yaml webhook_id must match {webhook_id}")

    for name, value in (
        ("TOPIC_REED", quoted(cpp, "TOPIC_REED")),
        ("TOPIC_AVAILABILITY", quoted(cpp, "TOPIC_AVAILABILITY")),
        ("PAYLOAD_OPEN", quoted(cpp, "PAYLOAD_OPEN")),
        ("PAYLOAD_CLOSED", quoted(cpp, "PAYLOAD_CLOSED")),
    ):
        if value not in mqtt_yaml:
            fail(f"sewer-mqtt.yaml missing {name} value {value!r}")

    if "dependencies.HttpClient=" not in props or "dependencies.MQTT=" not in props:
        fail("project.properties must pin HttpClient and MQTT")

    print("firmware strings match HA packages")


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
    check_firmware()
    check_host_firmware()


if __name__ == "__main__":
    try:
        main()
    except subprocess.CalledProcessError as e:
        sys.exit(e.returncode)
    except Exception as e:
        print(e, file=sys.stderr)
        sys.exit(1)
