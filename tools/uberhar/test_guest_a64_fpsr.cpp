// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Synthetic A32 guest code on the actual ARM64 Dynarmic backend.
// This fixture isolates host incidental FPSR from guest status; it is not timing.
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "testenv.h"

static constexpr u32 Status = 0x0800009f;
static unsigned checks = 0;
static void Require(bool ok, const char* what) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL %s check=%u\n", what, checks); std::exit(2); }
}
static void SetHostStatus(u32 value) {
    asm volatile("msr fpsr, %0" : : "r"(u64{value}) : "memory");
}
static u64 HostControls() {
    u64 value; asm volatile("mrs %0, fpcr" : "=r"(value)); return value;
}

// CodexAstraLocal: Every tested callback may leave arbitrary host exception bits,
// while the ending SVC only requests a clean guest block exit.
struct Env final : ArmTestEnv {
    Dynarmic::A32::Jit* jit = nullptr;
    unsigned callbacks = 0, stops = 0;
    u32 poison = 0, width = 32, read_value = 0x67452301;
    u64 stored = 0;
    void CallSVC(u32 swi) override {
        if (swi == 0x99) { ++stops; jit->HaltExecution(); return; }
        Require(swi == 0x1ee, "SVC identifier");
        ++callbacks; SetHostStatus(poison);
    }
    u64 Read(u32 vaddr, unsigned bits) {
        Require(vaddr == 0x4000 && width == bits, "memory read route");
        ++callbacks;
        const u64 result = bits == 64 ? 0xabcdef9867452301ULL :
                          u64{read_value} & (bits == 32 ? 0xffffffffULL : (1ULL << bits)-1);
        SetHostStatus(poison); return result;
    }
    void Write(u32 vaddr, u64 value, unsigned bits) {
        Require(vaddr == 0x4000 && width == bits, "memory write route");
        stored = value; ++callbacks; SetHostStatus(poison);
    }
    // CodexAstraLocal: Materialize narrow storage so the callback's actual return
    // carrier is zero extended, as an actual byte/half load. The inherited JIT
    // assumes clean upper return bits; arbitrary narrow-return carriers need a
    // separate ABI check. This fixture isolates the FP-status boundary.
    u8 MemoryRead8(u32 a) override { volatile u8 v = u8(Read(a, 8)); return v; }
    u16 MemoryRead16(u32 a) override { volatile u16 v = u16(Read(a, 16)); return v; }
    u32 MemoryRead32(u32 a) override { return u32(Read(a, 32)); }
    u64 MemoryRead64(u32 a) override { return Read(a, 64); }
    void MemoryWrite8(u32 a, u8 v) override { Write(a, v, 8); }
    void MemoryWrite16(u32 a, u16 v) override { Write(a, v, 16); }
    void MemoryWrite32(u32 a, u32 v) override { Write(a, v, 32); }
    void MemoryWrite64(u32 a, u64 v) override { Write(a, v, 64); }
};

int main() {
    // CodexAstraLocal: LLVM assembled these exact words in guest.S. Fresh guest FP
    // precedes the callback and follows an immediate VMRS. A second Run exercises
    // exit/reentry with changed host flags while preserving accumulated guest state.
    const std::array<u32, 9> access = {0xef0001ee, 0xe5d12000, 0xe5c12000,
        0xe1d120b0, 0xe1c120b0, 0xe5912000, 0xe5812000, 0xe1c120d0, 0xe1c120f0};
    const std::array<u32, 6> status_seeds = {0, 0x08000000, 0x80, 0x08000080, 1, Status};
    const std::array<u32, 6> poisons = {0, Status, 0x08000000, 0x80, 0x1c, 3};
    const auto saved_controls = HostControls();
    unsigned cases = 0, executions = 0;
    for (unsigned mode = 0; mode < 16; ++mode) {
        const u32 controls = ((mode & 3) << 22) | ((mode & 4) << 22) | ((mode & 8) << 22);
        for (unsigned path = 0; path < access.size(); ++path) {
            for (unsigned mapping = 0; mapping < 3; ++mapping) {
                if (path == 0 && mapping == 2) continue;
                Env env;
                env.width = path <= 2 ? 8 : path <= 4 ? 16 : path <= 6 ? 32 : 64;
                env.code_mem = {0xee800a81, access[path], 0xeef16a10,
                    0xeec21a22, 0xee334a23, 0xeef17a10, 0xef000099, 0xeafffffe};
                // VMRS destination r6/r7 leaves doubleword memory r2/r3 intact.
                auto table = std::make_unique<std::array<u8*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>>();
                alignas(4096) std::array<u8, 4096> page{};
                if (mapping == 2) (*table)[4] = page.data();
                Dynarmic::A32::UserConfig config{&env};
                config.define_unpredictable_behaviour = true; // actual emulator policy
                if (mapping != 0) config.page_table = table.get();
                Dynarmic::A32::Jit jit{config}; env.jit = &jit;
                for (u32 seed : status_seeds) for (u32 poison : poisons) {
                    ++cases; jit.Reset(); jit.SetCpsr(0x1d0); jit.SetFpscr(controls | seed);
                    for (unsigned round = 0; round < 2; ++round) {
                        ++executions;
                        const u32 prior = seed | (round ? 0x13 : 0);
                        jit.ClearHalt(); jit.Regs()[15] = 0; jit.Regs()[1] = 0x4000;
                        jit.Regs()[2] = 0x67452301; jit.Regs()[3] = 0xabcdef98;
                        auto& fp = jit.ExtRegs();
                        fp[1] = 0x3f800000; fp[2] = 0; fp[4] = 0; fp[5] = 0;
                        fp[6] = 0x3f800000; fp[7] = 0x33800000;
                        u64 memory = 0xabcdef9867452301ULL;
                        std::memcpy(page.data(), &memory, sizeof(memory));
                        env.callbacks = env.stops = 0; env.stored = 0;
                        env.poison = round ? (poison ^ Status) : poison;
                        env.ticks_left = 64; SetHostStatus(env.poison);
                        jit.Run();
                        const u32 before = controls | prior | 2;
                        const u32 after = controls | prior | 0x13;
                        const u32 expected_sum = (mode & 3) == 1 ? 0x3f800001 : 0x3f800000;
                        std::printf("{\"case\":%u,\"round\":%u,\"mode\":%u,\"path\":%u,\"mapping\":%u,\"seed\":%u,\"poison\":%u,\"first\":%u,\"second\":%u,\"fpscr\":%u,\"sum\":%u,\"callbacks\":%u}\n",
                            cases, round, mode, path, mapping, seed, env.poison,
                            jit.Regs()[6], jit.Regs()[7], jit.Fpscr(), fp[8], env.callbacks);
                        Require(jit.Regs()[6] == before, "immediate post-callback guest FPSCR");
                        Require(jit.Regs()[7] == after, "post-fresh-FP guest FPSCR");
                        Require(jit.Fpscr() == after, "block exit guest FPSCR");
                        Require(fp[8] == expected_sum, "guest rounding controls");
                        Require(fp[0] == 0x7f800000, "prior divide result");
                        Require(fp[3] == 0x7fc00000, "subsequent invalid divide result");
                        Require(env.callbacks == (mapping == 2 ? 0U : 1U), "actual callback population");
                        Require(env.stops == 1, "guest exit reached");
                        Require(HostControls() == saved_controls, "host FPCR restored");
                        if (path != 0) {
                            const u64 mask = env.width == 64 ? ~u64{0} : (u64{1} << env.width)-1;
                            if (path & 1) {
                                const u64 got = env.width == 64 ? u64{jit.Regs()[2]} | (u64{jit.Regs()[3]} << 32) : jit.Regs()[2];
                                if (got != (memory & mask)) std::fprintf(stderr, "data got=%llx expected=%llx r2=%x r3=%x width=%u path=%u mapping=%u\n", (unsigned long long)got, (unsigned long long)(memory & mask), jit.Regs()[2], jit.Regs()[3], env.width, path, mapping);
                                Require(got == (memory & mask), "actual guest loaded data");
                            } else {
                                u64 got = env.stored;
                                if (mapping == 2) std::memcpy(&got, page.data(), sizeof(got));
                                Require((got & mask) == (memory & mask), "actual guest stored data");
                            }
                        }
                    }
                }
            }
        }
    }
    std::fprintf(stderr, "PASS cases=%u executions=%u checks=%u\n", cases, executions, checks);
}
