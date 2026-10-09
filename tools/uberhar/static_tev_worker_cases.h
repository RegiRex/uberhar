#pragma once
// CodexAstraLocal: Partial compilation shares the real optional worker. Caller
// snapshots, terminal failure, finite ownership and queue ordering are tested
// independently of the separate actual-generator pixel/depth proof.
void PartialDemand(Fixture& f,const FSConfig& family,const GLSL::StaticTevPlan& plan,unsigned count=16) {
    for(unsigned n=0;n<count;++n)f.cache.UseStaticTevFragmentShader(family,plan,{});
}
void PartialSnapshot() {
    Probe::Reset();Fixture f;auto family=Config();GLSL::StaticTevPlan plan{};
    plan.buffer_mask=256;plan.stages[0].sources=42;const auto original=plan;
    const auto original_family=family;const auto original_profile=f.parent.profile;
    {std::lock_guard lock(Probe::mutex);Probe::pause=true;}
    PartialDemand(f,family,plan);Probe::AwaitEntry();
    auto* entry=f.cache.static_tev_fragments.at(StaticTevPolicy::Key::Hash(family,plan)).get();
    Require(!entry->shader.IsDone(),"held partial is pending");
    for(unsigned n=0;n<32;++n)
        Require(!f.cache.UseStaticTevFragmentShader(family,plan,{},false),"pending partial lookup nonblocking");
    family.texture.fog_flip.Assign(1);plan.stages[0].sources=99;
    f.parent.profile.enable_accurate_mul=!f.parent.profile.enable_accurate_mul;
    f.cache.static_tev_fragments.rehash(127);
    Probe::Release();f.parent.shader_workers.WaitForRequests();
    Require(entry->shader.IsDone() && !entry->shader.HasFailed(),"partial completion published");
    Require(Probe::observed_config==original_family && Probe::observed_profile==original_profile &&
            Probe::observed_plan==original,"partial generator owns original snapshots");
    Require(!Probe::optimizer_disabled,"partial explicitly optimized");
    Require(entry->source_bytes>0 && entry->spirv_bytes==4,"partial metrics published with module");
}
void PartialLimits() {
    Probe::Reset();Fixture f;auto family=Config();GLSL::StaticTevPlan plan{};plan.buffer_mask=256;
    PartialDemand(f,family,plan);f.parent.shader_workers.WaitForRequests();
    auto* original=f.cache.static_tev_fragments.at(StaticTevPolicy::Key::Hash(family,plan)).get();
    for(unsigned n=1;n<8;++n){auto changed=plan;changed.stages[5].sources=n;
        PartialDemand(f,family,changed);f.parent.shader_workers.WaitForRequests();}
    auto missing=plan;missing.stages[5].sources=80;PartialDemand(f,family,missing,32);
    Require(f.cache.static_tev_fragments.size()==8 && f.cache.static_tev_attempts==8,
            "partial module bound includes all retained owners");
    Require(f.cache.UseStaticTevFragmentShader(family,plan,{},false)->second==&original->shader,
            "partial cap preserves ready module hit");
    Demand(f,Config(123));f.parent.shader_workers.WaitForRequests();
    Require(f.cache.ready_fragments.size()==1,"partial modules cannot consume full module slots");
    {
        Fixture denied;auto config=Config();GLSL::StaticTevPlan empty{};
        PartialDemand(denied,config,empty,15);
        for(unsigned n=0;n<16;++n){Probe::fail_allocation_after=0;
            denied.cache.UseStaticTevFragmentShader(config,empty,{});Probe::fail_allocation_after=-1;}
        PartialDemand(denied,config,empty,32);
        Require(denied.cache.static_tev_attempts==16 && denied.cache.static_tev_fragments.empty() &&
                denied.parent.shader_workers.accepted==0,"allocation attempts remain finite without owners");
    }
}
void PartialFailures() {
    for(unsigned failure=1;failure<=8;++failure) {
        Probe::Reset();Fixture f;auto family=Config();GLSL::StaticTevPlan plan{};
        Probe::failure=failure;PartialDemand(f,family,plan);f.parent.shader_workers.WaitForRequests();
        auto* entry=f.cache.static_tev_fragments.at(StaticTevPolicy::Key::Hash(family,plan)).get();
        Require(entry->shader.IsDone() && entry->shader.HasFailed(),"partial failure publishes terminal state");
        const auto attempts=f.parent.shader_workers.attempts;
        PartialDemand(f,family,plan,32);
        Require(f.parent.shader_workers.attempts==attempts && f.cache.static_tev_attempts==1,
                "partial failure never retries");
    }
}
void SharedLane() {
    Probe::Reset();Fixture f;auto config=Config();GLSL::StaticTevPlan plan{};
    {std::lock_guard lock(Probe::mutex);Probe::pause=true;}
    Demand(f,config);Probe::AwaitEntry();
    PartialDemand(f,config,plan);
    Require(f.parent.shader_workers.accepted==1 && f.cache.static_tev_fragments.empty(),
            "partial cannot queue behind held full compilation");
    Probe::Release();f.parent.shader_workers.WaitForRequests();
    Shader pending_pipeline{f.parent.instance};f.parent.warming_ready_vertex=&pending_pipeline;
    PartialDemand(f,config,plan);
    Require(f.parent.shader_workers.accepted==1,"partial respects pending PSO lane");
    pending_pipeline.MarkFailed();PartialDemand(f,config,plan,1);f.parent.shader_workers.WaitForRequests();
    Require(f.parent.shader_workers.accepted==2 && f.cache.static_tev_fragments.size()==1,
            "shared lane advances after terminal dependency");
    f.parent.warming_ready_vertex=nullptr;
}
