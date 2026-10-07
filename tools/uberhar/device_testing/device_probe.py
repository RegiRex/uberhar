#!/usr/bin/env python3
"""CodexAstraUlt: Observe one device through an already-running local ADB server.

No server start/restart, pairing, TCP device connection, APK installation or game launch.
Only adb version and fixed read-only device requests are permitted by this probe.
"""

import argparse
import json
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

PACKAGE = "org.uberhar.uberhar_emu"
MAX_OUTPUT = 128 * 1024
SERIAL = re.compile(r"[A-Za-z0-9._:-]{1,256}\Z")


class ProbeError(Exception):
    # CodexAstraUlt: Errors expose categories, never raw command output or exception text.
    def __init__(self, code):
        self.code = code


class LocalAdb:
    """CodexAstraUlt: Minimal read-only smart-socket client; loopback only, no daemon lifecycle."""

    def __init__(self, timeout=5.0, port=5037):
        self.timeout = timeout
        self.port = port

    def _receive(self, connection, count, deadline):
        result = bytearray()
        while len(result) < count:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProbeError("timeout")
            connection.settimeout(remaining)
            part = connection.recv(count - len(result))
            if not part:
                raise ProbeError("adb_protocol_error")
            result.extend(part)
        return bytes(result)

    def _request(self, connection, service, deadline):
        data = service.encode("ascii")
        connection.settimeout(max(0.001, deadline - time.monotonic()))
        connection.sendall(f"{len(data):04x}".encode("ascii") + data)
        # CodexAstraUlt: Suppress FAIL payloads, which can contain host/device details.
        if self._receive(connection, 4, deadline) != b"OKAY":
            raise ProbeError("adb_request_rejected")

    def _query(self, service, serial=None):
        deadline = time.monotonic() + self.timeout
        try:
            with socket.create_connection(("127.0.0.1", self.port), self.timeout) as connection:
                if serial is not None:
                    self._request(connection, "host:transport:" + serial, deadline)
                self._request(connection, service, deadline)
                if serial is None:
                    try:
                        length = int(self._receive(connection, 4, deadline), 16)
                    except ValueError:
                        raise ProbeError("adb_protocol_error") from None
                    if length > MAX_OUTPUT:
                        raise ProbeError("output_limit")
                    return self._receive(connection, length, deadline).decode("utf-8", "replace")
                # CodexAstraUlt: Shell replies end at EOF; cap bytes and total elapsed time.
                result = bytearray()
                while True:
                    remaining = deadline - time.monotonic()
                    if remaining <= 0:
                        raise ProbeError("timeout")
                    connection.settimeout(remaining)
                    part = connection.recv(min(4096, MAX_OUTPUT + 1 - len(result)))
                    if not part:
                        return result.decode("utf-8", "replace")
                    result.extend(part)
                    if len(result) > MAX_OUTPUT:
                        raise ProbeError("output_limit")
        except (TimeoutError, socket.timeout):
            raise ProbeError("timeout") from None
        except ConnectionRefusedError:
            raise ProbeError("local_adb_server_unavailable") from None
        except OSError:
            raise ProbeError("local_adb_io_error") from None

    def devices(self):
        return self._query("host:devices-l")

    def property(self, serial, name):
        # CodexAstraUlt: Fixed allowlist prevents user-controlled remote shell commands.
        if name not in ("ro.product.manufacturer", "ro.product.model", "ro.build.version.sdk"):
            raise ProbeError("unsupported_query")
        return self._query("shell:getprop " + name, serial)

    def package_list(self, serial):
        return self._query("shell:pm list packages " + PACKAGE, serial)

    def package_details(self, serial):
        return self._query("shell:dumpsys package " + PACKAGE, serial)


def safe_value(value):
    # CodexAstraUlt: JSON contains only short printable metadata, never full dumpsys output.
    value = value.strip()
    if not value or len(value) > 128 or any(not character.isprintable() for character in value):
        raise ProbeError("invalid_metadata")
    return value


# CodexAstraLocal: "ready" confirms one authorized transport and readable package
# metadata. The caller still identifies the intended Thor and verifies signing,
# effective game settings and scene control before claiming test readiness.
def probe(serial=None, timeout=5.0, client=None):
    report = {"schema": 1, "status": "not_ready", "package": PACKAGE,
              "scope": "local_device_readiness_only"}
    try:
        if serial is not None and not SERIAL.fullmatch(serial):
            raise ProbeError("invalid_serial")
        adb = shutil.which("adb")
        if not adb:
            raise ProbeError("adb_not_on_path")
        # CodexAstraUlt: Version does not contact/start the daemon. Device operations below
        # use read-only socket services so adb cannot implicitly restart a mismatched server.
        try:
            result = subprocess.run([adb, "version"], capture_output=True, timeout=timeout,
                                    check=False)
        except subprocess.TimeoutExpired:
            raise ProbeError("adb_version_timeout") from None
        except OSError:
            raise ProbeError("adb_version_failed") from None
        version = re.search(rb"Android Debug Bridge version ([0-9.]+)", result.stdout[:4096])
        if result.returncode or not version:
            raise ProbeError("adb_version_failed")
        report["adb_version"] = version.group(1).decode("ascii")
        client = client or LocalAdb(timeout)
        devices = []
        for line in client.devices().splitlines():
            if not line.strip():
                continue
            parts = line.split()
            if len(parts) < 2 or not SERIAL.fullmatch(parts[0]):
                raise ProbeError("invalid_device_list")
            state = "no_permissions" if parts[1:3] == ["no", "permissions"] else parts[1]
            if state not in ("device", "unauthorized", "offline", "no_permissions", "recovery",
                             "sideload", "bootloader", "authorizing", "connecting"):
                state = "unavailable"
            devices.append({"serial": parts[0], "state": state})
        report["devices"] = devices
        if not devices:
            raise ProbeError("no_devices")
        if serial is None:
            if len(devices) != 1:
                raise ProbeError("serial_required")
            selected = devices[0]
        else:
            matches = [device for device in devices if device["serial"] == serial]
            if len(matches) != 1:
                raise ProbeError("serial_not_found")
            selected = matches[0]
        report["selected_serial"] = selected["serial"]
        if selected["state"] != "device":
            raise ProbeError("device_" + selected["state"])
        serial = selected["serial"]
        report["manufacturer"] = safe_value(client.property(serial, "ro.product.manufacturer"))
        report["model"] = safe_value(client.property(serial, "ro.product.model"))
        api = safe_value(client.property(serial, "ro.build.version.sdk"))
        if not api.isascii() or not api.isdecimal() or len(api) > 3:
            raise ProbeError("invalid_metadata")
        report["android_api"] = int(api)
        packages = client.package_list(serial).splitlines()
        installed = "package:" + PACKAGE in (line.strip() for line in packages)
        report["package_installed"] = installed
        if not installed:
            raise ProbeError("package_missing" if not packages else "package_query_unconfirmed")
        details = client.package_details(serial)
        version_name = re.search(r"(?m)^\s*versionName=([^\r\n]+)", details)
        version_code = re.search(r"\bversionCode=([0-9]+)\b", details)
        if not version_name or not version_code:
            raise ProbeError("package_version_unavailable")
        report["version_name"] = safe_value(version_name.group(1))
        report["version_code"] = int(version_code.group(1))
        report["status"] = "ready"
    except ProbeError as error:
        report["reason"] = error.code
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", help="Exact existing ADB serial; required when multiple devices exist")
    parser.add_argument("--timeout", type=float, default=5.0, help="Seconds per bounded query (0.1–30)")
    parser.add_argument("--output", type=Path, help="Also create this local JSON report; existing files are preserved")
    args = parser.parse_args()
    if not 0.1 <= args.timeout <= 30:
        parser.error("--timeout must be between 0.1 and 30 seconds")
    report = probe(args.serial, args.timeout)
    text = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    if args.output:
        try:
            # CodexAstraUlt: Only this explicitly requested local file is written; never overwrite.
            with args.output.open("x", encoding="utf-8") as target:
                target.write(text)
        except OSError:
            report["report_write_error"] = True
            text = json.dumps(report, indent=2, ensure_ascii=True) + "\n"
    sys.stdout.write(text)
    return 0 if report["status"] == "ready" and not report.get("report_write_error") else 1


if __name__ == "__main__":
    sys.exit(main())
