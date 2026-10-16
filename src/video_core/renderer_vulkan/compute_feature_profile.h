// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <array>
#include <chrono>
#include <utility>
#include "common/calculated_renderer_settings.h"
#include "common/logging/log.h"
#include "common/uberhar_activity.h"
#include "video_core/renderer_vulkan/compute_failure_packet.h"
namespace Vulkan {
// CodexAstraLocal TEST-ONLY LOG: Attribute each independent candidate to actual
// work. Remove these sampled aggregates after Calculated hardware qualification;
// retain feature/cache identity and terminal error evidence. No GPU readback added.
class ComputeFeatureProfile {
public:
    enum class Cost : u32 { GuestCompile, GuestLink, AssemblyCompile, AssemblyLink,
        RasterCompile, RasterLink, TransferCompile, TransferLink, Prepare, Enqueue, Wait, Count };
    inline static constexpr std::array CostNames{"guest_compile", "guest_link", "assembly_compile",
        "assembly_link", "raster_compile", "raster_link", "transfer_compile", "transfer_link",
        "prepare", "owner_enqueue", "owner_wait"};
    struct Timing { u64 calls{},ns{}; };
    explicit ComputeFeatureProfile(const char* owner_) : owner{owner_} {}
    ~ComputeFeatureProfile() { Report(true); }
    const CalculatedFeatures::Snapshot config;
    // CodexAstraLocal: Producer outputs are completed vertices; raster outputs
    // are active-rectangle pixel opportunities. Raster raw/output bytes describe
    // packed scratch / logical RGBA-word bounds, not writes or memory traffic.
    u64 draws{},success{},failures{},inputs{},outputs{},zero_outputs{};
    u64 inside{},polygon{},intersections{},counter_unavailable{},raw_bytes{},output_bytes{};
    u64 guest_hits{},guest_misses{},assembly_hits{},assembly_misses{},raster_hits{},raster_misses{};
    u64 retry_attempts{},retry_no_alternate{},retry_created{},retry_cache_hits{},retry_failed{};
    u64 transform_ns{},transform_input_bytes{},transform_output_bytes{};
    u64 dispatch_groups{},full_target_groups{},active_pixels{},triangle_pixel_pairs{};
    void Begin(const std::shared_ptr<ComputeFailurePacket>& packet) noexcept {
        ++draws;sampled=config.diagnostics&&(draws&63u)==1u;
        if(!session)session=Common::UberharActivity::Capture().run;
        if(packet) {
            packet->Number("features.requested",config.requested);
            packet->Number("features.effective",config.effective);
            packet->Number("features.revision",CalculatedFeatures::Revision);
            packet->Number("features.diagnostics",config.diagnostics);
            packet->Number("features.owner_draw",draws);
        }
        if(draws==1)Report(false,true);
    }
    struct Timer {
        ComputeFeatureProfile* profile{};Cost cost{};u64 begin{};
        Timer(ComputeFeatureProfile& p,Cost c,bool enabled):profile{enabled?&p:nullptr},cost{c},begin{enabled?Now():0} {}
        Timer(const Timer&)=delete;
        ~Timer(){End();}
        void End() noexcept { if(profile){auto& t=profile->timings[static_cast<u32>(cost)];++t.calls;t.ns+=Now()-begin;profile=nullptr;} }
    };
    Timer Measure(Cost cost,bool rare=false) { return Timer{*this,cost,config.diagnostics&&(rare||sampled)}; }
    template<class F> decltype(auto) Time(Cost cost,F&& action,bool rare=false) {
        auto timer=Measure(cost,rare);return std::forward<F>(action)();
    }
    void Complete(bool ok) noexcept {
        if(ok)++success;else ++failures;
        if(!ok || (draws&4095u)==1u)Report(false,true);
    }
    void Report(bool final,bool force=false) noexcept {
        // CodexAstraLocal: Even diagnostics-OFF retains one initial/effective policy
        // and final operation census. OFF avoids normal sampled/compile clocks
        // and shader counters; rare structural-retry timing remains failure evidence.
        // periodic optional records cannot flood the shared logging queue.
        if((!force&&!final)||(!draws&&final)||
           (!config.diagnostics&&!final&&draws!=1))return;
        try {
            LOG_INFO_WITH_DELIVERY(Render_Vulkan, final?Common::Log::Delivery::Reliable:Common::Log::Delivery::Diagnostic,
                "Calculated feature cost: session={} owner={} final={} revision={} requested={} effective={} diagnostics={} "
                "feature_ids=calc.assembly_storage.v1|calc.inside_triangle.v1|calc.structural_retry.v1|calc.clip_storage.v1|calc.primary_color.v1|calc.raster_tiles.v1 "
                "assembly_storage_v1={} inside_triangle_v1={} structural_retry_v1={} clip_storage_v1={} primary_color_v1=false "
                "primary_color_refusal=unqualified_interpolation raster_tiles_v1={} retry_off_restores_terminal=true "
                "draws={} success={} failures={} inputs={} outputs={} zero_outputs={} inside={} polygon={} intersections={} "
                "counter_unavailable={} raw_bytes={} output_bytes={} guest_hits={} guest_misses={} assembly_hits={} assembly_misses={} "
                "raster_hits={} raster_misses={} retry_attempts={} retry_no_alternate={} retry_created={} retry_cache_hits={} retry_failed={} "
                "transform_ms={:.6f} transform_input_bytes={} transform_output_bytes={} dispatch_groups={} full_target_groups={} "
                "active_pixels={} triangle_pixel_pairs={} gpu_time=separate_work_profile bandwidth=unavailable saved_ms=unavailable "
                "cumulative=true cpu_sample_stride=64 compile_cost=all_misses_when_diagnostics_enabled counts_not_time=true "
                "shader_route_counts_available={} gpu_timestamp_control=independent_global "
                "success_scope=producer_header_or_raster_recorded prepare_may_include_allocator_waits=true",
                session,owner,final,CalculatedFeatures::Revision,config.requested,config.effective,config.diagnostics,
                config.Has(CalculatedFeatures::AssemblyStorage),config.Has(CalculatedFeatures::InsideTriangle),
                config.Has(CalculatedFeatures::StructuralRetry),config.Has(CalculatedFeatures::ClipStorage),
                config.Has(CalculatedFeatures::RasterTiles),draws,success,failures,inputs,outputs,zero_outputs,
                inside,polygon,intersections,counter_unavailable,raw_bytes,output_bytes,guest_hits,guest_misses,
                assembly_hits,assembly_misses,raster_hits,raster_misses,retry_attempts,retry_no_alternate,retry_created,
                retry_cache_hits,retry_failed,transform_ns/1e6,transform_input_bytes,transform_output_bytes,
                dispatch_groups,full_target_groups,active_pixels,triangle_pixel_pairs,config.diagnostics);
            for(u32 i=0;i<timings.size();++i)if(timings[i].calls)
                // CodexAstraLocal TEST-ONLY LOG: Final phase totals use reliable delivery,
                // just like operation totals. Remove after hardware qualification.
                LOG_INFO_WITH_DELIVERY(Render_Vulkan,final?Common::Log::Delivery::Reliable:Common::Log::Delivery::Diagnostic,"Calculated CPU cost: session={} owner={} phase={} calls={} wall_ms={:.6f} "
                    "host_wall_only=true gpu_time=false cumulative=true unsampled_cost=unknown parent_gpu_wait_may_overlap=true",
                    session,owner,CostNames[i],timings[i].calls,timings[i].ns/1e6);
        } catch(...) {} // CodexAstraLocal: Optional evidence cannot change rendering/refusal.
    }
private:
    static u64 Now() { return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count(); }
    const char* owner;
    u64 session{};bool sampled{};
    std::array<Timing,static_cast<u32>(Cost::Count)> timings{};
};
} // namespace Vulkan
