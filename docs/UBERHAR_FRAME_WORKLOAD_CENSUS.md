# Optional frame workload census

<!-- CodexAstraLocal: Document the bounded .57 diagnostic contract, renderer integration and interpretation. Remove or promote with the corresponding TEST-ONLY LOG investigation. -->

`uberhar_frame_workload_census` is launch-owned, alpha Testing only, default OFF.
`video_core/uberhar_frame_workload_census.h` is the pure owner-thread helper;
`video_core/uberhar_frame_workload_census_log.h` is its production Diagnostic text
adapter. This observes successful enqueue work and configured resources. It does
not measure fragments, actual sampled mips, GPU duration, DRAM traffic or bandwidth.

## Integration contract

Keep one helper shared by the serialized PICA/GPU/rasterizer owner and one logger
per Vulkan renderer lifetime. OFF must bypass both field preparation and helper
calls. The helper allocates one fixed state only when enabled; `AllocationFailed()`
must produce one startup diagnostic if the requested census could not start.
No per-draw allocation, clock, log, hash, GPU readback or wait is introduced.
The helper is deliberately not thread-safe: worker callbacks must not mutate it.

```cpp
using Census = VideoCore::FrameWorkloadCensus;
// Launch construction; root owns settings/profile resolution.
std::unique_ptr<Census> frame_census;
if (Settings::values.uberhar_frame_workload_census.GetValue())
    frame_census = std::make_unique<Census>(true);
VideoCore::FrameWorkloadCensusLog census_log; // Persistent, lazy stream identity.
u64 census_callback_index{};

// At the existing drained TickFrame boundary; no new wait for the census.
if (frame_census && frame_census->Enabled()) {
    Census::FrameIdentity identity{};
    identity.callback_index = ++census_callback_index;
    identity.command_lists_total = pica.command_lists_submitted;
    identity.command_lists_known = true;
    identity.game_submissions_total = Common::UberharActivity::submissions.load(
        std::memory_order_relaxed);
    identity.game_submissions_known = true;
    frame_census->FinishFrame(identity, census_log);
}
```

The callback index counts `TickFrame` calls. Command-list totals and existing game
submission totals retain their own source meanings; none substitutes for measured
guest FPS. The caller may consume a final partial interval with `partial=true`
at an already safe shutdown boundary. No destructor logging or additional drain is
performed. Without that explicit hook the unfinished final interval is unreported.
There is no recording time, sample-count or inactivity cutoff.

Record a successful draw after its actual enqueue, once across ordinary, indexed,
ready-GPU, CPU/deferred and compute paths. Do not count failed optional attempts,
empty/no-framebuffer work, skipped eyes or a CPU retry twice. The following is a
construction pattern for the current Vulkan framebuffer helper:

```cpp
if (!frame_census || !frame_census->Enabled()) return;
Census::Draw draw{};
const auto& guest = regs.framebuffer.framebuffer;
const auto* fb = fb_helper.Framebuffer();
auto& target = draw.target;
target.known = Census::Target::GuestColor | Census::Target::GuestDepth |
               Census::Target::GuestExtent | Census::Target::HostExtent;
target.color_address = guest.GetColorBufferPhysicalAddress();
target.depth_address = guest.GetDepthBufferPhysicalAddress();
target.guest_width = guest.GetWidth();
target.guest_height = guest.GetHeight();
target.guest_color_format = static_cast<u32>(guest.color_format.Value());
target.guest_depth_format = static_cast<u32>(guest.depth_format.Value());
target.host_width = fb->Extent().width;
target.host_height = fb->Extent().height;
// Only mark sample/layer/host-format/rectangle fields known from actual backend
// state. Framebuffer construction presently proves one host sample/layer;
// this is independent of guest AA passes and guest resolution.

const auto textures = regs.texturing.GetTextures();
for (u32 unit = 0; unit < textures.size(); ++unit) {
    if (!textures[unit].enabled) continue;
    const auto& input = textures[unit].config;
    auto& output = draw.textures[unit];
    draw.texture_mask |= 1U << unit;
    output.known = Census::Texture::Address | Census::Texture::Extent |
                   Census::Texture::Format | Census::Texture::MipRange |
                   Census::Texture::Filters | Census::Texture::LodBias;
    output.unit = unit;
    output.address = input.GetPhysicalAddress();
    output.width = input.width.Value();
    output.height = input.height.Value();
    output.format = static_cast<u32>(textures[unit].format);
    output.mip_min = input.lod.min_level.Value();
    output.mip_max = input.lod.max_level.Value();
    output.min_filter = input.min_filter.Value();
    output.mag_filter = input.mag_filter.Value();
    output.mip_filter = input.mip_filter.Value();
    output.lod_bias_raw = static_cast<u32>(input.lod.bias.Value());
    if (unit == 0) { // This register's Type field is defined only for unit 0.
        output.known |= Census::Texture::Type;
        output.type = static_cast<u32>(input.type.Value());
    }
}
draw.route = selected_route;
draw.submitted_vertices = actual_vertex_or_index_count;
draw.indexed = actual_indexed_draw;
frame_census->RecordDraw(draw);
```

Texture descriptors describe enabled guest units, including bindings unused by the
selected shader. They do not enumerate cube faces, utility/LUT/procedural resources,
host cache-upload operations or actual texture samples. LOD bias is the signed
fixed-point register value represented as an unsigned 32-bit value. Route 3 is
`CpuVertexUnclassified`: CPU upload is observed but fragment tier is not yet known
to the owner. Do not infer generic/partial/full from requested pipeline tokens when
the worker can arbitrate differently.

| Route ID | Observed route |
| --- | --- |
| 0 | Unknown |
| 1 | GPU vertices |
| 2 | Ready-GPU vertices |
| 3 | CPU vertices, fragment tier unclassified |
| 4 | CPU vertices, verified generic fragment tier |
| 5 | CPU vertices, verified partial fragment tier |
| 6 | CPU vertices, verified full fragment tier |
| 7 | Compute rectangle |

Use effective viewport and dynamic scissor bounds for a graphics draw. A compute
dispatch has no graphics viewport/scissor. `FramebufferHelper::DrawRect()` is the
requested render area, which can differ from an already-active larger pass under
contained-pass reuse; never describe it as an observed actual pass extent. Actual
effective pass area needs its own owner-side witness or must stay unknown. Bounds
area and attachment dimensions do not count shaded fragments or guest AA passes.

Record guest transfers in `GPU::MemoryTransfer` only after the chosen actual
operation, including software fallback; do not record transfers suppressed by the
right-eye policy. The current guest-transfer pattern is:

```cpp
if (transfer_executed && frame_census && frame_census->Enabled()) {
    Census::Transfer transfer{};
    auto& key = transfer.key;
    key.kind = config.is_texture_copy ? Census::TransferKind::TextureCopy
                                     : Census::TransferKind::Display;
    key.known = Census::TransferKey::Addresses | Census::TransferKey::Flags;
    key.source_address = config.GetPhysicalInputAddress();
    key.destination_address = config.GetPhysicalOutputAddress();
    key.flags = config.flags;
    if (!config.is_texture_copy) {
        key.known |= Census::TransferKey::InputExtent | Census::TransferKey::OutputExtent |
                     Census::TransferKey::Formats | Census::TransferKey::ScaleMode;
        key.input_width = config.input_width.Value();
        key.input_height = config.input_height.Value();
        key.output_width = config.output_width.Value();
        key.output_height = config.output_height.Value();
        key.input_format = static_cast<u32>(config.input_format.Value());
        key.output_format = static_cast<u32>(config.output_format.Value());
        key.scale_mode = static_cast<u32>(config.scaling.Value());
    }
    transfer.logical_bytes_known = true;
    transfer.logical_bytes = config.is_texture_copy ? (config.texture_copy.size & ~u32{15}) :
        u64{config.input_width.Value()} * config.input_height.Value() *
        BytesPerPixel(config.input_format);
    frame_census->RecordTransfer(transfer);
}
```

Display bytes above are nominal configured input bytes, not proof of every touched
byte after crop/scaling. Texture-copy bytes remove the ignored low four size bits;
copy gaps/strides are not included as payload. Unknown bytes must set
`logical_bytes_known=false`, rather than pass an invented estimate. DMA/fill enum
values reserve explicit hooks; their presence alone does not mean those paths are
currently covered. Cache upload bytes and CPU/GPU read/write traffic remain absent.

## Bounded output and quality

The helper retains 16 target, 32 texture and 8 transfer dictionary slots. Each slot
has a generation number. Slots used in an interval cannot be replaced until its
summary, so target-to-texture masks refer to the texture generations in that same
summary. These masks describe frame-level co-occurrence, not per-draw joined
worksets. Once a dictionary is full, all-draw/vertex/route/transfer totals still
advance and explicit overflow counters describe the omitted groups.

Each callback attempts one `frame-workload frame` line and at most four descriptor
lines in round-robin dictionary order. A definition is attempted on first
appearance/change; known drops stay pending for retry. An Unknown delivery result
cannot be retried indiscriminately without a flood. Missing definitions must remain
unknown in analysis, even when summary counters survived. Pending/unconfirmed
evictions and used unconfirmed descriptors are explicit. Tuple schemas are written
in the frame line itself, and metadata carries actual numeric dimensions and
configured mip values; there is no opaque hash-only workset.

Partition by process lifetime and `stream`; join by descriptor kind, slot and
generation. `report_sequence` gaps identify missing summary records. A surviving
summary contains all observations for that callback interval, subject to
`counter_saturated=false`; it does not restore absent intervals. The current
Diagnostic logger reports aggregate omissions but returns no per-record acceptance.
The adapter therefore returns `Unknown` and prints unconfirmed counts; zero known
drops is not proof of complete delivery. Delivery counters in a summary include
its descriptor attempts but not its own eventual delivery result, which is known
only to the following interval. Formatting uses one fixed 8192-byte tuple buffer;
`tuple_text_truncated` reports any loss independently of dictionary overflow.

Every 64th callback samples host wall time around `FinishFrame`, including the
synchronous logging calls. The result is emitted in a later summary with its
sampled report sequence. It excludes descriptor preparation, per-draw bookkeeping,
asynchronous logger backend cost and GPU execution. It is not total observer cost
or GPU time. Device ON/OFF validation remains necessary before using the census as
performance evidence; ordinary observer timing and the functional compute-selector
timestamps are independent controls.

## Focused verification

```sh
python3 tools/uberhar/test_frame_workload_census.py --output /tmp/frame-census-check
```

The host fixture executes the actual helper and production formatting adapter with
a recording logger endpoint. It checks OFF allocation/sink absence, fixed ON state,
no helper hot allocations, all-draw conservation after quota/overflow, generations,
viewport/mip identity, explicit unknown delivery, retry, exception containment,
saturation, tuple buffer capacity and sparse report-only timing. Its synthetic
ON/OFF cost includes one finish per 256 draws and excludes descriptor construction,
production logger queue/backend and the device. It does not validate renderer hook
coverage, multithread ownership or actual Vulkan rendering; those need integration
checks and normal application validation.
