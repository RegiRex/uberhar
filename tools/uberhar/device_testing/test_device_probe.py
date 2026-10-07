#!/usr/bin/env python3
"""CodexAstraUlt: Fake local ADB executable/server tests; never contact a phone or real daemon."""

import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import socketserver
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

# CodexAstraLocal: Load the actual adjacent probe so the fake transport exercises
# production validation, rather than a duplicate implementation of its decisions.
HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("device_probe", HERE / "device_probe.py")
PROBE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PROBE)


# CodexAstraLocal: Bind only an ephemeral loopback fixture; no request reaches a
# real ADB daemon or connected device, including authorization-failure tests.
class FakeAdbServer(socketserver.ThreadingTCPServer):
    # CodexAstraUlt: Ephemeral loopback test port cannot replace the user's real ADB server.
    allow_reuse_address = True
    daemon_threads = True


# CodexAstraLocal: Model the limited length-prefixed transport and record every
# requested service, making unexpected device reads visible to the assertions.
class Handler(socketserver.BaseRequestHandler):
    def read_service(self):
        header = self.request.recv(4)
        if len(header) != 4:
            return ""
        size = int(header, 16)
        data = bytearray()
        while len(data) < size:
            part = self.request.recv(size - len(data))
            if not part:
                break
            data.extend(part)
        service = data.decode("ascii")
        self.server.services.append(service)
        return service

    def handle(self):
        try:
            service = self.read_service()
            if self.server.delay:
                time.sleep(self.server.delay)
            if service == "host:devices-l":
                data = self.server.listing.encode()
                self.request.sendall(b"OKAY" + f"{len(data):04x}".encode() + data)
                return
            if not service.startswith("host:transport:"):
                self.request.sendall(b"FAIL0000")
                return
            self.request.sendall(b"OKAY")
            service = self.read_service()
            data = self.server.replies.get(service)
            if data is None:
                self.request.sendall(b"FAIL0000")
            else:
                self.request.sendall(b"OKAY" + data.encode())
        except (BrokenPipeError, ConnectionResetError):
            pass  # CodexAstraUlt: Expected when the bounded client rejects a timeout/oversized reply.


# CodexAstraLocal: Each case owns its fake executable, response bank and server;
# teardown restores PATH and releases the fixture even after an assertion fails.
class DeviceProbeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        adb = root / "adb"
        # CodexAstraUlt: Assert that production invokes only the safe version command.
        adb.write_text("#!/usr/bin/env python3\nimport os, sys, time\n"
                       "assert sys.argv[1:] == ['version']\n"
                       "if os.environ.get('FAKE_ADB_SLOW'): time.sleep(2)\n"
                       "print('Android Debug Bridge version 1.0.41')\n")
        adb.chmod(0o700)
        self.environment = patch.dict(os.environ, {"PATH": str(root) + os.pathsep + os.environ["PATH"]})
        self.environment.start()
        self.server = FakeAdbServer(("127.0.0.1", 0), Handler)
        self.server.services = []
        self.server.delay = 0
        self.server.listing = "THOR123\tdevice product:ignored model:ignored transport_id:1\n"
        self.server.replies = {
            "shell:getprop ro.product.manufacturer": "AYN\n",
            "shell:getprop ro.product.model": "Thor\n",
            "shell:getprop ro.build.version.sdk": "33\n",
            "shell:pm list packages " + PROBE.PACKAGE: "package:" + PROBE.PACKAGE + "\n",
            "shell:dumpsys package " + PROBE.PACKAGE:
                "Private detail must not appear\n  versionCode=120 minSdk=29\n  versionName=0.1.20\n",
        }
        self.thread = threading.Thread(target=self.server.serve_forever, kwargs={"poll_interval": .01})
        self.thread.start()

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join()
        self.environment.stop()
        self.temporary.cleanup()

    # CodexAstraLocal: Inject the fixture port while retaining real probe code,
    # serial selection and timeout handling.
    def run_probe(self, serial=None, timeout=.5):
        return PROBE.probe(serial, timeout, PROBE.LocalAdb(timeout, self.server.server_address[1]))

    # CodexAstraLocal: Readiness must expose only validated public metadata and
    # the small read allowlist, excluding unrelated package dump details.
    def test_ready_and_only_allowlisted_reads(self):
        report = self.run_probe()
        self.assertEqual(report["status"], "ready")
        self.assertEqual((report["model"], report["android_api"], report["version_name"],
                          report["version_code"]), ("Thor", 33, "0.1.20", 120))
        self.assertNotIn("Private detail", json.dumps(report))
        self.assertTrue(all(service in self.server.replies or service in
                            ("host:devices-l", "host:transport:THOR123")
                            for service in self.server.services))

    # CodexAstraLocal: Authorization and ambiguous/wrong serials must stop before
    # opening a device transport; explicit selection may choose the authorized one.
    def test_unauthorized_device_never_queries_phone(self):
        self.server.listing = "THOR123\tunauthorized\n"
        self.assertEqual(self.run_probe()["reason"], "device_unauthorized")
        self.assertEqual(self.server.services, ["host:devices-l"])

    def test_multiple_devices_require_explicit_serial(self):
        self.server.listing += "SECOND\tdevice\n"
        self.assertEqual(self.run_probe()["reason"], "serial_required")
        self.assertEqual(self.server.services, ["host:devices-l"])

    def test_explicit_serial_works_with_multiple_devices(self):
        self.server.listing += "SECOND\tunauthorized\n"
        self.assertEqual(self.run_probe("THOR123")["status"], "ready")
        self.assertNotIn("host:transport:SECOND", self.server.services)

    def test_wrong_serial_never_queries_phone(self):
        self.assertEqual(self.run_probe("OTHER")["reason"], "serial_not_found")
        self.assertEqual(self.server.services, ["host:devices-l"])

    # CodexAstraLocal: Installation and version availability are separate gates;
    # neither an absent package nor missing version text may invent readiness.
    def test_missing_package(self):
        self.server.replies["shell:pm list packages " + PROBE.PACKAGE] = ""
        report = self.run_probe()
        self.assertEqual(report["reason"], "package_missing")
        self.assertFalse(report["package_installed"])
        self.assertNotIn("shell:dumpsys package " + PROBE.PACKAGE, self.server.services)

    def test_package_version_not_invented(self):
        self.server.replies["shell:dumpsys package " + PROBE.PACKAGE] = "unavailable\n"
        self.assertEqual(self.run_probe()["reason"], "package_version_unavailable")

    # CodexAstraLocal: Bound both socket and executable-version waits, and retain
    # explicit missing-tool/permission/device reasons instead of attempting repair.
    def test_socket_timeout_is_bounded(self):
        self.server.delay = .4
        start = time.monotonic()
        self.assertEqual(self.run_probe(timeout=.1)["reason"], "timeout")
        self.assertLess(time.monotonic() - start, 1)

    def test_version_subprocess_timeout_is_bounded(self):
        with patch.dict(os.environ, {"FAKE_ADB_SLOW": "1"}):
            self.assertEqual(self.run_probe(timeout=.1)["reason"], "adb_version_timeout")
        self.assertEqual(self.server.services, [])

    def test_missing_adb(self):
        with patch.dict(os.environ, {"PATH": "/nonexistent-probe-path"}):
            self.assertEqual(self.run_probe()["reason"], "adb_not_on_path")

    def test_permission_failure_is_explicit(self):
        self.server.listing = "THOR123\tno permissions (user in plugdev group)\n"
        self.assertEqual(self.run_probe()["reason"], "device_no_permissions")

    def test_no_devices(self):
        self.server.listing = ""
        self.assertEqual(self.run_probe()["reason"], "no_devices")

    # CodexAstraLocal: Reject oversized or control-bearing metadata and unsafe
    # serial input before it can reach a report or an unintended command.
    def test_oversized_device_output_is_rejected(self):
        self.server.replies["shell:getprop ro.product.model"] = "X" * (PROBE.MAX_OUTPUT + 1)
        self.assertEqual(self.run_probe()["reason"], "output_limit")

    def test_control_characters_are_not_reported(self):
        self.server.replies["shell:getprop ro.product.model"] = "Thor\x1b[31m"
        self.assertEqual(self.run_probe()["reason"], "invalid_metadata")

    def test_invalid_serial_rejected_before_adb(self):
        self.assertEqual(self.run_probe("bad; command")["reason"], "invalid_serial")
        self.assertEqual(self.server.services, [])

    # CodexAstraLocal: Optional report persistence is create-only; failure must
    # report the conflict while preserving the owner's existing evidence bytes.
    def test_optional_report_preserves_existing_file(self):
        path = Path(self.temporary.name) / "report.json"
        path.write_text("keep me")
        with patch.object(PROBE, "probe", return_value={"status": "ready"}), \
                patch("sys.argv", ["device_probe.py", "--output", str(path)]), \
                contextlib.redirect_stdout(io.StringIO()) as output:
            self.assertEqual(PROBE.main(), 1)
        self.assertTrue(json.loads(output.getvalue())["report_write_error"])
        self.assertEqual(path.read_text(), "keep me")


# CodexAstraLocal: Direct execution runs only the isolated host unittest suite.
if __name__ == "__main__":
    unittest.main(verbosity=2)
