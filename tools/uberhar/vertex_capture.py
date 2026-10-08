#!/usr/bin/env python3
"""CodexAstraLocal: Bounded reader for private, opt-in vertex evidence packets.

This format contains guest data. Inspect locally; do not attach raw captures to
public issues or release artifacts. A submitted draw is not proof of completed
GPU execution, correct pixels, or the cause of a visible scene defect.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import re
import struct
from typing import Any


# CodexAstraLocal: All sizes are protocol limits, not allocation hints from an
# untrusted manifest. There is no pickle, host-ABI struct, or external section path.
MAGIC = b"UBVCAP01"
HEADER = struct.Struct("<8sII")
MAX_CAPTURE = 4 * 1024 * 1024
MAX_MANIFEST = 128 * 1024
MAX_PACKET = 512 * 1024
MAX_PACKETS = 8
MAX_VERTICES = 4096
MAX_DISCOVERY = 128
MAX_PAYLOAD = MAX_CAPTURE - 256 * 1024
SAFE_ID = re.compile(r"[A-Za-z0-9_-]{1,48}\Z")
HEX64 = re.compile(r"[0-9a-fA-F]{16}\Z")


class CaptureError(ValueError):
    """CodexAstraLocal: Invalid or unsupported evidence, never guessed state."""


# CodexAstraLocal: Shared scalar/shape checks reject coercion, duplicate
# keys and excessive nesting before section or replay interpretation.
def integer(value: Any, name: str, low: int = 0, high: int = 0xFFFFFFFF) -> int:
    if type(value) is not int or not low <= value <= high:
        raise CaptureError(f"{name}: expected integer in [{low}, {high}]")
    return value


def boolean(value: Any, name: str) -> bool:
    if type(value) is not bool:
        raise CaptureError(f"{name}: expected boolean")
    return value


def hex64(value: Any, name: str) -> str:
    if not isinstance(value, str) or not HEX64.fullmatch(value):
        raise CaptureError(f"{name}: expected exactly 16 hexadecimal digits")
    return value.lower()


def _object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result = {}
    for key, value in pairs:
        if key in result:
            raise CaptureError(f"duplicate JSON key: {key}")
        result[key] = value
    return result


def _shape(value: Any, depth: int = 0) -> None:
    if depth > 24:
        raise CaptureError("manifest nesting exceeds 24 levels")
    if isinstance(value, dict):
        for key, item in value.items():
            if len(key) > 128:
                raise CaptureError("manifest key exceeds 128 characters")
            _shape(item, depth + 1)
    elif isinstance(value, list):
        if len(value) > 4096:
            raise CaptureError("manifest array exceeds 4096 entries")
        for item in value:
            _shape(item, depth + 1)
    elif isinstance(value, float) and not math.isfinite(value):
        raise CaptureError("nonfinite JSON number")


def decode(data: bytes) -> tuple[dict[str, Any], bytes]:
    """CodexAstraLocal: Validate lengths before decoding any nested metadata."""
    if not HEADER.size <= len(data) <= MAX_CAPTURE:
        raise CaptureError("capture must be 16 bytes through 4 MiB")
    magic, manifest_length, payload_length = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise CaptureError("unsupported capture magic/version")
    if not 2 <= manifest_length <= MAX_MANIFEST:
        raise CaptureError("manifest must be 2 through 131072 bytes")
    if HEADER.size + manifest_length + payload_length != len(data):
        raise CaptureError("container lengths do not match exact file size")
    if payload_length > MAX_PAYLOAD:
        raise CaptureError("payload exceeds the reserved metadata/capacity budget")
    try:
        manifest = json.loads(data[HEADER.size:HEADER.size + manifest_length].decode("utf-8"),
                              object_pairs_hook=_object,
                              parse_constant=lambda value: (_ for _ in ()).throw(
                                  CaptureError(f"nonfinite JSON constant: {value}")))
        _shape(manifest)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise CaptureError(f"invalid manifest: {error}") from error
    if not isinstance(manifest, dict):
        raise CaptureError("manifest must be an object")
    return manifest, data[HEADER.size + manifest_length:]


@dataclass(frozen=True)
class Capture:
    manifest: dict[str, Any]
    payload: bytes
    source_sha256: str | None = None

    def section(self, packet: dict[str, Any], name: str,
                expected: int | None = None) -> bytes:
        """CodexAstraLocal: Sections are immutable slices of this file only."""
        try:
            descriptor = packet["sections"][name]
        except (KeyError, TypeError) as error:
            raise CaptureError(f"missing section: {name}") from error
        if not isinstance(descriptor, dict) or set(descriptor) != {"offset", "size"}:
            raise CaptureError(f"{name}: expected offset/size section descriptor")
        offset = integer(descriptor["offset"], f"{name}.offset", 0, len(self.payload))
        size = integer(descriptor["size"], f"{name}.size", 0, len(self.payload))
        if size > len(self.payload) - offset:
            raise CaptureError(f"{name}: section exceeds payload")
        if expected is not None and size != expected:
            raise CaptureError(f"{name}: expected {expected} bytes, found {size}")
        return self.payload[offset:offset + size]


def read(path: Path) -> Capture:
    # CodexAstraLocal: Bound the actual read even when a file changes after stat.
    with path.open("rb") as stream:
        data = stream.read(MAX_CAPTURE + 1)
    manifest, payload = decode(data)
    result = Capture(manifest, payload, hashlib.sha256(data).hexdigest())
    try:
        validate(result)
    except (KeyError, TypeError, AttributeError, OverflowError) as error:
        raise CaptureError(f"invalid manifest structure: {error}") from error
    return result


def encode(manifest: dict[str, Any], payload: bytes) -> bytes:
    """CodexAstraLocal: Shared synthetic-fixture encoder, also size checked."""
    raw = json.dumps(manifest, separators=(",", ":"), allow_nan=False).encode("utf-8")
    result = HEADER.pack(MAGIC, len(raw), len(payload)) + raw + payload
    decode(result)
    return result


# CodexAstraLocal: Structural validation applies even to incomplete packets.
# Playback imposes additional identity, submission and reconstruction checks below.
SECTION_SIZES = {"regs": 3072, "program": 16384, "swizzle": 16384,
                 "defaults": 256, "uniform_f": 1536, "uniform_b": 16,
                 "uniform_i": 16, "vs_pica": 1616, "vs_extra": 32, "fs": 1328}


def validate(capture: Capture) -> None:
    manifest = capture.manifest
    if manifest.get("schema") != 1 or type(manifest["schema"]) is not int:
        raise CaptureError("unsupported manifest schema")
    if not SAFE_ID.fullmatch(str(manifest.get("capture_id", ""))):
        raise CaptureError("invalid capture_id")
    hex64(manifest.get("title_id"), "title_id")
    hex64(manifest.get("run"), "run")
    if manifest.get("mode") not in ("discover", "capture"):
        raise CaptureError("unsupported capture mode")
    window = integer(manifest.get("window_swaps"), "window_swaps", 1, 8)
    per_swap = integer(manifest.get("packets_per_swap"), "packets_per_swap", 1, 2)
    if window * per_swap > MAX_PACKETS:
        raise CaptureError("window * packets_per_swap exceeds eight")
    packets = manifest.get("packets")
    discovery = manifest.get("discovery")
    if not isinstance(packets, list) or len(packets) > MAX_PACKETS:
        raise CaptureError("packets must be an array of at most eight entries")
    if not isinstance(discovery, list) or len(discovery) > MAX_DISCOVERY:
        raise CaptureError("discovery must be an array of at most 128 entries")
    if manifest["mode"] == "discover" and packets:
        raise CaptureError("discovery mode cannot contain full draw packets")
    first_swap = integer(manifest.get("first_swap"), "first_swap", 0, (1 << 64) - window)
    if (packets or discovery) and not first_swap:
        raise CaptureError("evidence exists before a nonzero capture-window start")
    attempts = set()
    per_interval = [0] * window

    def temporal(record: dict[str, Any], kind: str) -> None:
        # CodexAstraLocal: Interval claims must match the producer's swap clock;
        # malformed metadata cannot masquerade as consecutive temporal evidence.
        interval = integer(record.get("interval"), f"{kind}.interval", 0, window - 1)
        swap = integer(record.get("swap"), f"{kind}.swap", 1, (1 << 64) - 1)
        if swap != first_swap + interval:
            raise CaptureError(f"{kind}: swap does not match first_swap + interval")
        ordinal = integer(record.get("attempt_ordinal"), f"{kind}.attempt_ordinal", 0, 1023)
        identity = (interval, ordinal)
        if identity in attempts:
            raise CaptureError("duplicate global attempt ordinal within one swap interval")
        attempts.add(identity)
        if kind == "packet":
            per_interval[interval] += 1
            if per_interval[interval] > per_swap:
                raise CaptureError("packet count exceeds per-swap capture quota")

    all_ranges = []
    ids = set()
    for packet in packets:
        if not isinstance(packet, dict):
            raise CaptureError("packet must be an object")
        temporal(packet, "packet")
        packet_id = integer(packet.get("id"), "packet.id", 0, MAX_PACKETS - 1)
        if packet_id in ids:
            raise CaptureError("duplicate packet id")
        ids.add(packet_id)
        integer(packet.get("interval"), "packet.interval", 0, window - 1)
        integer(packet.get("attempt_ordinal"), "packet.attempt_ordinal", 0, 1023)
        for flag in ("recorded", "accepted", "completed"):
            boolean(packet.get(flag), f"packet.{flag}")
        if packet["completed"] and not packet["accepted"]:
            raise CaptureError("completed packet is not accepted")
        if packet["accepted"] and not packet["recorded"]:
            raise CaptureError("accepted packet was not recorded")
        hex64(packet.get("tick"), "packet.tick")
        sections = packet.get("sections")
        if not isinstance(sections, dict) or len(sections) > 26:
            raise CaptureError("invalid packet sections")
        packet_bytes = len(json.dumps(packet, separators=(",", ":")).encode())
        for name, descriptor in sections.items():
            if name not in SECTION_SIZES and name not in ("fixed", "indices") and not re.fullmatch(
                    r"vertex_(?:[0-9]|1[01])", name):
                raise CaptureError(f"unknown section: {name}")
            section = capture.section(packet, name, SECTION_SIZES.get(name))
            packet_bytes += len(section)
            if section:
                all_ranges.append((descriptor["offset"], descriptor["offset"] + len(section)))
        if packet_bytes > MAX_PACKET:
            raise CaptureError("packet including metadata exceeds 512 KiB")
    all_ranges.sort()
    end = 0
    for start, stop in all_ranges:
        if start != end:
            raise CaptureError("payload sections overlap or contain unaccounted bytes")
        end = stop
    if end != len(capture.payload):
        raise CaptureError("payload contains bytes not owned by a section")
    for row in discovery:
        temporal(row, "discovery")
        integer(row.get("interval"), "discovery.interval", 0, window - 1)
        integer(row.get("attempt_ordinal"), "discovery.attempt_ordinal", 0, 1023)
        integer(row.get("recorded_count"), "discovery.recorded_count", 1, 0xFFFFFFFF)
        for field in ("first_tick", "last_tick"):
            hex64(row.get(field), f"discovery.{field}")
        if int(row["last_tick"], 16) < int(row["first_tick"], 16):
            raise CaptureError("discovery ticks are reversed")
        for field in ("accepted_all", "completed_all"):
            boolean(row.get(field), f"discovery.{field}")
        if row["completed_all"] and not row["accepted_all"]:
            raise CaptureError("completed discovery row is not accepted")
        validate_key(row.get("key"))


# CodexAstraLocal: Discovery and payload keys share exact scalar bounds;
# a selector must not silently broaden malformed program/draw identities.
def validate_key(key: Any) -> None:
    if not isinstance(key, dict):
        raise CaptureError("missing draw key")
    for field in ("program_hash", "swizzle_hash", "color_address", "depth_address"):
        hex64(key.get(field), f"key.{field}")
    integer(key.get("entry"), "key.entry", 0, 4095)
    integer(key.get("vertex_count"), "key.vertex_count", 1, 0xFFFFFFFF)
    integer(key.get("input_count"), "key.input_count", 1, 16)
    integer(key.get("output_mask"), "key.output_mask", 0, 65535)


def replay_packet(capture: Capture, packet: dict[str, Any]) -> list[int]:
    """CodexAstraLocal: Refuse unsupported reconstruction before invoking C++."""
    if not packet["recorded"] or not packet["accepted"]:
        raise CaptureError("packet does not prove a successfully submitted draw")
    if int(packet["tick"], 16) == 0:
        raise CaptureError("accepted packet lacks a submission tick")
    validate_key(packet.get("key"))
    integer(packet.get("program_words"), "program_words", 0, 4096)
    integer(packet.get("swizzle_words"), "swizzle_words", 0, 4096)
    for name, size in SECTION_SIZES.items():
        capture.section(packet, name, size)
    draw = packet.get("draw", {})
    count = integer(draw.get("count"), "draw.count", 1, MAX_VERTICES)
    indexed = boolean(draw.get("indexed"), "draw.indexed")
    minimum = integer(draw.get("minimum"), "draw.minimum")
    maximum = integer(draw.get("maximum"), "draw.maximum", minimum)
    span = maximum - minimum + 1
    if span > MAX_VERTICES or packet["key"]["vertex_count"] != count:
        raise CaptureError("draw count/span does not match bounded key")
    vertex_offset = integer(draw.get("vertex_offset"), "draw.vertex_offset")
    base_vertex = integer(draw.get("base_vertex"), "draw.base_vertex", -0x80000000, 0x7FFFFFFF)
    if base_vertex != (-minimum if indexed else 0):
        raise CaptureError("unsupported Vulkan base-vertex reconstruction")
    original = integer(draw.get("original_index_width"), "draw.original_index_width", 0, 2)
    uploaded = integer(draw.get("uploaded_index_width"), "draw.uploaded_index_width", 0, 2)
    if indexed:
        if (original, uploaded) not in ((1, 1), (1, 2), (2, 2)) or maximum > 65535:
            raise CaptureError("unsupported index format")
        raw = capture.section(packet, "indices", count * uploaded)
        indices = list(struct.unpack(f"<{count}{'B' if uploaded == 1 else 'H'}", raw))
        # CodexAstraLocal: Live indices may change after the earlier range scan.
        # A narrower actual set is still reproducible from the frozen uploaded span.
        if min(indices) < minimum or max(indices) > maximum:
            raise CaptureError("uploaded indices exceed frozen upload span")
        if original == 1 and maximum > 255:
            raise CaptureError("widened u8 indices exceed original representation")
    else:
        if original or uploaded or "indices" in packet["sections"]:
            raise CaptureError("nonindexed draw carries index data")
        if minimum != vertex_offset or maximum != vertex_offset + count - 1:
            raise CaptureError("nonindexed offset/range mismatch")
        indices = list(range(minimum, maximum + 1))
    available = integer(packet.get("available_attributes"), "available_attributes", 1, 16)
    if packet["key"]["input_count"] > available:
        raise CaptureError("native input count exceeds available attributes")
    native = packet.get("native_inputs")
    if not isinstance(native, list) or len(native) != 16:
        raise CaptureError("expected sixteen native input descriptors")
    for attr in native:
        boolean(attr.get("is_default"), "native_input.is_default")
        for field in ("offset", "stride"):
            integer(attr.get(field), f"native_input.{field}")
        integer(attr.get("elements"), "native_input.elements", 0, 4)
        integer(attr.get("format"), "native_input.format", 0, 3)
    bindings = packet.get("bindings")
    if not isinstance(bindings, list) or len(bindings) > 12:
        raise CaptureError("invalid uploaded bindings")
    by_binding = {}
    for binding in bindings:
        number = integer(binding.get("binding"), "binding.binding", 0, 12)
        if number in by_binding:
            raise CaptureError("duplicate uploaded binding")
        by_binding[number] = binding
        integer(binding.get("guest_offset"), "binding.guest_offset")
        stride = integer(binding.get("guest_stride"), "binding.guest_stride", 1, 255)
        integer(binding.get("upload_stride"), "binding.upload_stride", stride, 65536)
        if binding.get("span") != span or binding.get("section") != f"vertex_{number}":
            raise CaptureError("binding span/section mismatch")
        capture.section(packet, binding["section"], span * stride)
    fixed = capture.section(packet, "fixed")
    if not 16 <= len(fixed) <= 272 or len(fixed) % 16:
        raise CaptureError("fixed binding must contain 1 through 17 complete vec4 values")
    extra = packet.get("extra", {})
    for field in ("use_clip_planes", "use_geometry_shader", "sanitize_mul", "separable_shader"):
        boolean(extra.get(field), f"extra.{field}")
    # CodexAstraLocal: Version 0.1.25 predates this policy. Missing means the
    # legacy generator; a present value must remain a JSON boolean, never truthy text.
    boolean(extra.get("precise_jit_dot", False), "extra.precise_jit_dot")
    if extra["use_geometry_shader"]:
        raise CaptureError("geometry-shader replay is unsupported")
    flags = extra.get("load_flags")
    if not isinstance(flags, list) or len(flags) != 16 or any(
            type(flag) is not int or flag not in (1, 2, 4, 9, 10, 12) for flag in flags):
        raise CaptureError("unsupported vertex load flags")
    layout = packet.get("layout", {})
    layout_bindings = layout.get("bindings")
    attributes = layout.get("attributes")
    if not isinstance(layout_bindings, list) or not 1 <= len(layout_bindings) <= 13:
        raise CaptureError("invalid layout bindings")
    by_layout = {}
    for binding in layout_bindings:
        number = integer(binding.get("binding"), "layout.binding", 0, 12)
        if number in by_layout:
            raise CaptureError("duplicate layout binding")
        by_layout[number] = binding
        is_fixed = boolean(binding.get("fixed"), "layout.fixed")
        stride = integer(binding.get("stride"), "layout.stride", 0, 65536)
        if is_fixed:
            if stride:
                raise CaptureError("fixed binding has a nonzero stride")
        elif number not in by_binding or stride != by_binding[number]["upload_stride"]:
            raise CaptureError("layout/upload binding stride mismatch")
    if sum(binding["fixed"] for binding in layout_bindings) != 1:
        raise CaptureError("exactly one fixed binding is required")
    if not isinstance(attributes, list) or len(attributes) != 16:
        raise CaptureError("expected sixteen layout attributes")
    locations = set()
    for attribute in attributes:
        location = integer(attribute.get("location"), "attribute.location", 0, 15)
        if location in locations:
            raise CaptureError("duplicate attribute location")
        locations.add(location)
        integer(attribute.get("binding"), "attribute.binding", 0, 12)
        integer(attribute.get("offset"), "attribute.offset", 0, 65535)
        integer(attribute.get("type"), "attribute.type", 0, 3)
        integer(attribute.get("size"), "attribute.size", 1, 4)
        integer(attribute.get("native_format"), "attribute.native_format", 1, 0xFFFFFFFF)
        for field in ("needs_conversion", "needs_emulation"):
            boolean(attribute.get(field), f"attribute.{field}")
        if attribute["binding"] not in by_layout:
            raise CaptureError("attribute references missing layout binding")
    for name in ("vs_pica", "vs_extra", "fs"):
        ranges = packet.get("ubo_ranges", {}).get(name)
        if not isinstance(ranges, list) or not ranges:
            raise CaptureError(f"{name}: missing defined UBO byte ranges")
        raw = capture.section(packet, name)
        defined = bytearray(len(raw))
        for region in ranges:
            if not isinstance(region, list) or len(region) != 2:
                raise CaptureError("invalid UBO byte range")
            offset = integer(region[0], "ubo.offset", 0, len(raw))
            size = integer(region[1], "ubo.size", 1, len(raw) - offset)
            if any(defined[offset:offset + size]):
                raise CaptureError("overlapping UBO semantic byte ranges")
            defined[offset:offset + size] = b"\x01" * size
        if any(value and not marked for value, marked in zip(raw, defined)):
            raise CaptureError("unobserved UBO padding is not zero")
        if name == "vs_pica" and defined != bytes([1] * 4 + [0] * 12 + [1] * 1600):
            raise CaptureError("PICA uniform semantic coverage is incomplete")
        if name == "vs_extra" and defined != bytes([1] * 8 + [0] * 8 + [1] * 16):
            raise CaptureError("extra vertex uniform semantic coverage is incomplete")
    if any(value not in (0, 1) for value in capture.section(packet, "uniform_b")):
        raise CaptureError("intended boolean uniform is not 0/1")
    return indices


def select(capture: Capture, row_number: int, capture_id: str, delay: int = 0,
           window: int = 4, per_swap: int = 2) -> dict[str, Any]:
    # CodexAstraLocal: Preserve the GLOBAL ordinal. Filtering must not renumber it.
    rows = capture.manifest["discovery"]
    integer(row_number, "row", 0, len(rows) - 1)
    row = rows[row_number]
    if not row["accepted_all"]:
        raise CaptureError("discovery row is not wholly submission-accepted")
    if not SAFE_ID.fullmatch(capture_id) or capture_id == capture.manifest["capture_id"]:
        raise CaptureError("choose a fresh safe capture_id")
    integer(delay, "delay_swaps", 0, 120)
    integer(window, "window_swaps", 1, 8)
    integer(per_swap, "packets_per_swap", 1, 2)
    if window * per_swap > MAX_PACKETS:
        raise CaptureError("window * packets_per_swap exceeds eight")
    key = row["key"]
    # CodexAstraLocal: Discovery can observe larger eligible GPU draws than the
    # payload budget supports. Never emit a sidecar the device will silently reject.
    integer(key["vertex_count"], "selected vertex_count", 1, MAX_VERTICES)
    return {"schema": 1, "enabled": True, "mode": "capture",
            "trigger": "next_gameplay_transition", "title_id": capture.manifest["title_id"],
            "capture_id": capture_id, "delay_swaps": delay,
            "window_swaps": window, "packets_per_swap": per_swap,
            "selector": {"program_hash": key["program_hash"],
                         "swizzle_hash": key["swizzle_hash"], "entry": key["entry"],
                         "vertex_count": key["vertex_count"],
                         "color_addresses": [key["color_address"]],
                         "attempt_ordinal": row["attempt_ordinal"]}}


def discovery_config(title: str, capture_id: str, delay: int = 1,
                     window: int = 4, per_swap: int = 2) -> dict[str, Any]:
    # CodexAstraLocal: Emit the actual strict production parser contract. This
    # prepares a local sidecar only; installing/arming it remains the device owner.
    title = hex64(title, "title_id")
    if int(title, 16) == 0 or not SAFE_ID.fullmatch(capture_id):
        raise CaptureError("nonzero title_id and safe capture_id are required")
    integer(delay, "delay_swaps", 0, 120)
    integer(window, "window_swaps", 1, 8)
    integer(per_swap, "packets_per_swap", 1, 2)
    if window * per_swap > MAX_PACKETS:
        raise CaptureError("window * packets_per_swap exceeds eight")
    return {"schema": 1, "enabled": True, "mode": "discover", "title_id": title,
            "capture_id": capture_id, "trigger": "next_gameplay_transition",
            "delay_swaps": delay, "window_swaps": window,
            "packets_per_swap": per_swap, "selector": {"attempt_ordinal": 0}}


def inspect(capture: Capture) -> dict[str, Any]:
    # CodexAstraLocal: Print bounded identities/status, never raw guest words.
    manifest = capture.manifest
    result = {key: manifest.get(key) for key in
              ("schema", "capture_id", "mode", "title_id", "run", "version", "revision",
               "first_swap", "window_swaps", "packets_per_swap", "limits", "summary")}
    result["discovery"] = [{"row": index, **row} for index, row in
                           enumerate(manifest["discovery"])]
    packets = []
    for packet in manifest["packets"]:
        info = {key: packet.get(key) for key in
                ("id", "swap", "interval", "attempt_ordinal", "recorded", "accepted",
                 "completed", "tick", "status", "key", "draw", "pipeline")}
        try:
            replay_packet(capture, packet)
            info["structural_replay_check"] = "passed; native identity/fetch validation pending"
        except CaptureError as error:
            info["structural_replay_check"] = str(error)
        packets.append(info)
    result["packets"] = packets
    result["intervals"] = intervals(capture)
    result["scope"] = ("Private vertex evidence. Swaps are not video/guest frames. Submission "
                       "acceptance and completion are separate; neither proves correct pixels.")
    return result


def intervals(capture: Capture) -> list[dict[str, Any]]:
    # CodexAstraLocal: Global caps/failures cannot explain a particular missing
    # swap. Absence is censored/unknown evidence, never a clean frame or zero draws.
    manifest = capture.manifest
    result = []
    for interval in range(manifest["window_swaps"]):
        packets = [packet for packet in manifest["packets"] if packet["interval"] == interval]
        rows = [row for row in manifest["discovery"] if row["interval"] == interval]
        result.append({"interval": interval, "packets": len(packets), "discovery_rows": len(rows),
                       "accepted_packets": sum(packet["accepted"] for packet in packets),
                       "completed_packets": sum(packet["completed"] for packet in packets),
                       "status": "evidence_present" if packets or rows else "no_evidence_unknown_censored"})
    return result


# CodexAstraLocal: Inspect retained evidence or emit a bounded request;
# this host CLI performs no device action and does not arm a session.
def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    show = commands.add_parser("inspect")
    show.add_argument("capture", type=Path)
    choose = commands.add_parser("select")
    choose.add_argument("capture", type=Path)
    choose.add_argument("--row", type=int, required=True)
    choose.add_argument("--capture-id", required=True)
    choose.add_argument("--delay", type=int, default=0)
    choose.add_argument("--window", type=int, default=4)
    choose.add_argument("--per-swap", type=int, default=2)
    choose.add_argument("--output", type=Path)
    discover = commands.add_parser("discover")
    discover.add_argument("--title-id", required=True)
    discover.add_argument("--capture-id", required=True)
    discover.add_argument("--delay", type=int, default=1)
    discover.add_argument("--window", type=int, default=4)
    discover.add_argument("--per-swap", type=int, default=2)
    discover.add_argument("--output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "discover":
            value = discovery_config(args.title_id, args.capture_id, args.delay,
                                     args.window, args.per_swap)
        else:
            capture = read(args.capture)
            value = inspect(capture) if args.command == "inspect" else select(
                capture, args.row, args.capture_id, args.delay, args.window, args.per_swap)
        text = json.dumps(value, indent=2, allow_nan=False) + "\n"
        if args.command != "inspect" and args.output:
            with args.output.open("x") as stream:
                stream.write(text)
            args.output.chmod(0o600)
            print(f"Wrote {args.output}; arm with a fresh Automatic -> Gameplay phase edge.")
        else:
            print(text, end="")
    except (CaptureError, OSError) as error:
        parser.exit(2, f"capture: {error}\n")


if __name__ == "__main__":
    main()
