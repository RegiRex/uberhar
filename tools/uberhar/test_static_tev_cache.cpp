#include "static_tev_cache_fixture.h"

// CodexAstraLocal: These controls execute actual selection, probation, retirement
// and recorded bindings. Driver creation is modeled; temporal's separate shader
// proof owns pixel/depth parity and no host control here measures throughput.
void SeedPartial(PipelineCache& p, const FSConfig& config) {
    const auto family = GLSL::MakeDynamicTevFamilyConfig(config, p.profile);
    const auto plan = GLSL::MakeStaticTevPlan(GLSL::MakeDynamicTevState(config, p.profile));
    StaticTevPolicy::Key key{family, p.profile, plan};
    auto entry = std::make_unique<ShaderDiskCache::StaticTevEntry>(p.instance, key);
    entry->shader.module = vk::ShaderModule{reinterpret_cast<VkShaderModule>(++Probe::next_handle)};
    entry->shader.MarkDone();
    p.curr_disk_cache->static_tev_fragments.emplace(StaticTevPolicy::Key::Hash(family, plan), std::move(entry));
}
GraphicsPipeline* Warm(PipelineCache& p, bool partial, const FSConfig& config = Config()) {
    // CodexAstraLocal: Tier tests toggle only CPU-fragment capability.
    p.allow_full_cpu_fragments = !partial;
    if (partial) SeedPartial(p, config); else SeedModule(p, config);
    for (unsigned frame=0; frame<4; ++frame) {
        for (unsigned draw=0; draw<16; ++draw) DrawCpu(p, Info(), config);
        Advance(p);
    }
    Require(p.ready_vertex_worker->tasks.size()==1, "one qualified tier build queued");
    p.ready_vertex_worker->WaitForRequests(); Advance(p);
    auto token=DrawCpu(p,Info(),config);
    Require(bool(token), "qualified tier selects actual owner");
    auto* owner=p.ready_cpu_bank->AllSlots()[token.slot].owner.get();
    Require(owner && owner->IsDone() && !owner->HasFailed(), "qualified owner ready");
    return owner;
}

void PromotionAndPriority() {
    PipelineCache p;
    auto* partial=Warm(p,true);
    p.allow_full_cpu_fragments=true;
    bool pending_full_selected_partial=false;
    for(unsigned frame=0;frame<14;++frame) {
        for(unsigned draw=0;draw<32;++draw) {
            const auto token=DrawCpu(p,Info(),Config());
            Require(bool(token), "partial remains complete during full advancement");
            if (!p.ready_vertex_worker->tasks.empty() &&
                p.ready_cpu_bank->AllSlots()[token.slot].owner.get()==partial)
                pending_full_selected_partial=true;
        }
        p.ready_vertex_worker->WaitForRequests(); Advance(p);
    }
    Require(p.ready_cpu_bank->Owned(0)==1 && p.ready_cpu_bank->Owned(1)==1,
            "CPU demand still promotes full while partial ready");
    Require(pending_full_selected_partial, "pending full never blocks ready partial");
    const auto before=p.ready_cpu_full_selected;
    auto token=DrawCpu(p,Info(),Config());
    Require(bool(token) && p.ready_cpu_bank->AllSlots()[token.slot].owner.get()!=partial &&
            p.ready_cpu_full_selected==before+1, "exact ready full wins priority");
    Require(Probe::optional_waits==0, "promotion adds no optional wait");
}

// CodexAstraLocal: Start with both modules absent so the partial tier actually
// bridges a cold CPU draw, then require the inherited full route to take over.
void ColdBridge() {
    PipelineCache p;
    bool partial_first{},partial_draw{},full_draw{};
    for(unsigned frame=0;frame<24;++frame) {
        for(unsigned draw=0;draw<32;++draw) {
            DrawCpu(p,Info(),Config());
            partial_first|=!p.curr_disk_cache->static_tev_fragments.empty() &&
                            p.curr_disk_cache->ready_fragments.empty();
            partial_draw|=p.ready_cpu_static_selected!=0;
            full_draw|=p.ready_cpu_full_selected!=0;
        }
        p.ready_vertex_worker->WaitForRequests();Advance(p);
    }
    Require(partial_first && partial_draw && full_draw,
            "cold partial bridge preserves later full promotion");
}

// CodexAstraLocal: Consume real Reserve/failed/retire/destroy transitions with
// synthetic unique keys. No private counter mutation can bypass an admission.
CpuFragmentObservation PolicyKey(bool partial, unsigned id) {
    auto state=Info().state; state.shader_ids[0]=id+1;
    auto fs=Config(); Profile profile{};
    return {state,fs,profile,false,partial ? std::optional{GLSL::StaticTevPlan{}} : std::nullopt};
}
bool Spend(PipelineCache& p, bool partial, unsigned id, unsigned max_frames=32) {
    auto key=PolicyKey(partial,id);
    const u64 hash=1000+id;
    for(unsigned f=0;f<max_frames;++f) {
        for(unsigned n=0;n<16;++n) p.ready_cpu_bank->Observe(hash,key);
        if (auto* slot=p.ready_cpu_bank->Reserve(hash,key,false,0,p.ready_optional_attempts,false)) {
            p.ready_cpu_bank->AllocationFailed(*slot);
            return true;
        }
        Advance(p); p.ready_vertex_worker->WaitForRequests();
    }
    return false;
}
void PartitionPressure() {
    {
        PipelineCache p;
        const auto full=PolicyKey(false,1),partial=PolicyKey(true,1);
        bool full_ready{},partial_ready{};
        for(unsigned frame=0;frame<4;++frame) {
            for(unsigned n=0;n<16;++n) {
                full_ready=p.ready_cpu_bank->Observe(7,full);
                partial_ready=p.ready_cpu_bank->Observe(7,partial);
            }
            if(frame!=3)Advance(p);
        }
        Require(full_ready && partial_ready,
                "stable cross-tier bucket collision preserves both probation histories");
    }
    {
        PipelineCache p;
        for(unsigned i=0;i<8;++i) Require(Spend(p,false,i), "fill full partition");
        Require(p.ready_cpu_bank->Owned(0)==8 && p.ready_cpu_bank->Owned(1)==0,
                "eight full slots independent of empty partial bank");
        Require(Spend(p,false,8), "full partition pressure permits ninth full replacement");
        Require(p.ready_cpu_bank->Owned(0)==8 && p.ready_cpu_bank->Owned(1)==0 &&
                p.ready_cpu_bank->Attempts(0)==9, "same full partition retains bound");
    }
    {
        PipelineCache p;
        for(unsigned i=0;i<8;++i) Require(Spend(p,true,100+i), "fill partial partition");
        Require(Spend(p,true,108) && p.ready_cpu_bank->Owned(1)==8,
                "partial ninth owner replaces inside eight-owner cap");
        for(unsigned i=0;i<8;++i) Require(Spend(p,false,200+i), "fill full beside partial");
        const auto partial_owned=p.ready_cpu_bank->Owned(1);
        Require(Spend(p,false,208), "full replacement beside older partial owners");
        Require(p.ready_cpu_bank->Owned(1)==partial_owned && p.ready_cpu_bank->Owned(0)==8,
                "full pressure never retires partial owner");
        Require(p.ready_cpu_bank->stats.max_owned==16, "both bounded partitions include retirement");
    }
}
void TerminalBudgetsKeepHits() {
    PipelineCache p;
    auto* full=Warm(p,false);
    auto* partial=Warm(p,true);
    for(unsigned i=0;i<63;++i) Require(Spend(p,false,1000+i), "full lifetime allowance preserved");
    Require(p.ready_cpu_bank->Attempts(0)==64 && p.ready_cpu_bank->Attempts(1)==1,
            "partial cannot consume full attempts");
    p.allow_full_cpu_fragments=true;
    auto token=DrawCpu(p,Info(),Config());
    Require(bool(token) && p.ready_cpu_bank->AllSlots()[token.slot].owner.get()==full,
            "full ready hit survives 64 full attempts");
    for(unsigned i=0;i<15;++i) Require(Spend(p,true,2000+i), "partial lifetime allowance independent");
    Require(p.ready_cpu_bank->Attempts()==80 && p.ready_cpu_bank->Attempts(1)==16,
            "separate terminal work budgets close");
    p.allow_full_cpu_fragments=false;
    token=DrawCpu(p,Info(),Config());
    Require(bool(token) && p.ready_cpu_bank->AllSlots()[token.slot].owner.get()==partial,
            "partial ready hit survives 16 partial attempts");
    Require(!p.ready_cpu_bank->AcceptingDemand(p.ready_optional_attempts,0) &&
            !p.ready_cpu_bank->AcceptingDemand(p.ready_optional_attempts,1),
            "both budgets deny only new demand");
    p.allow_full_cpu_fragments=true;
    token=DrawCpu(p,Info(),Config());
    Require(bool(token) && p.ready_cpu_bank->AllSlots()[token.slot].owner.get()==full,
            "partial exhaustion cannot hide full ready hit");
}

void PartialTransportAndIdentity() {
    PipelineCache p; auto config=Config(); auto* partial=Warm(p,true,config);
    Advance(p);p.scheduler.completed.clear();
    auto layout=SoftwareLayout(); auto info=Info();
    std::array<GLSL::DynamicTevState,3> values{};
    for(unsigned n=0;n<3;++n) {
        p.Fresh(config);
        p.tev_constants.texture^=u32(n)<<7;
        p.tev_constants.framebuffer^=u32(n)<<3;
        p.tev_constants.lighting_luts_lo^=n*0x101;
        values[n]=p.tev_constants;
        p.offsets={n,n+1,n+2};
        PipelineCache::CpuFragmentToken token;
        Require(p.BindPipeline(info,true,nullptr,true,nullptr,&layout,&token) && token,
                "runtime-only values select partial");
    }
    p.scheduler.WaitWorker();
    Require(p.scheduler.completed.size()==3, "three immutable partial bindings");
    for(unsigned n=0;n<3;++n) {
        const auto& row=p.scheduler.completed[n];
        Require(row.pipeline==partial->Handle() &&
                std::memcmp(row.constants.data(),&values[n],128)==0 && row.offsets[0]==n,
                "partial retains exact per-draw runtime transport");
    }
    auto& disk=*p.curr_disk_cache;
    const auto family=GLSL::MakeDynamicTevFamilyConfig(config,p.profile);
    const auto plan=GLSL::MakeStaticTevPlan(GLSL::MakeDynamicTevState(config,p.profile));
    auto& entry=*disk.static_tev_fragments.at(StaticTevPolicy::Key::Hash(family,plan));
    auto original=entry.key;
    for(unsigned field=0;field<5;++field) {
        entry.key=original;
        switch(field) {
        case 0:entry.key.plan.stages[5].sources^=1;break;
        case 1:entry.key.family.texture.fog_flip.Assign(!entry.key.family.texture.fog_flip);break;
        case 2:entry.key.profile.enable_accurate_mul=!entry.key.profile.enable_accurate_mul;break;
        case 3:++entry.key.interface_abi;break;
        case 4:++entry.key.policy;break;
        }
        Require(!disk.UseStaticTevFragmentShader(family,plan,{},false),
                "exact partial key rejects same-hash aliases");
    }
    entry.key=original;
    Require(disk.UseStaticTevFragmentShader(family,plan,{},false).has_value(),
            "exact partial key remains reusable");
}

void FourRouteBindings() {
    PipelineCache p;auto* partial=Warm(p,true);auto* full=Warm(p,false);
    Advance(p);p.scheduler.completed.clear();
    const auto config=Config();const auto layout=SoftwareLayout();auto info=Info();
    std::array<GLSL::DynamicTevState,4> values{};
    for(unsigned n=0;n<4;++n) {
        p.Fresh(config);p.allow_full_cpu_fragments=n==2;
        p.tev_constants.framebuffer^=n<<3;values[n]=p.tev_constants;
        p.offsets={n,n+1,n+2};
        p.scheduler.MakeDirty(StateFlags::FragmentConstants);
        PipelineCache::CpuFragmentToken token;
        Require(p.BindPipeline(info,true,nullptr,true,nullptr,&layout,n==0?nullptr:&token),
                "generic partial full partial bindings complete");
    }
    p.scheduler.WaitWorker();const auto& rows=p.scheduler.completed;
    Require(rows.size()==4 && rows[0].pipeline!=partial->Handle() &&
            rows[1].pipeline==partial->Handle() && rows[2].pipeline==full->Handle() &&
            rows[3].pipeline==partial->Handle(),"generic partial full partial owner sequence");
    // CodexAstraLocal: Exercise all CPU-layout route tags through the actual
    // queued selector, not a separately implemented classification function.
    Require(rows[0].gpu_route==0 && rows[1].gpu_route==1 && rows[2].gpu_route==2 &&
            rows[3].gpu_route==1,"generic partial full partial profiler sequence");
    Require(rows[1].pushes==rows[0].pushes+1 && rows[2].pushes==rows[1].pushes &&
            rows[3].pushes==rows[2].pushes+1 &&
            std::memcmp(rows[3].constants.data(),&values[3],128)==0,
            "full transition invalidates exact partial push transport");
    auto token=DrawCpu(p,Info(),config,true);
    Require(bool(token) && p.ready_cpu_bank->AllSlots()[token.slot].last_use_tick==p.scheduler.tick,
            "partial records actual post-flush draw tick");
}
void NativeAndFailureFallback() {
    // CodexAstraLocal: Native keeps force_tev and GPU permissions disabled,
    // but independently admits exact full CPU-layout fragments after readiness.
    PipelineCache p;p.allow_ready_gpu_vertices=false;p.allow_specialized_fragments=false;
    p.force_tev=true;
    auto* partial=Warm(p,true);
    Require(partial && p.ready_vertex_worker, "Native owns optional CPU compiler");
    auto* full=Warm(p,false);
    Require(full && p.ready_cpu_full_selected != 0 && p.force_tev &&
                !p.allow_specialized_fragments,
            "Native forced generic policy does not prohibit full CPU fragments");
    p.virtual_fs_config.reset();p.current_shaders={&p.other_shader,&p.generic_shader,nullptr};
    p.shader_hashes={31,47,0};auto info=Info();
    const auto queued=p.ready_vertex_worker->scheduled;
    Require(!p.PrepareReadyGpuVertex(info) && p.ready_vertex_worker->scheduled==queued,
            "Native CPU compiler does not admit GPU vertices");
    for(int failure:{1,2,3,4}) {
        PipelineCache q;q.allow_full_cpu_fragments=false;SeedPartial(q,Config());
        Probe::build_failure=failure;
        for(unsigned f=0;f<4;++f){for(unsigned n=0;n<16;++n)DrawCpu(q,Info(),Config());Advance(q);}
        q.ready_vertex_worker->WaitForRequests();Advance(q);Probe::build_failure=0;
        const auto attempts=q.ready_optional_attempts;
        for(unsigned n=0;n<20;++n)Require(!DrawCpu(q,Info(),Config()),"failed partial retains generic draw");
        Require(q.ready_optional_attempts==attempts && attempts==1,"failed partial never retries");
    }
}

// CodexAstraLocal: Compare all transport bytes with the original uncached
// producer, and execute real new-draw preparation through generic/full/generic
// transitions. Reuse must never hide a changed register/profile or queue state.
void LazyTransportAndExactReuse() {
    DynamicTevPreparation cache;
    Profile profile{};
    auto config = Config();
    const auto check = [&](const FSConfig& next, const Profile& host) {
        const auto expected = GLSL::MakeDynamicTevState(next, host);
        const auto& actual = cache.Get(next, host);
        Require(std::memcmp(&actual, &expected, sizeof(actual)) == 0,
                "cached transport equals original producer bytes");
    };
    check(config, profile); check(config, profile);
    Require(cache.Stats().builds == 1 && cache.Stats().hits == 1,
            "identical complete input reuses transport");
    // CodexAstraLocal: Challenge each owned input domain, including inactive
    // values. Exact equality conservatively rebuilds even if bytes stay equal.
    config.texture.tev_stages[0].sources_raw ^= 1; check(config, profile);
    config.texture.tev_stages[1].modifiers_raw ^= 1; check(config, profile);
    config.texture.tev_stages[2].ops_raw ^= 1; check(config, profile);
    config.texture.tev_stages[3].scales_raw ^= 1; check(config, profile);
    config.texture.combiner_buffer_input.Assign(17); check(config, profile);
    config.texture.fog_flip.Assign(1); check(config, profile);
    config.framebuffer.scissor_test_mode.Assign(Pica::RasterizerRegs::ScissorMode::Include);
    check(config, profile);
    config.lighting.lights[0].two_sided_diffuse.Assign(1); check(config, profile);
    config.proctex.lut_width = 16; check(config, profile);
    profile.has_custom_border_color = 1; check(config, profile);
    profile.vk_format_traits[3].native_format = 17; check(config, profile);
    Require(cache.Stats().builds == 12 && cache.Stats().hits == 1,
            "every complete config/profile change invalidates exact reuse");
    cache.Reset(); check(config, profile);
    Require(cache.Stats().builds == 1 && cache.Stats().hits == 0,
            "title reset cannot reuse old transport");

    Pica::RegsInternal regs{};
    regs.lighting.disable.Assign(1);
    regs.framebuffer.output_merger.alphablend_enable.Assign(1);
    const FSConfig original{regs};
    PipelineCache p;
    auto* full = Warm(p, false, original);
    p.dynamic_tev_preparation.Reset();
    const auto prepared_before = p.tev_transport_prepared;
    auto info = Info(); const auto layout = SoftwareLayout();
    PipelineCache::CpuFragmentToken token;
    p.UseFragmentShader(regs, {}, false);
    Require(p.pending_tev_transport.has_value(), "new draw defers transport");
    Require(p.BindPipeline(info, true, nullptr, true, nullptr, &layout, &token) && token,
            "full ready original draw binds");
    Require(p.ready_cpu_bank->AllSlots()[token.slot].owner.get() == full &&
            p.tev_transport_prepared == prepared_before &&
            p.dynamic_tev_preparation.Stats().requests == 0 &&
            p.tev_transport_bypassed_cpu == 1,
            "full ready draw performs no generic transport preparation");
    // CodexAstraLocal: A new original register value after full specialization
    // must reach the queued generic draw, not the skipped draw's stale bytes.
    regs.texturing.fog_flip.Assign(1);
    p.UseFragmentShader(regs, {}, false);
    Require(p.BindPipeline(info, true), "changed generic draw binds");
    p.scheduler.WaitWorker();
    const auto expected = GLSL::MakeDynamicTevState(FSConfig{regs}, p.profile);
    Require(std::memcmp(p.scheduler.completed.back().constants.data(), &expected, 128) == 0,
            "post-full generic command owns changed exact transport");
    p.UseFragmentShader(regs, {}, false);
    Require(p.BindPipeline(info, true), "same generic draw binds");
    Require(p.dynamic_tev_preparation.Stats().builds == 1 &&
            p.dynamic_tev_preparation.Stats().hits == 1,
            "second actual draw reuses pure preparation only");
    p.ClearTevFallbacks();
    Require(!p.pending_tev_transport && p.dynamic_tev_preparation.Stats().requests == 0,
            "actual drained reset clears pending draw and reuse evidence");

    // CodexAstraLocal: A partial lookup itself consumes the prepared plan before
    // final command capture. A manually prefilled fixture would hide this dependency.
    PipelineCache partial;
    const FSConfig partial_config{regs};
    auto* partial_owner = Warm(partial, true, partial_config);
    partial.tev_constants = {};
    partial.UseFragmentShader(regs, {}, false);
    auto partial_info = Info();
    Require(partial.BindPipeline(partial_info, true, nullptr, true, nullptr, &layout, &token) &&
            token && partial.ready_cpu_bank->AllSlots()[token.slot].owner.get() == partial_owner,
            "actual partial lookup prepares current plan before selection");
    partial.scheduler.WaitWorker();
    const auto partial_bytes = GLSL::MakeDynamicTevState(partial_config, partial.profile);
    Require(std::memcmp(partial.scheduler.completed.back().constants.data(), &partial_bytes, 128) == 0,
            "actual partial draw owns current runtime bytes");

    // CodexAstraLocal: Specialized-only GPU preparation bypasses transport;
    // refusal followed by a CPU retry must reestablish it from current registers.
    PipelineCache retry;
    retry.UseFragmentShader(regs, {}, true);
    Require(!retry.pending_tev_transport && retry.tev_transport_prepared == 0,
            "specialized GPU preparation has no generic obligation");
    regs.texturing.fog_flip.Assign(0);
    retry.UseFragmentShader(regs, {}, false);
    auto retry_info = Info();
    Require(retry.BindPipeline(retry_info, true), "GPU refusal CPU retry binds complete draw");
    retry.scheduler.WaitWorker();
    const auto retry_bytes = GLSL::MakeDynamicTevState(FSConfig{regs}, retry.profile);
    Require(std::memcmp(retry.scheduler.completed.back().constants.data(), &retry_bytes, 128) == 0,
            "CPU retry captures its own original input epoch");

    // CodexAstraLocal: Custom's generic alternative can win on the worker even
    // when the owner selected specialization. Force that route with no virtual
    // config, then mutate owner state before execution to challenge snapshotting.
    PipelineCache alternative;
    Settings::values.uberhar_test_mode.value = Settings::UberharTestMode::Custom;
    alternative.force_tev = true;
    alternative.UseFragmentShader(regs, {}, false);
    Require(!alternative.virtual_fs_config && alternative.pending_tev_transport,
            "custom alternative starts outside virtual-generic route");
    auto alternative_info = Info();
    Require(alternative.BindPipeline(alternative_info, true), "worker alternative draw binds");
    const auto alternative_bytes = GLSL::MakeDynamicTevState(FSConfig{regs}, alternative.profile);
    regs.texturing.fog_flip.Assign(1);
    alternative.UseFragmentShader(regs, {}, false);
    alternative.EnsureDynamicTevState();
    alternative.scheduler.WaitWorker();
    Require(std::memcmp(alternative.scheduler.completed.back().constants.data(), &alternative_bytes, 128) == 0,
            "late generic winner retains original queued transport");
    Require(alternative.scheduler.completed.back().gpu_route == 0,
            "late generic winner overrides prepared profiler route");
    Settings::values.uberhar_test_mode.value = Settings::UberharTestMode::Automatic;
}

int main() {
    try {
        Probe::partial_tier=true;
        PromotionAndPriority(); ColdBridge(); PartitionPressure(); TerminalBudgetsKeepHits();
        PartialTransportAndIdentity(); FourRouteBindings(); NativeAndFailureFallback();
        LazyTransportAndExactReuse();
        Require(Probe::optional_waits==0 && Probe::lifetime_errors==0,
                "no optional wait or borrowed shader destruction");
        std::cout<<"PASS checks="<<Probe::checks<<" builds="<<Probe::pipeline_builds
                 <<" optional_waits="<<Probe::optional_waits<<'\n';
    } catch(const std::exception& e) {
        std::cerr<<"FAIL "<<e.what()<<" checks="<<Probe::checks<<'\n';return 1;
    }
}
