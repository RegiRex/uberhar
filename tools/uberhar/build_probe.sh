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
