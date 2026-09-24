// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// AstraEH: Validate effective native values and untouched resolution/custom
// behavior.
#include <cstdio>
#include <stdexcept>
#include "common/uberhar_test_profile.h"

Settings::Values Settings::values;
void Check(bool ok, const char* text) {
    if (!ok)
        throw std::runtime_error(text);
}
int main() {
    using namespace Settings;
    for (auto mode :
         {UberharTestMode::Native, UberharTestMode::Compute, UberharTestMode::Automatic}) {
        values.uberhar_test_mode = mode;
        values.graphics_api = GraphicsAPI::OpenGL;
        values.use_hw_shader = true;
        values.use_shader_jit = true;
        values.uberhar_hybrid_tev = false;
        values.resolution_factor = 3;
        values.use_integer_scaling = true;
        ApplyUberharTestProfile();
        Check(values.graphics_api.GetValue() == GraphicsAPI::Vulkan, "profile must select Vulkan");
        Check(!values.use_hw_shader.GetValue() && !values.use_shader_jit.GetValue(),
              "vertex specialization active");
        Check(values.uberhar_hybrid_tev.GetValue() && values.uberhar_force_tev.GetValue(),
              "generic fragment route disabled");
        Check(values.resolution_factor.GetValue() == 3 && values.use_integer_scaling.GetValue(),
              "resolution changed");
    }
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
    std::puts("PASS: three native profiles, preserved resolution/custom values, "
              "invalid-mode handling");
}
