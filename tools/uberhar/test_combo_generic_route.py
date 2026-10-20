#!/usr/bin/env python3
"""CodexAstraUlt: Execute production optional-fragment gates and fallback preparation.

Real preset/fragment policies and extracted PipelineCache bodies are used. Device,
shader compiler and FSConfig payloads are small boundary adapters, so this proves
routing and mandatory-recovery reachability, not Vulkan execution or image parity.
"""
import argparse
import os
from pathlib import Path
import subprocess


def extract(source, signature):
    # CodexAstraUlt: Existing production method formatting gives an unambiguous outer close.
    start = source.index(signature)
    return source[start:source.index('\n}\n', start) + 3]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=Path('build/uberhar-probe/combo-generic-route'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    source = Path('src/video_core/renderer_vulkan/vk_pipeline_cache.cpp').read_text()
    bodies = '\n'.join(extract(source, signature) for signature in (
        'bool PipelineCache::PreferReadySpecializedFragment(',
        'bool PipelineCache::ReadyGpuFragmentPreflight(',
        'void PipelineCache::UseFragmentShader(',
        # CodexAstraLocal: Exercise the real deferred consumer as well as setup;
        # adding only its pending member would retain obsolete eager assertions.
        'void PipelineCache::EnsureDynamicTevState()'))
    # CodexAstraUlt: Supplement execution with integration checks that the same gate
    # reaches binding/preparation and both Combo modes allocate their optional worker.
    for signature in ('bool PipelineCache::BindPipeline(',
                      'GraphicsPipeline* PipelineCache::PrepareReadyGpuVertex('):
        assert 'PreferReadySpecializedFragment(' in extract(source, signature), signature
    # CodexAstraLocal: Bind the adapter's mode-derived immutable capabilities and
    # optional-worker presence to the real constructor, rather than granting a
    # worker or GPU permission unconditionally to make the fixture compile.
    constructor = ''.join(extract(source, 'PipelineCache::PipelineCache(').split())
    # CodexAstraLocal: Bind Native's saved full-fragment control as well as mode;
    # the single-argument default would silently ignore the OFF comparison.
    native_control = ',Settings::values.uberhar_native_full_fragments.GetValue()'
    for field, policy, prefix, extra_argument in (
            ('allow_specialized_fragments', 'AllowsSpecializedFragments', '', ''),
            ('allow_static_cpu_tev', 'AllowsStaticCpuTev', 'hybrid_tev&&', ''),
            # CodexAstraLocal: Native CPU full fragments must not inherit the
            # different ready-GPU permission tested below.
            ('allow_full_cpu_fragments', 'AllowsFullCpuFragments', 'hybrid_tev&&', native_control),
            ('allow_ready_gpu_vertices', 'UsesReadyGpuVertices', 'hybrid_tev&&', '')):
        assert (f'{field}{{{prefix}Settings::{policy}'
                f'(Settings::values.uberhar_test_mode.GetValue(){extra_argument})}}') in constructor, field
    assert ('if(allow_ready_gpu_vertices||allow_static_cpu_tev)'
            'ready_vertex_worker=std::make_unique<Common::ThreadWorker>') in constructor
    fixture = r'''
#include <array>
#include <algorithm>
#include <chrono>
#include "common/scope_exit.h"
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>
#include "common/uberhar_test_profile.h"
#include "video_core/renderer_vulkan/uberhar_fragment_policy.h"
Settings::Values Settings::values;
// CodexAstraUlt: Model shader payload/device boundaries only; routing methods below are extracted.
namespace Pica {
// CodexAstraLocal: A distinct payload per draw exposes stale retry snapshots.
struct RegsInternal { bool supported{true}; int transport{128}; };
namespace Shader {
struct UserConfig { bool cacheable{true}; bool IsCacheable() const { return cacheable; } };
struct FSConfig {
    explicit FSConfig(const RegsInternal& regs)
        : supported{regs.supported}, transport{regs.transport} {}
    bool supported{};
    int transport{};
    bool UsesSpirvIncompatibleConfig() const { return false; }
    struct { std::array<int,6> tev_stages{}; struct { void Assign(int) {} } combiner_buffer_input; } texture;
};
}}
using Pica::Shader::FSConfig;
namespace Vulkan {
namespace GLSL {
enum class DynamicTevSupport { Ready, Unsupported };
// CodexAstraLocal: Arithmetic is a distinct endpoint here; actual raw-register
// capture/cache/output is covered by runtime and ready-fragment qualifications.
struct FragmentRuntimeValues {};
inline FragmentRuntimeValues CaptureFragmentRuntimeValues(const Pica::RegsInternal&,
                                                          const FSConfig&, int) { return {}; }
inline DynamicTevSupport CheckDynamicTevSupport(const FSConfig& config, const Pica::Shader::UserConfig&) {
    return config.supported ? DynamicTevSupport::Ready : DynamicTevSupport::Unsupported;
}
inline int MakeDynamicTevState(const FSConfig& config, int) { return config.transport; }
}
// CodexAstraLocal: Model only the pure producer endpoint. Exact full-config
// cache equality is covered by the separate production-cache fixture; real
// EnsureDynamicTevState below owns consumption, reset and accounting here.
struct DynamicTevPreparation {
    unsigned calls{};
    int Get(const FSConfig& config, int profile, const GLSL::FragmentRuntimeValues&) {
        ++calls;
        return GLSL::MakeDynamicTevState(config, profile);
    }
};
enum ProgramType { VS, FS, GS };
struct Shader {};
struct Disk {
    Shader shader;
    unsigned optional_calls{}, mandatory_calls{};
    bool ready{};
    std::optional<std::pair<u64,Shader*>> UseReadyFragmentShader(const FSConfig&, const Pica::Shader::UserConfig&) {
        ++optional_calls;
        return ready ? std::make_optional(std::pair<u64,Shader*>{7,&shader}) : std::nullopt;
    }
    std::optional<std::pair<u64,Shader*>> UseFragmentShader(const FSConfig&, const Pica::Shader::UserConfig&) {
        ++mandatory_calls;
        return std::pair<u64,Shader*>{9,&shader};
    }
};
struct PipelineCache {
    bool force_tev{}, hybrid_tev{true};
    // CodexAstraLocal: Model the real constructor's immutable mode capabilities.
    // Native owns a CPU-FS worker, which must not authorize guest GPU vertices.
    const bool allow_specialized_fragments, allow_static_cpu_tev,
               allow_full_cpu_fragments, allow_ready_gpu_vertices;
    bool ready_vertex_worker;
    explicit PipelineCache(Settings::UberharTestMode mode)
        : allow_specialized_fragments{Settings::AllowsSpecializedFragments(mode)},
          allow_static_cpu_tev{hybrid_tev && Settings::AllowsStaticCpuTev(mode)},
          // CodexAstraLocal: Match the checked production initializer, freezing
          // the saved Native control without changing the Combo capabilities.
          allow_full_cpu_fragments{hybrid_tev && Settings::AllowsFullCpuFragments(
              mode, Settings::values.uberhar_native_full_fragments.GetValue())},
          allow_ready_gpu_vertices{hybrid_tev && Settings::UsesReadyGpuVertices(mode)},
          ready_vertex_worker{allow_ready_gpu_vertices || allow_static_cpu_tev} {}
    Disk* curr_disk_cache{};
    struct Profile { bool vk_use_spirv_generator{}; operator int() const { return 0; } } profile;
    int tev_constants{};
    bool feature_diagnostics = true, pending_fragment_cacheable{}, pending_recovery_glsl{};
    GLSL::FragmentRuntimeValues pending_fragment_values;
    std::optional<int> pending_fragment_evidence;
    u64 fragment_prepare_samples{}, fragment_prepare_ns{}, fragment_prepare_max_ns{};
    u64 fragment_runtime_samples{}, fragment_runtime_ns{}, fragment_runtime_max_ns{};
    bool tev_supported{};
    GLSL::DynamicTevSupport tev_support_reason{};
    std::optional<FSConfig> tev_family_config, virtual_fs_config;
    // CodexAstraLocal: Match .46's draw-owned deferred transport obligation.
    std::optional<FSConfig> pending_tev_transport;
    DynamicTevPreparation dynamic_tev_preparation;
    // CodexAstraLocal: UseFragmentShader must clear this borrowed previous-draw
    // pointer before either supported generic setup or mandatory recovery.
    const FSConfig* prepared_cpu_family{};
    Pica::Shader::UserConfig tev_user;
    std::array<Shader*,3> current_shaders{};
    std::array<u64,3> shader_hashes{};
    unsigned ready_fragment_preflight_deferred{}, tev_transport_prepared{},
             tev_transport_bypassed_gpu{}, tev_transport_skipped{};
    // CodexAstraLocal: Record only the optional evidence boundary. The extracted
    // production body still owns sampling cadence, failure isolation and routing;
    // an injected reporter exception must not suppress mandatory recovery.
    struct { u64 attempts{}, failures{}; } fragment_evidence;
    unsigned evidence_calls{};
    bool evidence_throws{}, evidence_preparing_gpu{}, evidence_supported{};
    int evidence_input{}, evidence_config{};
    void SampleFragmentEvidence(const Pica::RegsInternal& regs, const FSConfig& config,
                                bool preparing_gpu) {
        ++evidence_calls;
        pending_fragment_evidence = config.transport;
        evidence_preparing_gpu = preparing_gpu;
        evidence_supported = config.supported;
        evidence_input = regs.transport;
        evidence_config = config.transport;
        if (evidence_throws) throw std::runtime_error("injected optional evidence failure");
    }
    bool PreferReadySpecializedFragment(const Pica::Shader::UserConfig&) const;
    bool ReadyGpuFragmentPreflight(const Pica::RegsInternal&, const Pica::Shader::UserConfig&);
    void UseFragmentShader(const Pica::RegsInternal&, const Pica::Shader::UserConfig&, bool);
    void EnsureDynamicTevState();
};
'''
    tests = r'''
} // CodexAstraUlt: Vulkan boundary adapter.
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
// CodexAstraLocal: Preserve exact assertion diagnostics for finite defect
// controls instead of an unqualified abort or timeout.
int main() try {
    using namespace Settings;
    using namespace Vulkan;
    // CodexAstraLocal: Exercise ready/missing optional cases for the surviving Combo preset.
    // CodexAstraLocal: Native's saved ON/OFF choice must leave each Combo policy
    // and its complete generic/mandatory recovery behavior unchanged.
    // CodexAstraLocal: Preserve generic-GPU consumer coverage through its real
    // force flag, without keeping the retired generic-only selectable model.
    for (bool force_generic : {false, true})
    for (const auto [mode, native_full] : {
             std::pair{UberharTestMode::Automatic, false},
             std::pair{UberharTestMode::Automatic, true}}) {
        for (bool ready : {false,true}) {
            values.uberhar_test_mode = mode;
            values.uberhar_native_full_fragments = native_full;
            ApplyUberharTestProfile();
            Disk disk; disk.ready = ready;
            PipelineCache cache{mode};
            cache.curr_disk_cache = &disk;
            cache.force_tev = values.uberhar_force_tev.GetValue() || force_generic;
            const bool specialized = !force_generic;
            Check(cache.allow_full_cpu_fragments &&
                  cache.allow_static_cpu_tev &&
                  cache.allow_ready_gpu_vertices && cache.ready_vertex_worker,
                  "Native full-fragment control changed Combo capabilities");
            Pica::RegsInternal regs;
            Pica::Shader::UserConfig user;
            const bool admitted = cache.ReadyGpuFragmentPreflight(regs,user);
            Check(admitted == (!specialized || ready), "optional preflight selection changed");
            Check(disk.optional_calls == unsigned(specialized), "generic mode warmed optional FS");
            const FSConfig previous{regs};
            cache.prepared_cpu_family = &previous;
            cache.pending_tev_transport = previous;
            cache.UseFragmentShader(regs,user,true);
            // CodexAstraLocal: Sampling sees this draw's original GPU-attempt
            // inputs; it cannot inherit the seeded previous-draw snapshot.
            Check(cache.fragment_evidence.attempts == 1 && cache.evidence_calls == 1 &&
                  cache.fragment_evidence.failures == 0 && cache.evidence_preparing_gpu &&
                  cache.evidence_supported && cache.evidence_input == regs.transport &&
                  cache.evidence_config == regs.transport,
                  "first fragment evidence lost original input or attempt scope");
            Check(!cache.prepared_cpu_family, "stale CPU family survived new fragment setup");
            Check(cache.virtual_fs_config && !cache.current_shaders[FS],
                  "covered setup lost generic recovery snapshot");
            // CodexAstraLocal: Setup defers work; specialized GPU preparation
            // drops a stale obligation, while generic preparation keeps its own.
            Check(bool(cache.pending_tev_transport) == !specialized &&
                  cache.tev_transport_prepared == 0 && cache.dynamic_tev_preparation.calls == 0,
                  "GPU setup did not preserve deferred transport policy");
            cache.EnsureDynamicTevState();
            Check(!cache.pending_tev_transport &&
                  cache.tev_transport_prepared == unsigned(!specialized) &&
                  cache.dynamic_tev_preparation.calls == unsigned(!specialized) &&
                  cache.tev_constants == (specialized ? 0 : 128),
                  "generic GPU consumer lacks current transport");
            // CodexAstraUlt: A failed/pending GPU attempt re-enters this actual CPU setup;
            // it must retain the generic snapshot without enqueueing specialization.
            // CodexAstraLocal: Change the input before retry and materialize only
            // at the consumer; a repeated consume must perform no further work.
            const auto optional_before = disk.optional_calls;
            regs.transport = 257;
            // CodexAstraLocal: Jump to the exact sampling boundary without
            // repeating 1,022 unchanged draws. Attempt 1,023 must stay unsampled.
            cache.fragment_evidence.attempts = 1023;
            cache.UseFragmentShader(regs,user,false);
            Check(cache.fragment_evidence.attempts == 1024 && cache.evidence_calls == 1 &&
                  cache.fragment_evidence.failures == 0,
                  "fragment evidence sampled before its 1,024-attempt boundary");
            Check(cache.virtual_fs_config && cache.pending_tev_transport &&
                  cache.pending_tev_transport->transport == 257 &&
                  cache.tev_transport_prepared == unsigned(!specialized),
                  "CPU retry lost its deferred current input");
            cache.EnsureDynamicTevState();
            cache.EnsureDynamicTevState();
            Check(!cache.pending_tev_transport && cache.tev_constants == 257 &&
                  cache.tev_transport_prepared == unsigned(!specialized)+1 &&
                  cache.dynamic_tev_preparation.calls == unsigned(!specialized)+1 &&
                  disk.optional_calls == optional_before && disk.mandatory_calls == 0,
                  "CPU retry no longer prepares full generic recovery");
            regs.supported = false;
            Check(cache.ReadyGpuFragmentPreflight(regs,user), "unsupported state blocked recovery");
            cache.prepared_cpu_family = &previous;
            cache.pending_tev_transport = previous;
            // CodexAstraLocal: The next sampled attempt fails only its reporter.
            // Existing assertions below must still reach exact mandatory recovery.
            cache.evidence_throws = true;
            cache.UseFragmentShader(regs,user,false);
            Check(cache.fragment_evidence.attempts == 1025 && cache.evidence_calls == 2 &&
                  cache.fragment_evidence.failures == 1 && !cache.evidence_preparing_gpu &&
                  !cache.evidence_supported && cache.evidence_input == 257 &&
                  cache.evidence_config == 257,
                  "sampled evidence failure lost CPU recovery input or escaped isolation");
            Check(!cache.prepared_cpu_family, "stale CPU family survived unsupported recovery");
            cache.EnsureDynamicTevState();
            Check(!cache.pending_tev_transport && cache.tev_constants == 257 &&
                  cache.dynamic_tev_preparation.calls == unsigned(!specialized)+1,
                  "unsupported recovery consumed previous draw transport");
            Check(!cache.virtual_fs_config && disk.mandatory_calls == 1 &&
                  cache.current_shaders[FS] == &disk.shader && cache.shader_hashes[FS] == 9,
                  "mandatory unsupported-state specialization disabled");
            // CodexAstraUlt: Non-cacheable user state never enters optional fragment work.
            user.cacheable = false;
            Check(!cache.PreferReadySpecializedFragment(user), "uncacheable optional shader selected");
        }
    }
    // CodexAstraUlt: The explicit mode capability must protect mode 4 even if force state changes.
    // CodexAstraLocal: Raw retired mode cannot gain specialization before migration.
    PipelineCache guarded{static_cast<UberharTestMode>(4)};
    guarded.force_tev = false;
    Check(!guarded.PreferReadySpecializedFragment({}), "mode capability bypassed by force flag");
    // CodexAstraLocal: Actual Native mode creates the optional CPU-fragment lane.
    // Neither module readiness, unsupported state nor a later global mode edit
    // may turn that worker's presence into immutable GPU-vertex eligibility.
    // CodexAstraLocal: Both saved Native settings use the real policy helper;
    // mutating the globals later must not change the constructed capabilities.
    for (const auto [native_full, ready] : {
             std::pair{false, false}, std::pair{false, true},
             std::pair{true, false}, std::pair{true, true}}) {
        for (bool supported : {false, true}) {
            values.uberhar_test_mode = UberharTestMode::Native;
            values.uberhar_native_full_fragments = native_full;
            ApplyUberharTestProfile();
            Disk disk; disk.ready = ready;
            PipelineCache native{UberharTestMode::Native};
            native.curr_disk_cache = &disk;
            native.force_tev = values.uberhar_force_tev.GetValue();
            Pica::RegsInternal regs{supported};
            Pica::Shader::UserConfig user;
            Check(native.allow_full_cpu_fragments == native_full,
                  "Native full-fragment ON/OFF control was not captured");
            Check(native.allow_static_cpu_tev && native.ready_vertex_worker &&
                  !native.allow_ready_gpu_vertices,
                  "Native worker/capability setup does not model the production mode");
            Check(!native.ReadyGpuFragmentPreflight(regs, user) && disk.optional_calls == 0,
                  "Native optional worker granted GPU vertex admission");
            values.uberhar_test_mode = UberharTestMode::Automatic;
            values.uberhar_native_full_fragments = !native_full;
            Check(native.allow_full_cpu_fragments == native_full,
                  "immutable Native full-fragment policy followed a later setting");
            Check(!native.ReadyGpuFragmentPreflight(regs, user),
                  "immutable Native GPU policy followed a later global mode");
            values.uberhar_test_mode = UberharTestMode::Native;
            const FSConfig previous{regs};
            native.prepared_cpu_family = &previous;
            native.pending_tev_transport = previous;
            native.UseFragmentShader(regs, user, false);
            Check(!native.prepared_cpu_family, "Native setup retained a stale CPU family");
            // CodexAstraLocal: Native has the same deferred-consumer contract;
            // unsupported recovery must clear the deliberately seeded stale input.
            Check(bool(native.pending_tev_transport) == supported &&
                  native.tev_transport_prepared == 0,
                  "Native setup did not defer only supported transport");
            native.EnsureDynamicTevState();
            native.EnsureDynamicTevState();
            Check(bool(native.virtual_fs_config) == supported &&
                  native.tev_transport_prepared == unsigned(supported) &&
                  native.dynamic_tev_preparation.calls == unsigned(supported) &&
                  !native.pending_tev_transport &&
                  native.tev_constants == (supported ? regs.transport : 0) &&
                  disk.mandatory_calls == unsigned(!supported) && disk.optional_calls == 0,
                  "Native CPU generic/mandatory recovery changed");
        }
    }
    std::puts("PASS: extracted Combo fragment gates, generic CPU retry, mandatory recovery, Native full-fragment ON/OFF and CPU-only admission");
} catch (const std::exception& error) {
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    cpp = args.output.with_suffix('.cpp')
    cpp.write_text(fixture + bodies + tests)
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O1' if args.sanitize else '-O2',
               '-DENABLE_VULKAN', '-DFMT_HEADER_ONLY', '-Isrc', '-Ibuild/uberhar-profile',
               '-Iexternals/fmt/include', '-Iexternals/boost', str(cpp), '-o', str(args.output)]
    if args.sanitize:
        command += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    subprocess.run(command, check=True)
    subprocess.run([str(args.output)], check=True)


if __name__ == '__main__':
    main()
