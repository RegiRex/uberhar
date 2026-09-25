#!/usr/bin/env bash
# AstraEH: Compile the real generator into a small host-side test executable.
# Header-only dependencies avoid building the emulator; errors still fail the probe.
set -euo pipefail
mkdir -p build/uberhar-probe
python3 tools/uberhar/check_android_keys.py
# AstraEH: Catch incorrect family merges before exercising the numerical shader corpus.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Isrc/common -Iexternals/fmt/include -Iexternals/boost \
  -Iexternals/xxHash -Iexternals/nihstro/include \
  tools/uberhar/test_tev_family.cpp \
  src/video_core/shader/generator/glsl_fs_shader_gen.cpp \
  src/video_core/shader/generator/pica_fs_config.cpp \
  -o build/uberhar-probe/test-tev-family
build/uberhar-probe/test-tev-family
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Isrc/common -Iexternals/fmt/include -Iexternals/boost \
  -Iexternals/xxHash -Iexternals/nihstro/include \
  tools/uberhar/shader_probe.cpp \
  src/video_core/shader/generator/glsl_fs_shader_gen.cpp \
  src/video_core/shader/generator/pica_fs_config.cpp \
  -o build/uberhar-probe/shader-probe
build/uberhar-probe/shader-probe build/uberhar-probe/cases
# AstraEH: Compile the production runtime-key/policy code with pinned Vulkan headers.
# Do not add -Isrc/common here: it would shadow the C library's assert.h in Vulkan-Hpp.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Iexternals/fmt/include -Iexternals/boost -Iexternals/xxHash \
  -Iexternals/nihstro/include -Iexternals/vulkan-headers/include \
  tools/uberhar/test_pipeline_keys.cpp -o build/uberhar-probe/test-pipeline-keys
build/uberhar-probe/test-pipeline-keys
c++ -std=c++20 -O2 -pthread -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include \
  tools/uberhar/test_pipeline_policy.cpp -o build/uberhar-probe/test-pipeline-policy
timeout 30s build/uberhar-probe/test-pipeline-policy
# AstraEH: Real primitive assembly must match native topology and leave no bridge tail.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include -Iexternals/boost \
  tools/uberhar/test_bridge_assembly.cpp src/video_core/pica/primitive_assembly.cpp \
  -o build/uberhar-probe/test-bridge-assembly
build/uberhar-probe/test-bridge-assembly
c++ -std=c++20 -O2 -pthread -Isrc tools/uberhar/test_wait_diagnostics.cpp \
  -o build/uberhar-probe/test-wait-diagnostics
timeout 30s build/uberhar-probe/test-wait-diagnostics
# AstraEH: Emit complete shaders and production transports for offscreen rendering checks.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Iexternals/fmt/include -Iexternals/boost -Iexternals/xxHash -Iexternals/nihstro/include \
  tools/uberhar/fragment_state_probe.cpp \
  src/video_core/shader/generator/glsl_fs_shader_gen.cpp \
  src/video_core/shader/generator/pica_fs_config.cpp \
  -o build/uberhar-probe/fragment-state-probe
build/uberhar-probe/fragment-state-probe build/uberhar-probe/fragment-state

# AstraEH: Profile/compute admission gates exercise the same code compiled into Android.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include -Iexternals/boost \
  tools/uberhar/test_compute_rect.cpp -o build/uberhar-probe/test-compute-rect
build/uberhar-probe/test-compute-rect
# Generate native setting keys without configuring the full emulator.
mkdir -p build/uberhar-profile-source/common
cp src/common/setting_keys.h.in build/uberhar-profile-source/common/setting_keys.h.in
python3 - <<'INNER'
from pathlib import Path
root = Path.cwd()
Path("build/uberhar-profile-source/CMakeLists.txt").write_text(
    'cmake_minimum_required(VERSION 3.22)\nproject(UberharProfile NONE)\n'
    f'include("{root}/CMakeModules/GenerateSettingKeys.cmake")\n')
INNER
cmake -S build/uberhar-profile-source -B build/uberhar-profile
c++ -std=c++20 -O2 -DENABLE_VULKAN -DFMT_HEADER_ONLY -Isrc -Ibuild/uberhar-profile \
  -Iexternals/fmt/include -Iexternals/boost tools/uberhar/test_graphics_profile.cpp \
  -o build/uberhar-probe/test-graphics-profile
build/uberhar-probe/test-graphics-profile

# AstraEH: Exact CPU cache/stack behavior and safe generic-module reuse gate the next build.
c++ -std=c++20 -O2 -Isrc -Iexternals/boost tools/uberhar/test_vertex_runtime.cpp \
  -o build/uberhar-probe/test-vertex-runtime
build/uberhar-probe/test-vertex-runtime
c++ -std=c++20 -O2 -DXXH_INLINE_ALL -Isrc -Iexternals/xxHash \
  tools/uberhar/test_spirv_cache.cpp -o build/uberhar-probe/test-spirv-cache
build/uberhar-probe/test-spirv-cache

# AstraEH: Check actual interpreter execution, not only its stack container.
c++ -std=c++20 -O2 -DMICROPROFILE_ENABLED=0 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Iexternals/fmt/include -Iexternals/boost -Iexternals/xxHash \
  -Iexternals/nihstro/include -Iexternals/microprofile \
  tools/uberhar/test_vertex_interpreter.cpp src/video_core/shader/shader_interpreter.cpp \
  src/video_core/pica/shader_setup.cpp src/video_core/pica/shader_unit.cpp \
  -o build/uberhar-probe/test-vertex-interpreter
timeout 30s build/uberhar-probe/test-vertex-interpreter

# AstraEH: Run accounting must exclude explicit pauses and survive clock/state discontinuities.
c++ -std=c++20 -O2 -Isrc tools/uberhar/test_frame_diagnostics.cpp \
  -o build/uberhar-probe/test-frame-diagnostics
build/uberhar-probe/test-frame-diagnostics
