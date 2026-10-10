// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// AstraEH: Validate effective native values and untouched resolution/custom
// behavior.
#include <cstdio>
#include <stdexcept>
#include "common/uberhar_test_profile.h"
#include "video_core/renderer_vulkan/uberhar_gpu_vertex_policy.h" // CodexAstraUlt: Real admission policy.

Settings::Values Settings::values;
void Check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
int main() {
    using namespace Settings;
    // CodexAstraUlt: Persisted IDs are a compatibility contract; append the diagnostic preset.
    static_assert(static_cast<u32>(UberharTestMode::Custom) == 0);
    static_assert(static_cast<u32>(UberharTestMode::Native) == 1);
    static_assert(static_cast<u32>(UberharTestMode::Compute) == 2);
    static_assert(static_cast<u32>(UberharTestMode::Automatic) == 3);
    static_assert(static_cast<u32>(UberharTestMode::ComboGeneric) == 4);
    // CodexAstraLocal: Software is appended; existing saved choices never move.
    static_assert(static_cast<u32>(UberharTestMode::Software) == 5);
    for (auto mode :
         {UberharTestMode::Native, UberharTestMode::Compute, UberharTestMode::Automatic,
          UberharTestMode::ComboGeneric}) { // CodexAstraUlt: Exercise every supported preset.
        values.uberhar_test_mode = mode;
        values.graphics_api = GraphicsAPI::OpenGL;
        values.use_hw_shader = true;
        values.use_shader_jit = false;
        values.uberhar_hybrid_tev = false;
        values.resolution_factor = 3;
        values.use_integer_scaling = true;
        ApplyUberharTestProfile();
        Check(values.graphics_api.GetValue() == GraphicsAPI::Vulkan, "profile must select Vulkan");
        Check(!values.use_hw_shader.GetValue() && values.use_shader_jit.GetValue(),
              "profile must use CPU JIT with GPU vertex specialization disabled");
        Check(values.disable_spirv_optimizer.GetValue(), "first-use optimizer still active");
        Check(values.uberhar_hybrid_tev.GetValue(), "generic CPU recovery disabled");
        // CodexAstraUlt: Replace AstraPro's three-mode expectation with an independent
        // four-mode matrix: existing Combo alone permits optional specialized fragments.
        Check(values.uberhar_force_tev.GetValue() == (mode != UberharTestMode::Automatic),
              "incorrect forced-generic profile");
        Check(values.resolution_factor.GetValue() == 3 && values.use_integer_scaling.GetValue(),
              "resolution changed");
        // CodexAstraUlt: Verify capabilities against persisted IDs, not the helper's own result.
        const auto id = static_cast<u32>(mode);
        Check(UsesReadyGpuVertices(mode) == (id == 3 || id == 4), "vertex capability changed");
        Check(AllowsSpecializedFragments(mode) == (id == 3), "fragment capability changed");
        Check(UsesAutomaticCompute(mode) == (id == 3 || id == 4), "compute selector changed");
        Check(AllowsComputeRendering(mode) == (id >= 2), "compute capability changed");
        // CodexAstraLocal: Isolation is exactly persisted mode2, independently
        // of optional compute availability; all other modes keep recovery.
        Check(RequiresComputeOnly(mode) == (id == 2), "strict compute isolation changed");
        Check(!values.async_shader_compilation.GetValue() &&
                  !values.uberhar_cpu_vertex_bridge.GetValue(),
              "preset introduced skip-on-pending or legacy bridge behavior");
        using namespace Vulkan::ReadyVertexPolicy;
        const auto admission = Classify(UsesReadyGpuVertices(mode), false, true, false,
            Pica::PipelineRegs::TriangleTopology::List, 96, false, true);
        Check(IsEligible(admission) == (id == 3 || id == 4), "eligible GPU draw lost route");
        // CodexAstraUlt: New mode must retain pending/failed/mismatched CPU recovery guards.
        Check(!CanSelect(false, false, 7, 7) && !CanSelect(true, true, 7, 7) &&
                  !CanSelect(true, false, 7, 8) && CanSelect(true, false, 7, 7),
              "ready GPU selection bypasses completion/failure/identity");
    }
    // CodexAstraLocal: Switching from GPU presets to Software must not leave an
    // accelerated renderer or stale high resolution active, nor enable compute.
    values.uberhar_test_mode = UberharTestMode::Software;
    values.graphics_api = GraphicsAPI::Vulkan;
    values.resolution_factor = 4;
    values.use_cpu_jit = false;
    values.simulate_3ds_gpu_timings = true;
    values.software_renderer_workers = 2;
    values.use_disk_shader_cache = true;
    ApplyUberharTestProfile();
    Check(values.graphics_api.GetValue() == GraphicsAPI::Software &&
              values.resolution_factor.GetValue() == 1 && !values.use_hw_shader.GetValue() &&
              values.use_shader_jit.GetValue() && !values.use_disk_shader_cache.GetValue(),
          "software retained GPU graphics or stale high resolution");
    Check(!values.use_cpu_jit.GetValue(), "software changed guest CPU execution choice");
    Check(values.simulate_3ds_gpu_timings.GetValue() &&
              values.software_renderer_workers.GetValue() == 2,
          "software changed guest timing or selected worker budget");
    Check(IsUberharTestProfile(UberharTestMode::Software) &&
              !UsesReadyGpuVertices(UberharTestMode::Software) &&
              !AllowsComputeRendering(UberharTestMode::Software) &&
              !RequiresComputeOnly(UberharTestMode::Software) &&
              !AllowsStaticCpuTev(UberharTestMode::Software),
          "software gained accelerated graphics capabilities");
    // AstraEH: Config::ReadValues reloads original INI values before applying a
    // mode.
    values.uberhar_test_mode = UberharTestMode::Custom;
    values.use_shader_jit = true;
    values.uberhar_hybrid_tev = false;
    values.graphics_api = GraphicsAPI::OpenGL;
    ApplyUberharTestProfile();
    Check(values.use_shader_jit.GetValue() && !values.uberhar_hybrid_tev.GetValue() &&
              values.graphics_api.GetValue() == GraphicsAPI::OpenGL,
          "custom values overwritten");
    values.uberhar_test_mode = static_cast<UberharTestMode>(99);
    ApplyUberharTestProfile();
    Check(values.uberhar_test_mode.GetValue() == UberharTestMode::Custom,
          "invalid profile accepted");
    // CodexAstraUlt: Unknown/Custom modes cannot accidentally enable diagnostic capabilities.
    for (const auto mode : {UberharTestMode::Custom, static_cast<UberharTestMode>(99)}) {
        Check(!IsUberharTestProfile(mode) && !UsesReadyGpuVertices(mode) &&
                  !AllowsSpecializedFragments(mode) && !AllowsComputeRendering(mode) &&
                  !RequiresComputeOnly(mode),
              "invalid/custom mode gained preset capabilities");
    }
    std::puts("PASS: four GPU profiles plus CPU Software, stable IDs, independent route matrix, ready guards, "
              "preserved resolution/custom values and invalid-mode handling");
}
