// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>
#include "common/arch.h" // CodexAstraLocal: Keep each existing generated-call pointer type exact.
#include "common/common_types.h"

namespace Pica {

struct ShaderSetup;
struct ShaderUnit;

// CodexAstraLocal: A borrowed, draw-local call tuple resolves the already compiled
// entry once after SetupBatch. It owns no code or uniforms and must never survive
// this synchronous draw; every setup/program/entry change requires a new tuple.
struct ShaderRunContext {
#if CITRA_ARCH(arm64)
    using EntryAddress = const std::byte*;
#else
    using EntryAddress = const u8*;
#endif
    using Function = void(const void*, void*, EntryAddress);
    Function* program{};
    const void* uniforms{};
    EntryAddress entry{};

    explicit operator bool() const { return program != nullptr; }
    void Run(ShaderUnit& state) const { program(uniforms, &state, entry); }
};

class ShaderEngine {
public:
    virtual ~ShaderEngine() = default;

    // AstraEH: Report the engine actually constructed, including non-JIT host fallback.
    virtual const char* EngineName() const {
        return "cpu_interpreter";
    }

    /**
     * Performs any shader unit setup that only needs to happen once per shader (as opposed to once
     * per vertex, which would happen within the `Run` function).
     */
    virtual void SetupBatch(ShaderSetup& setup, u32 entry_point) = 0;

    /**
     * Runs the currently setup shader.
     *
     * @param setup Shader engine state, must be setup with SetupBatch on each shader change.
     * @param state Shader unit state, must be setup with input data before each shader invocation.
     */
    virtual void Run(const ShaderSetup& setup, ShaderUnit& state) const = 0;

    // CodexAstraLocal: Unsupported/interpreter engines retain their existing Run
    // path. Callers select the prepared or inherited loop once, outside the FIFO.
    virtual ShaderRunContext BindForDraw(const ShaderSetup&) const { return {}; }
};

std::unique_ptr<ShaderEngine> CreateEngine(bool use_jit);

} // namespace Pica
