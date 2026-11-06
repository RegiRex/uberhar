// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <string_view>
#include <fmt/format.h>
#include "common/logging/log.h"
#include "video_core/uberhar_frame_workload_census.h"

namespace VideoCore {

// CodexAstraLocal TEST-ONLY LOG: Production text adapter for the .57 full-frame
// workload investigation; remove with the census after work attribution is settled.
// Keep this adapter as a renderer-lifetime member: stream IDs separate reconstructed
// renderer dictionaries within a process. PID/process lifetime remain capture keys.
// Diagnostic logging may drop records and exposes no acceptance result. Returning
// Unknown is mandatory; aggregate logger omission records are additional evidence.
class FrameWorkloadCensusLog {
    using Census = FrameWorkloadCensus;
    using Delivery = Census::Delivery;
public:
    FrameWorkloadCensusLog() noexcept = default;

    Delivery operator()(const Census::Descriptor<Census::Target>& d) const {
        const auto stream = Stream();
        const auto& k = d.key;
        LOG_INFO_DIAGNOSTIC(Render_Vulkan,
            "CodexAstraLocal TEST-ONLY LOG frame-workload target stream={} slot={} gen={} known={} "
            "guest_color_addr={} guest_depth_addr={} guest_wh={},{} guest_formats={},{} "
            "host_wh={},{} host_samples={} host_layers={} host_formats={},{} "
            "viewport_xywh={},{},{},{} scissor_xywh={},{},{},{} requested_render_area_xywh={},{},{},{}",
            stream, d.slot, d.generation, k.known, k.color_address, k.depth_address,
            k.guest_width, k.guest_height, k.guest_color_format, k.guest_depth_format,
            k.host_width, k.host_height, k.host_samples, k.host_layers,
            k.host_color_format, k.host_depth_format,
            k.viewport.x, k.viewport.y, k.viewport.width, k.viewport.height,
            k.scissor.x, k.scissor.y, k.scissor.width, k.scissor.height,
            k.render_area.x, k.render_area.y, k.render_area.width, k.render_area.height);
        return Delivery::Unknown;
    }
    Delivery operator()(const Census::Descriptor<Census::Texture>& d) const {
        const auto stream = Stream();
        const auto& k = d.key;
        LOG_INFO_DIAGNOSTIC(Render_Vulkan,
            "CodexAstraLocal TEST-ONLY LOG frame-workload texture stream={} slot={} gen={} known={} "
            "unit={} guest_addr={} configured_wh={},{} format={} type={} configured_mips={},{} "
            "min_mag_mip_filters={},{},{} lod_bias_raw={} actual_sampled_mip=unmeasured",
            stream, d.slot, d.generation, k.known, k.unit, k.address, k.width, k.height,
            k.format, k.type, k.mip_min, k.mip_max, k.min_filter, k.mag_filter, k.mip_filter,
            k.lod_bias_raw);
        return Delivery::Unknown;
    }
    Delivery operator()(const Census::Descriptor<Census::TransferKey>& d) const {
        const auto stream = Stream();
        const auto& k = d.key;
        LOG_INFO_DIAGNOSTIC(Render_Vulkan,
            "CodexAstraLocal TEST-ONLY LOG frame-workload transfer stream={} slot={} gen={} "
            "kind={} known={} source_addr={} destination_addr={} input_wh={},{} output_wh={},{} "
            "input_output_formats={},{} flags={} scale_mode={} bytes_scope=logical_not_bus",
            stream, d.slot, d.generation, static_cast<unsigned>(k.kind), k.known,
            k.source_address, k.destination_address, k.input_width, k.input_height,
            k.output_width, k.output_height, k.input_format, k.output_format, k.flags, k.scale_mode);
        return Delivery::Unknown;
    }

    Delivery operator()(const Census::FrameReport& r) const {
        const auto stream = Stream();
        // CodexAstraLocal: At most 56 bin tuples plus seven routes in one fixed
        // buffer. Formatting occurs only at the callback boundary, never a draw.
        // Even unexpected future expansion reports truncation explicitly.
        Buffer bins;
        bins.Append("routes(id:draws:submitted_vertices)=");
        for (unsigned i = 0; i < r.routes.size(); ++i)
            bins.Append("{}:{}:{};", i, r.routes[i].draws, r.routes[i].submitted_vertices);
        bins.Append(" targets(slot:gen:draws:submitted_vertices:texture_slot_mask:texture_overflow)=");
        for (unsigned i = 0; i < r.targets.size(); ++i) {
            const auto& b = r.targets[i];
            if (b.operations) bins.Append("{}:{}:{}:{}:{}:{};", i, b.generation, b.operations,
                b.submitted_vertices, b.texture_slots, b.texture_overflow_bindings);
        }
        bins.Append(" textures(slot:gen:bindings)=");
        for (unsigned i = 0; i < r.textures.size(); ++i) {
            const auto& b = r.textures[i];
            if (b.operations) bins.Append("{}:{}:{};", i, b.generation, b.operations);
        }
        bins.Append(" transfers(slot:gen:operations:known_logical_bytes:unknown_byte_operations)=");
        for (unsigned i = 0; i < r.transfers.size(); ++i) {
            const auto& b = r.transfers[i];
            if (b.operations) bins.Append("{}:{}:{}:{}:{};", i, b.generation, b.operations,
                b.logical_bytes, b.unknown_byte_operations);
        }
        const auto& q = r.quality;
        LOG_INFO_DIAGNOSTIC(Render_Vulkan,
            "CodexAstraLocal TEST-ONLY LOG frame-workload frame stream={} report_sequence={} "
            "callback_index={} command_lists_total={} command_lists_known={} "
            "game_submissions_total={} game_submissions_known={} partial={} "
            "draws={} submitted_vertices={} indexed_draws={} texture_bindings={} "
            "transfer_operations={} known_logical_transfer_bytes={} unknown_byte_transfers={} "
            "target_overflow_draws={} texture_overflow_bindings={} transfer_overflow_operations={} "
            "invalid_routes={} invalid_texture_mask_draws={} evicted_pending_descriptors={} "
            "evicted_unconfirmed_descriptors={} descriptor_attempts={} pending_descriptors={} "
            "used_unconfirmed_descriptors={} sink_attempts_before_this_summary={} "
            "sink_known_drops_before_this_summary={} sink_unconfirmed_before_this_summary={} "
            "sink_exceptions_before_this_summary={} counter_saturated={} tuple_text_truncated={} "
            "sampled_report_sequence={} sampled_report_host_ns={} "
            "host_sample_scope=finish_report_only_not_draw_bookkeeping_or_gpu "
            "delivery=unconfirmed see_aggregate_logger_omissions=1 {}",
            stream, r.report_sequence, r.identity.callback_index, r.identity.command_lists_total,
            r.identity.command_lists_known, r.identity.game_submissions_total,
            r.identity.game_submissions_known, r.identity.partial, r.draws, r.submitted_vertices,
            r.indexed_draws, r.texture_bindings, r.transfer_operations, r.transfer_logical_bytes,
            r.transfer_unknown_byte_operations, q.target_overflow_draws, q.texture_overflow_bindings,
            q.transfer_overflow_operations, q.invalid_routes, q.invalid_texture_mask_draws,
            q.evicted_pending_descriptors, q.evicted_unconfirmed_descriptors, r.descriptor_attempts,
            q.pending_descriptors, q.used_unconfirmed_descriptors, q.sink_attempts,
            q.sink_known_drops, q.sink_unconfirmed, q.sink_exceptions, q.counter_saturated,
            bins.truncated, r.sampled_report_sequence, r.sampled_report_host_ns, bins.View());
        return Delivery::Unknown;
    }

private:
    struct Buffer {
        std::array<char, 8192> text{};
        std::size_t used{};
        bool truncated{};
        template <class... Args> void Append(fmt::format_string<Args...> format, Args&&... args) {
            const auto available = text.size() - used;
            const auto result = fmt::format_to_n(text.data() + used, available, format,
                                                 std::forward<Args>(args)...);
            truncated |= result.size > available;
            used += std::min(result.size, available);
        }
        std::string_view View() const noexcept { return {text.data(), used}; }
    };
    inline static std::atomic<Census::U64> next_stream{1};
    // CodexAstraLocal: Lazy identity means an OFF census never touches even the
    // launch-time stream counter. Only this owner thread calls the sink.
    Census::U64 Stream() const noexcept {
        if (!stream) stream = next_stream.fetch_add(1, std::memory_order_relaxed);
        return stream;
    }
    mutable Census::U64 stream{};
};

} // namespace VideoCore
