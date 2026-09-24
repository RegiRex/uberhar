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
# AstraEH: Emit complete shaders and production transports for offscreen rendering checks.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Iexternals/fmt/include -Iexternals/boost -Iexternals/xxHash -Iexternals/nihstro/include \
  tools/uberhar/fragment_state_probe.cpp \
  src/video_core/shader/generator/glsl_fs_shader_gen.cpp \
  src/video_core/shader/generator/pica_fs_config.cpp \
  -o build/uberhar-probe/fragment-state-probe
build/uberhar-probe/fragment-state-probe build/uberhar-probe/fragment-state
