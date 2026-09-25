// Copyright 2016 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

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

private:
    std::unordered_map<u64, std::unique_ptr<JitShader>> cache;
    // AstraEH: Count only actual compilation; cached SetupBatch calls do not read clocks.
    bool report_virtual{};
    u64 compiled{}, compile_ns{}, compile_max_ns{};
};

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
