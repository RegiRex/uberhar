#!/usr/bin/env python3
"""CodexAstraLocal: Execute Android's actual four setting layers against real INIReader."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--inih-root', type=Path, default=Path('externals/inih/inih'))
# CodexAstraLocal: An optional source permits finite defect controls without tree changes.
parser.add_argument('--source', type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
inih = args.inih_root.resolve()
source = (args.source or root / 'src/android/app/src/main/jni/config.cpp').read_text()
start = source.index('// CodexAstraLocal: Typed overlay parsing')
end = source.index('void Config::ReadValues()', start)
# CodexAstraLocal: Compile the actual production read order and profile application.
# Unrelated input/audio/platform owners are the only omitted ReadValues sections.
renderer_start = source.index('    // Core\n', end)
renderer_end = source.index('    // Layout\n', renderer_start)
utility_start = source.index('    ReadSetting("Utility",', renderer_end)
utility_end = source.index('\n    // Audio', utility_start)
apply_effective = ('void Config::ReadValues() {\n    LoadTestingOverlay();\n' +
                   source[renderer_start:renderer_end] +
                   source[utility_start:utility_end] +
                   '\n    Settings::ApplyUberharTestProfile();\n}\n')
# CodexAstraLocal: Only file/log I/O and the alpha version boundary are substituted.
prefix = r'''
#include <cassert>
#include <charconv>
#include <iostream>
#include <iomanip>
#include <memory>
#include <ranges>
#include <sstream>
#include <INIReader.h>
#include "common/settings.h"
#include "common/uberhar_test_profile.h"
#include "common/file_util.h"
#define private public
#include "jni/config.h"
#undef private
#define LOG_INFO(...)
#define LOG_ERROR(...)
#define LOG_WARNING(...)
namespace Settings { Values values; }
namespace Common { bool g_uberhar_alpha_testing = true; }
namespace FileUtil {
std::string test_file;
const std::string& GetUserPath(UserPath) { static std::string path = "test/"; return path; }
std::size_t ReadFileToString(bool, const std::string&, std::string& output) {
    output = test_file; return output.size();
}
}
Config::Config(u64 id) : title_id{id} {}
Config::~Config() = default;
'''
main = r'''
std::unique_ptr<INIReader> Parse(const std::string& text) {
    return std::make_unique<INIReader>(text.c_str(), text.size());
}
int main() {
    Config cfg{0x400000f700100};
    auto& v = Settings::values;
    auto load = [&](const std::string& global, const std::string& title,
                    const std::string& testing) {
        cfg.android_config = Parse(global);
        cfg.title_config = Parse(title);
        FileUtil::test_file = testing;
        cfg.ReadValues();
    };
    const std::string title_section = "[Testing.000400000F700100]\n";
    load("[Renderer]\nresolution_factor=2\nuberhar_cpu_vertex_only=false\nuberhar_test_mode=3\n"
         "[Audio]\nvolume=0.7\n[System]\nis_new_3ds=true\n",
         "[Renderer]\nresolution_factor=4\nuberhar_cpu_vertex_only=true\n"
         "[Audio]\nvolume=broken\n[System]\nis_new_3ds=false\n", "");
    assert(v.resolution_factor.GetValue() == 4 && v.resolution_factor.GetValue(true) == 2);
    assert(!v.resolution_factor.UsingGlobal());
    assert(v.uberhar_cpu_vertex_only.GetValue() && !v.uberhar_cpu_vertex_only.GetValue(true));
    cfg.ReadSetting("Audio", v.volume);
    assert(v.volume.GetValue() == 0.7f);
    cfg.ReadSetting("System", v.is_new_3ds);
    assert(v.is_new_3ds.GetValue());
    cfg.ReadSetting("Layout", v.large_screen_proportion, 2.25f);
    assert(v.large_screen_proportion.GetValue() == 2.25f);

    // CodexAstraLocal: Owner's counterexample: normal title 4x beats global Testing 2x.
    // Every independent title control also beats its global Testing default, including false/zero.
    const std::string normal_title = "[Renderer]\nresolution_factor=4\nuberhar_test_mode=1\n"
        "uberhar_cpu_vertex_only=false\nuse_hw_shader=true\ndisable_right_eye_render=false\n"
        "delay_game_render_thread_us=0\nfilter_mode=true\nfactor_3d=0\n";
    const std::string testing_global = "[Testing]\nenabled=true\npreset=1\nresolution=2\n"
        "use_hw_shader=false\ndisable_right_eye_render=true\ndelay_game_render_thread_us=100\n"
        "filter_mode=false\nfactor_3d=1\n";
    load("[Renderer]\nresolution_factor=1\n", normal_title, testing_global);
    assert(v.resolution_factor.GetValue() == 4 && v.resolution_factor.GetValue(true) == 2);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Native);
    assert(!v.uberhar_cpu_vertex_only.GetValue() && v.use_hw_shader.GetValue());
    assert(!v.disable_right_eye_render.GetValue() && v.delay_game_render_thread_us.GetValue() == 0);
    assert(v.filter_mode.GetValue() && v.factor_3d.GetValue() == 0);
    assert(cfg.title_config->GetInteger("Renderer", "resolution_factor", 0) == 4);
    assert(cfg.android_config->GetInteger("Renderer", "resolution_factor", 0) == 1);

    // CodexAstraLocal: Explicit title Testing is the final layer, including both preset fields.
    load("[Renderer]\nresolution_factor=1\n", normal_title, testing_global + title_section +
         "preset=1\nresolution=2\nuse_hw_shader=true\ndisable_right_eye_render=true\n"
         "delay_game_render_thread_us=200\nfilter_mode=false\nfactor_3d=1\n");
    assert(v.resolution_factor.GetValue() == 2);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Automatic);
    assert(v.uberhar_cpu_vertex_only.GetValue() && !v.use_hw_shader.GetValue());
    assert(v.disable_right_eye_render.GetValue() && v.delay_game_render_thread_us.GetValue() == 200);
    assert(!v.filter_mode.GetValue() && v.factor_3d.GetValue() == 1);

    // CodexAstraLocal: Title master overrides global in both directions; disabled saved
    // values survive and become effective again on re-enable, without changing normal files.
    load("[Renderer]\nuberhar_test_mode=0\nresolution_factor=3\n", "",
         testing_global + title_section + "enabled=false\nresolution=4\n");
    assert(!testing_overlay_active && v.resolution_factor.GetValue() == 3);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    load("[Renderer]\nuberhar_test_mode=0\nresolution_factor=3\n", "",
         "[Testing]\nenabled=false\npreset=2\nresolution=2\n" + title_section + "enabled=true\n");
    assert(testing_overlay_active && v.resolution_factor.GetValue() == 2);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Automatic);
    assert(!v.uberhar_cpu_vertex_only.GetValue());
    // CodexAstraLocal: Blank/malformed masters use the inherited master just like Kotlin.
    load("", "", testing_global + title_section + "enabled=broken\n");
    assert(testing_overlay_active);

    // CodexAstraLocal: Enabling Testing alone never invents a preset, scale or experiment.
    const std::string custom = "[Renderer]\nuberhar_test_mode=0\nresolution_factor=3\n"
        "use_hw_shader=true\ndisable_right_eye_render=true\ndelay_game_render_thread_us=250\n"
        "filter_mode=false\nfactor_3d=37\n";
    load(custom, "", "[Testing]\nenabled=true\n");
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    assert(v.resolution_factor.GetValue() == 3 && v.use_hw_shader.GetValue());
    assert(v.disable_right_eye_render.GetValue() && v.delay_game_render_thread_us.GetValue() == 250);
    assert(!v.filter_mode.GetValue() && v.factor_3d.GetValue() == 37);
    // CodexAstraLocal: Resetting one title key restores only its inheritance.
    load(custom, "", testing_global + title_section + "resolution=4\nfilter_mode=true\n");
    assert(v.resolution_factor.GetValue() == 4 && v.filter_mode.GetValue());
    load(custom, "", testing_global + title_section + "filter_mode=true\n");
    assert(v.resolution_factor.GetValue() == 2 && v.filter_mode.GetValue());

    // CodexAstraLocal: Explicit title Custom and non-Vulkan backends beat global modes.
    load("[Renderer]\nuberhar_test_mode=3\n", "[Renderer]\nuberhar_test_mode=0\n", testing_global);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    load("[Renderer]\nuberhar_test_mode=3\n", "[Renderer]\ngraphics_api=1\n", testing_global);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    assert(v.graphics_api.GetValue() == Settings::GraphicsAPI::OpenGL);
    load("[Renderer]\nuberhar_test_mode=3\n", "[Renderer]\ngraphics_api=1\n",
         testing_global + title_section + "preset=0\n");
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Native);
    assert(v.graphics_api.GetValue() == Settings::GraphicsAPI::Vulkan);

    // CodexAstraLocal: Safe profile defaults fill absent/blank normal keys only.
    for (int preset = 0; preset <= 2; ++preset) {
        const auto testing = "[Testing]\nenabled=true\npreset=" + std::to_string(preset) + "\n";
        load("[Renderer]\nuse_hw_shader=\nshaders_accurate_mul=\n", "", testing);
        assert(!v.use_hw_shader.GetValue() && v.shaders_accurate_mul.GetValue());
        assert(v.disable_spirv_optimizer.GetValue() && v.use_disk_shader_cache.GetValue());
        for (bool requested : {false, true}) {
            const auto value = requested ? "true\n" : "false\n";
            load(std::string("[Renderer]\nuse_hw_shader=") + value + "shaders_accurate_mul=false\n",
                 "", testing);
            assert(v.use_hw_shader.GetValue() == (requested && preset != 1));
            assert(!v.shaders_accurate_mul.GetValue());
            load("", "", testing + title_section + "use_hw_shader=" + value);
            assert(v.use_hw_shader.GetValue() == (requested && preset != 1));
        }
    }

    // CodexAstraLocal: Native/Combo preserve saved independent controls beyond the
    // Testing controls, and disabling Testing restores ordinary Custom values.
    const std::string independent = custom + "disable_spirv_optimizer=false\nuse_shader_jit=false\n"
        "shaders_accurate_mul=false\nuse_disk_shader_cache=false\ntexture_filter=1\ntexture_sampling=1\n"
        "render_3d=1\nrender_3d_which_display=1\nswap_eyes_3d=true\nuse_skip_duplicate_frames=true\n"
        "[Utility]\ndump_textures=true\ncustom_textures=true\nasync_custom_loading=false\n";
    for (int mode : {1, 3}) {
        load(independent, "[Renderer]\nuberhar_test_mode=" + std::to_string(mode) + "\n",
             "[Testing]\nenabled=true\n");
        assert(!v.disable_spirv_optimizer.GetValue() && !v.use_shader_jit.GetValue());
        assert(!v.shaders_accurate_mul.GetValue() && !v.use_disk_shader_cache.GetValue());
        assert(static_cast<int>(v.texture_filter.GetValue()) == 1);
        assert(static_cast<int>(v.texture_sampling.GetValue()) == 1);
        assert(v.delay_game_render_thread_us.GetValue() == 250 && v.factor_3d.GetValue() == 37);
        assert(static_cast<int>(v.render_3d.GetValue()) == 1 && v.swap_eyes_3d.GetValue());
        assert(v.dump_textures.GetValue() && v.custom_textures.GetValue());
        assert(!v.async_custom_loading.GetValue() && v.use_skip_duplicate_frames.GetValue());
    }
    load(independent, "", "[Testing]\nenabled=false\n");
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    assert(v.use_hw_shader.GetValue() && v.factor_3d.GetValue() == 37);

    // CodexAstraLocal: Existing launch resource locks and Testing controls remain
    // stable, while live normal-title and explicit-title Testing resolutions work.
    load("", "", testing_global + title_section + "preset=2\nuse_hw_shader=true\n");
    cfg.runtime_reload = true;
    load("", "[Renderer]\nresolution_factor=4\n",
         "[Testing]\nenabled=false\npreset=0\nresolution=2\n" + title_section +
         "use_hw_shader=false\ndisable_right_eye_render=false\ndelay_game_render_thread_us=0\n"
         "filter_mode=true\nfactor_3d=0\n");
    assert(testing_overlay_active && v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Automatic);
    assert(!v.uberhar_cpu_vertex_only.GetValue() && v.use_hw_shader.GetValue());
    assert(v.disable_right_eye_render.GetValue() && v.delay_game_render_thread_us.GetValue() == 100);
    assert(!v.filter_mode.GetValue() && v.factor_3d.GetValue() == 1);
    assert(v.resolution_factor.GetValue() == 4);
    FileUtil::test_file += "resolution=2\n";
    cfg.ReadValues();
    assert(v.resolution_factor.GetValue() == 2);
    for (const auto* missing_or_broken : {"", "[broken"}) {
        FileUtil::test_file = missing_or_broken;
        cfg.ReadValues();
        assert(v.resolution_factor.GetValue() == 2 && v.use_hw_shader.GetValue());
        assert(v.disable_right_eye_render.GetValue() && v.delay_game_render_thread_us.GetValue() == 100);
    }
    cfg.runtime_reload = false;
    load("", "", "[Testing]\nenabled=false\n");
    cfg.runtime_reload = true;
    load("", "[Renderer]\nresolution_factor=3\n", testing_global);
    assert(!testing_overlay_active && v.resolution_factor.GetValue() == 3);
    cfg.runtime_reload = false;

    // CodexAstraLocal: Invalid preset/scale inherit the lower layer; invalid
    // experiment values retain the documented safe baseline, not a global opt-in.
    for (const auto* invalid : {"-1", "16001", "broken", "100garbage", "1garbage", "0x10", "2147483648"}) {
        load("", normal_title, testing_global + title_section + "preset=" + invalid + "\nresolution=" + invalid +
             "\ndelay_game_render_thread_us=" + invalid + "\nfactor_3d=" + invalid +
             "\nfilter_mode=broken\nuse_hw_shader=broken\ndisable_right_eye_render=broken\n");
        assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Native);
        assert(v.resolution_factor.GetValue() == 4);
        assert(v.delay_game_render_thread_us.GetValue() == 0 && v.factor_3d.GetValue() == 0);
        assert(v.filter_mode.GetValue() && !v.use_hw_shader.GetValue() && !v.disable_right_eye_render.GetValue());
    }
    for (const auto* decimal : {"0100", "+100"}) {
        load("", "", "[Testing]\nenabled=true\n" + title_section +
             "delay_game_render_thread_us=" + decimal + "\nfactor_3d=+1\npreset=00\n");
        assert(v.delay_game_render_thread_us.GetValue() == 100 && v.factor_3d.GetValue() == 1);
        assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Native);
    }
    // CodexAstraLocal: Another title and Home never inherit this title's master or values.
    Config other{0x400000123400};
    other.android_config = Parse(custom);
    FileUtil::test_file = "[Testing]\nenabled=false\n" + title_section + "enabled=true\nresolution=4\n";
    other.ReadValues();
    assert(!testing_overlay_active && v.resolution_factor.GetValue() == 3);
    Config home;
    home.android_config = Parse(custom);
    FileUtil::test_file = testing_global;
    home.ReadValues();
    assert(!testing_overlay_active && v.resolution_factor.GetValue() == 3);
    // CodexAstraLocal: Beta/full builds ignore every stale alpha Testing value.
    Common::g_uberhar_alpha_testing = false;
    load(custom, "", testing_global + title_section + "enabled=true\nresolution=4\n");
    assert(!testing_overlay_active && v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    assert(v.resolution_factor.GetValue() == 3 && v.use_hw_shader.GetValue());
    assert(v.delay_game_render_thread_us.GetValue() == 250 && v.factor_3d.GetValue() == 37);
    std::cout << "Android four setting layers, sparse master/preset/control overrides, profile defaults, "
                 "ordinary title priority, launch latch and live resolution: PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='uberhar-settings-') as tmp:
    path = Path(tmp)
    (path / 'fixture.cpp').write_text(prefix + source[start:end] + apply_effective + main)
    command = ['c++', '-std=c++20', '-O0', '-DFMT_HEADER_ONLY', '-DENABLE_VULKAN', '-Isrc',
               '-Isrc/android/app/src/main', '-Ibuild/uberhar-profile',
               '-Iexternals/fmt/include', '-Iexternals/boost', '-I'+str(inih/'cpp'),
               str(path/'fixture.cpp'), str(inih/'cpp/INIReader.cpp'), str(inih/'ini.c'),
               '-o', str(path/'fixture')]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(path/'fixture')], cwd=root, check=True)
