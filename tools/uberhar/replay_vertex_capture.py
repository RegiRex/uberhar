#!/usr/bin/env python3
"""CodexAstraLocal: Replay accepted private vertex packets on production CPU/Mesa.

The CPU oracle retains draw-local register state and the production 64-entry FIFO.
Mesa uses the captured raw vertex formats and actual bound UBO bytes. Results are
pre-primitive vertex evidence, not an Adreno replay or a fragment/pixel verdict.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
from typing import Any

import vertex_capture as vc


ROOT = Path(__file__).resolve().parents[2]
SOURCES = ["src/video_core/pica/shader_unit.cpp", "src/video_core/pica/shader_setup.cpp",
           "src/video_core/pica/output_vertex.cpp", "src/video_core/shader/shader_interpreter.cpp",
           "src/video_core/shader/generator/glsl_shader_gen.cpp",
           "src/video_core/shader/generator/glsl_shader_decompiler.cpp",
           "src/video_core/shader/generator/shader_gen.cpp"]
VARYINGS = ["gl_Position", "normquat", "primary_color", "texcoord0", "texcoord1",
            "texcoord0_w", "view", "texcoord2"]
SEMANTICS = [f"{name}.{component}" for name, components in
             (("position", "xyzw"), ("quaternion", "xyzw"), ("color", "rgba"),
              ("texcoord0", "uv"), ("texcoord1", "uv"), ("texcoord0_w", "w"),
              ("view", "xyz"), ("texcoord2", "uv")) for component in components]


def build(binary: Path, source: Path | None = None) -> None:
    # CodexAstraLocal: Compile actual production math/transport without an emulator
    # build tree. The one stub is an unused include in shader_gen.cpp, not behavior.
    binary.parent.mkdir(parents=True, exist_ok=True)
    stubs = binary.parent / "stubs"
    (stubs / "common").mkdir(parents=True, exist_ok=True)
    (stubs / "common/settings.h").write_text(
        "// CodexAstraLocal: Generator does not use the settings interface.\n#pragma once\n")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2",
                    "-DMICROPROFILE_ENABLED=0", "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL",
                    f"-I{stubs}", "-Isrc", "-Iexternals/fmt/include", "-Iexternals/boost",
                    "-Iexternals/xxHash", "-Iexternals/nihstro/include", "-Iexternals/microprofile",
                    "-Iexternals/json", str(source or ROOT / "tools/uberhar/replay_vertex_capture.cpp"),
                    *SOURCES, "-o", str(binary)], cwd=ROOT, check=True)


# CodexAstraLocal: VkFormat values are stable API enumerants, checked against the
# captured scalar type/effective width. GL fetch uses the same signedness, bit
# width, normalized=false and integer/scaled route; no CPU float preconversion.
FORMAT_GROUPS = [
    (0, "i1", "f", (12, 19, 26, 40)), (0, "i1", "i", (14, 21, 28, 42)),
    (1, "u1", "f", (11, 18, 25, 39)), (1, "u1", "i", (13, 20, 27, 41)),
    (2, "i2", "f", (73, 80, 87, 94)), (2, "i2", "i", (75, 82, 89, 96)),
    (3, "f4", "f", (100, 103, 106, 109))]
FORMATS = {number: (kind, fmt, cls, components + 1) for kind, fmt, cls, group in
           FORMAT_GROUPS for components, number in enumerate(group)}


def attribute_format(attribute: dict[str, Any], flag: int) -> tuple[str, str, int]:
    try:
        kind, fmt, cls, components = FORMATS[attribute["native_format"]]
    except KeyError as error:
        raise vc.CaptureError("unsupported actual VkFormat") from error
    effective = 4 if attribute["needs_emulation"] else attribute["size"]
    if attribute["needs_emulation"] and attribute["size"] != 3:
        raise vc.CaptureError("only three-component format emulation is understood")
    if kind != attribute["type"] or components != effective:
        raise vc.CaptureError("actual format disagrees with captured layout traits")
    expected_flag = (2 if kind in (0, 2) else 4) if cls == "i" else 1
    if attribute["needs_conversion"] != (cls == "i") or flag != (
            expected_flag | (8 if attribute["needs_emulation"] else 0)):
        raise vc.CaptureError("load flags disagree with actual native format/traits")
    return cls, f"{components}{fmt}", components * int(fmt[1:])


def intended_ubo(capture: vc.Capture, packet: dict[str, Any]) -> bytes:
    # CodexAstraLocal: Counterfactual GPU uniforms are explicitly separate from
    # actual bytes. They test a transport hypothesis and never replace evidence.
    result = bytearray(1616)
    bits = sum(value << bit for bit, value in enumerate(capture.section(packet, "uniform_b")))
    struct.pack_into("<I", result, 0, bits)
    struct.pack_into("<16I", result, 16, *capture.section(packet, "uniform_i"))
    result[80:] = capture.section(packet, "uniform_f")
    return bytes(result)


def compare(left: bytes, right: bytes, names: list[str], limit: int = 16) -> dict[str, Any]:
    # CodexAstraLocal: Count bit differences and finite tolerances independently.
    # Equal NaN classes remain marked nonfinite; no NaN/Inf is silently a pass.
    if len(left) != len(right) or len(left) % (len(names) * 4):
        raise vc.CaptureError("comparison dimensions disagree")
    examples = []
    bits_different = significant = nonfinite = 0
    maximum = 0.0
    for index, (a, b) in enumerate(zip(struct.iter_unpack("<f", left), struct.iter_unpack("<f", right))):
        x, y = a[0], b[0]
        bit_mismatch = left[index * 4:index * 4 + 4] != right[index * 4:index * 4 + 4]
        bits_different += bit_mismatch
        finite = math.isfinite(x) and math.isfinite(y)
        if not finite:
            nonfinite += 1
            mismatch = True
        else:
            difference = abs(x - y)
            maximum = max(maximum, difference)
            mismatch = difference > 1e-6 + 1e-5 * max(abs(x), abs(y))
        significant += mismatch
        if mismatch and len(examples) < limit:
            examples.append({"vertex": index // len(names), "component": names[index % len(names)],
                             "left": x if math.isfinite(x) else str(x),
                             "right": y if math.isfinite(y) else str(y),
                             "left_bits": left[index * 4:index * 4 + 4].hex(),
                             "right_bits": right[index * 4:index * 4 + 4].hex()})
    return {"scalars": len(left) // 4, "bit_differences": bits_different,
            "significant_differences": significant, "nonfinite_pairs": nonfinite,
            "maximum_finite_absolute_difference": maximum, "examples": examples,
            "tolerance": {"absolute": 1e-6, "relative": 1e-5}}


def render(capture: vc.Capture, packet: dict[str, Any], directory: Path) -> dict[str, Any]:
    import moderngl

    indices = vc.replay_packet(capture, packet)
    source = (directory / "vertex.vert").read_text()
    native_source = (directory / "native.vert").read_text()
    # CodexAstraLocal: Only declarations from the actual production generator
    # determine active input registers; captured layout still decides their fetch.
    declarations = re.findall(r"layout\(location = (\d+)\) in ([iu]?vec4) vs_in_typed_reg\d+;", source)
    registers = [int(location) for location, _ in declarations]
    if len(set(registers)) != len(registers) or any(reg > 15 for reg in registers):
        raise vc.CaptureError("unexpected generated vertex input declarations")
    os.environ.setdefault("MESA_SHADER_CACHE_DIR", str(directory / "mesa-cache"))
    context = moderngl.create_standalone_context(require=430, backend="egl")
    resources = []
    try:
        target = context.simple_framebuffer((1, 1))
        resources.append(target)
        target.use()
        draw = packet["draw"]
        count = draw["count"]
        # CodexAstraLocal: Compact rows are expanded with zero padding only when
        # every active fetch is proven to stay in captured initialized row bytes.
        layout = {binding["binding"]: binding for binding in packet["layout"]["bindings"]}
        uploaded = {binding["binding"]: binding for binding in packet["bindings"]}
        attributes = {attr["location"]: attr for attr in packet["layout"]["attributes"]}
        for reg in registers:
            attr = attributes[reg]
            _, _, width = attribute_format(attr, packet["extra"]["load_flags"][reg])
            binding = layout[attr["binding"]]
            length = len(capture.section(packet, "fixed")) if binding["fixed"] else \
                uploaded[attr["binding"]]["guest_stride"]
            if attr["offset"] + width > length:
                raise vc.CaptureError("active GPU fetch requires uncaptured padding/cross-row bytes")
        buffers = {}
        allocated = 0
        for number, binding in layout.items():
            if binding["fixed"]:
                raw = capture.section(packet, "fixed")
            else:
                upload = uploaded[number]
                raw_compact = capture.section(packet, upload["section"])
                stride = upload["guest_stride"]
                host_stride = upload["upload_stride"]
                length = host_stride * upload["span"]
                if length > vc.MAX_CAPTURE - allocated:
                    raise vc.CaptureError("reconstructed GPU buffers exceed 4 MiB")
                raw = bytearray(length)
                for vertex in range(upload["span"]):
                    raw[vertex * host_stride:vertex * host_stride + stride] = \
                        raw_compact[vertex * stride:(vertex + 1) * stride]
            allocated += len(raw)
            if allocated > vc.MAX_CAPTURE:
                raise vc.CaptureError("reconstructed GPU buffers exceed 4 MiB")
            buffers[number] = context.buffer(raw)
            resources.append(buffers[number])
        index_buffer = None
        if draw["indexed"]:
            width = draw["uploaded_index_width"]
            # No guest shader can read gl_VertexID. This is exactly its Vulkan
            # base-vertex adjustment, while CPU cache keys retain original indices.
            index_buffer = context.buffer(struct.pack(
                f"<{count}{'B' if width == 1 else 'H'}", *(index - draw["minimum"] for index in indices)))
            resources.append(index_buffer)
        ubo_pica = context.buffer(capture.section(packet, "vs_pica"))
        ubo_extra = context.buffer(capture.section(packet, "vs_extra"))
        resources.extend((ubo_pica, ubo_extra))
        ubo_pica.bind_to_uniform_block(0)
        ubo_extra.bind_to_uniform_block(1)

        def execute(shader: str, varyings: list[str], scalars: int,
                    native_input: bytes | None = None) -> bytes:
            program = context.program(vertex_shader=shader, varyings=varyings)
            try:
                vao = context.vertex_array(program, [], index_buffer=(
                    index_buffer if native_input is None else None),
                    index_element_size=draw["uploaded_index_width"] or 4)
                vertex_buffer = None
                try:
                    if native_input is None:
                        for reg in registers:
                            name = f"vs_in_typed_reg{reg}"
                            if name not in program:
                                continue
                            attr = attributes[reg]
                            binding = layout[attr["binding"]]
                            cls, fmt, _ = attribute_format(attr, packet["extra"]["load_flags"][reg])
                            vao.bind(program[name].location, cls, buffers[attr["binding"]], fmt,
                                     offset=attr["offset"], stride=binding["stride"],
                                     divisor=1 if binding["fixed"] else 0, normalize=False)
                    else:
                        if len(native_input) != count * 22 * 4:
                            raise vc.CaptureError("CPU output size mismatch")
                        vertex_buffer = context.buffer(native_input)
                        for name, offset, components in (("position", 0, 4), ("normquat", 4, 4),
                                ("color", 8, 4), ("texcoord0", 12, 2), ("texcoord1", 14, 2),
                                ("texcoord0_w", 16, 1), ("view", 17, 3), ("texcoord2", 20, 2)):
                            attribute = f"vert_{name}"
                            if attribute in program:
                                vao.bind(program[attribute].location, "f", vertex_buffer,
                                         f"{components}f4", offset=offset * 4, stride=88)
                    output = context.buffer(reserve=count * scalars * 4)
                    try:
                        vao.transform(output, mode=moderngl.POINTS, vertices=count)
                        context.finish()
                        return output.read()
                    finally:
                        output.release()
                finally:
                    vao.release()
                    if vertex_buffer is not None:
                        vertex_buffer.release()
            finally:
                program.release()

        actual = execute(source, VARYINGS, 22)
        intended = intended_ubo(capture, packet)
        ubo_pica.write(intended)
        counterfactual = execute(source, VARYINGS, 22)
        ubo_pica.write(capture.section(packet, "vs_pica"))
        cpu_raw = (directory / "cpu.bin").read_bytes()
        cpu_fresh = (directory / "cpu_fresh.bin").read_bytes()
        transported = execute(native_source, VARYINGS, 22, cpu_raw)
        fresh_transported = execute(native_source, VARYINGS, 22, cpu_fresh)
        # CodexAstraLocal: Observe typed fetch and generator's declared W padding
        # separately, without substituting those inputs in the production shader.
        input_source = ["#version 430"]
        input_body = []
        for reg, (_, declaration) in zip(registers, declarations):
            input_source += [f"layout(location={reg}) in {declaration} vs_in_typed_reg{reg};",
                             f"out vec4 capture_input_{reg};"]
            input_body.append(f"capture_input_{reg}=vec4(vs_in_typed_reg{reg});")
            if packet["extra"]["load_flags"][reg] & 8:
                input_body.append(f"capture_input_{reg}.w=1;")
        input_source.append("void main(){" + "".join(input_body) + "gl_Position=vec4(0,0,0,1);}")
        inputs = execute("\n".join(input_source), [f"capture_input_{reg}" for reg in registers],
                         len(registers) * 4) if registers else b""
        cpu_inputs = (directory / "cpu_inputs.bin").read_bytes()
        if len(cpu_inputs) != count * 16 * 4 * 4:
            raise vc.CaptureError("CPU loaded-register image size mismatch")
        selected_cpu_inputs = b"".join(cpu_inputs[vertex * 256 + reg * 16:
                                                vertex * 256 + reg * 16 + 16]
                                      for vertex in range(count) for reg in registers)
        for name, data in (("gpu_actual.bin", actual), ("gpu_intended_uniforms.bin", counterfactual),
                           ("cpu_transported.bin", transported), ("gpu_inputs.bin", inputs)):
            (directory / name).write_bytes(data)
        uniform_actual = capture.section(packet, "vs_pica")
        uniform_differences = [offset for offset in range(1616)
                               if uniform_actual[offset] != intended[offset]]
        return {"renderer": context.info["GL_RENDERER"], "version": context.info["GL_VERSION"],
                "cpu_vs_gpu_actual": compare(transported, actual, SEMANTICS),
                "cpu_vs_gpu_intended_uniforms": compare(transported, counterfactual, SEMANTICS),
                "cpu_carry_vs_fresh_unit": compare(transported, fresh_transported, SEMANTICS),
                "cpu_vs_gpu_inputs": compare(selected_cpu_inputs, inputs,
                    [f"input{reg}.{component}" for reg in registers for component in "xyzw"])
                    if registers else {"scalars": 0, "note": "shader declares no vertex inputs"},
                "actual_vs_intended_uniforms": {"different_bytes": len(uniform_differences),
                    "first_offsets": uniform_differences[:32],
                    "scope": "PICA bool/int/float uniforms only. Actual clip/viewport UBO is "
                             "applied to both routes; intended extra-VS/FS state is not compared."},
                "scope": "Actual uploaded inputs and bound VS UBOs, before primitive quaternion "
                         "correction. Host GL/CPU evidence only; no Adreno, FS, pixel, or frame proof."}
    finally:
        for resource in reversed(resources):
            resource.release()
        context.release()


def replay(path: Path, output: Path, binary: Path, gpu: bool = True,
           timeout: int = 60) -> dict[str, Any]:
    capture = vc.read(path)
    output.mkdir(parents=True, exist_ok=False)
    output.chmod(0o700)
    # CodexAstraLocal: Workers read one frozen validated snapshot, so changes to
    # the original file between CPU and Mesa cannot manufacture a mismatch.
    snapshot = output / "validated-snapshot.uvc"
    frozen = vc.encode(capture.manifest, capture.payload)
    snapshot.write_bytes(frozen)
    snapshot.chmod(0o400)
    report: dict[str, Any] = {"capture_id": capture.manifest["capture_id"],
                            "source_capture_sha256": capture.source_sha256,
                            "snapshot_sha256": hashlib.sha256(frozen).hexdigest(),
                            "source_version": capture.manifest.get("version"),
                            "source_revision": capture.manifest.get("revision"),
                            "capture_summary": capture.manifest.get("summary"),
                            "capture_intervals": vc.intervals(capture), "packets": []}
    # CodexAstraLocal: Preserve actual worker identity and current source context.
    # A reused binary is explicitly caller supplied; checkout identity alone does
    # not prove its build provenance or that it matches the captured APK.
    head = subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, capture_output=True,
                          text=True, timeout=10, check=True).stdout.strip()
    critical = [*SOURCES, "tools/uberhar/replay_vertex_capture.cpp",
                "src/video_core/pica/uberhar_vertex_input.h",
                "src/video_core/pica/uberhar_vertex_output.h",
                "src/video_core/pica/uberhar_vertex_cache.h", "src/video_core/pica_types.h",
                "src/video_core/pica/shader_unit.h", "src/video_core/pica/shader_setup.h"]
    digest = hashlib.sha256()
    for source_path in critical:
        digest.update(source_path.encode() + b"\0")
        digest.update((ROOT / source_path).read_bytes())
    revision = capture.manifest.get("revision", "")
    report["host_provenance"] = {"checkout_head": head,
        "worker_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "current_critical_sources_sha256": digest.hexdigest(),
        "capture_revision_matches_head": bool(isinstance(revision, str) and
            re.fullmatch(r"[0-9a-fA-F]{7,40}", revision) and head.startswith(revision.lower())),
        "note": "Current critical-source fingerprint is not a complete build attestation; "
                "use a worker built from the matching APK source."}
    for index, packet in enumerate(capture.manifest["packets"]):
        record: dict[str, Any] = {"id": packet["id"], "completed_on_device": packet["completed"]}
        try:
            vc.replay_packet(capture, packet)
            directory = output / f"packet-{packet['id']}"
            directory.mkdir(mode=0o700)
            result = subprocess.run([str(binary.resolve()), str(snapshot.resolve()), str(index),
                                     str(directory.resolve())], capture_output=True, text=True,
                                    timeout=timeout, check=False)
            (directory / "cpu-worker.log").write_text(result.stdout + result.stderr)
            if result.returncode:
                raise vc.CaptureError(f"CPU replay exited {result.returncode}: {result.stderr.strip()}")
            record["cpu"] = json.loads((directory / "cpu.json").read_text())
            if gpu:
                # CodexAstraLocal: Guest control flow and driver compilation run
                # in a separate bounded-time process, just like the CPU worker.
                result = subprocess.run([sys.executable, str(Path(__file__).resolve()),
                    str(snapshot.resolve()), "--render-worker", str(index), "--output",
                    str(directory.resolve())], capture_output=True, text=True,
                    timeout=timeout, check=False)
                (directory / "mesa-worker.log").write_text(result.stdout + result.stderr)
                if result.returncode:
                    raise vc.CaptureError(f"Mesa replay exited {result.returncode}: {result.stderr.strip()}")
                record["mesa"] = json.loads((directory / "mesa.json").read_text())
            record["status"] = "replayed"
        except (vc.CaptureError, subprocess.TimeoutExpired, RuntimeError) as error:
            record.update(status="unsupported_or_invalid", reason=str(error))
        report["packets"].append(record)
    report["scope"] = "A replay mismatch is vertex evidence, not a demonstrated cause of moon " \
                      "flashing/ghost corruption. Accepted-but-pending packets remain marked."
    (output / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")
    return report


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path, nargs="?")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--binary", type=Path,
                        default=ROOT / "build/uberhar-probe/vertex-capture/replay")
    parser.add_argument("--build-only", action="store_true")
    parser.add_argument("--reuse-binary", action="store_true",
                        help="explicitly reuse an already built, matching-checkout replay worker")
    parser.add_argument("--cpu-only", action="store_true")
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--render-worker", type=int, help=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        if args.render_worker is not None:
            capture = vc.read(args.capture)
            vc.integer(args.render_worker, "packet", 0, len(capture.manifest["packets"]) - 1)
            result = render(capture, capture.manifest["packets"][args.render_worker], args.output)
            (args.output / "mesa.json").write_text(json.dumps(result, indent=2, allow_nan=False) + "\n")
            return
        if not args.reuse_binary:
            build(args.binary)
        if args.build_only:
            print(f"Built {args.binary}")
            return
        if not args.capture or not args.output:
            parser.error("capture and --output are required for replay")
        vc.integer(args.timeout, "timeout", 1, 300)
        report = replay(args.capture, args.output, args.binary, not args.cpu_only, args.timeout)
        print(json.dumps(report, indent=2, allow_nan=False))
        if not report["packets"] or any(p["status"] != "replayed" for p in report["packets"]):
            parser.exit(2)
    except (vc.CaptureError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(2, f"replay: {error}\n")


if __name__ == "__main__":
    main()
