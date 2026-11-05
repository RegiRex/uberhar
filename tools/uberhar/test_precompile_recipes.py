#!/usr/bin/env python3
"""CodexAstraUlt-2: Exercise the production codec and extracted production data declarations.

The sparse-checkout fixture uses actual FSConfig, BitField, static pipeline declarations,
register headers/constructors and CPU-layout construction. Vulkan objects, unused float24/logging/serialization APIs and
external hash link symbols are excluded; driver compilation is outside this fixture.
"""
import argparse
import os
from pathlib import Path
import subprocess


def declaration(text: str, signature: str) -> str:
    start = text.index(signature)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end] + ";\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--negative-controls", action="store_true")
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/precompile-recipes"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    stubs = output.parent / "precompile-recipes-types"
    (stubs / "video_core/pica").mkdir(parents=True, exist_ok=True)
    (stubs / "video_core/renderer_vulkan").mkdir(parents=True, exist_ok=True)
    # CodexAstraUlt-2: Full production registers and constructors remain intact. Only unused
    # logging/serialization/viewport float APIs are supplied to this SDK-free host fixture.
    obsolete = stubs / "video_core/pica/regs_internal.h"
    if obsolete.exists():
        obsolete.unlink()
    definitions = {
        "boost/serialization/access.hpp": "#pragma once\nnamespace boost::serialization { class access {}; }\n",
        "common/assert.h": '#pragma once\n#include <stdexcept>\n#include "common/common_funcs.h"\n'
            '#define ASSERT(condition) do { if (!(condition)) throw std::runtime_error("assertion"); } while(false)\n'
            '#define ASSERT_MSG(condition, ...) ASSERT(condition)\n'
            '#define UNREACHABLE() throw std::runtime_error("unreachable")\n'
            '#define UNREACHABLE_MSG(...) UNREACHABLE()\n#define UNIMPLEMENTED() UNREACHABLE()\n',
        "common/logging/log.h": '#pragma once\n#include <stdexcept>\n'
            '#define LOG_CRITICAL(...) throw std::runtime_error("unexpected error log")\n',
        "video_core/pica_types.h": '#pragma once\n#include <stdexcept>\n#include "common/common_types.h"\n'
            'namespace Pica { struct f24 { static f24 FromRaw(u32) { throw std::runtime_error("unused float24"); } '
            'operator float() const { throw std::runtime_error("unused float24"); } '
            'float ToFloat32() const { throw std::runtime_error("unused float24"); } }; }\n',
    }
    for name, content in definitions.items():
        path = stubs / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content)
    # CodexAstraUlt-2: Uncalled external hash APIs need declarations only; the codec owns its checksum.
    (stubs / "xxhash.h").write_text('#pragma once\n#include <cstdint>\n#include <cstddef>\nstd::uint64_t XXH3_64bits(const void*,std::size_t);\n')
    source = (root / "src/video_core/renderer_vulkan/vk_graphics_pipeline.h").read_text()
    fields = source[source.index("namespace Vulkan {"):source.index("struct DynamicPipelineInfo")]
    pixel = declaration((root / "src/video_core/rasterizer_cache/pixel_format.h").read_text(), "enum class PixelFormat : u32 {")
    header = '#pragma once\n#include <algorithm>\n#include "common/hash.h"\n#include "video_core/pica/regs_internal.h"\n'
    header += "namespace VideoCore {\n" + pixel + "}\n"
    header += '#define LAYOUT_HASH static_cast<u64>(sizeof(T)), static_cast<u64>(alignof(T))\n'
    header += '#define FIELD_HASH(x) static_cast<u64>(offsetof(T,x)), static_cast<u64>(sizeof(x))\n'
    header += fields + '}\n#undef LAYOUT_HASH\n#undef FIELD_HASH\n'
    (stubs / "video_core/renderer_vulkan/vk_graphics_pipeline.h").write_text(header)
    source = (root / "src/video_core/renderer_vulkan/vk_rasterizer.cpp").read_text()
    layout = declaration(source, "void RasterizerVulkan::MakeSoftwareVertexLayout()")
    (stubs / "precompile_software_layout.inc").write_text(layout)
    flags = ["-O1", "-g", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"] if args.sanitize else ["-O2"]
    command = [os.environ.get("CXX", "c++"), "-std=c++20", *flags, "-Wall", "-Wextra",
        f"-I{stubs}", f"-I{root / 'src'}", str(root / "tools/uberhar/test_precompile_recipes.cpp"),
        str(root / "src/video_core/shader/generator/pica_fs_config.cpp"), "-o", str(output)]
    subprocess.run(command, check=True)
    environment = dict(os.environ)
    if args.sanitize:
        environment["ASAN_OPTIONS"] = "detect_leaks=0"
    subprocess.run([str(output)], check=True, env=environment)
    if args.negative_controls:
        # CodexAstraUlt-2: Mutate the included production codec only. A compiler failure or
        # unrelated process crash is not a successful negative-control observation.
        codec = (root / "src/video_core/renderer_vulkan/uberhar_precompile_recipes.h").read_text()
        override = stubs / "video_core/renderer_vulkan/uberhar_precompile_recipes.h"
        variants = [
            ("identity", "title != identity.title ||", "false ||", "FAIL: wrong title"),
            ("checksum", "checksum != Detail::Checksum(bytes)", "false", "FAIL: single-byte corruption accepted"),
            ("validation", "!read.good || !recipe.uses || !IsEligible(recipe.config, recipe.pipeline)",
             "!read.good || !recipe.uses", "FAIL: checksummed invalid state decoded"),
        ]
        try:
            for name, old, new, expected in variants:
                if codec.count(old) != 1:
                    raise RuntimeError("negative-control extraction drift: " + name)
                override.write_text(codec.replace(old, new))
                binary = output.with_name(output.name + "-mutant-" + name)
                subprocess.run(command[:-1] + [str(binary)], check=True)
                result = subprocess.run([str(binary)], env=environment, capture_output=True, text=True)
                if result.returncode != 1 or expected not in result.stderr:
                    raise RuntimeError("negative control did not detect " + name + ": " + result.stderr)
                print("PASS: production mutant detected: " + name)
            # CodexAstraUlt-2: Force all deduplication buckets to collide while retaining
            # the unchanged exact equality path; every positive case must still pass.
            start = codec.index("inline u64 Hash(")
            end = codec.index("inline bool Equals(", start)
            colliding = codec[:start] + codec[start:end].replace("return hash.value;", "return 0;") + codec[end:]
            override.write_text(colliding)
            binary = output.with_name(output.name + "-collisions")
            subprocess.run(command[:-1] + [str(binary)], check=True)
            subprocess.run([str(binary)], env=environment, check=True)
            print("PASS: forced hash collisions preserve exact deduplication")
        finally:
            override.unlink(missing_ok=True)



if __name__ == "__main__":
    main()
