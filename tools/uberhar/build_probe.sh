#!/usr/bin/env bash
# AstraEH: Compile the real generator into a small host-side test executable.
# Header-only dependencies avoid building the emulator; errors still fail the probe.
set -euo pipefail
mkdir -p build/uberhar-probe
python3 tools/uberhar/check_android_keys.py
# CodexAstraLocal: Exercise alpha/beta release classification with the real shell
# block and a local gh substitute; this gate performs no repository publication.
python3 tools/uberhar/test_release_publication.py
# AstraPro: A deferred GPU attempt must not erase an unuploaded vertex uniform block.
python3 tools/uberhar/test_uniform_retry.py
# AstraPro: Preserve current/old/older logs under restarts and filesystem faults.
c++ -std=c++20 -O2 -Isrc tools/uberhar/test_log_retention.cpp \
  -o build/uberhar-probe/test-log-retention
build/uberhar-probe/test-log-retention
# AstraEH: Exercise the actual file writer, including a terminated helper and full storage.
python3 tools/uberhar/test_session_backend.py
# AstraEH: The existing log worker owns idle flushing; no Android timer is required.
c++ -std=c++20 -O2 -pthread -Isrc tools/uberhar/test_log_queue.cpp \
  -o build/uberhar-probe/test-log-queue
build/uberhar-probe/test-log-queue

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
# CodexAstraUlt-2: Failed compiler jobs and canceled GPU submissions must wake every owner.
c++ -std=c++20 -O2 -pthread -Isrc tools/uberhar/test_shader_failure.cpp \
  -o build/uberhar-probe/test-shader-failure
timeout 30s build/uberhar-probe/test-shader-failure
# CodexAstraLocal: Bound diagnostic capture windows/copies and distinguish actual
# recorded/accepted/completed classification with supplied host sequences.
c++ -std=c++20 -O2 -pthread -Isrc tools/uberhar/test_vertex_capture_policy.cpp \
  -o build/uberhar-probe/test-vertex-capture-policy
timeout 30s build/uberhar-probe/test-vertex-capture-policy
# CodexAstraLocal: Compile the real finite capture owner against modeled IO and
# independently read its artifacts; replay synthetic input through production CPU.
python3 tools/uberhar/test_vertex_capture.py
python3 tools/uberhar/test_vertex_capture_replay.py
# CodexAstraUlt: Execute the real queued compiler jobs with frozen optional/required inputs.
python3 tools/uberhar/test_shader_compile_policy.py
# CodexAstraLocal: Execute actual optional-worker completion/failure controls and
# extracted CPU-fragment selection/binding, including deliberately broken routes.
# GPU image/transport execution remains a separate mandatory shader-workflow gate.
python3 tools/uberhar/test_optional_fragment_worker.py
python3 tools/uberhar/test_ready_cpu_fragments.py --mutants
# CodexAstraLocal: Real worker release, completed GPU use and fixed ownership
# must all permit reuse; intended broken lifetime/admission policies fail closed.
python3 tools/uberhar/test_adaptive_cpu_cache.py --mutants
# CodexAstraLocal: Actual compute admission/report hooks must preserve exact
# interval counts, final flushes and bounded diagnostics without changing draws.
python3 tools/uberhar/test_compute_census.py
# CodexAstraLocal: Strict isolation must also reject unavailable backends through
# the existing terminal frontend path instead of silently selecting graphics.
python3 tools/uberhar/test_strict_compute_backend.py
# CodexAstraLocal: Prove new masked/endpoint admission with original CPU vertices
# and immutable command capture; shader CI separately executes the pixel fixtures.
python3 tools/uberhar/test_compute_rect_pixels.py
# CodexAstraLocal: The default-off scratch owner must consume requests once,
# preserve completion/resource horizons and reject intentional lifecycle defects.
# Its exact emitted original-input pixels run in the separate mandatory shader step.
python3 tools/uberhar/test_compute_benchmark.py --mutants
# CodexAstraUlt-2: Terminal readback errors must leave noexcept cleanup safely.
python3 tools/uberhar/test_download_recovery.py
# CodexAstraUlt: Verify real resource reuse/failed-allocation cleanup and event-only memory counters.
python3 tools/uberhar/test_resource_pool_reuse.py
python3 tools/uberhar/test_stream_buffer_ownership.py
c++ -std=c++20 -O2 -pthread -Isrc tools/uberhar/test_memory_diagnostics.cpp \
  -o build/uberhar-probe/test-memory-diagnostics
timeout 30s build/uberhar-probe/test-memory-diagnostics
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
# CodexAstraUlt: Execute production route control with the generated setting keys.
python3 tools/uberhar/test_combo_generic_route.py
# CodexAstraUlt: Reproduce triangle quaternion parity using the real CPU submission
# and generated vertex program; the shader job also executes the emitted GPU cases.
python3 tools/uberhar/test_gpu_quaternion_parity.py
# CodexAstraUlt: The wider Vulkan fetch must retain CPU defaults before guest code.
python3 tools/uberhar/test_gpu_attribute_padding.py
# CodexAstraLocal: Never-written consumed output W must retain the complete CPU
# batch before GPU reads/uploads; also exercise real output lifetime and mapping.
python3 tools/uberhar/test_gpu_output_guard.py
# CodexAstraLocal: Exercise actual JIT and production-generated ordered dots;
# shader CI separately verifies mixed depth coverage and both SPIR-V policies.
python3 tools/uberhar/test_gpu_dot_depth.py
# CodexAstraLocal: Execute real draw-local dispatch, FIFO/state carry and the
# extracted diagnostic call sites with profiler disabled and enabled.
python3 tools/uberhar/test_shader_draw_context.py
# CodexAstraLocal: Joined workers preserve controls/lifetimes; full ordered batches
# and actual interpreter/JIT differential mutants gate parallel shader admission.
c++ -std=c++20 -O2 -pthread -frounding-math -Isrc tools/uberhar/test_parallel_work.cpp \
  -o build/uberhar-probe/test-parallel-work
timeout 45s build/uberhar-probe/test-parallel-work
c++ -std=c++20 -O2 -pthread -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include \
  -Iexternals/boost tools/uberhar/test_parallel_batch.cpp \
  src/video_core/pica/primitive_assembly.cpp -o build/uberhar-probe/test-parallel-batch
timeout 60s build/uberhar-probe/test-parallel-batch
python3 tools/uberhar/test_parallel_vertex.py --mutants
# CodexAstraLocal: Exact source identity must survive uploads, cache collisions,
# descriptor/entry/uniform/output changes and serialized guest-state restoration.
python3 tools/uberhar/test_parallel_cache.py --mutants
# CodexAstraLocal: Execute deliberately colliding source keys through the real
# JIT, including live-setup reuse, before trusting a compiled independence proof.
python3 tools/uberhar/test_shader_jit_identity.py --mutants
# CodexAstraLocal: Actual DSP/RPC ownership and pending async-job lifetimes must
# refuse parallel guest-memory reads; configured settings alone cannot admit them.
python3 tools/uberhar/test_parallel_memory.py --mutants

# AstraEH: Exact CPU cache/stack behavior and safe generic-module reuse gate the next build.
c++ -std=c++20 -O2 -Isrc -Iexternals/boost tools/uberhar/test_vertex_runtime.cpp \
  -o build/uberhar-probe/test-vertex-runtime
build/uberhar-probe/test-vertex-runtime
# AstraEH: Prepared native transport and final-vertex reuse must be bitwise identical
# to the inherited output path and retain FIFO, winding and cross-draw assembly.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include -Iexternals/boost \
  tools/uberhar/test_vertex_output.cpp src/video_core/pica/output_vertex.cpp \
  src/video_core/pica/shader_unit.cpp src/video_core/pica/primitive_assembly.cpp \
  -o build/uberhar-probe/test-vertex-output
build/uberhar-probe/test-vertex-output
# CodexAstraLocal: Finite CPU observation must preserve the complete FIFO/carry
# and assembler path under clock/phase failures; strict reports cannot invent time.
python3 tools/uberhar/test_vertex_timing.py
python3 tools/uberhar/test_vertex_timing_reader.py
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

# AstraEH: Phase evidence never hides a gameplay stall or treats streaming as confirmed loading.
c++ -std=c++20 -O2 -pthread -Isrc tools/uberhar/test_activity.cpp \
  -o build/uberhar-probe/test-activity
build/uberhar-probe/test-activity

# AstraEH: Startup cache evidence must distinguish empty files from unverified reuse.
c++ -std=c++20 -O2 -Isrc tools/uberhar/test_cache_diagnostics.cpp \
  -o build/uberhar-probe/test-cache-diagnostics
build/uberhar-probe/test-cache-diagnostics

# AstraEH: Exact preparation/census reuse must survive collision, eviction and title/profile changes.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Iexternals/fmt/include -Iexternals/boost -Iexternals/xxHash \
  -Iexternals/nihstro/include -Iexternals/vulkan-headers/include \
  tools/uberhar/test_tev_preparation.cpp \
  src/video_core/shader/generator/glsl_fs_shader_gen.cpp \
  src/video_core/shader/generator/pica_fs_config.cpp \
  -o build/uberhar-probe/test-tev-preparation
build/uberhar-probe/test-tev-preparation

# AstraEH: Suppress uploads only when ordered command state retains every required ABI byte.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include \
  -Iexternals/boost -Iexternals/xxHash -Iexternals/nihstro/include \
  tools/uberhar/test_push_constants.cpp -o build/uberhar-probe/test-push-constants
build/uberhar-probe/test-push-constants

# AstraEH: Fused input must preserve exact scalar conversion/register semantics and safe fallback.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include -Iexternals/boost \
  tools/uberhar/test_vertex_input.cpp src/video_core/pica/shader_unit.cpp \
  -o build/uberhar-probe/test-vertex-input
build/uberhar-probe/test-vertex-input
# CodexAstraLocal: Complete recipes must preserve real legacy input decoding,
# interpreter/JIT carry, FIFO/output/assembly and exact once-per-draw usage counts.
# This synthetic gate requires both semantic and actual-source failing controls.
python3 tools/uberhar/test_vertex_input_recipes.py --mutants

# AstraPro: Optional GPU eligibility, real winding/assembly and framebuffer ownership must be exact.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Iexternals/fmt/include -Iexternals/boost -Iexternals/xxHash \
  -Iexternals/vulkan-headers/include tools/uberhar/test_ready_gpu_vertices.cpp \
  src/video_core/pica/primitive_assembly.cpp -o build/uberhar-probe/test-ready-gpu-vertices
build/uberhar-probe/test-ready-gpu-vertices

# AstraPro: Optional specialization must be bounded and cannot alias configs by hash.
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -DXXH_INLINE_ALL \
  -Isrc -Iexternals/fmt/include -Iexternals/boost -Iexternals/xxHash \
  -Iexternals/nihstro/include tools/uberhar/test_fragment_promotion.cpp \
  src/video_core/shader/generator/pica_fs_config.cpp \
  -o build/uberhar-probe/test-fragment-promotion
build/uberhar-probe/test-fragment-promotion

# AstraPro: Bound fixed GPU uploads and reuse only immutable Native mapping plans.
python3 tools/uberhar/test_fixed_attribute_reserve.py
# CodexAstraUlt-2: Compare real Native/GPU input packing and quarantine zero-stride promotion.
python3 tools/uberhar/test_gpu_input_parity.py
# CodexAstraUlt: Exercise production APT responses and bounded repeated-warning delivery.
python3 tools/uberhar/test_applet_utility_logs.py
c++ -std=c++20 -O2 -DFMT_HEADER_ONLY -Isrc -Iexternals/fmt/include -Iexternals/boost \
  tools/uberhar/test_vertex_plan_cache.cpp src/video_core/pica/output_vertex.cpp \
  src/video_core/pica/shader_unit.cpp src/video_core/pica/primitive_assembly.cpp \
  -o build/uberhar-probe/test-vertex-plan-cache
build/uberhar-probe/test-vertex-plan-cache
