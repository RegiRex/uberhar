// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include "common/arch.h" // CodexAstraLocal: Keep each existing generated-call pointer type exact.
#include "common/common_types.h"

namespace Pica {

struct ShaderSetup;
struct ShaderUnit;
struct Uniforms;
namespace Shader { class JitEngine; }

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

// CodexAstraLocal: A deferred job retains the exact compiled source and executable
// allocation independently of ShaderSetup and JitEngine lifetime. Bind explicitly
// borrows the packet's uniforms; its context and read-only source views remain
// valid only while this lease (or a copy) and those uniforms remain alive. Acquire
// on the engine owner thread after SetupBatch; published leases may run concurrently.
class ShaderRunLease {
public:
    ShaderRunLease() = default;
    explicit operator bool() const { return owner && program; }
    std::span<const u32> Program() const { return owner ? code : std::span<const u32>{}; }
    std::span<const u32> Swizzles() const { return owner ? swizzles : std::span<const u32>{}; }
    const void* OwnerIdentity() const { return owner.get(); }
    u32 EntryPoint() const { return entry_point; }
    ShaderRunContext Bind(const Uniforms& uniforms) const {
        return *this ? ShaderRunContext{program, &uniforms, entry} : ShaderRunContext{};
    }

private:
    friend class Shader::JitEngine;
    ShaderRunLease(std::shared_ptr<const void> owner, ShaderRunContext context,
                   std::span<const u32> code, std::span<const u32> swizzles, u32 entry_point)
        : owner{std::move(owner)}, program{context.program}, entry{context.entry},
          code{code}, swizzles{swizzles}, entry_point{entry_point} {}
    std::shared_ptr<const void> owner;
    ShaderRunContext::Function* program{};
    ShaderRunContext::EntryAddress entry{};
    std::span<const u32> code;
    std::span<const u32> swizzles;
    u32 entry_point{};
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

    // CodexAstraLocal: Deferred execution requires explicit compiled ownership;
    // unsupported engines retain the synchronous path rather than borrowed code.
    virtual ShaderRunLease LeaseForDraw(const ShaderSetup&) const { return {}; }

    // CodexAstraLocal: Only an audited emitter may opt into selected-output
    // independence. Callers still prove the program and exclude GS/observers,
    // preserve FP controls and establish guest-backend status isolation.
    virtual bool SupportsObservableVertexContract() const { return false; }
};

std::unique_ptr<ShaderEngine> CreateEngine(bool use_jit);

} // namespace Pica
