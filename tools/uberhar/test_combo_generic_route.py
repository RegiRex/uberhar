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
        'void PipelineCache::UseFragmentShader('))
    # CodexAstraUlt: Supplement execution with integration checks that the same gate
    # reaches binding/preparation and both Combo modes allocate their optional worker.
    for signature in ('bool PipelineCache::BindPipeline(',
                      'GraphicsPipeline* PipelineCache::PrepareReadyGpuVertex('):
        assert 'PreferReadySpecializedFragment(' in extract(source, signature), signature
    assert 'Settings::UsesReadyGpuVertices(Settings::values.uberhar_test_mode.GetValue())' in source
    fixture = r'''
#include <array>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <utility>
#include "common/uberhar_test_profile.h"
#include "video_core/renderer_vulkan/uberhar_fragment_policy.h"
Settings::Values Settings::values;
// CodexAstraUlt: Model shader payload/device boundaries only; routing methods below are extracted.
namespace Pica {
struct RegsInternal { bool supported{true}; };
namespace Shader {
struct UserConfig { bool cacheable{true}; bool IsCacheable() const { return cacheable; } };
struct FSConfig {
    explicit FSConfig(const RegsInternal& regs) : supported{regs.supported} {}
    bool supported{};
    struct { std::array<int,6> tev_stages{}; struct { void Assign(int) {} } combiner_buffer_input; } texture;
};
}}
using Pica::Shader::FSConfig;
namespace Vulkan {
namespace GLSL {
enum class DynamicTevSupport { Ready, Unsupported };
inline DynamicTevSupport CheckDynamicTevSupport(const FSConfig& config, const Pica::Shader::UserConfig&) {
    return config.supported ? DynamicTevSupport::Ready : DynamicTevSupport::Unsupported;
}
inline int MakeDynamicTevState(const FSConfig&, int) { return 128; }
}
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
    bool allow_specialized_fragments{}, force_tev{}, hybrid_tev{true};
    bool ready_vertex_worker{true};
    Disk* curr_disk_cache{};
    int profile{}, tev_constants{};
    bool tev_supported{};
    GLSL::DynamicTevSupport tev_support_reason{};
    std::optional<FSConfig> tev_family_config, virtual_fs_config;
    Pica::Shader::UserConfig tev_user;
    std::array<Shader*,3> current_shaders{};
    std::array<u64,3> shader_hashes{};
    unsigned ready_fragment_preflight_deferred{}, tev_transport_prepared{},
             tev_transport_bypassed_gpu{}, tev_transport_skipped{};
    bool PreferReadySpecializedFragment(const Pica::Shader::UserConfig&) const;
    bool ReadyGpuFragmentPreflight(const Pica::RegsInternal&, const Pica::Shader::UserConfig&);
    void UseFragmentShader(const Pica::RegsInternal&, const Pica::Shader::UserConfig&, bool);
};
'''
    tests = r'''
} // CodexAstraUlt: Vulkan boundary adapter.
void Check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
int main() {
    using namespace Settings;
    using namespace Vulkan;
    // CodexAstraUlt: Execute both ready/missing optional cases for both Combo presets.
    for (auto mode : {UberharTestMode::Automatic, UberharTestMode::ComboGeneric}) {
        for (bool ready : {false,true}) {
            values.uberhar_test_mode = mode;
            ApplyUberharTestProfile();
            Disk disk; disk.ready = ready;
            PipelineCache cache;
            cache.curr_disk_cache = &disk;
            cache.ready_vertex_worker = UsesReadyGpuVertices(mode);
            cache.allow_specialized_fragments = AllowsSpecializedFragments(mode);
            cache.force_tev = values.uberhar_force_tev.GetValue();
            const bool specialized = mode == UberharTestMode::Automatic;
            Pica::RegsInternal regs;
            Pica::Shader::UserConfig user;
            const bool admitted = cache.ReadyGpuFragmentPreflight(regs,user);
            Check(admitted == (!specialized || ready), "optional preflight selection changed");
            Check(disk.optional_calls == unsigned(specialized), "generic mode warmed optional FS");
            cache.UseFragmentShader(regs,user,true);
            Check(cache.virtual_fs_config && !cache.current_shaders[FS],
                  "covered setup lost generic recovery snapshot");
            Check(cache.tev_transport_prepared == unsigned(!specialized),
                  "generic GPU route lacks complete dynamic transport");
            // CodexAstraUlt: A failed/pending GPU attempt re-enters this actual CPU setup;
            // it must retain the generic snapshot, rebuild transport and not enqueue specialization.
            const auto optional_before = disk.optional_calls;
            cache.UseFragmentShader(regs,user,false);
            Check(cache.virtual_fs_config && cache.tev_constants == 128 &&
                  cache.tev_transport_prepared == unsigned(!specialized)+1 &&
                  disk.optional_calls == optional_before && disk.mandatory_calls == 0,
                  "CPU retry no longer prepares full generic recovery");
            regs.supported = false;
            Check(cache.ReadyGpuFragmentPreflight(regs,user), "unsupported state blocked recovery");
            cache.UseFragmentShader(regs,user,false);
            Check(!cache.virtual_fs_config && disk.mandatory_calls == 1 &&
                  cache.current_shaders[FS] == &disk.shader && cache.shader_hashes[FS] == 9,
                  "mandatory unsupported-state specialization disabled");
            // CodexAstraUlt: Non-cacheable user state never enters optional fragment work.
            user.cacheable = false;
            Check(!cache.PreferReadySpecializedFragment(user), "uncacheable optional shader selected");
        }
    }
    // CodexAstraUlt: The explicit mode capability must protect mode 4 even if force state changes.
    PipelineCache guarded;
    guarded.allow_specialized_fragments = AllowsSpecializedFragments(UberharTestMode::ComboGeneric);
    guarded.force_tev = false;
    Check(!guarded.PreferReadySpecializedFragment({}), "mode capability bypassed by force flag");
    std::puts("PASS: extracted Combo fragment gates, generic CPU retry state and mandatory recovery");
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
