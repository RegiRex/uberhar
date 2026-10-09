// CodexAstraLocal: Production-loader regression. The same fixture is
// compiled against baseline/candidate; emitted initialized fields and live input
// bytes must match exactly. Synthetic aligned RAM does not model guest ownership.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/vertex_loader.h"

static unsigned errors = 0;
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                      fmt::string_view, const fmt::format_args&) {
    if (level >= Level::Error) ++errors;
}
}
static u32 seed = 0x83c75ad1U;
static u32 random_word() {
    seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed;
}
static void emit(u32 value) {
    const u32_le le = value;
    if (std::fwrite(&le, sizeof(le), 1, stdout) != 1) std::abort();
}
template <class T> static void word(T& field, u32 value) {
    static_assert(sizeof(T) == sizeof(u32_le));
    static_assert(std::is_trivially_copyable_v<T>);
    const u32_le le = value; std::memcpy(&field, &le, sizeof(le));
}
static void require(bool ok) { if (!ok) std::exit(7); }
int main() {
    // CodexAstraLocal: Exercise every encoded value at every valid position,
    // independently checking extracted values before constructor composition.
    Pica::PipelineRegs regs{};
    unsigned scalar_checks = 0;
    for (unsigned row = 0; row < 16384; ++row) {
        const u32 lo = random_word(), hi = random_word();
        auto& attrs = regs.vertex_attributes;
        word(attrs.format0, lo); word(attrs.format8, hi);
        word(attrs.attribute_loaders[0].comp0, hi);
        word(attrs.attribute_loaders[0].comp8, lo);
        for (unsigned n = 0; n < 12; ++n) {
            const u32 nibble = ((n < 8 ? lo : hi) >> (4 * (n % 8))) & 15;
            require(unsigned(attrs.GetFormat(n)) == (nibble & 3));
            require(attrs.GetNumElements(n) == (nibble / 4 + 1));
            require(attrs.attribute_loaders[0].GetComponent(n) ==
                    (((n < 8 ? hi : lo) >> (4 * (n % 8))) & 15));
            scalar_checks += 3;
        }
    }
    // CodexAstraLocal: Explicit all-value/position witnesses prevent random
    // coverage from being mistaken for exhaustive nibble coverage.
    for (unsigned n = 0; n < 12; ++n) for (u32 value = 0; value < 16; ++value) {
        auto& a = regs.vertex_attributes;
        u32 words[2] = {0xffffffffU, 0xffffffffU};
        const u32 shift = 4 * (n % 8);
        words[n / 8] = (words[n / 8] & ~(15U << shift)) | (value << shift);
        word(a.format0, words[0]); word(a.format8, words[1]);
        word(a.attribute_loaders[0].comp0, words[0]);
        word(a.attribute_loaders[0].comp8, words[1]);
        require(unsigned(a.GetFormat(n)) == (value & 3));
        require(a.GetNumElements(n) == value / 4 + 1);
        require(a.attribute_loaders[0].GetComponent(n) == value);
        scalar_checks += 3;
    }
    std::vector<u8> ram(65536);
    for (std::size_t i = 0; i < ram.size(); i += 4) {
        const u32 bits = random_word() & 0xfeffffffU;
        std::memcpy(ram.data() + i, &bits, 4);
    }
    Memory::MemorySystem memory; memory.Set(0, ram);
    Pica::AttributeBuffer input{}, defaults{};
    for (unsigned a = 0; a < 16; ++a) for (unsigned c = 0; c < 4; ++c) {
        defaults[a][c] = Pica::f24::FromFloat32(float(a * 4 + c) / 8);
        input[a][c] = Pica::f24::FromFloat32(-float(a * 4 + c + 1));
    }
    // CodexAstraLocal: Full constructor cases include duplicate mappings,
    // padding IDs12..15, default/absent attributes, overflow component counts,
    // live RAM mutations and all sixteen total-attribute counts. Retain input
    // across draws, matching the loader's unconfigured-attribute behavior.
    for (unsigned row = 0; row < 8192; ++row) {
        auto& attrs = regs.vertex_attributes;
        word(attrs.format0, random_word()); word(attrs.format8, random_word());
        for (auto& loader : attrs.attribute_loaders) {
            word(loader.data_offset, random_word() & 0x7f0U);
            word(loader.comp0, random_word());
            const u32 high = random_word();
            word(loader.comp8, (high & 0xf000ffffU) | ((random_word() & 0xf0U) << 16));
        }
        Pica::VertexLoader loader(memory, regs);
        emit(loader.GetNumTotalAttributes());
        for (unsigned a = 0; a < 16; ++a) {
            const auto desc = loader.DescribeNativeInput(a);
            emit(desc.offset); emit(desc.stride); emit(desc.elements);
            emit(unsigned(desc.format)); emit(desc.is_default);
        }
        for (unsigned vertex : {0U, 1U, 7U}) {
            ram[(row * 4) % ram.size()] ^= u8(row);
            loader.LoadVertex(0, vertex, vertex, input, defaults);
            for (const auto& vector : input) for (unsigned c = 0; c < 4; ++c) {
                const float value = vector[c].ToFloat32();
                u32 bits; std::memcpy(&bits, &value, 4); emit(bits);
            }
        }
    }
    emit(errors);
    std::fprintf(stderr, "scalar_checks=%u constructor_cases=8192 live_loads=24576 errors=%u\n",
                 scalar_checks, errors);
}
