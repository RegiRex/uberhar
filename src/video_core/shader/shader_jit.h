// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <array>
#include <memory>
#include <unordered_map>
#include "common/common_types.h"
#include "video_core/shader/shader.h"

namespace Pica::Shader {

class JitShader;

class JitEngine final : public ShaderEngine {
public:
    JitEngine();
    ~JitEngine() override;

    // AstraEH: Diagnostics must distinguish CPU translation from a GPU interpreter.
    const char* EngineName() const override {
        return "cpu_jit";
    }

    void SetupBatch(ShaderSetup& setup, u32 entry_point) override;
    void Run(const ShaderSetup& setup, ShaderUnit& state) const override;

    // CodexAstraLocal: Bind after unchanged SetupBatch; no generated instructions change.
    ShaderRunContext BindForDraw(const ShaderSetup& setup) const override;

    // CodexAstraLocal: The A64 emitter's unchanged result dependencies and lack
    // of external calls in the certified opcode domain have an executed proof.
    // x64/interpreter semantics retain the previous arithmetic-read contract.
    bool SupportsObservableVertexContract() const override {
#if CITRA_ARCH(arm64)
        return true;
#else
        return false;
#endif
    }

private:
    // CodexAstraLocal: Hashes only select a bucket. Each compiled owner retains
    // exact source bytes, so a colliding key cannot bind different arithmetic
    // than the caller's independence certificate. Two live setup bindings avoid
    // repeating full-array comparisons on unchanged VS/GS draws.
    struct CacheEntry;
    struct Binding {
        const ShaderSetup* setup{};
        u64 revision{};
        const CacheEntry* entry{};
    };
    std::unordered_multimap<u64, std::unique_ptr<CacheEntry>> cache;
    std::array<Binding, 2> bindings{};
    u32 binding_cursor{};
    // AstraEH: Count only actual compilation; cached SetupBatch calls do not read clocks.
    bool report_virtual{};
    u64 compiled{}, compile_ns{}, compile_max_ns{};
};

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
