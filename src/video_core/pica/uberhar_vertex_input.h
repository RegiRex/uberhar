// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
#pragma once

#include <array>
#include <cstring>
#include <limits>
#include <span>
#include "video_core/pica/regs_pipeline.h"
#include "video_core/pica/regs_shader.h"
#include "video_core/pica/shader_unit.h"

namespace Pica {

// AstraEH: A value-only description from the inherited loader, not another decoder
// for the guest register layout. Defaults and unconfigured attributes never read
// the inherited loader's uninitialized format slots.
struct NativeInputAttribute {
    u32 offset{}, stride{}, elements{};
    PipelineRegs::VertexAttributeFormat format{};
    bool is_default{};
};

// CodexAstraLocal TEST-ONLY LOG: Fixed per-invocation actual-read provenance.
// Each scalar is copied from the SAME local used by conversion, never reread
// from guest RAM after a fault. Remove after Software source-input localization.
struct NativeInputReadTrace {
    struct Attribute {
        PAddr address{};
        u32 attribute{},input_register{},format{},elements{},size{};
        bool is_default{};
        std::array<u8,16> bytes;
    };
    std::array<Attribute,16> attributes;
    u32 count{},guest_bytes{};
};

// AstraEH: Fuse attribute conversion and VS-register transport on validated no-GS
// draws. Resolve bounded, pinned memory once per draw, never across draws/frames.
// The caller supplies a conservative maximum index and retains mapped MemoryRefs.
// Any uncertain shape/range falls back before a shader invocation or input write.
class NativeVertexInputPlan {
public:
    enum class Result : u32 { Ready, MissingAttribute, Unconfigured, AddressWrap, ShortMapping, Count };
    static constexpr std::size_t ResultCount = static_cast<std::size_t>(Result::Count);

    template <typename Describe, typename Map>
    Result Prepare(const ShaderRegs& shader, u32 available_attributes, PAddr base, u64 last_vertex,
                   Describe&& describe, Map&& map, bool complete_recipe = false) {
        // CodexAstraLocal: A retry or rejected mapping must never retain an
        // executor selected from a different prepared plan.
        recipe = nullptr;
        recipe_slot = 0;
        count = 0;
        mapped_attributes = 0;
        ready = false;
        const u32 requested = shader.max_input_attribute_index + 1;
        if (requested > available_attributes || requested > ops.size())
            return Result::MissingAttribute;
        if (last_vertex > std::numeric_limits<u32>::max())
            return Result::AddressWrap;
        for (u32 attr = 0; attr < requested; ++attr) {
            const auto desc = describe(attr);
            auto& op = ops[attr];
            op.reg = shader.GetRegisterForAttribute(attr);
            op.attribute = attr;
            op.is_default = desc.is_default;
            if (op.is_default)
                continue;
            if (desc.elements < 1 || desc.elements > 4 || static_cast<u32>(desc.format) > 3)
                return Result::Unconfigured;
            const u64 address = static_cast<u64>(base) + desc.offset;
            const u64 bytes = static_cast<u64>(desc.elements) * FormatBytes(desc.format);
            const u64 required = static_cast<u64>(desc.stride) * last_vertex + bytes;
            // AstraEH: Preserve the original unsigned-address behavior by declining
            // wraparound cases; do not turn them into out-of-bounds host arithmetic.
            if (address > std::numeric_limits<u32>::max() ||
                required > (u64{1} << 32) - address)
                return Result::AddressWrap;
            const std::span<const u8> memory = map(static_cast<PAddr>(address));
            ++mapped_attributes;
            if (!memory.data() || required > memory.size())
                return Result::ShortMapping;
            op.data = memory.data();
            op.stride = desc.stride;
            op.read = Reader(desc.format, desc.elements);
        }
        count = requested;
        // CodexAstraLocal: Select only after all original range/reader checks;
        // the caller disables this bounded work for empty draws.
        if (complete_recipe)
            recipe = SelectRecipe();
        // AstraPro: Retain the validated bound for live-index checks after a retry.
        admitted_maximum = static_cast<u32>(last_vertex);
        ready = true;
        return Result::Ready;
    }

    bool Ready() const { return ready; }
    // AstraPro: Index memory is live, not frozen by a preceding maximum scan.
    bool CanLoad(u32 vertex) const { return ready && vertex <= admitted_maximum; }
    u32 MappedAttributes() const { return mapped_attributes; }
    u32 AttributeCount() const { return count; }
    // CodexAstraLocal: Slot zero is generic prepared transport; the other four
    // tags identify the current complete recipe for once-per-draw usage counts.
    u32 RecipeSlot() const { return recipe_slot; }

    // AstraEH: Caller only invokes after Ready, for indices <= the admitted bound.
    // Retain ascending writes (including aliases), live defaults, exact f24
    // conversion, and untouched shader registers. No JIT math or FIFO policy changes.
    void Load(ShaderUnit& unit, const AttributeBuffer& defaults, u32 vertex) const {
        // CodexAstraLocal: A complete recipe removes repeated reader dispatch
        // while preserving exact conversion order and live per-miss reads.
        if (recipe) {
            recipe(*this, unit, vertex);
            return;
        }
        for (u32 attr = 0; attr < count; ++attr) {
            const auto& op = ops[attr];
            if (op.is_default) {
                unit.input[op.reg] = defaults[op.attribute];
            } else {
                op.read(unit.input[op.reg], op.data + static_cast<std::size_t>(op.stride) * vertex);
            }
        }
    }

    // CodexAstraLocal TEST-ONLY LOG: Select only for the armed Software
    // diagnostic draw. Original Load/recipes and every other renderer stay
    // unchanged. Descriptor fields come from the same qualified loader plan.
    void LoadObserved(ShaderUnit& unit,const AttributeBuffer& defaults,u32 vertex,
                      PAddr base,std::span<const NativeInputAttribute> descriptions,
                      NativeInputReadTrace& trace) const {
        trace.count=count;trace.guest_bytes=0;
        for(u32 attr=0;attr<count;++attr) {
            const auto& op=ops[attr];const auto& desc=descriptions[attr];
            auto& record=trace.attributes[attr];
            record.attribute=attr;record.input_register=op.reg;
            record.is_default=op.is_default;
            record.format=static_cast<u32>(desc.format);record.elements=desc.elements;
            if(op.is_default) {
                unit.input[op.reg]=defaults[op.attribute];record.address=0;record.size=16;
                std::memcpy(record.bytes.data(),&unit.input[op.reg],16);
            } else {
                record.address=base+desc.offset+desc.stride*vertex;
                record.size=desc.elements*FormatBytes(desc.format);trace.guest_bytes+=record.size;
                TraceReader(desc.format,desc.elements)(unit.input[op.reg],
                    op.data+static_cast<std::size_t>(op.stride)*vertex,record.bytes.data());
            }
        }
    }

private:
    using Read = void (*)(Common::Vec4<f24>&, const u8*);
    using TraceRead = void (*)(Common::Vec4<f24>&, const u8*,u8*);
    struct Op {
        const u8* data{};
        Read read{};
        u32 stride{}, reg{}, attribute{};
        bool is_default{};
    };
    static u32 FormatBytes(PipelineRegs::VertexAttributeFormat format) {
        return format == PipelineRegs::VertexAttributeFormat::FLOAT ? 4 :
               format == PipelineRegs::VertexAttributeFormat::SHORT ? 2 : 1;
    }
    // AstraEH: Select both format and element count once, allowing unrolled scalar
    // conversions without unaligned typed-pointer dereferences. This is not SIMD
    // approximation; use exactly the inherited f24::FromFloat32 operation.
    // CodexAstraLocal: One actual read/conversion implementation; the ordinary
    // specialization compiles out provenance and keeps its existing recipe ABI.
    template <typename T,u32 N,bool Trace>
    static void ConvertValues(Common::Vec4<f24>& out,const u8* data,u8* copied=nullptr) {
        for(u32 component=0;component<N;++component) {
            T value;
            std::memcpy(&value,data+component*sizeof(T),sizeof(T));
            if constexpr(Trace) std::memcpy(copied+component*sizeof(T),&value,sizeof(T));
            out[component]=f24::FromFloat32(value);
        }
        for(u32 component=N;component<4;++component)
            out[component]=component==3 ? f24::One() : f24::Zero();
    }
    template<typename T,u32 N>
    static void Convert(Common::Vec4<f24>& out,const u8* data) { ConvertValues<T,N,false>(out,data); }
    template<typename T,u32 N>
    static void ConvertTraced(Common::Vec4<f24>& out,const u8* data,u8* bytes) {
        ConvertValues<T,N,true>(out,data,bytes);
    }
    template<typename T> static TraceRead SizedTrace(u32 count) {
        constexpr std::array<TraceRead,4> readers{ConvertTraced<T,1>,ConvertTraced<T,2>,
                                                ConvertTraced<T,3>,ConvertTraced<T,4>};
        return readers[count-1];
    }
    static TraceRead TraceReader(PipelineRegs::VertexAttributeFormat format,u32 count) {
        switch(format) {
        case PipelineRegs::VertexAttributeFormat::BYTE:return SizedTrace<s8>(count);
        case PipelineRegs::VertexAttributeFormat::UBYTE:return SizedTrace<u8>(count);
        case PipelineRegs::VertexAttributeFormat::SHORT:return SizedTrace<s16>(count);
        case PipelineRegs::VertexAttributeFormat::FLOAT:return SizedTrace<f32>(count);
        }
        return nullptr; // Ready() has already rejected unsupported formats.
    }
    template <typename T>
    static Read SizedReader(u32 count) {
        constexpr std::array<Read, 4> readers{Convert<T, 1>, Convert<T, 2>, Convert<T, 3>, Convert<T, 4>};
        return readers[count - 1];
    }
    static Read Reader(PipelineRegs::VertexAttributeFormat format, u32 count) {
        switch (format) {
        case PipelineRegs::VertexAttributeFormat::BYTE: return SizedReader<s8>(count);
        case PipelineRegs::VertexAttributeFormat::UBYTE: return SizedReader<u8>(count);
        case PipelineRegs::VertexAttributeFormat::SHORT: return SizedReader<s16>(count);
        case PipelineRegs::VertexAttributeFormat::FLOAT: return SizedReader<f32>(count);
        }
        return nullptr; // Prepare rejected every out-of-range format above.
    }
    // CodexAstraLocal: Match complete reader/default/count sequences only.
    // Destinations, addresses and strides stay in this draw's validated plan.
    template <typename T_, u32 N_>
    struct Element { using T = T_; static constexpr u32 N = N_; };
    using F3 = Element<f32, 3>;
    using F2 = Element<f32, 2>;
    using B3 = Element<s8, 3>;
    using B4 = Element<s8, 4>;
    using S2 = Element<s16, 2>;
    using S3 = Element<s16, 3>;
    using S4 = Element<s16, 4>;
    using U2 = Element<u8, 2>;
    using U4 = Element<u8, 4>;
    using Recipe = void (*)(const NativeVertexInputPlan&, ShaderUnit&, u32);
    template <class... E>
    bool MatchesRecipe() const {
        constexpr std::array<Read, sizeof...(E)> readers{&Convert<typename E::T, E::N>...};
        for (u32 index = 0; index < readers.size(); ++index)
            if (ops[index].is_default || ops[index].read != readers[index])
                return false;
        return true;
    }
    // CodexAstraLocal: The comma fold sequences every original typed conversion;
    // aliased destination registers keep ascending-attribute last-write behavior.
    template <class E>
    static void ConvertRecipeElement(const Op& op, ShaderUnit& unit, u32 vertex) {
        Convert<typename E::T, E::N>(unit.input[op.reg],
            op.data + static_cast<std::size_t>(op.stride) * vertex);
    }
    template <class... E>
    static void LoadRecipe(const NativeVertexInputPlan& plan, ShaderUnit& unit, u32 vertex) {
        u32 index = 0;
        (ConvertRecipeElement<E>(plan.ops[index++], unit, vertex), ...);
    }
    // CodexAstraLocal: Publish the tag only with its exact matching executor.
    // Early rejection and every retry retain the Prepare entry's generic tag.
    Recipe SelectRecipe() {
        switch (count) {
        case 2:
            if (MatchesRecipe<F3, F2>()) {
                recipe_slot = 3;
                return LoadRecipe<F3, F2>;
            }
            break;
        case 4:
            if (MatchesRecipe<F3, B3, S2, U4>()) {
                recipe_slot = 4;
                return LoadRecipe<F3, B3, S2, U4>;
            }
            break;
        case 5:
            if (MatchesRecipe<F3, B3, S2, S2, U4>()) {
                recipe_slot = 1;
                return LoadRecipe<F3, B3, S2, S2, U4>;
            }
            break;
        case 11:
            if (MatchesRecipe<S3, B4, S2, U2, S3, S4, S4, S4, S4, S4, S4>()) {
                recipe_slot = 2;
                return LoadRecipe<S3, B4, S2, U2, S3, S4, S4, S4, S4, S4, S4>;
            }
            break;
        }
        return nullptr;
    }
    // CodexAstraLocal: This value is valid only for the currently prepared ops;
    // no extra owner, guest-data cache, generation lookup or allocation is added.
    Recipe recipe{};
    std::array<Op, 16> ops{};
    u32 count{}, mapped_attributes{}, admitted_maximum{};
    bool ready{};
    // CodexAstraLocal: Reset with the same prepared ops, never a cross-draw key.
    u8 recipe_slot{};
};
} // namespace Pica
