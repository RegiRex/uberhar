#!/usr/bin/env bash
set -euo pipefail
mkdir -p build/uberhar-probe
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Isrc/common -Iexternals/fmt/include -Iexternals/boost \
  -Iexternals/xxHash -Iexternals/nihstro/include \
  tools/uberhar/shader_probe.cpp \
  src/video_core/shader/generator/glsl_fs_shader_gen.cpp \
  src/video_core/shader/generator/pica_fs_config.cpp \
  -o build/uberhar-probe/shader-probe
build/uberhar-probe/shader-probe build/uberhar-probe/cases
