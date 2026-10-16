// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Compare the actual parallel upload decoder with the inherited
// codec, including compressed/4-bit formats, rectangular orientation and guards.
#include <iostream>
#include <stdexcept>
#include <vector>
#include "video_core/rasterizer_cache/texture_decode_work.h"
#include "video_core/rasterizer_cache/uberhar_cpu_transfer_policy.h" // CodexAstraLocal: Raw transfer boundaries.

namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level, const char*, unsigned, const char*, fmt::string_view f,
                       const fmt::format_args& a) {
    throw std::runtime_error(fmt::vformat(f, a));
}
} // namespace Common::Log

using namespace VideoCore;
void Need(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void Case(PixelFormat format, bool converted, u32 width, u32 height,
          bool enabled = true, bool immutable = true, unsigned processors = 8,
          bool tiled = true, bool partial = false, bool diagnostics = true) {
    SurfaceParams info{};
    info.width = info.stride = width;
    info.height = height;
    info.pixel_format = format;
    info.is_tiled = tiled;
    info.addr = 0x100000;
    info.end = info.addr + width * height * GetFormatBpp(format) / 8;
    if (partial) info.end -= width * 8 * GetFormatBpp(format) / 8;
    const auto bytes = info.end - info.addr;
    std::vector<u8> source(bytes + 64, 0xD3);
    for (u32 i = 0; i < bytes; ++i) source[i + 32] = (i * 113 + i / 17 + 31) % 256;
    const auto original_source = source;
    const auto output_bytes = width * height * (converted ? 4 : GetFormatBytesPerPixel(format));
    std::vector<u8> expected(output_bytes + 64, 0xA7), actual = expected;
    auto input = std::span{source}.subspan(32, bytes);
    DecodeTexture(info, info.addr, info.end, input,
                  std::span{expected}.subspan(32, output_bytes), converted);
    // CodexAstraLocal: Diagnostic controls must not change codec/ownership results.
    ParallelTextureDecoder decoder{enabled, processors, nullptr, true, diagnostics};
    unsigned ownership_queries{};
    const auto guard = [&] { ++ownership_queries; return immutable; };
    decoder.Decode(info, input, std::span{actual}.subspan(32, output_bytes), converted, guard);
    Need(actual == expected, "parallel/serial bytes or output guard differ");
    Need(source == original_source, "decoder modified guest source or input guards");
    const bool admit = enabled && immutable && tiled && !partial &&
                       width * height >= 32768 && format < PixelFormat::D16 && processors > 3;
    Need((decoder.CreatedWorkers() > 0) == admit, "admission or lazy worker creation wrong");
    if (!enabled || !tiled || partial || width * height < 32768 || format >= PixelFormat::D16)
        Need(ownership_queries == 0, "serial upload queried guest ownership");
    // CodexAstraLocal: Reuse the same pool with new data to expose stale borrowed
    // spans/results, not merely a successful first generation.
    for (u32 i = 0; i < bytes; ++i) source[i + 32] ^= 0x5A;
    DecodeTexture(info, info.addr, info.end, input,
                  std::span{expected}.subspan(32, output_bytes), converted);
    decoder.Decode(info, input, std::span{actual}.subspan(32, output_bytes), converted, guard);
    Need(actual == expected, "reused pool retained old upload data");
}

int main() try {
    unsigned cases{};
    // CodexAstraLocal: New CPU-transfer admission must reject aliases, gap
    // semantics and address overflow. Adjacency remains a valid disjoint copy.
    Need(CpuRawTransferShape(0x100000, 0x104000, 16384, 0, 0), "adjacent copy refused");
    Need(!CpuRawTransferShape(0x100000, 0x103fff, 16384, 0, 0), "overlap accepted");
    Need(!CpuRawTransferShape(0x100000, 0x100000, 16384, 0, 0), "self copy accepted");
    Need(!CpuRawTransferShape(0xfffff000, 0x100000, 16384, 0, 0), "wrapped range accepted");
    Need(!CpuRawTransferShape(0x100000, 0x200000, 16384, 1, 0), "input gap accepted");
    Need(!CpuRawTransferShape(0x100000, 0x200000, 16384, 0, 1), "output gap accepted");
    Need(!CpuRawTransferShape(0x100000, 0x200000, 8192, 0, 0), "small copy admitted");
    // CodexAstraLocal: Exclusive classifications preserve the existing safety boundary.
    Need(ClassifyCpuRawTransfer(0x100000, 0x200000, 8192, 0, 0) == CpuTransferShape::BelowSize,
         "small refusal is not attributed");
    Need(ClassifyCpuRawTransfer(0x100000, 0x200000, 16384, 1, 0) == CpuTransferShape::Gaps,
         "gap refusal is not attributed");
    for (bool enabled : {false, true}) for (bool diagnostics : {false, true}) {
        ParallelTextureDecoder decoder{enabled, 8, nullptr, true, diagnostics};
        for (std::size_t size : {16384U, 524287U, 524288U, 1048592U}) {
            std::vector<u8> input(size + 64, 0xC9), output(size + 64, 0xA7);
            for (unsigned generation = 0; generation < 3; ++generation) {
                for (std::size_t i = 32; i < size + 32; ++i)
                    input[i] = static_cast<u8>(i * 31 + generation * 17);
                const auto before = input;
                decoder.CopyCurrentBytes(std::span<const u8>{input}.subspan(32, size),
                                         std::span{output}.subspan(32, size));
                Need(std::equal(input.begin() + 32, input.end() - 32, output.begin() + 32),
                     "raw copy differs from exact current bytes");
                Need(input == before, "raw copy modified source");
                Need(std::all_of(output.begin(), output.begin() + 32, [](u8 b) { return b == 0xA7; }) &&
                     std::all_of(output.end() - 32, output.end(), [](u8 b) { return b == 0xA7; }),
                     "raw copy damaged guards or partial last block");
                ++cases;
            }
        }
        Need((decoder.CreatedWorkers() != 0) == enabled, "raw copy ignored worker toggle");
    }
    for (u32 format = 0; format < 14; ++format) {
        for (bool converted : {false, true}) {
            if (converted && format >= 5) continue;
            for (auto dimensions : {std::pair{128U, 256U}, std::pair{512U, 64U},
                                    std::pair{8U, 4096U}}) {
                Case(static_cast<PixelFormat>(format), converted, dimensions.first, dimensions.second);
                ++cases;
            }
        }
    }
    // CodexAstraLocal: Large compressed/ordinary and serial diagnostic-OFF controls
    // must retain exact bytes, source/target guards and lazy worker admission.
    Case(PixelFormat::RGBA8, false, 128, 256, true, true, 8, true, false, false); ++cases;
    Case(PixelFormat::ETC1, false, 128, 256, true, true, 8, true, false, false); ++cases;
    Case(PixelFormat::I4, false, 128, 128, true, true, 8, true, false, false); ++cases;
    // CodexAstraLocal: OFF, concurrent-writer refusal, owner-only budgets, small,
    // partial, linear and depth uploads must retain serial output without workers.
    Case(PixelFormat::RGBA8, false, 128, 256, false); ++cases;
    Case(PixelFormat::RGBA8, false, 128, 256, true, false); ++cases;
    for (unsigned processors : {0, 1, 2, 3, 4}) {
        Case(PixelFormat::RGBA8, false, 128, 256, true, true, processors); ++cases;
    }
    Case(PixelFormat::I4, false, 128, 128); ++cases;
    Case(PixelFormat::RGB8, false, 128, 256, true, true, 8, true, true); ++cases;
    Case(PixelFormat::RGBA8, false, 128, 256, true, true, 8, false); ++cases;
    Case(PixelFormat::D16, false, 128, 256); ++cases;
    std::cout << "PASS parallel texture cases=" << cases
              << " exact bytes, guards, reuse, lazy admission\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n'; return 1;
}
