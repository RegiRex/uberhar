#pragma once
// CodexAstraLocal: Execute exact extracted candidate helpers and bind closure
// with controlled driver/queue endpoints.
#include "video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h"
#include "video_core/renderer_vulkan/uberhar_static_tev_policy.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/uberhar_activity.h"
#include "video_core/shader/generator/shader_gen.h"
#include <bit>
#include <cassert>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <new>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>
// CodexAstraLocal: Preserve the real state/layout/Shader types; only
// driver-backed PSO construction is modeled.
#define GraphicsPipeline UnusedDriverGraphicsPipeline
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#undef GraphicsPipeline
#include "video_core/renderer_vulkan/pica_to_vk.h"
#include "video_core/renderer_vulkan/uberhar_fragment_policy.h"
#include "video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h"
#include "video_core/renderer_vulkan/uberhar_pipeline_policy.h"
#include "video_core/renderer_vulkan/uberhar_push_constants.h"
#include "video_core/renderer_vulkan/uberhar_shader_compile_policy.h"
#include "video_core/renderer_vulkan/uberhar_wait_diagnostics.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"
#define MICROPROFILE_SCOPE(...) ((void)0)
using namespace Pica::Shader::Generator;
using Pica::Shader::FSConfig;
using Pica::Shader::Profile;
using Pica::Shader::UserConfig;
// CodexAstraLocal: Fail one real allocation at a time only inside optional CPU
// admission; after the failure, restoration/reporting allocate normally and
// rendering controls continue.
namespace AllocationProbe {
thread_local bool active{};
thread_local std::size_t calls{}, fail_at{};
void Start(std::size_t fail = 0) {
  active = true;
  calls = 0;
  fail_at = fail;
}
std::size_t Stop() {
  active = false;
  return calls;
}
void *Allocate(std::size_t size) {
  if (active && ++calls == fail_at) {
    active = false;
    throw std::bad_alloc{};
  }
  if (void *result = std::malloc(size ? size : 1))
    return result;
  throw std::bad_alloc{};
}
} // namespace AllocationProbe
void *operator new(std::size_t n) { return AllocationProbe::Allocate(n); }
void *operator new[](std::size_t n) { return AllocationProbe::Allocate(n); }
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
namespace Probe {
// CodexAstraLocal: The legacy suite isolates the inherited full route with
// a terminal partial-module budget. The new suite enables actual partial work;
// both execute the same production cache and binding bodies.
bool partial_tier{};
unsigned checks{}, compiler_calls{}, pipeline_builds{}, required_waits{},
    optional_waits{}, lifetime_errors{}, destroyed_pipelines{};
int compiler_failure{}, build_failure{};
bool logger_throws{};
std::set<const void *> live_shaders;
std::vector<std::string> compiled_sources;
std::vector<bool> optimizer_disabled;
std::uintptr_t next_handle = 100;
void Require(bool c, const char *m) {
  ++checks;
  if (!c)
    throw std::runtime_error(m);
}
} // namespace Probe
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char *, unsigned, const char *,
                       fmt::string_view, const fmt::format_args &) {
  if (level >= Level::Error && Probe::logger_throws)
    throw 991;
}
} // namespace Common::Log
namespace Vulkan {
class Instance {
public:
  bool dynamic{};
  bool IsExtendedDynamicStateSupported() const { return dynamic; }
  vk::Device GetDevice() const { return {}; }
  bool UseGeometryShaders() const { return false; }
  bool IsFragmentShaderBarycentricSupported() const { return false; }
  template <class... T> void ReportMemoryUsage(T &&...) const {}
};
class RenderManager {};
Shader::Shader(const Instance &) { Probe::live_shaders.insert(this); }
Shader::~Shader() { Probe::live_shaders.erase(this); }
// CodexAstraLocal: Real AsyncHandle and production execution identity remain;
// GPU allocation alone is controlled.
class GraphicsPipeline : public Common::AsyncHandle {
public:
  const Instance &instance;
  PipelineInfo info;
  std::array<Shader *, 3> stages;
  PipelineBuildOptions options;
  bool failed{}, handle{}, optional{};
  int failure{};
  const u64 handle_id = ++Probe::next_handle;
  std::optional<u64> forged_key;
  GraphicsPipeline(const Instance &i, RenderManager &, const PipelineInfo &p,
                   vk::PipelineCache, vk::PipelineLayout,
                   std::array<Shader *, 3> s, Common::ThreadWorker *,
                   PipelineBuildOptions o = {})
      : Common::AsyncHandle(false, o.completion), instance(i), info(p),
        stages(s), options(o), optional(o.recoverable_failure),
        failure(Probe::build_failure) {}
  ~GraphicsPipeline() {
    ++Probe::destroyed_pipelines;
    for (auto *s : stages)
      if (s && !Probe::live_shaders.contains(s))
        ++Probe::lifetime_errors;
  }
  u64 ActualKey() const noexcept;
  u64 Key() const noexcept { return forged_key.value_or(ActualKey()); }
  bool MatchesExecution(const PipelineInfo &, const std::array<u64, 3> &) const;
  bool HasFailed() const { return failed; }
  void MarkFailed() {
    failed = true;
    MarkDone();
  }
  u64 Handle() const { return handle ? handle_id : 0; }
  void WaitDone() {
    if (optional) {
      ++Probe::optional_waits;
      throw std::runtime_error("optional wait");
    }
    ++Probe::required_waits;
    handle = true;
    MarkDone();
  }
  bool Build() {
    ++Probe::pipeline_builds;
    if (failure == 1)
      return false;
    if (failure == 2)
      throw std::runtime_error("driver");
    if (failure == 3)
      throw 773;
    if (failure == 4) {
      MarkDone();
      return true;
    }
    handle = true;
    MarkDone();
    return true;
  }
  bool TryBuild(bool, bool = false) { return Build(); }
  u32 PendingShaderMask() const { return 0; }
  u32 BuildPhase() const { return IsDone() ? 3 : 0; }
  u64 DriverBuildNs() const { return 0; }
  u32 ShaderStageMask() const {
    return !!stages[0] | (!!stages[1] << 1) | (!!stages[2] << 2);
  }
  void RecordFallbackUse() {}
};
std::vector<u32> CompileGLSL(const std::string &s, vk::ShaderStageFlagBits,
                             const char *, bool disabled) {
  ++Probe::compiler_calls;
  Probe::compiled_sources.push_back(s);
  Probe::optimizer_disabled.push_back(disabled);
  if (Probe::compiler_failure == 1)
    throw std::runtime_error("compiler");
  if (Probe::compiler_failure == 2)
    throw 991;
  if (Probe::compiler_failure == 3)
    return {};
  return {0x7230203};
}
vk::ShaderModule CompileSPV(const std::vector<u32> &, vk::Device) {
  if (Probe::compiler_failure == 4)
    return {};
  return vk::ShaderModule{
      reinterpret_cast<VkShaderModule>(++Probe::next_handle)};
}
// CodexAstraLocal: The recording endpoint stores command values so live
// producer mutation can be adversarial.
struct CommandsState {
  u64 pipeline{};
  std::array<u32, 3> offsets{};
  std::array<vk::DescriptorSet, 3> descriptors{};
  std::array<std::byte, 128> constants{};
  unsigned pushes{}, pipeline_binds{}, descriptor_binds{};
  vk::Viewport viewport{};
  vk::Rect2D scissor{};
  vk::CompareOp depth{};
  vk::CullModeFlags cull{};
  bool depth_test{}, depth_write{}, stencil_test{};
  u32 stencil_compare{}, stencil_write{}, stencil_reference{};
  std::array<float, 4> blend{};
  vk::FrontFace front{};
  vk::PrimitiveTopology topology{};
  vk::StencilOp stencil_fail{}, stencil_pass{}, stencil_depth_fail{};
  vk::CompareOp stencil_func{};
};
struct RecordingCommands {
  CommandsState *s;
  void setViewport(u32, const vk::Viewport &v) { s->viewport = v; }
  void setScissor(u32, const vk::Rect2D &v) { s->scissor = v; }
  void setStencilCompareMask(vk::StencilFaceFlags, u32 x) {
    s->stencil_compare = x;
  }
  void setStencilWriteMask(vk::StencilFaceFlags, u32 x) {
    s->stencil_write = x;
  }
  void setStencilReference(vk::StencilFaceFlags, u32 x) {
    s->stencil_reference = x;
  }
  void setBlendConstants(const float *x) {
    std::copy_n(x, 4, s->blend.begin());
  }
  void setCullModeEXT(vk::CullModeFlags v) { s->cull = v; }
  void setFrontFaceEXT(vk::FrontFace x) { s->front = x; }
  void setDepthCompareOpEXT(vk::CompareOp v) { s->depth = v; }
  void setDepthTestEnableEXT(bool v) { s->depth_test = v; }
  void setDepthWriteEnableEXT(bool v) { s->depth_write = v; }
  void setPrimitiveTopologyEXT(vk::PrimitiveTopology x) { s->topology = x; }
  void setStencilTestEnableEXT(bool x) { s->stencil_test = x; }
  void setStencilOpEXT(vk::StencilFaceFlags, vk::StencilOp f, vk::StencilOp p,
                       vk::StencilOp d, vk::CompareOp c) {
    s->stencil_fail = f;
    s->stencil_pass = p;
    s->stencil_depth_fail = d;
    s->stencil_func = c;
  }
  void bindPipeline(vk::PipelineBindPoint, u64 p) {
    s->pipeline = p;
    ++s->pipeline_binds;
  }
  void bindDescriptorSets(vk::PipelineBindPoint, vk::PipelineLayout, u32,
                          const std::array<vk::DescriptorSet, 3> &d,
                          const std::array<u32, 3> &o) {
    s->descriptors = d;
    s->offsets = o;
    ++s->descriptor_binds;
  }
  void pushConstants(vk::PipelineLayout, vk::ShaderStageFlagBits, u32, u32 n,
                     const void *p) {
    Probe::Require(n == 128, "push ABI");
    std::memcpy(s->constants.data(), p, n);
    ++s->pushes;
  }
};
enum class StateFlags {
  AllDirty = 0,
  Pipeline = 1,
  DescriptorSets = 2,
  FragmentConstants = 4
};
DECLARE_ENUM_FLAG_OPERATORS(StateFlags)
struct Scheduler {
  StateFlags state{};
  CommandsState current{};
  std::vector<CommandsState> completed;
  std::vector<std::function<void(RecordingCommands)>> records;
  template <class F> void Record(F &&f) {
    records.emplace_back(std::forward<F>(f));
  }
  void MarkStateNonDirty(StateFlags f) { state |= f; }
  void MakeDirty(StateFlags f) { state &= ~f; }
  bool IsStateDirty(StateFlags f) const { return False(state & f); }
  void Finish() {
    auto r = std::move(records);
    records.clear();
    for (auto &f : r) {
      f({&current});
      completed.push_back(current);
    }
  }
  void WaitWorker() { Finish(); }
  u64 tick=1,known{};
  u64 CurrentTick() const { return tick; }
  Scheduler *GetMasterSemaphore() { return this; }
  u64 KnownGpuTick() const { return known; }
};

// CodexAstraLocal: Exact production observation-key declaration.
#include "cpu_fragment_key.inc"

class ShaderDiskCache;
class PipelineCache {
public:
  Instance instance;
  RenderManager renderpass_cache;
  Scheduler scheduler;
  Profile profile{};
  std::unique_ptr<vk::PipelineCache> driver_pipeline_cache =
      std::make_unique<vk::PipelineCache>();
  std::unique_ptr<vk::PipelineLayout> pipeline_layout =
      std::make_unique<vk::PipelineLayout>();
  Common::ThreadWorker pipeline_workers, shader_workers;
  std::unique_ptr<Common::ThreadWorker> tev_worker =
      std::make_unique<Common::ThreadWorker>();
  std::unique_ptr<Common::ThreadWorker> ready_vertex_worker =
      std::make_unique<Common::ThreadWorker>();
  std::unordered_map<u64, std::unique_ptr<GraphicsPipeline>> ready_vertex_pipelines;
  std::unique_ptr<ReadyCpuBank> ready_cpu_bank=std::make_unique<ReadyCpuBank>();
  using CpuFragmentToken=ReadyCpuBank::Token;
  std::size_t ready_optional_attempts{};
  std::size_t ReadyCpuOwned()const{return ready_cpu_bank?ready_cpu_bank->Owned():0;}
  void CompleteReadyCpuDraw(CpuFragmentToken);
  void RetireReadyCpuAfterWorkerDrain();
  GraphicsPipeline *warming_ready_vertex{};
  PipelineBuildStats ready_vertex_build_stats, ready_cpu_build_stats;
  Common::AsyncCompletion pipeline_completion;
  Shader trivial_vertex_shader{instance};
  Shader generic_shader{instance};
  Shader other_shader{instance};
  std::array<Shader *, 3> current_shaders{};
  std::array<u64, 3> shader_hashes{};
  std::unique_ptr<ShaderDiskCache> curr_disk_cache;
  bool hybrid_tev = true, force_tev = false, allow_specialized_fragments = true;
  bool allow_static_cpu_tev = true, allow_ready_gpu_vertices = true;
  const FSConfig* prepared_cpu_family{};
  std::optional<FSConfig> owned_family;
  u64 ready_cpu_static_selected{}, ready_cpu_full_selected{}, ready_fragment_preflight_deferred{};
  std::optional<FSConfig> virtual_fs_config, tev_family_config;
  UserConfig tev_user{};
  bool tev_supported = true;
  GLSL::DynamicTevSupport tev_support_reason{};
  using TevPushConstants = GLSL::DynamicTevState;
  TevPushConstants tev_constants{};
  ExactPushConstants<TevPushConstants> tev_push_constants;
  std::unordered_map<u64, std::unique_ptr<Shader>> tev_shaders;
  std::unordered_map<u64, std::unique_ptr<GraphicsPipeline>> tev_pipelines,
      mandatory_pipelines;
  GraphicsPipeline *warming_tev_pipeline{}, *bound_pipeline{};
  PipelineInfo current_info{};
  std::array<vk::DescriptorSet, 3> bound_descriptor_sets{};
  std::array<u32, 3> offsets{};
  std::array<std::unordered_set<u64>, 16> tev_candidate_keys;
  struct Preparation {
    void Reset() {}
  } tev_preparation;
  u64 tev_prepare_hit_samples{}, tev_prepare_miss_samples{},
      tev_prepare_hit_ns{}, tev_prepare_miss_ns{};
  bool tev_census_capped{};
  u32 tev_light_counts_mask{}, tev_family_details{};
  u64 ready_cpu_requests{}, ready_cpu_selected{}, ready_cpu_dependencies{},
      ready_cpu_deferred{}, ready_cpu_capped{}, ready_cpu_mismatches{},
      ready_cpu_failed_hits{}, ready_cpu_admission_failures{};
  std::array<u64, 2> ready_cpu_selected_lighting{}, ready_cpu_bank_lighting{};
  u64 ready_vertex_requests{}, ready_vertex_selected{},
      ready_vertex_dependency_misses{}, ready_vertex_deferred{},
      ready_vertex_capped{}, ready_vertex_failed{},
      ready_vertex_key_mismatches{};
  u64 draw_requests{}, cpu_bridge_draws{}, cpu_bridge_mismatches{},
      virtual_specialized_gpu_draws{}, virtual_generic_draws{},
      virtual_recovery_draws{}, virtual_waits{}, virtual_wait_ns{},
      virtual_max_wait_ns{}, specialized_pending{}, fallback_warming{},
      fallback_unavailable{}, skipped_draws{};
  u64 tev_transport_prepared{}, tev_transport_skipped{},
      tev_transport_bypassed_gpu{};
  std::array<u64, 11> virtual_recovery_reasons{};
  std::array<u64, 7> tev_loop_histogram{}, tev_active_histogram{};
  struct PhaseWait {
    u64 count{}, ns{}, maximum{};
  };
  std::array<PhaseWait, Common::UberharActivity::PhaseCount> phase_waits{};
  u32 phase_wait_details{};
  std::atomic<u64> first_ready_waits{}, late_fallback_draws{}, pipeline_waits{},
      pipeline_wait_ns{}, pipeline_wait_max_ns{}, fallback_wait_ns{},
      slow_pipeline_waits{}, fallback_draws{};
  PipelineWaitDiagnostics wait_diagnostics;
  std::chrono::steady_clock::time_point diagnostics_start =
                                            std::chrono::steady_clock::now(),
                                        next_progress = diagnostics_start +
                                                        std::chrono::hours(1);
  u64 current_program_id = 1, cache_session = 1;
  int generic_mode{};
  PipelineCache();
  ~PipelineCache();
  bool BindPipeline(PipelineInfo &, bool = false, GraphicsPipeline * = nullptr,
                    bool = true, GraphicsPipeline * = nullptr,
                    const VertexLayout * = nullptr, CpuFragmentToken* = nullptr);
  GraphicsPipeline *PrepareReadyCpuFragment(PipelineInfo &, GraphicsPipeline *,
                                            const VertexLayout &, CpuFragmentToken*, bool*) noexcept;
  // CodexAstraLocal: Legacy controls inspect only the selected owner; adapt
  // their old call shape without bypassing the actual five-argument selection.
  GraphicsPipeline* PrepareReadyCpuFragment(PipelineInfo& info,GraphicsPipeline* generic,
          const VertexLayout& layout,CpuFragmentToken* token) noexcept {
      bool partial{};return PrepareReadyCpuFragment(info,generic,layout,token,&partial);
  }
  GraphicsPipeline *PrepareReadyGpuVertex(const PipelineInfo &);
  bool ReadyVertexShaders() const;
  bool ReadyGpuFragmentPreflight(const Pica::RegsInternal&, const UserConfig&);
  bool PreferReadySpecializedFragment(const UserConfig &) const;
  void UseFragmentShader(const Pica::RegsInternal &, const UserConfig &,
                         bool = false);
  GraphicsPipeline *GetTevFallback(const PipelineInfo &, bool = false);
  void ReportUberharStats(const char *) {}
  u64 GetProgramID() const { return current_program_id; }
  void ClearTevFallbacks();
  void Fresh(const FSConfig &config) {
    virtual_fs_config = config;
    tev_family_config = config;
    owned_family = GLSL::MakeDynamicTevFamilyConfig(config, profile);
    // CodexAstraLocal: The partial suite uses the real transport producer;
    // legacy binding controls retain their explicitly assigned payload bytes.
    if (Probe::partial_tier) tev_constants = GLSL::MakeDynamicTevState(config, profile);
    current_shaders = {&trivial_vertex_shader, nullptr, nullptr};
    shader_hashes = {};
    tev_supported = true;
  }
};
class ShaderDiskCache {
public:
  PipelineCache &parent;
#include "fragment_members.inc"
  explicit ShaderDiskCache(PipelineCache &p) : parent(p) {}
  std::optional<std::pair<u64, Shader *const>>
  UseReadyFragmentShader(const FSConfig &, const UserConfig &, bool = true);
  std::optional<std::pair<u64, Shader *const>> UseStaticTevFragmentShader(
      const FSConfig&, const GLSL::StaticTevPlan&, const UserConfig&, bool = true);
  bool OptionalFragmentPending() const;
  std::optional<std::pair<u64, Shader *const>>
  UseFragmentShader(const FSConfig &c, const UserConfig &) {
    return std::make_pair(c.Hash(), &parent.generic_shader);
  }
  GraphicsPipeline *GetPipeline(const PipelineInfo &i) {
    auto stages = parent.current_shaders;
    auto k =
        i.state.ExecutionHash(parent.instance.dynamic, HostShaderIds(stages));
    auto it = parent.mandatory_pipelines.find(k);
    if (it != parent.mandatory_pipelines.end())
      return it->second.get();
    auto p = std::make_unique<GraphicsPipeline>(
        parent.instance, parent.renderpass_cache, i,
        *parent.driver_pipeline_cache, *parent.pipeline_layout, stages,
        &parent.pipeline_workers);
    p->handle = true;
    p->MarkDone();
    auto *r = p.get();
    parent.mandatory_pipelines.emplace(k, std::move(p));
    return r;
  }
};
PipelineCache::PipelineCache() {
  for (auto *s : {&trivial_vertex_shader, &generic_shader, &other_shader}) {
    s->module = vk::ShaderModule{
        reinterpret_cast<VkShaderModule>(++Probe::next_handle)};
    s->MarkDone();
  }
  profile.is_vulkan = true;
  profile.has_separable_shaders = true;
  profile.has_logic_op = true;
  profile.vk_disable_spirv_optimizer = true;
  curr_disk_cache = std::make_unique<ShaderDiskCache>(*this);
  if (!Probe::partial_tier)
    curr_disk_cache->static_tev_attempts=StaticTevPolicy::MaxModuleAttempts;
}
PipelineCache::~PipelineCache() {
  ClearTevFallbacks();
  mandatory_pipelines.clear();
  curr_disk_cache.reset();
}
GraphicsPipeline *PipelineCache::GetTevFallback(const PipelineInfo &i, bool) {
  prepared_cpu_family = owned_family ? &*owned_family : nullptr;
  if (generic_mode == 3)
    return nullptr;
  auto stages = current_shaders;
  stages[0] = &trivial_vertex_shader;
  stages[1] = &generic_shader;
  stages[2] = nullptr;
  auto k = i.state.ExecutionHash(instance.dynamic, HostShaderIds(stages));
  auto it = tev_pipelines.find(k);
  if (it != tev_pipelines.end())
    return it->second.get();
  auto p = std::make_unique<GraphicsPipeline>(
      instance, renderpass_cache, i, *driver_pipeline_cache, *pipeline_layout,
      stages, tev_worker.get());
  if (generic_mode == 2)
    p->MarkFailed();
  else if (generic_mode != 1) {
    p->handle = true;
    p->MarkDone();
  }
  auto *r = p.get();
  tev_pipelines.emplace(k, std::move(p));
  return r;
}
// CodexAstraLocal: Only these exact production/candidate function bodies are
// injected; driver creation and command execution are explicit endpoints above.
#include UBERHAR_CPU_FRAGMENT_FUNCTIONS
} // namespace Vulkan
using namespace Vulkan;
using Probe::Require;

// CodexAstraLocal: Actual software layout fields are extracted separately; no
// new ABI is invented for the selection tests.
VertexLayout SoftwareLayout() {
  VertexLayout software_layout{};
  using HardwareVertex = std::array<float, 22>;
#include "layout.inc"
  return software_layout;
}
PipelineInfo Info() {
  PipelineInfo i{};
  i.state.vertex_layout = SoftwareLayout();
  i.state.rasterization.topology.Assign(
      Pica::PipelineRegs::TriangleTopology::List);
  i.state.attachments.color = VideoCore::PixelFormat::RGBA8;
  i.state.attachments.depth = VideoCore::PixelFormat::D24S8;
  i.state.blending.blend_enable = 1;
  i.state.blending.color_write_mask = 15;
  i.state.depth_stencil.depth_test_enable.Assign(1);
  i.state.depth_stencil.depth_compare_op.Assign(
      Pica::FramebufferRegs::CompareFunc::LessThanOrEqual);
  i.dynamic_info.viewport = {0, 0, 800, 480};
  i.dynamic_info.scissor = {0, 0, 800, 480};
  return i;
}
FSConfig Config(unsigned n = 0) {
  Pica::RegsInternal r{};
  r.lighting.disable.Assign(1);
  r.framebuffer.output_merger.alphablend_enable.Assign(1);
  FSConfig c{r};
  c.framebuffer.alpha_test_func.Assign(
      static_cast<Pica::FramebufferRegs::CompareFunc>(n % 8));
  c.texture.fog_flip.Assign((n / 8) & 1);
  return c;
}
void SeedModule(PipelineCache &p, const FSConfig &c) {
  auto e = std::make_unique<ShaderDiskCache::ReadyFragmentEntry>(
      p.instance, ShaderDiskCache::ReadyFragmentKey{c, p.profile});
  e->shader.module =
      vk::ShaderModule{reinterpret_cast<VkShaderModule>(++Probe::next_handle)};
  e->shader.MarkDone();
  p.curr_disk_cache->ready_fragments.emplace(c.Hash(), std::move(e));
}
// CodexAstraLocal: Actual shadow helper/bind/reset functions are injected into
// production-key recording endpoints. These tests do not execute Vulkan.
void Advance(PipelineCache& p, bool completed = true) {
    p.scheduler.WaitWorker();
    if (completed) p.scheduler.known = p.scheduler.tick;
    ++p.scheduler.tick;
    p.RetireReadyCpuAfterWorkerDrain();
}

PipelineCache::CpuFragmentToken DrawCpu(PipelineCache& p, PipelineInfo info,
                                      const FSConfig& config, bool flush = false) {
    auto layout = SoftwareLayout();
    PipelineCache::CpuFragmentToken token{};
    p.Fresh(config);
    Require(p.BindPipeline(info, true, nullptr, true, nullptr, &layout, &token),
            "generic or specialized draw always binds");
    if (flush) { p.scheduler.WaitWorker(); ++p.scheduler.tick; }
    // CodexAstraLocal: Command emission is controlled, while the exact production
    // post-enqueue stamp below is extracted from the rasterizer shadow.
    p.scheduler.Record([](RecordingCommands command) {
        Require(command.s->pipeline != 0, "complete draw has actual pipeline");
    });
    auto cpu_fragment_use = token;
    auto& pipeline_cache = p;
#include "after_draw.inc"
    return token;
}
