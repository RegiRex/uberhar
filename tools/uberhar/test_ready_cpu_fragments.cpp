// CodexAstraLocal: Execute exact extracted candidate helpers and bind closure
// with controlled driver/queue endpoints.
#include "video_core/renderer_vulkan/uberhar_adaptive_cpu_policy.h"
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
                                            const VertexLayout &, CpuFragmentToken*) noexcept;
  GraphicsPipeline *PrepareReadyGpuVertex(const PipelineInfo &);
  bool ReadyVertexShaders() const;
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
    current_shaders = {&trivial_vertex_shader, nullptr, nullptr};
    shader_hashes = {};
    tev_supported = true;
  }
};
class ShaderDiskCache {
public:
  PipelineCache &parent;
  struct ReadyFragmentKey {
    FSConfig config;
    Profile profile;
    bool operator==(const ReadyFragmentKey &o) const {
      return config == o.config && profile == o.profile;
    }
  };
  struct ReadyFragmentEntry {
    ReadyFragmentKey key;
    Shader shader;
    ReadyFragmentEntry(const Instance &i, const ReadyFragmentKey &k)
        : key(k), shader(i) {}
  };
  std::unordered_map<u64, std::unique_ptr<ReadyFragmentEntry>> ready_fragments;
  ReadyFragmentPolicy::DemandGate<ReadyFragmentKey> ready_fragment_demand;
  Shader *warming_ready_fragment{};
  u64 ready_fragment_requests{}, ready_fragment_hits{}, ready_fragment_cold{},
      ready_fragment_busy{}, ready_fragment_capped{},
      ready_fragment_mismatches{}, ready_fragment_failed_hits{},
      ready_fragment_unsupported{}, ready_fragment_lookup_only_misses{};
  std::atomic<u64> ready_fragment_builds{}, ready_fragment_failures{},
      ready_fragment_compile_ns{}, ready_fragment_max_compile_ns{};
  explicit ShaderDiskCache(PipelineCache &p) : parent(p) {}
  std::optional<std::pair<u64, Shader *const>>
  UseReadyFragmentShader(const FSConfig &, const UserConfig &, bool = true);
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
}
PipelineCache::~PipelineCache() {
  ClearTevFallbacks();
  mandatory_pipelines.clear();
  curr_disk_cache.reset();
}
GraphicsPipeline *PipelineCache::GetTevFallback(const PipelineInfo &i, bool) {
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
    p.Fresh(c);
    auto i = Info();
    Require(p.BindPipeline(i, true, nullptr, true, nullptr, &layout, &token),
            "control mode draws");
    p.scheduler.Finish();
    Require(p.ready_cpu_requests == 0 &&
                p.curr_disk_cache->ready_fragment_requests == 0,
            "control mode no optional CPU demand");
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
