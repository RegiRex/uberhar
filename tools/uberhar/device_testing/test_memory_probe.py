#!/usr/bin/env python3
# Copyright 2026 Uberhar contributors
# Licensed under GPLv2 or any later version; see license.txt.
"""CodexAstraLocal: Offline parser/schedule controls with a fake client; never contacts ADB."""
import argparse
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("memory_probe", HERE / "memory_probe.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
OUTPUT = None


# CodexAstraLocal: Synthetic kernel replies exercise exact fixed framing and
# independent file failures without claiming actual kernel or device execution.
def reply(index=0, edits=None):
    values = {
        "uptime_before": (0, f"{100 + 5 * index}.00 240.00\n"),
        "uptime_after": (0, f"{100 + 5 * index}.10 240.03\n"),
        "pressure": (0, "some avg10=1.23 avg60=0.45 avg300=0.10 total=" +
                     str(1000 + 100 * index) + "\nfull avg10=0.00 avg60=0.00 "
                     "avg300=0.00 total=50\n"),
        "vmstat": (0, f"nr_free_pages 500\npgfault {100 + index}\npgmajfault 2\n"
                   "pgpgin 12\npgpgout 5\npswpin 0\npswpout 0\n"),
        "meminfo": (0, "MemTotal: 1000 kB\nMemFree: 250 kB\nMemAvailable: 500 kB\n"
                    "Dirty: 1 kB\nWriteback: 0 kB\nHugePages_Total: 0\n")}
    values.update(edits or {})
    result = []
    for key, path, cap in probe.FILES:
        rc, body = values[key]
        result += ["F", key, path, body, "R", str(rc)]
    return "\0".join(result + ["Z", str(len(probe.FILES)), ""])


class FakeClock:
    # CodexAstraLocal: Deterministic clock makes deadline and scheduling controls
    # run immediately, including missed slots and delayed query completion.
    def __init__(self):
        self.now = 0.0
        self.sleeps = []

    def monotonic(self):
        return self.now

    def sleep(self, value):
        self.sleeps.append(value)
        self.now += value

    def utc(self):
        return f"synthetic-relative-{self.now:.6f}"


class FakeClient:
    # CodexAstraLocal: The interface models only observed replies/failures; the
    # existing LocalAdb wire implementation is reused, not re-proven by this fake.
    def __init__(self, clock, fail=None, rows=None, lag=0.1, listing="TEST device\n"):
        self.clock, self.fail, self.rows, self.lag = clock, fail, rows, lag
        self.listing, self.calls = listing, []

    def devices(self):
        return self.listing

    def _query(self, service, serial):
        self.calls.append((service, serial, self.timeout))
        self.clock.now += self.lag
        if self.fail is not None:
            raise self.fail
        return self.rows[len(self.calls) - 1] if self.rows else reply(len(self.calls) - 1)


class Tests(unittest.TestCase):
    # CodexAstraLocal: Parser controls reject malformed units, incomplete records,
    # duplicates and resets rather than silently producing plausible zero values.
    def test_colocated_client_and_missing_explicit_root(self):
        # CodexAstraLocal: Packaging must use the adjacent reviewed client while
        # an invalid explicit root fails instead of selecting another transport.
        _, path = probe.load_probe()
        self.assertEqual(path.resolve(), (HERE / "device_probe.py").resolve())
        missing = Path(tempfile.mkdtemp(prefix="missing-root-", dir=OUTPUT))
        with self.assertRaisesRegex(ValueError, "existing_device_probe_not_found"):
            probe.load_probe(missing)

    def test_complete_reply(self):
        row = probe.parse_reply(reply())
        self.assertEqual(row["meminfo"]["value"]["MemAvailable"], 512000)
        self.assertEqual(row["pressure"]["value"]["some"]["total"], 1000)
        self.assertIsNone(row["vmstat"]["value"]["oom_kill"])

    def test_framing_negative_controls(self):
        data = reply()
        for bad in (data[:-1], data + "extra", data.replace("F\0pressure", "F\0wrong", 1),
                    data.replace("/proc/vmstat", "/proc/meminfo", 1),
                    data.replace("\0R\0" + "0\0", "\0R\0" + "999\0", 1),
                    data.replace("Z\0" + "5\0", "Z\0" + "6\0")):
            with self.subTest(bad=bad[-25:]), self.assertRaises((ValueError, UnicodeError)):
                probe.parse_reply(bad)

    def test_cap_plus_one(self):
        with self.assertRaisesRegex(ValueError, "truncated_file"):
            probe.parse_reply(reply(edits={"pressure": (0, "x" * 2049)}))

    def test_non_ascii(self):
        with self.assertRaises(UnicodeError):
            probe.parse_reply(reply().replace("pgfault", "pgfaült"))

    def test_optional_permission_is_not_zero(self):
        row = probe.parse_reply(reply(edits={"pressure": (1, "")}))
        self.assertIsNone(row["pressure"]["value"])
        self.assertEqual(row["pressure"]["returncode"], 1)
        self.assertEqual(row["meminfo"]["value"]["MemFree"], 256000)

    def test_bad_pressure_averages(self):
        for value in ("nan", "inf", "-1.00", "100.01"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                probe.parse_pressure("some avg10=" + value +
                                     " avg60=0.00 avg300=0.00 total=1")

    def test_pressure_missing_duplicate(self):
        for data in ("", "full avg10=0.00 avg60=0.00 avg300=0.00 total=0",
                     "some avg10=0.00 avg10=0.00 avg300=0.00 total=0"):
            with self.assertRaises(ValueError):
                probe.parse_pressure(data)

    def test_meminfo_units_and_duplicates(self):
        for data in ("MemTotal: 100 MB", "MemTotal: 100 kB\nMemTotal: 100 kB",
                     "MemTotal: 100 kB\nMemAvailable: 101 kB"):
            with self.assertRaises(ValueError):
                probe.parse_meminfo(data)

    def test_vmstat_duplicates_and_negative(self):
        for data in ("pgfault 2\npgfault 3\npgmajfault 0",
                     "pgfault -1\npgmajfault 0", "pgfault 2"):
            with self.assertRaises(ValueError):
                probe.parse_vmstat(data)

    def test_counter_overflow(self):
        with self.assertRaises(ValueError):
            probe.uint(str(1 << 64))

    def test_valid_interval_enclosure(self):
        row = probe.interval(probe.parse_reply(reply()), probe.parse_reply(reply(1)))
        self.assertTrue(row["valid_time"])
        self.assertEqual(row["elapsed_lower_ns"], 4_890_000_000)
        self.assertEqual(row["elapsed_upper_ns"], 5_110_000_000)
        self.assertEqual(row["pressure_stall_us"]["some"]["delta"], 100)
        self.assertEqual(row["vmstat_increments"]["pgfault"]["delta"], 1)

    def test_individual_counter_reset(self):
        a, b = probe.parse_reply(reply()), probe.parse_reply(reply(1))
        b["vmstat"]["value"]["pgfault"] = 1
        b["pressure"]["value"]["some"]["total"] = 0
        row = probe.interval(a, b)
        self.assertTrue(row["valid_time"])
        self.assertIsNone(row["vmstat_increments"]["pgfault"]["delta"])
        self.assertIsNone(row["pressure_stall_us"]["some"]["delta"])
        self.assertEqual(row["pressure_stall_us"]["full"]["delta"], 0)

    def test_uptime_reset(self):
        row = probe.interval(probe.parse_reply(reply(1)), probe.parse_reply(reply()))
        self.assertFalse(row["valid_time"])
        self.assertEqual(row["vmstat_increments"], {})

    def test_uptime_invalid_and_within_sample_reverse(self):
        for data in ("nan 1.00", "1 1", "1.0000000000 2.00", "-1.00 1.00"):
            with self.assertRaises(ValueError):
                probe.parse_uptime(data)
        with self.assertRaisesRegex(ValueError, "reversed"):
            probe.parse_reply(reply(edits={"uptime_after": (0, "99.00 240.00")}))

    def test_missing_uptime_invalidates_interval(self):
        row = probe.interval(probe.parse_reply(reply()),
                             probe.parse_reply(reply(1, {"uptime_before": (1, "")})))
        self.assertFalse(row["valid_time"])

    def test_impossible_pressure_delta(self):
        a, b = probe.parse_reply(reply()), probe.parse_reply(reply(1))
        b["pressure"]["value"]["some"]["total"] = 99_000_000
        row = probe.interval(a, b)
        self.assertIsNone(row["pressure_stall_us"]["some"]["delta"])

    # CodexAstraLocal: Full collector controls preserve create-only artifacts,
    # exact serial selection, failed observer bounds and a no-retry deadline.
    def run_collect(self, **kwargs):
        clock = kwargs.pop("clock", FakeClock())
        client = kwargs.pop("client", FakeClient(clock))
        out = Path(tempfile.mkdtemp(prefix="synthetic-", dir=OUTPUT)) / "capture"
        result = probe.collect("TEST", out, samples=3, client=client, clock=clock, **kwargs)
        return result, client, out

    def test_collector_success_and_no_bandwidth(self):
        result, client, out = self.run_collect()
        self.assertEqual(result["status"], "passive_observed")
        self.assertEqual(len(client.calls), 3)
        self.assertEqual(len(result["intervals"]), 2)
        self.assertEqual(result["samples"][2]["host_before_seconds"], 10)
        for key in ("ddr_bytes", "ddr_bytes_per_second", "bandwidth_percent", "cpu_cache_events"):
            self.assertIsNone(result["unsupported"][key])
        self.assertTrue((out / "report.json").is_file())
        self.assertTrue(all(call[1] == "TEST" and call[0].startswith("shell:timeout 2 sh -c")
                            for call in client.calls))

    def test_timeout_retains_bounds_and_stops(self):
        clock = FakeClock()
        client = FakeClient(clock, fail=TimeoutError("synthetic_timeout"), lag=2.5)
        result, client, _ = self.run_collect(clock=clock, client=client)
        self.assertEqual(len(client.calls), 1)
        self.assertEqual(result["samples"][0]["host_after_seconds"], 2.5)
        self.assertIn("transport_error", result["reason"])
        self.assertEqual(result["intervals"], [])

    def test_wrong_serial_no_query(self):
        clock = FakeClock()
        for listing in ("OTHER device\n", "TEST offline\n", "TEST device\nTEST device\n"):
            client = FakeClient(clock, listing=listing)
            result, _, _ = self.run_collect(clock=clock, client=client)
            self.assertEqual(client.calls, [])
            self.assertEqual(result["reason"], "explicit_device_not_online")

    def test_partial_framing_breaks_interval_chain(self):
        clock = FakeClock()
        client = FakeClient(clock, rows=[reply(), reply(1)[:-1], reply(2)])
        result, _, _ = self.run_collect(clock=clock, client=client)
        self.assertEqual(result["intervals"], [])
        self.assertIn("parse_error", result["samples"][1]["reason"])

    def test_overlarge_reply_stops(self):
        clock = FakeClock()
        client = FakeClient(clock, rows=["x" * (probe.MAX_REPLY + 1)])
        result, client, _ = self.run_collect(clock=clock, client=client)
        self.assertEqual(len(client.calls), 1)
        self.assertEqual(result["reason"], "reply_limit")

    def test_late_sample_no_catchup(self):
        clock = FakeClock()
        client = FakeClient(clock, lag=9.0)
        result, client, _ = self.run_collect(clock=clock, client=client)
        self.assertEqual(len(client.calls), 2)
        self.assertEqual(result["samples"][1]["reason"], "scheduled_sample_missed_no_catchup")

    def test_full_opening_schedule_is_finite(self):
        # CodexAstraLocal: Exercise the new maximum with the fake clock, retaining
        # exactly 61 calls and 60 adjacent enclosures without a real five-minute wait.
        clock = FakeClock()
        client = FakeClient(clock)
        out = Path(tempfile.mkdtemp(prefix="full-opening-", dir=OUTPUT)) / "capture"
        result = probe.collect("TEST", out, samples=61, client=client, clock=clock)
        self.assertEqual(len(client.calls), 61)
        self.assertEqual(len(result["intervals"]), 60)
        self.assertEqual(result["samples"][-1]["host_before_seconds"], 300)
        self.assertAlmostEqual(result["elapsed_seconds"], 300.1)
        self.assertEqual(result["status"], "passive_observed")

    def test_create_only_and_invalid_schedule(self):
        path = Path(tempfile.mkdtemp(prefix="existing-", dir=OUTPUT))
        with self.assertRaises(FileExistsError):
            probe.collect("TEST", path, client=FakeClient(FakeClock()))
        for values in ({"samples": 1}, {"samples": 62}, {"period": float("nan")},
                       {"period": 1}, {"timeout": 5}, {"delay": 61},
                       {"samples": 13, "period": 30}, {"samples": 61, "period": 5.01}):
            with self.subTest(values=values), self.assertRaises(ValueError):
                probe.collect("TEST", path / "unused", client=FakeClient(FakeClock()), **values)
        with self.assertRaises(ValueError):
            probe.collect("TEST;command", path / "unused", client=FakeClient(FakeClock()))


def main():
    # CodexAstraLocal: Retain finite source-bound synthetic results exclusively;
    # this entry point instantiates no LocalAdb and runs no generated shell.
    global OUTPUT
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/passive-memory"))
    args = parser.parse_args()
    # CodexAstraLocal: Repeated host gates retain a distinct source-bound fixture;
    # collection itself still requires a new capture directory.
    args.output.mkdir(parents=True, exist_ok=True)
    OUTPUT = Path(tempfile.mkdtemp(prefix="run-", dir=args.output))
    log = io.StringIO()
    result = unittest.TextTestRunner(stream=log, verbosity=2).run(
        unittest.defaultTestLoader.loadTestsFromTestCase(Tests))
    (OUTPUT / "tests.txt").write_text(log.getvalue())
    _, client_path = probe.load_probe()
    paths = [HERE / "memory_probe.py", Path(__file__), client_path]
    sources = {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}
    # CodexAstraLocal: Retain the exact helper/test/client used by this offline
    # proof so later packaging edits do not obscure its executed source.
    (OUTPUT / "sources").mkdir()
    for path in paths:
        (OUTPUT / "sources" / path.name).write_bytes(path.read_bytes())
    report = {"author": "CodexAstraLocal", "scope": "synthetic_offline_no_device_or_socket",
              "tests": result.testsRun, "failures": len(result.failures),
              "errors": len(result.errors), "pass": result.wasSuccessful(), "sources": sources}
    (OUTPUT / "provenance.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"pass": report["pass"], "tests": result.testsRun, "proof": str(OUTPUT / "provenance.json")}))
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
