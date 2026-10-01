#!/usr/bin/env python3
"""AstraPro: Exercise the production uniform-sync body across abandoned GPU draws.

The harness uses the real register, dirty-table and uniform types. Only the owning
rasterizer/PICA shell is replaced; no Vulkan submission or image parity is claimed.
Extracting the function avoids duplicating its sync arithmetic in a test model.
"""
import argparse
import hashlib
import os
from pathlib import Path
import subprocess


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path("src/video_core/rasterizer_accelerated.cpp"))
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/uniform-retry"))
    args = parser.parse_args()
    source = args.source.read_text()
    functions = "\n".join(extract_function(source, signature) for signature in (
        "static Common::Vec4f ColorRGBA8(",
        "static Common::Vec3f LightColor(",
        "void RasterizerAccelerated::SyncDrawUniforms()"))
    header = r'''
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include "video_core/pica/dirty_regs.h"
#include "video_core/shader/generator/shader_uniforms.h"
namespace VideoCore {
using Pica::f24;
using namespace Pica::Shader::Generator;
struct RasterizerAccelerated {
    struct PicaShell { Pica::DirtyRegs dirty_regs{}; } pica;
    Pica::RegsInternal regs{};
    VSUniformData vs_data{};
    FSUniformData fs_data{};
    bool vs_data_dirty = true, fs_data_dirty = true;
    u64 pending_vs_uniform_resyncs = 0;
    void SyncDrawUniforms();
};
'''
    tests = r'''
} // namespace VideoCore
namespace {
unsigned checks = 0, failures = 0;
void Require(bool value, const char* message) {
    ++checks;
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}
}
int main() {
    using R = VideoCore::RasterizerAccelerated;
    R r;
    r.SyncDrawUniforms();
    Require(r.vs_data_dirty, "initial pending VS upload was erased by a no-op sync");
    r.vs_data_dirty = false; r.fs_data_dirty = false; // successful transport
    r.SyncDrawUniforms();
    Require(!r.vs_data_dirty && !r.fs_data_dirty, "clean sync requests a redundant upload");

    // AstraPro: A GPU attempt consumes register dirty bits before it discovers
    // a pending pipeline. CPU retry must retain the unuploaded clip block.
    r.regs.rasterizer.clip_enable.Assign(1);
    r.pica.dirty_regs.Set(PICA_REG_INDEX(rasterizer.clip_enable));
    r.SyncDrawUniforms();
    const auto saved = r.vs_data;
    Require(r.vs_data_dirty && r.vs_data.enable_clip1 == 1, "clip edit was not prepared");
    Require(!r.pica.dirty_regs.CheckClipping(), "register dirty table was not consumed");
    for (unsigned retry = 0; retry < 4; ++retry) {
        r.SyncDrawUniforms();
        Require(r.vs_data_dirty, "pending clipping upload lost on retry");
        Require(std::memcmp(&saved, &r.vs_data, sizeof(saved)) == 0, "retry changes clip data");
    }
    r.vs_data_dirty = false; // modeled successful CPU uniform upload
    r.SyncDrawUniforms();
    Require(!r.vs_data_dirty, "uploaded block did not become clean");

    // AstraPro: Pending disable must survive as well as pending enable.
    r.regs.rasterizer.clip_enable.Assign(0);
    r.pica.dirty_regs.Set(PICA_REG_INDEX(rasterizer.clip_enable));
    r.SyncDrawUniforms();
    r.SyncDrawUniforms();
    Require(r.vs_data_dirty && r.vs_data.enable_clip1 == 0, "clip disable lost on retry");
    r.vs_data_dirty = false;

    // AstraPro: Force a flip discrepancy without depending on framebuffer layout
    // encoding. First sync reconciles it, subsequent unchanged sync must keep dirty.
    r.vs_data.flip_viewport = !r.regs.framebuffer.framebuffer.IsFlipped();
    r.SyncDrawUniforms();
    Require(r.vs_data_dirty, "flip edit was not prepared");
    Require(r.vs_data.flip_viewport == r.regs.framebuffer.framebuffer.IsFlipped(), "wrong flip");
    r.SyncDrawUniforms();
    Require(r.vs_data_dirty, "flip upload lost on retry");
    r.vs_data_dirty = false;

    // AstraPro: Fragment dirty bits already accumulate; the VS correction must
    // not disturb that behavior or turn fragment-only edits into VS uploads.
    r.regs.framebuffer.output_merger.alpha_test.ref.Assign(91);
    r.pica.dirty_regs.Set(PICA_REG_INDEX(framebuffer.output_merger.alpha_test));
    r.SyncDrawUniforms();
    Require(r.fs_data_dirty && r.fs_data.alphatest_ref == 91, "alpha uniform sync failed");
    Require(!r.vs_data_dirty, "fragment edit dirtied VS block");
    r.SyncDrawUniforms();
    Require(r.fs_data_dirty && r.fs_data.alphatest_ref == 91, "fragment dirty state lost");
    Require(!r.vs_data_dirty, "fragment retry dirtied VS block");
    r.fs_data_dirty = false;
    r.SyncDrawUniforms();
    Require(!r.vs_data_dirty && !r.fs_data_dirty, "clean final state was not preserved");
    std::printf("Uniform retry: %u checks, %u failures; extracted production body with real data types\n", checks, failures);
    return failures ? 1 : 0;
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cpp = args.output.with_suffix('.cpp')
    cpp.write_text(header + functions + tests)
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', *flags, '-DFMT_HEADER_ONLY',
               '-Isrc', '-Iexternals/fmt/include', '-Iexternals/boost', str(cpp), '-o', str(args.output)]
    subprocess.run(command, check=True)
    print("production_functions_sha256=" + hashlib.sha256(functions.encode()).hexdigest(), flush=True)
    subprocess.run([str(args.output)], check=True)


if __name__ == '__main__':
    main()
