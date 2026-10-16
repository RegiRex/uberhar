// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring> // CodexAstraLocal: Joined raw copies reuse the existing tile workers.
#include <limits>
#include <span>
#include "common/logging/log.h"
#include "common/uberhar_parallel_work.h"
#include "video_core/rasterizer_cache/surface_params.h"
#include "video_core/rasterizer_cache/texture_codec.h"
#include "video_core/rasterizer_cache/utils.h"

namespace VideoCore {

// CodexAstraLocal: A cache owner borrows pinned guest input and mapped staging only
// until this synchronous join. Workers perform codec arithmetic on disjoint tiles;
// they never map memory, submit GPU commands, invalidate surfaces or publish state.
class ParallelTextureDecoder {
public:
    // CodexAstraLocal: Leave headroom in this pool's allowance. Other emulator
    // pools may overlap; this does not reserve physical cores or cap all threads.
    explicit ParallelTextureDecoder(bool enabled_, unsigned processors =
                                        Common::Uberhar::AvailableProcessors(),
                                    void (*worker_start)() = nullptr)
        : enabled{enabled_}, pool{processors > 2 ? processors - 2 : 1, worker_start},
          last_report{Clock::now()} {}

    unsigned CreatedWorkers() const { return pool.CreatedWorkers(); }

    template <typename CanReadConcurrently>
    void Decode(const SurfaceParams& info, std::span<u8> source, std::span<u8> dest,
                bool convert, CanReadConcurrently&& can_read_concurrently) {
        ++calls;
        input_bytes += source.size();
        const u64 pixels = static_cast<u64>(info.width) * info.height;
        // CodexAstraLocal TEST-ONLY LOG: Sample small uploads and measure all large
        // ones, retaining counts beside wall times. Remove these clocks/counters
        // after the serial/parallel Thor comparison fixes the admission threshold.
        const bool measure = pixels >= MinPixels || calls % 64 == 0;
        const auto start = measure ? Clock::now() : Clock::time_point{};
        Common::Uberhar::ParallelWork::Result work{};
        bool admitted = false;
        // CodexAstraLocal: Query DSP/RPC ownership only after the cheap admission
        // checks, never for disabled, small or otherwise serial uploads.
        if (enabled && Eligible(info, source, dest, convert) && can_read_concurrently()) {
            const auto format = static_cast<std::size_t>(info.pixel_format);
            const auto decode = (convert ? ConvertedTiles : Tiles)[format];
            if (decode) {
                admitted = true;
                const u32 count = static_cast<u32>(pixels / 64);
                // CodexAstraLocal: Keep adaptive history specific to the codec;
                // raw transfer jobs below must not teach a texture decode policy.
                pool.SetWorkIdentity(0x200U + format + (convert ? 0x100U : 0U));
                work = pool.Run(count, Grain, [&](u32 first, u32 last) {
                    decode(info.width, info.height, source, dest, first, last);
                }, measure);
            }
        }
        if (!admitted) {
            // CodexAstraLocal: Small/partial/linear/depth inputs retain the exact
            // existing codec. This is serial CPU work, never a graphics fallback.
            DecodeTexture(info, info.addr, info.end, source, dest, convert);
        }
        if (work.worker_items) {
            ++parallel_calls;
            worker_tiles += work.worker_items;
            owner_tiles += work.owner_items;
            peak_threads = std::max(peak_threads, work.working_threads);
        } else {
            ++serial_calls;
        }
        if (measure) {
            const auto ns = Elapsed(start);
            if (work.worker_items) {
                ++parallel_samples;
                parallel_ns += ns;
                owner_ns += work.owner_process_ns;
                join_ns += work.join_ns;
            } else {
                ++serial_samples;
                serial_ns += ns;
            }
        }
    }

    // CodexAstraLocal: Caller proves complete current nonaliasing guest spans
    // and excludes concurrent writers. Reuse this pool, join before publishing
    // the guest transfer, and leave tiny copies to the ordinary libc path.
    void CopyCurrentBytes(std::span<const u8> source, std::span<u8> dest) {
        const auto start = Clock::now();
        constexpr u32 block = 64 * 1024;
        Common::Uberhar::ParallelWork::Result result{};
        ++copy_calls;
        copy_bytes += source.size();
        if (enabled && source.size() >= 512 * 1024) {
            pool.SetWorkIdentity(0x100);
            const auto count = static_cast<u32>((source.size() + block - 1) / block);
            result = pool.Run(count, 1, [&](u32 first, u32 last) {
                const auto begin = static_cast<std::size_t>(first) * block;
                const auto end = std::min(source.size(), static_cast<std::size_t>(last) * block);
                std::memcpy(dest.data() + begin, source.data() + begin, end - begin);
            }, true);
        } else {
            std::memcpy(dest.data(), source.data(), source.size());
        }
        copy_ns += Elapsed(start);
        copy_worker_blocks += result.worker_items;
        copy_join_ns += result.join_ns;
    }

    void Report(bool final = false) {
        const auto now = Clock::now();
        if (!final && now - last_report < std::chrono::seconds{5}) return;
        last_report = now;
        // CodexAstraLocal TEST-ONLY LOG: Cumulative owner-only counters qualify
        // real decode work and sampled costs; remove after texture placement is
        // qualified on Thor. These durations are wall time, not worker CPU time.
        const auto scheduling = pool.Scheduling();
        LOG_INFO(Render, "Uberhar texture decode: final={} enabled={} calls={} input_bytes={} "
                 "serial={} parallel={} parallel_owner_tiles={} worker_tiles={} peak_threads={} "
                 "serial_samples={} parallel_samples={} serial_ns={} parallel_ns={} "
                 "owner_ns={} join_ns={} budget={} created={} startup_failures={} "
                 "adaptive_helpers={} adaptive_samples={} adaptive_rejected={} "
                 "adaptive_trials={} adaptive_accepted={} adaptive_reverted={}",
                 final, enabled, calls, input_bytes, serial_calls, parallel_calls, owner_tiles,
                 worker_tiles, peak_threads, serial_samples, parallel_samples, serial_ns,
                 parallel_ns, owner_ns, join_ns, pool.Available(), pool.CreatedWorkers(),
                 pool.StartupFailures(), scheduling.desired, scheduling.samples,
                 scheduling.rejected, scheduling.trials, scheduling.accepted, scheduling.reverted);
        // CodexAstraLocal TEST-ONLY LOG: Explain why learning retained or refused
        // evidence; counters remain bounded and are removed after qualification.
        LOG_INFO_DIAGNOSTIC(Render, "Uberhar texture adaptation: history_hits={} history_misses={} "
            "history_evictions={} capacity_resets={} unkeyed={} rejected_invalid={} rejected_cost={} rejected_helpers={}",
            scheduling.history_hits, scheduling.history_misses, scheduling.evictions,
            scheduling.capacity_resets, scheduling.unkeyed, scheduling.rejected_invalid,
            scheduling.rejected_cost, scheduling.rejected_helpers);
        // CodexAstraLocal TEST-ONLY LOG: Compare transfer cost including joins
        // against actual future uploads; remove after CPU placement qualification.
        LOG_INFO_WITH_DELIVERY(Render, final ? Common::Log::Delivery::Reliable :
            Common::Log::Delivery::Diagnostic, "Uberhar CPU raw copy: final={} enabled={} calls={} bytes={} "
                 "wall_ns={} worker_blocks={} join_ns={} block_bytes=65536 no_new_readback=true",
                 final, enabled, copy_calls, copy_bytes, copy_ns, copy_worker_blocks, copy_join_ns);
    }

private:
    using Clock = std::chrono::steady_clock;
    using TileFunction = void (*)(u32, u32, std::span<u8>, std::span<u8>, u32, u32);
    static constexpr u64 MinPixels = 32768;
    static constexpr u32 Grain = 32;

    static u64 Elapsed(Clock::time_point start) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    }

    static bool Eligible(const SurfaceParams& info, std::span<u8> source,
                         std::span<u8> dest, bool convert) {
        const auto format = static_cast<u32>(info.pixel_format);
        if (!info.is_tiled || format >= static_cast<u32>(PixelFormat::D16) ||
            info.width == 0 || info.height == 0 || info.width != info.stride ||
            info.width % 8 || info.height % 8 ||
            info.end < info.addr) return false;
        const u64 pixels = static_cast<u64>(info.width) * info.height;
        if (pixels < MinPixels || pixels / 64 > std::numeric_limits<u32>::max()) return false;
        const u64 input_size = pixels * GetFormatBpp(info.pixel_format) / 8;
        const u64 output_size = pixels * (convert ? 4 : GetFormatBytesPerPixel(info.pixel_format));
        if (input_size != static_cast<u64>(info.end) - info.addr ||
            input_size > source.size() || output_size > dest.size()) return false;
        // CodexAstraLocal: Reject aliases before workers start. Subtract pointers
        // as integers to avoid unrelated-pointer ordering or end-address overflow.
        const auto src = reinterpret_cast<std::uintptr_t>(source.data());
        const auto dst = reinterpret_cast<std::uintptr_t>(dest.data());
        return src <= dst ? dst - src >= source.size() : src - dst >= dest.size();
    }

    template <PixelFormat Format, bool Converted = false>
    static void DecodeTiles(u32 width, u32 height, std::span<u8> source,
                            std::span<u8> dest, u32 first, u32 last) {
        constexpr u32 tile_bytes = GetFormatBpp(Format) * 64 / 8;
        constexpr u32 pixel_bytes = Converted ? 4 : GetFormatBytesPerPixel(Format);
        const u32 columns = width / 8;
        const std::size_t stride = (static_cast<std::size_t>(7) * width + 8) * pixel_bytes;
        for (u32 tile = first; tile < last; ++tile) {
            // CodexAstraLocal: Preserve MortonCopy's full-rectangle bottom-up
            // convention. Each tile writes its own 8x8 pixels despite overlapping
            // span envelopes; ETC/4-bit input bytes are read-only across workers.
            const u32 x = tile % columns * 8;
            const u32 y = tile / columns * 8;
            const std::size_t offset =
                (static_cast<std::size_t>(height - 8 - y) * width + x) * pixel_bytes;
            MortonCopyTile<true, Format, Converted>(width,
                source.subspan(static_cast<std::size_t>(tile) * tile_bytes, tile_bytes),
                dest.subspan(offset, stride));
        }
    }

    // CodexAstraLocal: Match existing codec dispatch exactly. Implicit RGBA
    // formats have no converted-table entry; no unsupported format is invented.
    static constexpr std::array<TileFunction, 14> Tiles{
        DecodeTiles<PixelFormat::RGBA8>, DecodeTiles<PixelFormat::RGB8>,
        DecodeTiles<PixelFormat::RGB5A1>, DecodeTiles<PixelFormat::RGB565>,
        DecodeTiles<PixelFormat::RGBA4>, DecodeTiles<PixelFormat::IA8>,
        DecodeTiles<PixelFormat::RG8>, DecodeTiles<PixelFormat::I8>,
        DecodeTiles<PixelFormat::A8>, DecodeTiles<PixelFormat::IA4>,
        DecodeTiles<PixelFormat::I4>, DecodeTiles<PixelFormat::A4>,
        DecodeTiles<PixelFormat::ETC1>, DecodeTiles<PixelFormat::ETC1A4>};
    static constexpr std::array<TileFunction, 14> ConvertedTiles{
        DecodeTiles<PixelFormat::RGBA8, true>, DecodeTiles<PixelFormat::RGB8, true>,
        DecodeTiles<PixelFormat::RGB5A1, true>, DecodeTiles<PixelFormat::RGB565, true>,
        DecodeTiles<PixelFormat::RGBA4, true>};

    const bool enabled;
    Common::Uberhar::ParallelWork pool;
    Clock::time_point last_report;
    u64 calls{}, input_bytes{}, serial_calls{}, parallel_calls{}, owner_tiles{}, worker_tiles{};
    u64 serial_samples{}, parallel_samples{}, serial_ns{}, parallel_ns{}, owner_ns{}, join_ns{};
    unsigned peak_threads{};
    u64 copy_calls{}, copy_bytes{}, copy_ns{}, copy_worker_blocks{}, copy_join_ns{};
};
} // namespace VideoCore
