#!/usr/bin/env python3
"""CodexAstraLocal: Behavioral capture-reader/CPU/Mesa regressions without a ROM."""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import struct
import subprocess
import tempfile

import vertex_capture as vc
import replay_vertex_capture as replay


def check(condition: bool, reason: str) -> None:
    if not condition:
        raise AssertionError(reason)


def rejected(function, reason: str) -> None:
    try:
        function()
    except vc.CaptureError:
        return
    raise AssertionError(f"accepted invalid {reason}")


def replace(capture: vc.Capture, name: str, raw: bytes) -> vc.Capture:
    # CodexAstraLocal: Malformed-fixture surgery preserves unrelated bytes and
    # recalculates all section offsets, avoiding accidental failures for overlap.
    manifest = copy.deepcopy(capture.manifest)
    packet = manifest["packets"][0]
    payload = bytearray()
    for section in packet["sections"]:
        data = raw if section == name else capture.section(capture.manifest["packets"][0], section)
        packet["sections"][section] = {"offset": len(payload), "size": len(data)}
        payload += data
    return vc.Capture(manifest, bytes(payload))


def structural_tests(capture: vc.Capture) -> int:
    packet = capture.manifest["packets"][0]
    vc.validate(capture)
    vc.replay_packet(capture, packet)
    encoded = vc.encode(capture.manifest, capture.payload)
    for raw in (b"", encoded[:15], b"BADMAGIC" + encoded[8:], encoded[:-1], encoded + b"x",
                vc.HEADER.pack(vc.MAGIC, vc.MAX_MANIFEST + 1, 0) + b" " * (vc.MAX_MANIFEST + 1),
                vc.HEADER.pack(vc.MAGIC, 13, 0) + b'{"schema":NaN}',
                vc.HEADER.pack(vc.MAGIC, 12, 0) + b'{"a":1e9999}',
                vc.HEADER.pack(vc.MAGIC, 13, 0) + b'{"a":1,"a":2}'):
        rejected(lambda raw=raw: vc.decode(raw), "container/JSON")
    utf16 = '{"schema":1}'.encode("utf-16")
    rejected(lambda: vc.decode(vc.HEADER.pack(vc.MAGIC, len(utf16), 0) + utf16), "non-UTF8 manifest")
    nested = {"a": {} }
    node = nested["a"]
    for _ in range(30):
        node["a"] = {}
        node = node["a"]
    rejected(lambda: vc.encode(nested, b""), "deep metadata")
    for field, value in (("schema", True), ("schema", 1.5), ("schema", 2),
                         ("window_swaps", 9), ("capture_id", "../escape"), ("run", "1"),
                         ("first_swap", 0)):
        bad = vc.Capture(copy.deepcopy(capture.manifest), capture.payload)
        bad.manifest[field] = value
        rejected(lambda bad=bad: vc.validate(bad), field)
    for mutate in (
        lambda p: p.update(completed=True, accepted=False),
        lambda p: p.update(accepted=True, recorded=False),
        lambda p: p["sections"]["fixed"].update(offset=0),
        lambda p: p.update(attempt_ordinal=True),
        lambda p: p.update(swap=12),
        lambda p: p["sections"].update(external={"offset": 0, "size": 16}),
    ):
        bad = vc.Capture(copy.deepcopy(capture.manifest), capture.payload)
        mutate(bad.manifest["packets"][0])
        rejected(lambda bad=bad: vc.validate(bad), "packet structure")
    for duplicate in (True, False):
        bad = vc.Capture(copy.deepcopy(capture.manifest), capture.payload)
        for index in range(1, 2 if duplicate else 3):
            pending = copy.deepcopy(packet)
            pending.update(id=index, sections={}, accepted=False, recorded=False,
                           attempt_ordinal=packet["attempt_ordinal"] if duplicate else
                                           packet["attempt_ordinal"] + index)
            bad.manifest["packets"].append(pending)
        rejected(lambda bad=bad: vc.validate(bad), "duplicate attempt/per-interval quota")
    for mutate in (
        lambda p: p.update(accepted=False),
        lambda p: p["extra"].update(use_geometry_shader=True),
        lambda p: p["draw"].update(count=4097),
        lambda p: p["draw"].update(maximum=5000),
        lambda p: p["draw"].update(base_vertex=-1),
        lambda p: p["draw"].update(uploaded_index_width=1),
        lambda p: p["bindings"][0].update(span=1),
        lambda p: p["extra"]["load_flags"].__setitem__(0, 3),
        lambda p: p["layout"]["attributes"][1].update(location=0),
        lambda p: p["ubo_ranges"].update(vs_pica=[[0, 1616]]),
    ):
        bad = vc.Capture(copy.deepcopy(capture.manifest), capture.payload)
        mutate(bad.manifest["packets"][0])
        rejected(lambda bad=bad: vc.replay_packet(bad, bad.manifest["packets"][0]), "reconstruction")
    raw = bytearray(capture.section(packet, "vs_pica"))
    raw[4] = 1
    bad = replace(capture, "vs_pica", bytes(raw))
    rejected(lambda: vc.replay_packet(bad, bad.manifest["packets"][0]), "unobserved UBO padding")
    rejected(lambda: vc.encode(capture.manifest, b"x" * vc.MAX_CAPTURE), "payload bound")
    # CodexAstraLocal: An optional legacy-default flag is still typed protocol
    # data; accepting 1 or "false" could silently select another generated shader.
    for value in (None, 0, 1, 1.0, "false", [], {}):
        bad = vc.Capture(copy.deepcopy(capture.manifest), capture.payload)
        bad.manifest["packets"][0]["extra"]["precise_jit_dot"] = value
        rejected(lambda bad=bad: vc.replay_packet(bad, bad.manifest["packets"][0]),
                 "non-boolean arithmetic policy")
    return 45


def selector_tests(capture: vc.Capture) -> int:
    # CodexAstraLocal: A,B,A discovery ordinals stay 0,1,2 when B is later selected;
    # B does not become the second B-matching attempt (which would never occur).
    manifest = copy.deepcopy(capture.manifest)
    packet = manifest["packets"][0]
    rows = []
    for ordinal, program in enumerate(("000000000000000a", "000000000000000b", "000000000000000a")):
        key = copy.deepcopy(packet["key"])
        key["program_hash"] = program
        rows.append({"swap": 11, "interval": 0, "attempt_ordinal": ordinal, "key": key,
                     "pipeline": {}, "recorded_count": 1, "first_tick": "0000000000000011",
                     "last_tick": "0000000000000011", "accepted_all": True,
                     "completed_all": False})
    manifest.update(mode="discover", packets=[], discovery=rows)
    discovery = vc.Capture(manifest, b"")
    vc.validate(discovery)
    config = vc.select(discovery, 1, "selected-b")
    check(config["selector"]["attempt_ordinal"] == 1 and
          config["selector"]["program_hash"].endswith("b"), "selector renumbered filtered population")
    check(config["schema"] == 1 and config["trigger"] == "next_gameplay_transition",
          "selector omitted production parser requirements")
    check(config["selector"]["vertex_count"] == 96 and
          config["selector"]["color_addresses"] == [packet["key"]["color_address"]],
          "selector lost draw guards")
    check(vc.intervals(discovery)[1]["status"] == "no_evidence_unknown_censored",
          "missing interval was presented as a clean frame")
    for function in (lambda: vc.select(discovery, 1, "../escape"),
                     lambda: vc.select(discovery, 1, manifest["capture_id"]),
                     lambda: vc.select(discovery, 1, "new", window=8, per_swap=2)):
        rejected(function, "selector configuration")
    rows[1]["key"]["vertex_count"] = 4097
    rejected(lambda: vc.select(discovery, 1, "new"), "discovery draw larger than payload cap")
    rows[1]["key"]["vertex_count"] = 96
    rows[1]["accepted_all"] = False
    rejected(lambda: vc.select(discovery, 1, "new"), "pending discovery")
    rows[1]["swap"] = 12
    rejected(lambda: vc.validate(discovery), "inconsistent discovery swap interval")
    initial = vc.discovery_config("0000000000000001", "discovery-new", window=8, per_swap=1)
    check(initial["mode"] == "discover" and initial["schema"] == 1 and
          initial["selector"]["attempt_ordinal"] == 0, "invalid initial discovery config")
    rejected(lambda: vc.discovery_config("0000000000000000", "new"), "zero discovery title")
    return 12


def run(directory: Path, binary: Path, fixture: Path, gpu: bool) -> dict:
    subprocess.run([str(fixture.resolve()), str(directory.resolve())], check=True)
    written = vc.read(directory / "written.uvc")
    checks = structural_tests(written) + selector_tests(written)
    results = {}
    names = ("written", "missing", "carry", "fifo_high", "u8_widened", "uniform", "signed",
             "ubyte", "sbyte_scaled", "emulated3", "fixed", "highwater", "offset",
             "dot_legacy", "dot_precise")
    for name in names:
        capture = vc.read(directory / f"{name}.uvc")
        packet = capture.manifest["packets"][0]
        vc.replay_packet(capture, packet)
        result = replay.replay(directory / f"{name}.uvc", directory / f"result-{name}", binary, gpu)
        record = result["packets"][0]
        check(record["status"] == "replayed", f"{name}: {record}")
        snapshot = directory / f"result-{name}/validated-snapshot.uvc"
        check(result["source_capture_sha256"] == capture.source_sha256 and
              vc.read(snapshot).source_sha256 == result["snapshot_sha256"],
              "CPU/Mesa did not use the recorded frozen capture snapshot")
        cpu = struct.unpack("<2112f", (directory / f"result-{name}/packet-0/cpu.bin").read_bytes())
        fresh = struct.unpack("<2112f", (directory / f"result-{name}/packet-0/cpu_fresh.bin").read_bytes())
        if name == "missing":
            check(all(cpu[vertex * 22 + 7] == 0 for vertex in range(96)), "missing-W CPU default changed")
        if name == "carry":
            check([cpu[v * 22 + 7] for v in range(3)] == [0, .75, .75], "draw-local carry lost")
            check([fresh[v * 22 + 7] for v in range(3)] == [0, .75, 0], "fresh-unit control ineffective")
        if name == "fifo_high":
            check(record["cpu"]["cpu_invocations"] == 66 and record["cpu"]["cpu_fifo_hits"] == 30,
                  "production FIFO eviction/order differs")
            check(cpu[7] == 0 and cpu[65 * 22 + 7] == .75, "evicted high index did not reshade with carry")
        if name == "u8_widened":
            check(record["cpu"]["cpu_invocations"] == 8 and record["cpu"]["cpu_fifo_hits"] == 88,
                  "widened u8 index identity/order lost")
        if name == "fixed":
            check(struct.pack("<f", cpu[4]) == capture.section(packet, "defaults")[:4],
                  "f24 storage bits were decoded as packed hardware f24")
        if name in ("signed", "emulated3"):
            check(tuple(cpu[4:8]) == (-300, 21, 1, 1), "s16 conversion/padding differs")
        if name == "ubyte":
            check(tuple(cpu[4:8]) == (1, 2, 200, 255), "unsigned raw input conversion differs")
        if name == "sbyte_scaled":
            check(tuple(cpu[4:8]) == (-4, 2, 100, 1), "signed scaled raw input conversion differs")
        if gpu:
            mesa = record["mesa"]
            actual = mesa["cpu_vs_gpu_actual"]["significant_differences"]
            check(bool(actual) == (name in ("missing", "carry", "fifo_high", "uniform")),
                  f"unexpected CPU/GPU result for {name}: {mesa}")
            if name == "uniform":
                check(mesa["actual_vs_intended_uniforms"]["different_bytes"] > 0 and
                      mesa["cpu_vs_gpu_intended_uniforms"]["significant_differences"] == 0,
                      "stale bound uniform was silently replaced/lost")
            else:
                check(mesa["actual_vs_intended_uniforms"]["different_bytes"] == 0,
                      "uniform control unexpectedly differs")
            check(mesa["cpu_vs_gpu_inputs"].get("significant_differences", 0) == 0,
                  "actual typed-fetch input parity differs")
        results[name] = record
        checks += 1

    # CodexAstraLocal: Old packets omit the policy while new packets explicitly
    # retain it. The same guest/config must reproduce two distinct source hashes;
    # removing a true policy cannot silently replay under the legacy generator.
    old, new = (results[name]["cpu"] for name in ("dot_legacy", "dot_precise"))
    check(all(old[key] == new[key] for key in
              ("program_hash", "swizzle_hash", "vs_config_hash")) and
          old["vs_source_hash"] != new["vs_source_hash"],
          "arithmetic policy lost guest/source identity separation")
    for name in ("dot_legacy", "dot_precise"):
        legacy = vc.read(directory / f"{name}.uvc")
        del legacy.manifest["packets"][0]["extra"]["precise_jit_dot"]
        path = directory / f"{name}-without-policy.uvc"
        path.write_bytes(vc.encode(legacy.manifest, legacy.payload))
        report = replay.replay(path, directory / f"result-{name}-without-policy", binary, False)
        record = report["packets"][0]
        if name == "dot_legacy":
            check(record["status"] == "replayed" and
                  record["cpu"]["vs_source_hash"] == old["vs_source_hash"],
                  "legacy packet no longer reproduces the old source")
        else:
            check(record["status"] == "unsupported_or_invalid" and
                  "GLSL hash differs" in record["reason"],
                  "missing true arithmetic policy silently generated the old shader")
    malformed = vc.read(directory / "dot_precise.uvc")
    malformed.manifest["packets"][0]["extra"]["precise_jit_dot"] = 1
    path = directory / "numeric-policy.uvc"
    path.write_bytes(vc.encode(malformed.manifest, malformed.payload))
    direct_output = directory / "result-numeric-policy"
    direct_output.mkdir()
    direct = subprocess.run([str(binary.resolve()), str(path), "0", str(direct_output)],
                            capture_output=True, text=True, timeout=30)
    check(direct.returncode == 2 and "invalid precise JIT dot flag" in direct.stderr,
          "direct worker accepted non-boolean arithmetic policy")
    checks += 4

    # CodexAstraLocal: Uppercase producer identities must replay identically, but
    # corrupted words/lengths/config hashes must fail before executing a shader.
    uppercase = vc.Capture(copy.deepcopy(written.manifest), written.payload)
    for group in (uppercase.manifest["packets"][0]["key"], uppercase.manifest["packets"][0]["pipeline"]):
        for key, value in group.items():
            if isinstance(value, str) and vc.HEX64.fullmatch(value):
                group[key] = value.upper()
    path = directory / "uppercase.uvc"
    path.write_bytes(vc.encode(uppercase.manifest, uppercase.payload))
    report = replay.replay(path, directory / "result-uppercase", binary, False)
    check(report["packets"][0]["status"] == "replayed", "uppercase hash incorrectly rejected")
    # CodexAstraLocal: Captured upload spans are analyzed earlier than live index
    # copying. A later subset stays valid and retains the actual uploaded order.
    subset = vc.read(directory / "u8_widened.uvc")
    subset = replace(subset, "indices", struct.pack("<96H", *([1] * 96)))
    path = directory / "indices-subset.uvc"
    path.write_bytes(vc.encode(subset.manifest, subset.payload))
    report = replay.replay(path, directory / "result-indices-subset", binary, gpu)
    record = report["packets"][0]
    check(record["status"] == "replayed" and record["cpu"]["cpu_invocations"] == 1 and
          record["cpu"]["cpu_fifo_hits"] == 95, "in-range live index subset was rejected or reordered")
    for name, corrupt in (("program", "program"), ("length", "program_words"),
                          ("pipeline", "vs_config_hash"), ("mapping", "mapping")):
        bad = vc.Capture(copy.deepcopy(written.manifest), written.payload)
        packet = bad.manifest["packets"][0]
        if corrupt == "program":
            raw = bytearray(bad.section(packet, "program"))
            raw[0] ^= 1
            bad = replace(bad, "program", bytes(raw))
        elif corrupt == "program_words":
            packet["program_words"] = 4096
        elif corrupt == "mapping":
            packet["native_inputs"][0]["offset"] = 4096
        else:
            packet["pipeline"][corrupt] = "1234567812345678"
        path = directory / f"corrupt-{name}.uvc"
        path.write_bytes(vc.encode(bad.manifest, bad.payload))
        report = replay.replay(path, directory / f"result-corrupt-{name}", binary, False)
        check(report["packets"][0]["status"] == "unsupported_or_invalid", f"accepted corrupt {name}")
    if gpu:
        bad = vc.read(directory / "emulated3.uvc")
        packet = bad.manifest["packets"][0]
        original = bad.section(packet, "vertex_0")
        bad = replace(bad, "vertex_0", b"".join(original[v * 8:v * 8 + 6] for v in range(96)))
        packet = bad.manifest["packets"][0]
        packet["bindings"][0]["guest_stride"] = 6
        packet["native_inputs"][0]["stride"] = 6
        # Config identity is unchanged: actual layout stride/type/size remains the same.
        path = directory / "uncaptured-padding.uvc"
        path.write_bytes(vc.encode(bad.manifest, bad.payload))
        report = replay.replay(path, directory / "result-uncaptured-padding", binary, True)
        check(report["packets"][0]["status"] == "unsupported_or_invalid" and
              "uncaptured padding" in report["packets"][0]["reason"], "invented missing GPU input bytes")
    return {"checks": checks + 6 + int(gpu), "render": gpu, "results": results,
            "scope": "Synthetic production-CPU/GLSL/Mesa and bounded parser/selector tests; no device claim."}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/vertex-capture"))
    parser.add_argument("--render", action="store_true")
    parser.add_argument("--reuse-build", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    binary = args.output / "replay"
    fixture = args.output / "fixture"
    if not args.reuse_build:
        replay.build(binary.resolve())
        replay.build(fixture.resolve(), replay.ROOT / "tools/uberhar/test_vertex_capture_fixture.cpp")
    directory = Path(tempfile.mkdtemp(prefix="regression-", dir=args.output.resolve()))
    report = run(directory, binary, fixture, args.render)
    (directory / "regression.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    print(f"PASS: {report['checks']} capture/replay checks, {len(report['results'])} production CPU cases, "
          f"Mesa={'yes' if args.render else 'not requested'}; evidence {directory}")


if __name__ == "__main__":
    main()
