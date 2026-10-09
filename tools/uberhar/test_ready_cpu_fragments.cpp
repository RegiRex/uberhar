#include "static_tev_cache_fixture.h"

GraphicsPipeline* QualifyAndBuild(PipelineCache& p, PipelineInfo info, const FSConfig& config) {
    if (!p.curr_disk_cache->ready_fragments.contains(config.Hash())) SeedModule(p, config);
    for (unsigned frame = 0; frame < 4; ++frame) {
        for (unsigned draw = 0; draw < 16; ++draw) DrawCpu(p, info, config);
        Advance(p);
    }
    Require(p.ready_vertex_worker->tasks.size() == 1, "one qualified CPU build queued");
    p.ready_vertex_worker->WaitForRequests();
    Advance(p);
    auto token = DrawCpu(p, info, config);
    Require(bool(token), "ready CPU selection yields draw-local token");
    auto* owner = p.ready_cpu_bank->AllSlots()[token.slot].owner.get();
    Require(owner && owner->IsDone() && !owner->HasFailed(), "selected owner usable");
    return owner;
}

void RealSelectionAndTransport() {
    PipelineCache p;
    auto info = Info();
    auto config = Config();
    auto* special = QualifyAndBuild(p, info, config);
    Advance(p);
    auto* generic = p.GetTevFallback(info, true);
    p.scheduler.completed.clear();
    p.tev_constants.buffer_mask = 11;
    p.offsets = {1, 2, 3};
    auto layout = SoftwareLayout();
    p.Fresh(config);
    // No token means no optional owner may be borrowed by an unknown caller.
    Require(p.BindPipeline(info, true, nullptr, true, nullptr, &layout, nullptr), "no-token generic binds");
    p.tev_constants.buffer_mask = 22;
    p.offsets = {4, 5, 6};
    p.scheduler.MakeDirty(StateFlags::FragmentConstants);
    auto token = DrawCpu(p, info, config);
    Require(bool(token), "specialized middle draw");
    p.tev_constants.buffer_mask = 11;
    p.offsets = {7, 8, 9};
    p.Fresh(config);
    Require(p.BindPipeline(info, true, nullptr, true, nullptr, &layout, nullptr), "same-A generic binds");
    Advance(p);
    const auto& rows = p.scheduler.completed;
    Require(rows.size() == 4, "three binds plus actual draw snapshots");
    Require(rows[0].pipeline == generic->Handle() && rows[1].pipeline == special->Handle() &&
            rows[2].pipeline == special->Handle() && rows[3].pipeline == generic->Handle(),
            "actual G/S/G pipeline identity");
    Require(rows[0].offsets == std::array<u32,3>{1,2,3} &&
            rows[1].offsets == std::array<u32,3>{4,5,6} &&
            rows[3].offsets == std::array<u32,3>{7,8,9}, "queued offsets immutable");
    Require(rows[1].pushes == rows[0].pushes && rows[3].pushes == rows[0].pushes + 1,
            "specialized dirty state invalidates same-A generic constants");
    Require(Probe::optional_waits == 0, "selection adds no optional wait");

    // Existing ready selections survive an exhausted creation budget.
    p.ready_optional_attempts = 256;
    const auto builds = Probe::pipeline_builds;
    auto retained = DrawCpu(p, info, config);
    Require(bool(retained) && Probe::pipeline_builds == builds,
            "exhausted creation budget retains ready hit");
    const auto observations = p.ready_cpu_bank->stats.observations;
    const auto retirements = p.ready_cpu_bank->stats.retirements;
    for (unsigned n = 0; n < 20; ++n) { DrawCpu(p, info, Config(1)); Advance(p); }
    Require(p.ready_cpu_bank->stats.retirements == retirements,
            "no destruction without replacement creation token");
    Require(p.ready_cpu_bank->stats.observations == observations,
            "terminal work budget avoids new observation key preparation");
}

void ActualBankReuse() {
    PipelineCache p;
    auto info = Info();
    std::array<FSConfig, 8> configs{Config(0),Config(1),Config(2),Config(3),
                                   Config(4),Config(5),Config(6),Config(7)};
    for (auto& c : configs) SeedModule(p,c);
    for (unsigned k = 0; k < 8; ++k) {
        for (unsigned frame = 0; frame < 4; ++frame) {
            for (unsigned old = 0; old < k; ++old) DrawCpu(p, info, configs[old]);
            for (unsigned d = 0; d < 16; ++d) DrawCpu(p, info, configs[k]);
            Advance(p);
        }
        p.ready_vertex_worker->WaitForRequests(); Advance(p);
    }
    Require(p.ReadyCpuOwned() == 8 && p.ready_optional_attempts == 8, "eight accounted owners");
    auto token = DrawCpu(p, info, configs[7], true);
    Require(bool(token), "actual populated bank selected");
    auto& slot = p.ready_cpu_bank->AllSlots()[token.slot];
    Require(slot.last_use_tick == p.scheduler.tick, "actual after-Map draw tick retained");
    const auto later_tick = p.scheduler.tick;
    p.scheduler.known = later_tick - 1;
    // Refresh seven residents; the eighth becomes the only stale replacement.
    for (unsigned f = 0; f < 14; ++f) {
        for (unsigned k=0;k<7;++k) DrawCpu(p, info, configs[k]);
        for (unsigned d=0;d<16;++d) DrawCpu(p, info, Config(8));
        Advance(p, false);
    }
    Require(slot.state == Vulkan::AdaptiveCpu::State::Retired && slot.owner &&
            p.ready_vertex_worker->tasks.empty(), "GPU-incomplete actual draw keeps retired owner");
    Require(p.ReadyCpuOwned() == 8, "retired capacity included");
    Require(p.warming_ready_vertex != slot.owner.get(),
            "retirement clears shared warming raw pointer");
    Advance(p);
    Require(slot.state == Vulkan::AdaptiveCpu::State::DestroyQueued && p.ReadyCpuOwned() == 8,
            "actual frame queues deletion without freeing capacity");
    auto stages = p.current_shaders;
    p.Fresh(Config(8));
    SeedModule(p, Config(8));
    const auto scheduled = p.ready_vertex_worker->scheduled;
    for (unsigned d=0;d<32;++d) DrawCpu(p, info, Config(8));
    Require(p.ready_vertex_worker->scheduled == scheduled && p.ReadyCpuOwned() == 8,
            "no new creation behind pending destruction");
    p.virtual_fs_config.reset();
    p.current_shaders={&p.other_shader,&p.generic_shader,nullptr};
    p.shader_hashes={99,88,0};
    Require(!p.PrepareReadyGpuVertex(info) && p.ready_vertex_pipelines.empty() &&
            p.ready_vertex_worker->scheduled==scheduled,
            "GPU shares CPU destruction pending slot");
    p.ready_vertex_worker->WaitForRequests();
    Advance(p);
    Require(p.ReadyCpuOwned() == 7, "actual destruction completion releases exactly one slot");
    for (unsigned d=0;d<32;++d) DrawCpu(p,info,Config(8));
    Require(p.ready_vertex_worker->tasks.size() == 1 && p.ReadyCpuOwned() == 8,
            "new observed family replaces only completed owner");
    p.ready_vertex_worker->WaitForRequests(); Advance(p);
    Require(bool(DrawCpu(p,info,Config(8))), "new family eventually selected automatically");
    Require(p.ready_cpu_bank->stats.max_owned == 8 && p.ready_optional_attempts == 9,
            "fixed physical cap plus cumulative creation accounting");
}

void FailuresProfilesAndModes() {
    for (unsigned failure=1; failure<=4; ++failure) {
        PipelineCache p; auto info=Info(); auto config=Config(); SeedModule(p,config);
        Probe::build_failure = failure;
        for (unsigned f=0;f<4;++f) {
            for(unsigned d=0;d<16;++d) DrawCpu(p,info,config);
            Advance(p);
        }
        p.ready_vertex_worker->WaitForRequests(); Advance(p);
        Probe::build_failure = 0;
        const auto attempts=p.ready_optional_attempts;
        for(unsigned d=0;d<64;++d) Require(!DrawCpu(p,info,config), "failed key renders full generic");
        Require(p.ready_optional_attempts==attempts && attempts==1, "failed hot key no retries");
    }
    {
        PipelineCache p; auto info=Info(); auto config=Config();
        auto* owner=QualifyAndBuild(p,info,config); Advance(p);
        auto changed=info; changed.state.vertex_layout.bindings[0].byte_count.Assign(96);
        Require(!DrawCpu(p,changed,config), "wrong software ABI rejects promotion");
        const auto saved=owner->info;
        owner->forged_key=owner->ActualKey();
        owner->info.state.vertex_layout.attributes[0].offset.Assign(4);
        Require(!DrawCpu(p,info,config), "hash collision cannot replace actual execution equality");
        owner->info=saved; owner->forged_key.reset();
        const auto attempts=p.ready_optional_attempts;
        p.profile.enable_accurate_mul=1;
        Require(!DrawCpu(p,info,config), "live profile mismatch stays generic");
        Require(p.ready_optional_attempts==attempts,"profile mismatch cannot alias old owner");
    }
    for (unsigned mode=0;mode<2;++mode) {
        PipelineCache p; auto info=Info(); auto config=Config();
        p.ready_cpu_bank.reset();
        if(mode==0) p.ready_vertex_worker.reset();
        else p.allow_specialized_fragments=false;
        for(unsigned n=0;n<20;++n) { DrawCpu(p,info,config); Advance(p); }
        Require(p.ready_cpu_requests==0 && p.ready_optional_attempts==0,
                "control modes have no adaptive metadata work");
    }
}

// CodexAstraLocal: Execute actual GPU admission against CPU physical ownership
// and lifetime budgets, preserving ready hits and reciprocal pending behavior.
void SharedLimitsAndAllocation() {
    {
        PipelineCache p; auto info=Info(); auto config=Config();
        auto* cpu=QualifyAndBuild(p,info,config); Advance(p);
        for(unsigned n=0;n<255;++n)
            p.ready_vertex_pipelines.emplace(10000+n,std::make_unique<GraphicsPipeline>(
                p.instance,p.renderpass_cache,info,*p.driver_pipeline_cache,*p.pipeline_layout,
                p.current_shaders,p.ready_vertex_worker.get()));
        p.virtual_fs_config.reset();
        p.current_shaders={&p.other_shader,&p.generic_shader,nullptr};
        p.shader_hashes={99,88,0};
        const auto before=p.ready_vertex_worker->scheduled;
        Require(!p.PrepareReadyGpuVertex(info) && p.ready_vertex_pipelines.size()==255 &&
                p.ready_vertex_worker->scheduled==before,"combined physical cap includes CPU owner");
        auto token=DrawCpu(p,info,config);
        Require(bool(token) && p.ready_cpu_bank->AllSlots()[token.slot].owner.get()==cpu,
                "combined physical saturation preserves CPU ready hit");
    }
    {
        PipelineCache p; auto info=Info();
        p.virtual_fs_config.reset();
        p.current_shaders={&p.other_shader,&p.generic_shader,nullptr};
        p.shader_hashes={99,88,0};
        Require(!p.PrepareReadyGpuVertex(info) && p.ready_optional_attempts==1,
                "GPU attempt charged before worker admission");
        p.ready_vertex_worker->WaitForRequests();
        auto* ready=p.PrepareReadyGpuVertex(info);
        Require(ready!=nullptr,"GPU control ready hit");
        p.ready_optional_attempts=256;
        Require(p.PrepareReadyGpuVertex(info)==ready,"GPU ready hit survives cumulative exhaustion");
        auto changed=info;changed.state.blending.color_write_mask=7;
        Require(!p.PrepareReadyGpuVertex(changed) && p.ready_vertex_pipelines.size()==1,
                "GPU creation respects nonrefundable combined budget");
    }
    // The exact helper must charge a failed real operator-new allocation before
    // constructing an owner, then retain no-retry metadata and the generic draw.
    {
        PipelineCache p;auto info=Info();auto config=Config();SeedModule(p,config);
        for(unsigned frame=0;frame<3;++frame) {
            for(unsigned d=0;d<16;++d)DrawCpu(p,info,config);
            Advance(p);
        }
        p.Fresh(config);auto* generic=p.GetTevFallback(info,true);auto layout=SoftwareLayout();
        PipelineCache::CpuFragmentToken token{};
        AllocationProbe::Start(1);
        const auto* result=p.PrepareReadyCpuFragment(info,generic,layout,&token);
        const auto allocations=AllocationProbe::Stop();
        Require(!result && !token && allocations==1 && p.ready_optional_attempts==1 &&
                p.ReadyCpuOwned()==1,"failed real allocation consumes bounded attempt and slot");
        for(unsigned d=0;d<64;++d)Require(!DrawCpu(p,info,config),"allocation failure retains generic");
        Require(p.ready_optional_attempts==1 && p.ready_vertex_worker->tasks.empty(),
                "allocation failure cannot retry or queue");
    }
}


// CodexAstraLocal: Retain the previous binding/state regression population while
// respecting adaptive probation; no compatibility shim bypasses admission.
GraphicsPipeline* WarmPipeline(PipelineCache& p, PipelineInfo& info,const FSConfig& config) {
    auto layout=SoftwareLayout(); PipelineCache::CpuFragmentToken token{};
    if(!p.curr_disk_cache->ready_fragments.contains(config.Hash())) SeedModule(p,config);
    for(unsigned frame=0;frame<8 && p.ready_vertex_worker->tasks.empty();++frame) {
        for(unsigned draw=0;draw<16;++draw) {
            p.Fresh(config);
            Require(!p.PrepareReadyCpuFragment(info,p.GetTevFallback(info,true),layout,&token),
                    "warm-up remains generic until worker completion");
        }
        if(p.ready_vertex_worker->tasks.empty()) Advance(p);
    }
    Require(p.ready_vertex_worker->tasks.size()==1,"one probated warm-up build");
    p.ready_vertex_worker->WaitForRequests();
    p.Fresh(config);
    auto* result=p.PrepareReadyCpuFragment(info,p.GetTevFallback(info,true),layout,&token);
    Require(result && bool(token),"completed warm-up selects exact owner");
    return result;
}


void BindTransportAndModes() {
  PipelineCache::CpuFragmentToken token{};
  auto layout = SoftwareLayout();
  auto c = Config();
  for (auto mode :
       {Settings::UberharTestMode::Native, Settings::UberharTestMode::Compute,
        Settings::UberharTestMode::ComboGeneric,
        Settings::UberharTestMode::Custom}) {
    PipelineCache p;
    Settings::values.uberhar_test_mode.value = mode;
    p.allow_specialized_fragments = false;
    p.allow_static_cpu_tev = mode==Settings::UberharTestMode::Native;
    p.Fresh(c);
    auto i = Info();
    Require(p.BindPipeline(i, true, nullptr, true, nullptr, &layout, &token),
            "control mode draws");
    p.scheduler.Finish();
    Require(p.ready_cpu_requests == (mode==Settings::UberharTestMode::Native ? 1U : 0U) &&
                p.curr_disk_cache->ready_fragment_requests == 0,
            "only Native control has partial CPU demand");
  }
  Settings::values.uberhar_test_mode.value =
      Settings::UberharTestMode::Automatic;
  {
    PipelineCache p;
    auto i = Info();
    p.Fresh(c);
    Require(p.BindPipeline(i, true), "null ABI token draws generic");
    p.scheduler.Finish();
    Require(p.ready_cpu_requests == 0, "null token no optional request");
  }
  for (bool dynamic : {false, true}) {
    PipelineCache p;
    p.instance.dynamic = dynamic;
    auto info = Info();
    auto *special = WarmPipeline(p, info, c);
    p.scheduler.completed.clear();
    auto *generic = p.GetTevFallback(info, true);
    auto call = [&](unsigned n, bool ready, bool dirty) {
      auto x = Info();
      p.Fresh(c);
      if (!ready)
        p.allow_specialized_fragments = false;
      else
        p.allow_specialized_fragments = true;
      p.tev_constants = {};
      p.tev_constants.framebuffer = n;
      p.offsets = {n, n + 1, n + 2};
      for (unsigned k = 0; k < 3; ++k)
        p.bound_descriptor_sets[k] =
            vk::DescriptorSet{reinterpret_cast<VkDescriptorSet>(
                static_cast<std::uintptr_t>(n + k + 100))};
      x.dynamic_info.viewport.left = n;
      x.dynamic_info.viewport.right = n + 800;
      x.dynamic_info.scissor.left = n;
      x.dynamic_info.scissor.right = n + 800;
      if (dirty)
        p.scheduler.MakeDirty(StateFlags::FragmentConstants);
      Require(p.BindPipeline(x, true, nullptr, true, nullptr, &layout, &token),
              "actual bind records one complete route");
      return x;
    };
    call(10, false, false);
    call(20, true, false);
    call(30, false, false);
    p.tev_constants.framebuffer = 999;
    p.offsets = {999, 999, 999};
    p.bound_descriptor_sets = {};
    p.scheduler.Finish();
    Require(p.scheduler.completed.size() == 3,
            "three immutable queued commands");
    const auto &a = p.scheduler.completed[0];
    const auto &b = p.scheduler.completed[1];
    const auto &z = p.scheduler.completed[2];
    // CodexAstraLocal: Unique modeled handles catch wrong selection even when
    // counters say specialized.
    Require(a.pipeline == generic->Handle() &&
                b.pipeline == special->Handle() &&
                z.pipeline == generic->Handle() && b.pipeline != a.pipeline,
            "actual G/S/G pipeline identity recorded");
    Require(a.offsets[0] == 10 && b.offsets[0] == 20 && z.offsets[0] == 30,
            "per-draw UBO offset snapshots");
    Require(a.descriptors[0] != b.descriptors[0] &&
                b.descriptors[0] != z.descriptors[0],
            "per-draw descriptor snapshots");
    Require(a.pushes == 1 && b.pushes == 1 && z.pushes == 2,
            "only generic draws push constants");
    Require(a.viewport.x == 10 && b.viewport.x == 20 && z.viewport.x == 30,
            "per-draw viewport snapshots");
    Require(p.ready_cpu_selected == 1 && p.virtual_generic_draws == 2 &&
                p.virtual_specialized_gpu_draws == 0,
            "exclusive CPU-specialized counters");
    Require(p.pipeline_waits == 0 && Probe::optional_waits == 0,
            "selected CPU pipeline no optional wait");
    auto at = p.scheduler.completed.size();
    call(30, false, false);
    call(40, true, true);
    call(30, false, false);
    p.scheduler.Finish();
    Require(p.scheduler.completed[at].pushes == 2 &&
                p.scheduler.completed[at + 1].pushes == 2 &&
                p.scheduler.completed[at + 2].pushes == 3,
            "specialization dirty invalidates same generic constants");
    // CodexAstraLocal: Source-level reset is executed, not merely assumed by
    // Fresh.
    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    p.UseFragmentShader(regs, {}, false);
    Require(p.virtual_fs_config.has_value() &&
                p.current_shaders[1] == nullptr && p.shader_hashes[1] == 0,
            "real UseFragmentShader clears prior specialized FS identity");
    auto changed = Info();
    changed.state.depth_stencil.depth_compare_op.Assign(
        Pica::FramebufferRegs::CompareFunc::GreaterThan);
    p.Fresh(c);
    p.allow_specialized_fragments = true;
    auto *g = p.GetTevFallback(changed, true);
    auto *same = p.PrepareReadyCpuFragment(changed, g, layout, &token);
    Require((same == special) == dynamic,
            "dynamic key normalized, static key distinct");
  }
  for (int failure : {2, 3}) {
    PipelineCache p;
    auto i = Info();
    p.Fresh(c);
    p.generic_mode = failure;
    Require(p.BindPipeline(i, true, nullptr, true, nullptr, &layout, &token),
            "required generic failure accurate recovery");
    p.scheduler.Finish();
    Require(p.virtual_recovery_draws == 1 && p.ready_cpu_requests == 0 &&
                p.skipped_draws == 0,
            "no optional admission or skipped recovery draw");
  }
  {
    PipelineCache p;
    auto i = Info();
    p.Fresh(c);
    p.profile.has_fragment_shader_barycentric = true;
    Require(p.BindPipeline(i, true, nullptr, true, nullptr, &layout, &token),
            "untested barycentric CPU ABI remains generic");
    p.scheduler.Finish();
    Require(p.curr_disk_cache->ready_fragment_requests == 0,
            "no unproved barycentric module demand");
  }
  {
    PipelineCache p;
    auto i = Info();
    p.Fresh(c);
    p.generic_mode = 1;
    auto n = Probe::required_waits;
    Require(p.BindPipeline(i, true, nullptr, true, nullptr, &layout, &token),
            "required cold generic completes");
    p.scheduler.Finish();
    Require(Probe::required_waits == n + 1 && p.ready_cpu_requests == 1,
            "only required generic waited before optional demand");
  }
}

void KeyAndDynamicState() {
  PipelineCache::CpuFragmentToken token{};
  // CodexAstraLocal: Distinguish admitted bank occupancy from successful draw
  // weighting.
  {
    PipelineCache p;
    auto unlit = Config();
    auto lit = Config(1);
    lit.lighting.enable.Assign(1);
    auto info = Info();
    auto layout = SoftwareLayout();
    WarmPipeline(p, info, unlit);
    WarmPipeline(p, info, lit);
    // CodexAstraLocal: The production report snapshots this same fixed metadata;
    // no test or reporter inspects a destruction-queued unique owner.
    std::array<u64,2> owned_lighting{};
    for(const auto& slot:p.ready_cpu_bank->AllSlots())
      if(slot.state!=Vulkan::AdaptiveCpu::State::Empty)++owned_lighting[slot.lit];
    Require(owned_lighting == std::array<u64, 2>{1, 1},
            "bank occupancy records lit and unlit admissions");
    for (const auto &c : {unlit, lit, lit}) {
      p.Fresh(c);
      Require(p.BindPipeline(info, true, nullptr, true, nullptr, &layout, &token),
              "lit/unlit ready draw");
    }
    p.scheduler.Finish();
    Require(p.ready_cpu_selected_lighting == std::array<u64, 2>{1, 2},
            "selection weights lit/unlit draws separately");
    p.ClearTevFallbacks();
    Require(p.ready_cpu_bank_lighting == std::array<u64, 2>{0, 0},
            "drained bank occupancy resets");
  }
  auto layout = SoftwareLayout();
  auto config = Config();
  for (bool dynamic : {false, true})
    for (unsigned changed = 0; changed < 8; ++changed) {
      PipelineCache p;
      p.instance.dynamic = dynamic;
      auto info = Info();
      auto *base = WarmPipeline(p, info, config);
      auto x = info;
      switch (changed) {
      case 0:
        x.state.attachments.color = VideoCore::PixelFormat::RGB8;
        break;
      case 1:
        x.state.attachments.depth = VideoCore::PixelFormat::D16;
        break;
      case 2:
        x.state.blending.color_write_mask = 7;
        break;
      case 3:
        x.state.blending.blend_enable = 0;
        break;
      case 4:
        x.state.depth_stencil.depth_compare_op.Assign(
            Pica::FramebufferRegs::CompareFunc::GreaterThan);
        break;
      case 5:
        x.state.depth_stencil.depth_write_enable.Assign(1);
        break;
      case 6:
        x.state.rasterization.cull_mode.Assign(
            Pica::RasterizerRegs::CullMode::KeepClockWise);
        break;
      case 7:
        x.state.rasterization.flip_viewport.Assign(1);
        break;
      }
      p.Fresh(config);
      auto *actual =
          p.PrepareReadyCpuFragment(x, p.GetTevFallback(x, true), layout, &token);
      Require((actual == base) == (dynamic && changed >= 4),
              "exact state key separates static and normalized dynamic fields");
      if (!actual) {
        Require(p.ReadyCpuOwned()==1 && p.ready_vertex_worker->tasks.empty(),
                "static miss cannot bypass probation");
        auto* distinct=WarmPipeline(p,x,config);
        Require(distinct!=base && p.ReadyCpuOwned()==2,
                "probated static state admits distinct stable PSO");
      }
    }
  {
    PipelineCache p;
    p.instance.dynamic = true;
    auto info = Info();
    WarmPipeline(p, info, config);
    for (unsigned n = 1; n <= 3; ++n) {
      auto x = Info();
      p.Fresh(config);
      x.dynamic_info.stencil_compare_mask = 11 * n;
      x.dynamic_info.stencil_write_mask = 13 * n;
      x.dynamic_info.stencil_reference = 17 * n;
      x.dynamic_info.blend_color = 0x01010101u * n;
      x.state.depth_stencil.depth_compare_op.Assign(
          static_cast<Pica::FramebufferRegs::CompareFunc>(n + 2));
      x.state.depth_stencil.depth_write_enable.Assign(n & 1);
      x.state.depth_stencil.depth_test_enable.Assign((n + 1) & 1);
      x.state.depth_stencil.stencil_test_enable.Assign(n & 1);
      x.state.depth_stencil.stencil_fail_op.Assign(
          static_cast<Pica::FramebufferRegs::StencilAction>(n));
      x.state.depth_stencil.stencil_pass_op.Assign(
          static_cast<Pica::FramebufferRegs::StencilAction>(n + 1));
      x.state.depth_stencil.stencil_depth_fail_op.Assign(
          static_cast<Pica::FramebufferRegs::StencilAction>(n + 2));
      x.state.depth_stencil.stencil_compare_op.Assign(
          static_cast<Pica::FramebufferRegs::CompareFunc>(n + 1));
      x.state.rasterization.cull_mode.Assign(
          static_cast<Pica::RasterizerRegs::CullMode>(n - 1));
      x.state.rasterization.flip_viewport.Assign(n & 1);
      x.dynamic_info.viewport = {n, n + 4, n + 804, n + 484};
      x.dynamic_info.scissor = {n + 1, n + 5, n + 800, n + 480};
      Require(p.BindPipeline(x, true, nullptr, true, nullptr, &layout, &token),
              "dynamic state specialized complete draw");
    }
    p.current_info = {};
    p.tev_constants = {};
    p.scheduler.Finish();
    Require(p.scheduler.completed.size() == 3 &&
                p.ReadyCpuOwned() == 1,
            "dynamic normalized PSO reused for three draws");
    for (unsigned n = 1; n <= 3; ++n) {
      const auto &x = p.scheduler.completed[n - 1];
      Require(x.stencil_compare == 11 * n && x.stencil_write == 13 * n &&
                  x.stencil_reference == 17 * n,
              "queued stencil masks/reference immutable");
      Require(std::equal(x.blend.begin(), x.blend.end(),
                         PicaToVK::ColorRGBA8(0x01010101u * n).AsArray()),
              "queued blend constants immutable");
      Require(
          x.depth ==
                  PicaToVK::CompareFunc(
                      static_cast<Pica::FramebufferRegs::CompareFunc>(n + 2)) &&
              x.depth_write == bool(n & 1) && x.depth_test == bool((n + 1) & 1),
          "queued depth state immutable");
      Require(
          x.stencil_test == bool(n & 1) &&
              x.stencil_fail ==
                  PicaToVK::StencilOp(
                      static_cast<Pica::FramebufferRegs::StencilAction>(n)) &&
              x.stencil_pass ==
                  PicaToVK::StencilOp(
                      static_cast<Pica::FramebufferRegs::StencilAction>(n +
                                                                        1)) &&
              x.stencil_depth_fail ==
                  PicaToVK::StencilOp(
                      static_cast<Pica::FramebufferRegs::StencilAction>(n +
                                                                        2)) &&
              x.stencil_func ==
                  PicaToVK::CompareFunc(
                      static_cast<Pica::FramebufferRegs::CompareFunc>(n + 1)),
          "queued stencil operations immutable");
      Require(x.cull == PicaToVK::CullMode(
                            static_cast<Pica::RasterizerRegs::CullMode>(n - 1),
                            n & 1) &&
                  x.front ==
                      PicaToVK::FrontFace(
                          static_cast<Pica::RasterizerRegs::CullMode>(n - 1)),
              "queued raster orientation immutable");
      Require(x.viewport.x == n && x.scissor.offset.x == n + 1 && x.pushes == 0,
              "CPU specialization preserves dynamic rectangles without generic "
              "push");
    }
  }
}

int main() {
    try {
        RealSelectionAndTransport(); ActualBankReuse(); FailuresProfilesAndModes();
        SharedLimitsAndAllocation();
        BindTransportAndModes(); KeyAndDynamicState();
        Require(Probe::optional_waits==0 && Probe::lifetime_errors==0,
                "no optional waits or borrowed shader destruction");
        std::cout << "PASS checks=" << Probe::checks << " builds=" << Probe::pipeline_builds
                  << " optional_waits=" << Probe::optional_waits << '\n';
        return 0;
    } catch(const std::exception& e) {
        std::cerr << "FAIL " << e.what() << " checks=" << Probe::checks << '\n'; return 1;
    }
}
