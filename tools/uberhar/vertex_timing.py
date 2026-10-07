#!/usr/bin/env python3
"""CodexAstraLocal: Read finite CPU vertex reports without extrapolating coverage.

Raw reports stay private. CPU and wall envelopes describe retained chunks only;
phase intervals include observer cost and are never calibration-subtracted.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re


# CodexAstraLocal: Enforce producer limits before decoding, then validate nested
# shapes and accounting independently of the C++ serializer and its status bits.
MAX_BYTES = 65536
MAX_CHUNKS = 64
MAX_VERTICES = 4096
U64 = (1 << 64) - 1
HEX = re.compile(r"[0-9a-fA-F]{16}\Z")
SAFE_ID = re.compile(r"[A-Za-z0-9_-]{1,48}\Z")
STAGES = ("lookup", "input", "shader_engine_run", "output_conversion", "cache_selection_store", "submit")


class TimingError(ValueError):
    """CodexAstraLocal: Unsupported or inconsistent evidence must fail visibly."""


def integer(value, name, low=0, high=U64):
    if type(value) is not int or not low <= value <= high:
        raise TimingError(f"{name}: expected integer in [{low}, {high}]")
    return value


def boolean(value, name):
    if type(value) is not bool:
        raise TimingError(f"{name}: expected boolean")
    return value


def require(condition, message):
    if not condition:
        raise TimingError(message)


def object_pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, f"duplicate JSON key: {key}")
        result[key] = value
    return result


def shape(value, depth=0):
    require(depth <= 10, "JSON nesting exceeds report bound")
    if isinstance(value, dict):
        require(len(value) <= 64, "too many object fields")
        for key, item in value.items():
            require(len(key) <= 128, "oversized field name")
            shape(item, depth + 1)
    elif isinstance(value, list):
        require(len(value) <= MAX_CHUNKS, "array exceeds report bound")
        for item in value:
            shape(item, depth + 1)
    elif isinstance(value, str):
        require(len(value) <= 512, "oversized report string")
    elif type(value) is int:
        integer(value, "JSON integer")
    else:
        require(type(value) is bool, "unsupported JSON scalar")


def read_report(path):
    # CodexAstraLocal: A size-limited read also catches growth after stat; raw
    # paths supplied by the report are never opened or followed by this tool.
    with Path(path).open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    return decode(data)


def decode(data):
    require(0 < len(data) <= MAX_BYTES, "report must be 1 through 65536 bytes")
    try:
        report = json.loads(data, object_pairs_hook=object_pairs,
                            parse_constant=lambda value: (_ for _ in ()).throw(
                                TimingError(f"invalid number: {value}")))
        shape(report)
        validate(report)
    except (KeyError, TypeError, UnicodeError, RecursionError, json.JSONDecodeError) as exc:
        raise TimingError(f"invalid report: {exc}") from exc
    return report, hashlib.sha256(data).hexdigest()


def validate(report):
    require(type(report) is dict, "report must be an object")
    require(integer(report["schema"], "schema") == 1, "unsupported schema")
    require(isinstance(report["diagnostic_id"], str) and
            SAFE_ID.fullmatch(report["diagnostic_id"]), "invalid diagnostic ID")
    require(report["mode"] in ("boundary", "detailed"), "unsupported timing mode")
    require(isinstance(report["title_id"], str) and HEX.fullmatch(report["title_id"]),
            "invalid title ID")
    require(report["selection"] == "periodic_first_eligible_cpu_draw_hashed_contiguous_input_offset",
            "unsupported selection contract")
    require(report["scope"] == "actual_observed_no_gs_loop_only_no_extrapolation",
            "unsupported scope")
    require(report["clock_bias"] == "raw_instrumented_cost_no_calibration_subtraction",
            "unsupported observer contract")
    require(report["stop"] in ("none", "window_elapsed", "budget_exhausted", "arm_timeout",
                              "identity_changed", "phase_changed", "clock_unavailable", "teardown",
                              "policy_changed"),
            "unsupported stop reason")
    for key in ("version", "revision", "engine"):
        require(isinstance(report[key], str) and bool(report[key]), f"missing {key}")
    for key in ("run", "owner_tid"):
        integer(report[key], key)
    cfg = report["config"]
    for key, low, high in (("delay_ms", 0, 180000), ("duration_ms", 1, 10000),
                           ("period_ms", 50, 1000), ("chunk_vertices", 1, 64),
                           ("max_chunks", 1, 64), ("max_vertices", 1, 4096),
                           ("seed", 0, (1 << 32) - 1)):
        integer(cfg[key], key, low, high)
    require(cfg["max_output_bytes"] == MAX_BYTES, "unexpected producer byte cap")
    require(cfg["max_draw_inputs"] == 4096, "unexpected whole-draw observer cap")
    clock = report["clock"]
    require(clock["cpu"] == "CLOCK_THREAD_CPUTIME_ID" and clock["wall"] == "std::chrono::steady_clock",
            "unsupported clock domain")
    for key in ("wall_resolution_ns", "cpu_resolution_ns", "wall_reads", "cpu_reads"):
        integer(clock[key], key)
    calibration = report["calibration"]
    for key in ("pairs", "wall_pairs"):
        integer(calibration[key], key, 0, 8)
    for key in ("cpu_ns", "outer_wall_ns", "wall_pair_ns", "maximum_wall_pair_ns"):
        integer(calibration[key], key)
    summary = report["summary"]
    for key, value in summary.items():
        if key == "phases":
            require(type(value) is list and len(value) == 5, "expected five phase counters")
            for item in value:
                integer(item, "phase counter")
        else:
            integer(value, f"summary.{key}")
    rows = report["records"]
    require(type(rows) is list and len(rows) <= cfg["max_chunks"], "record cap exceeded")
    require(summary["records"] == len(rows), "record count mismatch")
    require(summary["sparse_suppressed"] <= len(rows), "suppressed batch count exceeds selected draws")
    require(report["stage_order"] == list(STAGES), "unexpected stage order")
    if summary["armed_at_ns"]:
        require(summary["window_begin_ns"] == summary["armed_at_ns"] + cfg["delay_ms"] * 1000000 and
                summary["window_end_ns"] == summary["window_begin_ns"] + cfg["duration_ms"] * 1000000,
                "window bounds contradict configured duration/delay")
    if rows:
        require(report["run"] > 0 and report["owner_tid"] > 0 and summary["armed_at_ns"] > 0,
                "retained rows lack owner/run/arm identity")
    reserved = 0
    selected_draw_inputs = 0
    prior_ordinal = 0
    for row in rows:
        for key in ("program_hash", "swizzle_hash"):
            require(isinstance(row[key], str) and HEX.fullmatch(row[key]), f"invalid {key}")
        for key in ("indexed", "fused_plan", "begun", "completed", "clocks_valid",
                    "phase_stable", "overhang"):
            boolean(row[key], key)
        for key in ("draw_ordinal", "entry", "draw_count", "vs_input_attributes", "topology",
                    "first_input", "input_count", "observed_inputs", "hits", "misses",
                    "fused_misses", "legacy_misses", "start_offset_ns", "token_before", "token_after"):
            integer(row[key], key)
        require(prior_ordinal < row["draw_ordinal"] <= summary["cpu_batches"],
                "invalid draw ordinal order")
        prior_ordinal = row["draw_ordinal"]
        require(0 < row["input_count"] <= cfg["chunk_vertices"], "invalid chunk size")
        require(row["first_input"] + row["input_count"] <= row["draw_count"],
                "chunk escapes draw")
        require(0 < row["draw_count"] <= cfg["max_draw_inputs"], "selected draw exceeds observer cap")
        selected_draw_inputs += row["draw_count"]
        reserved += row["input_count"]
        require(row["hits"] + row["misses"] == row["observed_inputs"] <= row["input_count"],
                "hit/miss accounting mismatch")
        require(row["fused_misses"] + row["legacy_misses"] == row["misses"],
                "actual input route count mismatch")
        require(row["fused_plan"] or row["fused_misses"] == 0,
                "fused inputs without a ready plan")
        require(not row["completed"] or row["observed_inputs"] == row["input_count"],
                "complete chunk has partial input coverage")
        for key in ("begin", "end"):
            require(type(row[key]) is list and len(row[key]) == 3, "invalid clock bracket")
            for point in row[key]:
                integer(point, key)
        calls, durations = row["stage_calls"], row["stage_wall_ns"]
        require(type(calls) is list and type(durations) is list and
                len(calls) == len(durations) == len(STAGES), "invalid stage arrays")
        for key, count, duration in zip(STAGES, calls, durations):
            integer(count, f"{key}.calls", 0, row["input_count"])
            integer(duration, f"{key}.wall_ns")
            if report["mode"] == "boundary":
                require(duration == 0, "boundary report contains interior timing")
        if row["completed"]:
            for key in ("lookup", "cache_selection_store", "submit"):
                require(calls[STAGES.index(key)] == row["observed_inputs"], f"{key} call mismatch")
            for key in ("input", "shader_engine_run", "output_conversion"):
                require(calls[STAGES.index(key)] == row["misses"], f"{key} call mismatch")
        if row["clocks_valid"]:
            b, e = row["begin"], row["end"]
            require(row["begun"] and b[0] <= b[2] <= e[0] <= e[2] and b[1] <= e[1],
                    "valid-clock flag contradicts brackets")
            require(sum(durations) <= e[0] - b[2],
                    "stage times exceed enclosing inner wall interval")
            require(b[0] >= summary["window_begin_ns"] and
                    row["start_offset_ns"] == b[0] - summary["window_begin_ns"],
                    "chunk start offset mismatch")
            require(row["overhang"] == (e[2] > summary["window_end_ns"]),
                    "window-overhang flag mismatch")
        if row["phase_stable"]:
            require(row["token_before"] == row["token_after"], "stable-phase token mismatch")
        if row["begun"]:
            require(row["token_before"] & 7 == 3, "begun chunk lacks Gameplay phase")
    require(reserved == summary["reserved_vertices"] <= cfg["max_vertices"],
            "reserved vertex budget mismatch")
    require(selected_draw_inputs == summary["selected_draw_inputs"], "selected full-draw input mismatch")


def summarize(report, digest):
    # CodexAstraLocal: Keep failed/partial/overhanging observations visible and
    # outside the accepted population. Never silently turn an empty set into zero cost.
    excluded = Counter()
    accepted = []
    for row in report["records"]:
        reasons = [key for key in ("begun", "completed", "clocks_valid", "phase_stable")
                   if not row[key]]
        if row["overhang"]:
            reasons.append("overhang")
        if row["clocks_valid"]:
            cpu = row["end"][1] - row["begin"][1]
            outer = row["end"][2] - row["begin"][0]
            if cpu > outer:
                reasons.append("cpu_exceeds_outer_wall")
        if reasons:
            excluded.update(reasons)
        else:
            accepted.append(row)
    stages = {key: {"calls": sum(row["stage_calls"][index] for row in accepted),
                    "instrumented_wall_ns": sum(row["stage_wall_ns"][index] for row in accepted)
                    if accepted and report["mode"] == "detailed" else None}
              for index, key in enumerate(STAGES)}
    return {
        "schema": 1, "report_sha256": digest, "diagnostic_id": report["diagnostic_id"],
        "version": report["version"], "revision": report["revision"], "mode": report["mode"],
        "engine": report["engine"], "stop": report["stop"],
        "records": len(report["records"]), "accepted_chunks": len(accepted),
        "excluded_chunks": len(report["records"]) - len(accepted),
        "exclusion_reasons_may_overlap": dict(excluded),
        "observed_inputs": sum(row["observed_inputs"] for row in accepted),
        "hits": sum(row["hits"] for row in accepted),
        "misses": sum(row["misses"] for row in accepted),
        "fused_misses": sum(row["fused_misses"] for row in accepted),
        "legacy_misses": sum(row["legacy_misses"] for row in accepted),
        "retained_chunk_cpu_ns": sum(row["end"][1] - row["begin"][1] for row in accepted) if accepted else None,
        "retained_chunk_wall_lower_ns": sum(row["end"][0] - row["begin"][2] for row in accepted) if accepted else None,
        "retained_chunk_wall_upper_ns": sum(row["end"][2] - row["begin"][0] for row in accepted) if accepted else None,
        "stages": stages, "raw_calibration_no_subtraction": report["calibration"],
        "coverage": report["summary"],
        "limits": ["selected contiguous no-GS CPU loop population only; no extrapolation",
                   "interior wall intervals contain observer cost and preemption",
                   "CPU brackets include endpoint overhead; wall bounds are not GPU time",
                   "off/on changes include suppression of old sparse samples",
                   "program/entry/count and scene matching required for comparisons"],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    try:
        report, digest = read_report(args.report)
        print(json.dumps(summarize(report, digest), indent=2))
    except (OSError, TimingError) as exc:
        parser.exit(1, f"vertex timing: {exc}\n")


if __name__ == "__main__":
    main()
