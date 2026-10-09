#!/usr/bin/env python3
# Copyright 2026 Uberhar contributors
# Licensed under GPLv2 or any later version; see license.txt.
"""CodexAstraLocal: Finite explicit-serial passive memory observations, not bandwidth."""
import argparse
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import shlex
import time


# CodexAstraLocal: Fixed paths and caps prevent caller-supplied remote commands;
# cap+1 reads expose truncation, and uptime brackets the non-atomic file reads.
FILES = (("uptime_before", "/proc/uptime", 128),
         ("pressure", "/proc/pressure/memory", 2048),
         ("vmstat", "/proc/vmstat", 32768),
         ("meminfo", "/proc/meminfo", 16384),
         ("uptime_after", "/proc/uptime", 128))
VM_COUNTERS = ("pgfault", "pgmajfault", "pgpgin", "pgpgout", "pswpin", "pswpout",
               "pgscan_kswapd", "pgscan_direct", "pgsteal_kswapd", "pgsteal_direct",
               "allocstall_dma", "allocstall_dma32", "allocstall_normal", "allocstall_movable",
               "workingset_refault_anon", "workingset_refault_file", "oom_kill",
               "compact_stall", "compact_fail", "compact_success")
MEM_GAUGES = ("MemTotal", "MemFree", "MemAvailable", "Buffers", "Cached", "SwapTotal",
              "SwapFree", "Dirty", "Writeback", "AnonPages", "Slab", "SReclaimable",
              "SUnreclaim", "PageTables")
MAX_REPLY = 65536
U64_MAX = (1 << 64) - 1


# CodexAstraLocal: Importing the existing client is inert. No adb subprocess,
# daemon lifecycle, alternate transport, profile tool or policy write is used.
def load_probe(repo=None):
    # CodexAstraLocal: Co-located files also work outside a checkout. An explicit
    # repository selects only that client; no alternate transport is discovered.
    path = (Path(repo) / "tools/uberhar/device_testing/device_probe.py" if repo
            else Path(__file__).resolve().with_name("device_probe.py"))
    if not path.is_file():
        raise ValueError("existing_device_probe_not_found")
    spec = importlib.util.spec_from_file_location("memory_probe_local_adb", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module, path


def uint(text):
    if not re.fullmatch(r"[0-9]{1,20}", text) or int(text) > U64_MAX:
        raise ValueError("invalid_unsigned_counter")
    return int(text)


# CodexAstraLocal: Original source units remain explicit. PSI is stall pressure;
# vmstat increments and memory occupancy never become transferred byte counts.
def parse_uptime(text):
    fields = text.split()
    if len(fields) != 2 or any(not re.fullmatch(r"[0-9]{1,12}\.[0-9]{1,9}", x)
                               for x in fields):
        raise ValueError("invalid_uptime")
    value = Decimal(fields[0])
    return {"ns": int(value * 1_000_000_000),
            "quantum_ns": 10 ** (9 - len(fields[0].split(".")[1]))}


def parse_pressure(text):
    result = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 5 or parts[0] not in ("some", "full") or parts[0] in result:
            raise ValueError("invalid_pressure_row")
        values = {}
        for token in parts[1:]:
            if token.count("=") != 1:
                raise ValueError("invalid_pressure_field")
            key, value = token.split("=")
            if key in values:
                raise ValueError("duplicate_pressure_field")
            if key == "total":
                values[key] = uint(value)
            elif key in ("avg10", "avg60", "avg300"):
                if not re.fullmatch(r"[0-9]{1,3}\.[0-9]{1,9}", value):
                    raise ValueError("invalid_pressure_average")
                if not 0 <= Decimal(value) <= 100:
                    raise ValueError("pressure_average_out_of_range")
                values[key] = value
            else:
                raise ValueError("unknown_pressure_field")
        if set(values) != {"avg10", "avg60", "avg300", "total"}:
            raise ValueError("missing_pressure_field")
        result[parts[0]] = values
    if "some" not in result:
        raise ValueError("missing_pressure_some")
    return result


def parse_vmstat(text):
    values = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) != 2 or not re.fullmatch(r"[a-z][a-z0-9_]*", parts[0]):
            raise ValueError("invalid_vmstat_row")
        if parts[0] in values:
            raise ValueError("duplicate_vmstat_key")
        values[parts[0]] = uint(parts[1])
    if "pgfault" not in values or "pgmajfault" not in values:
        raise ValueError("incomplete_vmstat")
    return {key: values.get(key) for key in VM_COUNTERS}


def parse_meminfo(text):
    values = {}
    for line in text.splitlines():
        match = re.fullmatch(r"([A-Za-z][A-Za-z0-9_()]*):\s+([0-9]{1,20})(?:\s+(kB))?", line)
        if not match or match[1] in values:
            raise ValueError("invalid_or_duplicate_meminfo")
        values[match[1]] = (uint(match[2]), match[3])
    result = {}
    for key in MEM_GAUGES:
        row = values.get(key)
        if row is not None and row[1] != "kB":
            raise ValueError("invalid_meminfo_unit")
        result[key] = None if row is None else row[0] * 1024
    if not result["MemTotal"]:
        raise ValueError("missing_memtotal")
    for key in ("MemFree", "MemAvailable"):
        if result[key] is not None and result[key] > result["MemTotal"]:
            raise ValueError("invalid_memory_gauge")
    return result


# CodexAstraLocal: Preserve exact order/return codes and reject truncated framing.
# The reused text client decodes replies, so hashes are of the retained reply,
# not a claim to preserve arbitrary non-UTF8 wire bytes.
def parse_reply(reply):
    reply.encode("ascii", "strict")
    if len(reply) > MAX_REPLY:
        raise ValueError("reply_limit")
    parts = reply.split("\0")
    result, index = {}, 0
    parsers = {"uptime_before": parse_uptime, "uptime_after": parse_uptime,
               "pressure": parse_pressure, "vmstat": parse_vmstat, "meminfo": parse_meminfo}
    for key, path, cap in FILES:
        if len(parts) < index + 6 or parts[index:index + 3] != ["F", key, path]:
            raise ValueError("file_identity_or_order")
        body, marker, rc = parts[index + 3:index + 6]
        if marker != "R" or not re.fullmatch(r"[0-9]{1,3}", rc) or int(rc) > 255:
            raise ValueError("invalid_file_result")
        if len(body) > cap:
            raise ValueError("truncated_file_" + key)
        row = {"path": path, "returncode": int(rc), "value": None}
        if int(rc):
            row["reason"] = "read_failed"
        else:
            try:
                row["value"] = parsers[key](body.strip())
            except ValueError as exc:
                row["reason"] = str(exc)
        result[key] = row
        index += 6
    if parts[index:] != ["Z", str(len(FILES)), ""]:
        raise ValueError("missing_or_extra_footer")
    before, after = (result[key]["value"] for key in ("uptime_before", "uptime_after"))
    if before and after and after["ns"] < before["ns"]:
        raise ValueError("uptime_reversed_within_sample")
    return result


def device_script():
    # CodexAstraLocal: Only these fixed proc files are read; nonzero head status
    # and cap+1 payloads remain evidence. No remote temporary files are created.
    script = """memory_read() {
  printf 'F\\000%s\\000%s\\000' "$1" "$2"
  head -c "$3" "$2" 2>/dev/null
  memory_rc=$?
  printf '\\000R\\000%s\\000' "$memory_rc"
}
"""
    for key, path, cap in FILES:
        script += f"memory_read {key} {path} {cap + 1}\n"
    return script + f"printf 'Z\\000{len(FILES)}\\000'\n"


# CodexAstraLocal: Adjacent complete samples provide conservative uptime
# enclosures; reset/decreasing/missing counters remain null, never negative rate.
def interval(previous, current):
    result = {"valid_time": False, "pressure_stall_us": {}, "vmstat_increments": {}}
    try:
        p0, p1 = (previous[key]["value"] for key in ("uptime_before", "uptime_after"))
        c0, c1 = (current[key]["value"] for key in ("uptime_before", "uptime_after"))
        if not all((p0, p1, c0, c1)):
            raise ValueError("uptime_unavailable")
        lower = c0["ns"] - p1["ns"] - p1["quantum_ns"]
        upper = c1["ns"] + c1["quantum_ns"] - p0["ns"]
        if lower <= 0 or upper < lower:
            raise ValueError("uptime_reset_or_overlapping_enclosures")
        result.update(valid_time=True, elapsed_lower_ns=lower, elapsed_upper_ns=upper)
    except (KeyError, TypeError, ValueError) as exc:
        result["reason"] = str(exc)
        return result
    for group, source, keys in (("pressure_stall_us", "pressure", ("some", "full")),
                                ("vmstat_increments", "vmstat", VM_COUNTERS)):
        for key in keys:
            a = previous.get(source, {}).get("value")
            b = current.get(source, {}).get("value")
            av, bv = (x.get(key) if x else None for x in (a, b))
            if source == "pressure":
                av, bv = (x.get("total") if x else None for x in (av, bv))
            row = {"delta": None}
            if av is None or bv is None:
                row["reason"] = "counter_unavailable"
            elif bv < av:
                row["reason"] = "counter_decreased_or_reset"
            elif source == "pressure" and (bv - av) * 1000 > upper:
                row["reason"] = "stall_delta_exceeds_elapsed_enclosure"
            else:
                row["delta"] = bv - av
            result[group][key] = row
    return result


def unavailable_counters():
    # CodexAstraLocal: Absent real validated readers cannot be replaced by
    # occupancy, PSI, clocks, votes or a theoretical bandwidth denominator.
    return {"ddr_bytes": None, "ddr_bytes_per_second": None, "bandwidth_percent": None,
            "cpu_cache_events": None, "cpu_memory_stall_events": None,
            "reason": "no_validated_available_traffic_or_pmu_reader; passive_pressure_only"}


class Clock:
    # CodexAstraLocal: Clock injection permits finite scheduler tests without
    # sleeping, sockets, device commands or a real ADB server.
    monotonic = staticmethod(time.monotonic)
    sleep = staticmethod(time.sleep)

    @staticmethod
    def utc():
        return datetime.now(timezone.utc).isoformat()


def collect(serial, output, samples=5, period=5.0, timeout=2, delay=0.0,
            repo=None, client=None, clock=None):
    probe, probe_path = load_probe(repo)
    if not probe.SERIAL.fullmatch(serial):
        raise ValueError("invalid_explicit_serial")
    # CodexAstraLocal: Cover one complete opening with at most 61 five-second
    # observations; retain a finite 300-second span and all per-query limits.
    if (type(samples) is not int or not 2 <= samples <= 61 or type(timeout) is not int
            or not 1 <= timeout <= 3 or not math.isfinite(period) or not 5 <= period <= 30
            or not math.isfinite(delay) or not 0 <= delay <= 60
            or (samples - 1) * period > 300):
        raise ValueError("invalid_finite_schedule")
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    clock = clock or Clock()
    client = client or probe.LocalAdb(timeout + 0.5)
    script = device_script()
    service = f"shell:timeout {timeout} sh -c " + shlex.quote(script)
    (output / "read-only.sh").write_text(script)
    report = {"author": "CodexAstraLocal", "schema": 1, "serial": serial,
              "scope": "device_global_passive_memory_pressure_not_app_or_bandwidth",
              "client_sha256": hashlib.sha256(probe_path.read_bytes()).hexdigest(),
              "helper_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "script_sha256": hashlib.sha256(script.encode()).hexdigest(),
              "read_only": True, "samples_requested": samples, "period_seconds": period,
              "timeout_seconds": timeout, "delay_seconds": delay, "samples": [],
              "started_utc": clock.utc(), "unsupported": unavailable_counters(),
              "status": "unavailable", "intervals": []}
    began = clock.monotonic()
    deadline = began + delay + (samples - 1) * period + timeout + 1.5
    try:
        # CodexAstraLocal: Only the explicitly named online transport is used.
        # Listing queries the existing loopback server and never starts it.
        listed = [line.split() for line in client.devices().splitlines() if line.strip()]
        selected = [row for row in listed if row[0] == serial]
        if len(selected) != 1 or len(selected[0]) < 2 or selected[0][1] != "device":
            raise ValueError("explicit_device_not_online")
        previous = None
        for index in range(samples):
            planned = began + delay + index * period
            wait = planned - clock.monotonic()
            if wait > 0:
                clock.sleep(wait)
            row = {"index": index, "planned_offset_seconds": delay + index * period,
                   "host_before_utc": clock.utc(),
                   "host_before_seconds": clock.monotonic() - began}
            remaining = deadline - clock.monotonic()
            if remaining < timeout + 0.5 or clock.monotonic() > planned + period / 2:
                row["reason"] = "scheduled_sample_missed_no_catchup"
                report["samples"].append(row)
                previous = None
                continue
            client.timeout = min(timeout + 0.5, remaining)
            try:
                reply = client._query(service, serial)
            except (probe.ProbeError, OSError, TimeoutError) as exc:
                row.update(host_after_utc=clock.utc(),
                           host_after_seconds=clock.monotonic() - began,
                           reason=getattr(exc, "code", "transport_error:" + str(exc)))
                report["samples"].append(row)
                report["reason"] = row["reason"]
                break
            row.update(host_after_utc=clock.utc(), host_after_seconds=clock.monotonic() - began)
            data = reply.encode("utf-8")
            if len(data) > MAX_REPLY:
                row.update(reason="reply_limit", decoded_reply_bytes=len(data))
                report["samples"].append(row)
                report["reason"] = row["reason"]
                break
            filename = f"sample-{index:02d}.reply"
            (output / filename).write_bytes(data)
            row.update(reply=filename, reply_sha256=hashlib.sha256(data).hexdigest(),
                       hash_scope="LocalAdb_decoded_UTF8_reply")
            try:
                row["reads"] = parse_reply(reply)
            except (ValueError, UnicodeError) as exc:
                row["reason"] = "parse_error:" + str(exc)
            report["samples"].append(row)
            if "reads" in row and previous is not None:
                pair = interval(previous["reads"], row["reads"])
                pair.update(from_index=previous["index"], to_index=index)
                report["intervals"].append(pair)
            previous = row if "reads" in row else None
        usable = [x for x in report["intervals"] if x["valid_time"]]
        report["qualified_sources"] = sorted({key for row in report["samples"]
            for key in ("pressure", "vmstat", "meminfo")
            if row.get("reads", {}).get(key, {}).get("value") is not None})
        report["status"] = ("passive_observed" if usable and report["qualified_sources"]
                            else "partial_or_unavailable")
    except probe.ProbeError as exc:
        # CodexAstraLocal: A failed query ends collection; no retry, alternate
        # tool, server restart or policy modification hides the capability limit.
        report["reason"] = exc.code
    except (ValueError, OSError, TimeoutError) as exc:
        report["reason"] = str(exc)
    report.update(finished_utc=clock.utc(), elapsed_seconds=clock.monotonic() - began,
                  actual_samples=len(report["samples"]))
    with (output / "report.json").open("x") as stream:
        json.dump(report, stream, indent=2, allow_nan=False)
        stream.write("\n")
    return report


def main():
    # CodexAstraLocal: Explicit CLI invocation is the only device entry point;
    # local tests inject an inert client and never invoke this CLI.
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repo", type=Path)
    parser.add_argument("--samples", type=int, default=5)
    parser.add_argument("--period", type=float, default=5)
    parser.add_argument("--timeout", type=int, default=2)
    parser.add_argument("--delay", type=float, default=0)
    args = parser.parse_args()
    result = collect(args.serial, args.output, args.samples, args.period, args.timeout,
                     args.delay, args.repo)
    print(json.dumps({"status": result["status"], "actual_samples": result["actual_samples"],
                      "ddr_bytes_per_second": None, "bandwidth_percent": None}))
    return 0 if result["status"] == "passive_observed" else 2


if __name__ == "__main__":
    raise SystemExit(main())
