// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
#include "common/uberhar_activity.h" // AstraEH: Context only on real CPU compile misses.
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <chrono>
#include "common/assert.h"
#include "common/hash.h"
#include "common/microprofile.h"
#include "common/settings.h" // AstraEH: Scope experimental compilation diagnostics.
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_jit.h"
#if CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit_a64_compiler.h"
#endif
#if CITRA_ARCH(x86_64)
#include "video_core/shader/shader_jit_x64_compiler.h"
#endif

namespace Pica::Shader {

// CodexAstraLocal: Source snapshots live with their generated program, including
// all uploaded and padded words the backend compiles. Code identity is therefore
// exact even when prefix-size metadata or the inherited combined hash collides.
struct JitEngine::CacheEntry {
    CacheEntry(const ProgramCode& source, const SwizzleData& descriptors)
        : code{source}, swizzles{descriptors}, shader{std::make_unique<JitShader>()} {}
    ProgramCode code;
    SwizzleData swizzles;
    std::unique_ptr<JitShader> shader;
};

// AstraEH: Scope compilation timing to the experimental profiles; cache hits stay untimed.
JitEngine::JitEngine()
    : report_virtual{Settings::values.uberhar_test_mode.GetValue() !=
                     Settings::UberharTestMode::Custom} {}
JitEngine::~JitEngine() {
    if (report_virtual) {
        // AstraEH Log Line: One total separates CPU JIT compilation from vertex execution.
        LOG_INFO(Render_Vulkan,
                 "Uberhar CPU JIT totals: programs={} compile_ms={:.3f} max_compile_ms={:.3f}",
                 compiled, compile_ns / 1e6, compile_max_ns / 1e6);
    }
}

void JitEngine::SetupBatch(ShaderSetup& setup, u32 entry_point) {
    ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH);
    setup.entry_point = entry_point;

    setup.DoProgramCodeFixup();
    const u64 code_hash = setup.GetProgramCodeHash();
    const u64 swizzle_hash = setup.GetSwizzleDataHash();

    // CodexAstraLocal: Hash consumption above makes the next source mutation
    // advance the setup revision. The cached pointer also excludes a newly
    // constructed setup that later reuses an old setup's address/revision.
    const u64 revision = setup.GetCodeRevision();
    for (const auto& binding : bindings) {
        if (binding.setup == &setup && binding.revision == revision && binding.entry &&
            setup.cached_shader == binding.entry->shader.get()) {
            return;
        }
    }
    const u64 cache_key = Common::HashCombine(code_hash, swizzle_hash);
    const CacheEntry* selected{};
    const auto [first, last] = cache.equal_range(cache_key);
    for (auto iter = first; iter != last; ++iter) {
        const auto& entry = *iter->second;
        if (entry.code == setup.GetProgramCode() && entry.swizzles == setup.GetSwizzleData()) {
            selected = &entry;
            break;
        }
    }
    if (!selected) {
        // AstraEH: Compile once per program/swizzle pair, with a bounded first-8 detail budget.
        const auto start = report_virtual ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
        const auto activity_start = Common::UberharActivity::Capture();
        // CodexAstraLocal: Compile from the exact owner's snapshots and publish
        // the setup pointer only after insertion succeeds; allocation failure
        // cannot leave it pointing at a destroyed partial cache entry.
        auto entry = std::make_unique<CacheEntry>(setup.GetProgramCode(), setup.GetSwizzleData());
        entry->shader->Compile(&entry->code, &entry->swizzles);
        selected = entry.get();
        cache.emplace(cache_key, std::move(entry));
        if (report_virtual) {
            const u64 ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                               std::chrono::steady_clock::now() - start)
                               .count();
            ++compiled;
            compile_ns += ns;
            compile_max_ns = std::max(compile_max_ns, ns);
            if (compiled <= 8) {
                // AstraEH Log Line: First encounters only; no per-vertex diagnostic output.
                LOG_INFO(Render_Vulkan,
                         "Uberhar CPU JIT build: ordinal={} key={:016X} compile_ms={:.3f} phase={} "
                         "session={}",
                         compiled, cache_key, ns / 1e6,
                         Common::UberharActivity::Name(Common::UberharActivity::Between(
                             activity_start, Common::UberharActivity::Capture())),
                         activity_start.run);
            }
        }
    }
    setup.cached_shader = selected->shader.get();
    bindings[binding_cursor++ % bindings.size()] = {&setup, revision, selected};
}

// CodexAstraLocal: Resolve only the call arguments that the inherited Run would
// look up per miss. Keep profiler-enabled builds on the inherited scoped Run and
// leave null/unprepared state to that existing recovery/assertion contract.
ShaderRunContext JitEngine::BindForDraw(const ShaderSetup& setup) const {
#if MICROPROFILE_ENABLED
    return {};
#else
    if (!setup.cached_shader)
        return {};
    const auto* shader = static_cast<const JitShader*>(setup.cached_shader);
    return shader->BindForDraw(setup, setup.entry_point);
#endif
}

MICROPROFILE_DECLARE(GPU_Shader);

void JitEngine::Run(const ShaderSetup& setup, ShaderUnit& state) const {
    ASSERT(setup.cached_shader != nullptr);

    MICROPROFILE_SCOPE(GPU_Shader);

    const JitShader* shader = static_cast<const JitShader*>(setup.cached_shader);
    shader->Run(setup, state, setup.entry_point);
}

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
