// Copyright 2016-2026 Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
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

    const u64 cache_key = Common::HashCombine(code_hash, swizzle_hash);
    auto iter = cache.find(cache_key);
    if (iter != cache.end()) {
        setup.cached_shader = iter->second.get();
    } else {
        // AstraEH: Compile once per program/swizzle pair, with a bounded first-8 detail budget.
        const auto start = report_virtual ? std::chrono::steady_clock::now()
                                          : std::chrono::steady_clock::time_point{};
        auto shader = std::make_unique<JitShader>();
        shader->Compile(&setup.GetProgramCode(), &setup.GetSwizzleData());
        setup.cached_shader = shader.get();
        cache.emplace_hint(iter, cache_key, std::move(shader));
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
                         "Uberhar CPU JIT build: ordinal={} key={:016X} compile_ms={:.3f}",
                         compiled, cache_key, ns / 1e6);
            }
        }
    }
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
