// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include "common/common_types.h"
#include "common/uberhar_activity.h"

namespace Pica::VertexTiming {

// CodexAstraLocal: A finite opt-in observation never owns guest state, changes
// draw admission, or estimates unsampled work. These bounds include failed rows.
inline constexpr u32 MaxChunks = 64, MaxChunkVertices = 64, MaxVertices = 4096;
inline constexpr u32 MaxDrawInputs = 4096;
inline constexpr std::size_t MaxConfigBytes = 4096, MaxOutputBytes = 65536;
inline constexpr u64 ArmTimeoutNs = 300'000'000'000ULL;
enum class Mode : u32 { Off, Boundary, Detailed };
enum class Stage : u32 { Lookup, Input, Shader, Output, Cache, Submit, Count };
inline constexpr u32 StageCount = static_cast<u32>(Stage::Count);
enum class Stop : u32 { None, Window, Budget, ArmTimeout, Identity, Phase, Clock, Teardown, Policy };

struct Config {
    Mode mode{Mode::Off};
    std::string id;
    u64 title{};
    u32 delay_ms{}, duration_ms{10000}, period_ms{100}, chunk_vertices{32},
        max_chunks{32}, max_vertices{1024}, seed{};
};

// CodexAstraLocal: Injection is for host regression only. The production clock
// reads CLOCK_THREAD_CPUTIME_ID on the executing owner, never another thread.
struct Clock {
    void* context{};
    bool (*wall)(void*, u64&) noexcept{};
    bool (*cpu)(void*, u64&) noexcept{};
    u64 (*thread_id)(void*) noexcept{};
    u64 wall_resolution_ns{}, cpu_resolution_ns{};
};
Clock HostClock() noexcept;
std::optional<Config> ParseConfig(std::string_view bytes, u64 title) noexcept;

struct Draw {
    u64 program{}, swizzle{};
    u32 entry{}, count{}, inputs{}, topology{};
    bool indexed{}, fused{};
};
struct Range { u32 first{}, count{}; };
struct Point { u64 wall_before{}, cpu{}, wall_after{}; };
struct Record {
    Draw draw;
    u64 draw_ordinal{}, start_offset_ns{}, token_before{}, token_after{};
    Range range;
    Point begin, end;
    std::array<u64, StageCount> stage_ns{}, stage_calls{};
    u32 observed{}, hits{}, misses{}, fused_misses{}, legacy_misses{};
    bool begun{}, completed{}, clocks_valid{}, phase_stable{}, overhang{};
};
struct Calibration {
    u32 pairs{}, wall_pairs{};
    u64 cpu_ns{}, outer_wall_ns{}, wall_pair_ns{}, maximum_wall_pair_ns{};
};
struct Summary {
    u64 cpu_batches{}, eligible_batches{}, waiting_batches{}, in_window_batches{},
        period_filtered{}, empty_batches{}, oversized_batches{}, selected_draw_inputs{},
        unsupported_batches{}, budget_dropped{},
        clock_failures{}, identity_failures{}, phase_failures{}, sparse_suppressed{},
        wall_reads{}, cpu_reads{}, armed_at_ns{}, window_begin_ns{}, window_end_ns{},
        last_poll_ns{}, run{}, owner_tid{};
    std::array<u64, Common::UberharActivity::PhaseCount> phases{};
    u32 reserved_vertices{}, records{};
    Stop stop{Stop::None};
};

// CodexAstraLocal: One PICA-owned collector is loaded once and exported once at
// normal teardown. Terminal states stop all diagnostic clocks, not rendering.
class Session {
public:
    static std::unique_ptr<Session> Load(u64 title, const char* engine) noexcept;
    Session(Config config, Clock clock, std::string engine);
    ~Session();
    bool Polling() const noexcept { return summary.stop == Stop::None; }
    void Poll(u64 now_ns, Common::UberharActivity::Snapshot activity) noexcept;
    void Unsupported() noexcept;
    std::optional<Range> Select(const Draw& draw) noexcept;
    bool Begin(Common::UberharActivity::Snapshot activity) noexcept;
    void Mark(Stage stage) noexcept;
    void Input(bool hit) noexcept;
    void InputRoute(bool fused) noexcept;
    void SuppressSparse(bool would_sample) noexcept { summary.sparse_suppressed += would_sample; }
    void End(Common::UberharActivity::Snapshot activity) noexcept;
    void Close() noexcept;
    void PolicyChanged() noexcept { Fail(Stop::Policy); }
    bool Detailed() const noexcept { return config.mode == Mode::Detailed; }
    const Summary& GetSummary() const noexcept { return summary; }
    const Calibration& GetCalibration() const noexcept { return calibration; }
    const auto& Records() const noexcept { return records; }
    // CodexAstraLocal: A fixed caller buffer bounds both ordinary serialization
    // and regression fixtures. No unbounded JSON dump or background writer.
    std::optional<std::size_t> Serialize(std::span<char> output) const noexcept;

private:
    bool Wall(u64& value) noexcept;
    bool Cpu(u64& value) noexcept;
    bool ReadPoint(Point& point) noexcept;
    bool Owner() noexcept;
    bool Calibrate() noexcept;
    void Fail(Stop reason) noexcept;
    void Export() noexcept;
    Config config;
    Clock clock;
    std::string engine, path;
    Summary summary;
    Calibration calibration;
    std::array<Record, MaxChunks> records{};
    std::thread::id owner;
    u64 first_poll{}, previous_token{}, next_chunk{}, last_mark{};
    u32 current{MaxChunks};
    bool initialized{}, armed{}, calibrated{}, exported{}, measuring{};
};

} // namespace Pica::VertexTiming
