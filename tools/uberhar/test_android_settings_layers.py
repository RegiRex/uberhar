#!/usr/bin/env python3
"""CodexAstraLocal: Execute Android's actual typed title/testing overlay against real INIReader."""
import argparse
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--inih-root', type=Path, default=Path('externals/inih/inih'))
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
inih = args.inih_root.resolve()
source = (root / 'src/android/app/src/main/jni/config.cpp').read_text()
start = source.index('// CodexAstraLocal: Typed overlay parsing')
end = source.index('void Config::ReadValues()', start)
# CodexAstraLocal: Exercise the real post-load order, including profile enforcement of the opt-in.
apply_start = source.index('    ApplyTestingOverlay();', end)
apply_end = source.index('\n}\n\nvoid Config::Reload()', apply_start)
apply_effective = ('void ApplyEffective(Config& cfg) {\n' +
                   source[apply_start:apply_end].replace('ApplyTestingOverlay();',
                                                       'cfg.ApplyTestingOverlay();', 1) + '\n}\n')
# CodexAstraLocal: Production methods and real setting classes are compiled; only file/log I/O is substituted.
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
namespace Settings { Values values; }
// CodexAstraLocal: Only the compiled version boundary is replaceable; exercise both policies.
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
    cfg.android_config = Parse("[Renderer]\nresolution_factor=2\nuberhar_cpu_vertex_only=false\n"
                               "uberhar_test_mode=3\n[Audio]\nvolume=0.7\n[System]\nis_new_3ds=true\n");
    cfg.title_config = Parse("[Renderer]\nresolution_factor=4\nuberhar_cpu_vertex_only=true\n"
                             "[Audio]\nvolume=broken\n[System]\nis_new_3ds=false\n");
    auto& v = Settings::values;
    cfg.ReadSetting("Renderer", v.resolution_factor);
    assert(v.resolution_factor.GetValue() == 4 && v.resolution_factor.GetValue(true) == 2);
    assert(!v.resolution_factor.UsingGlobal());
    cfg.ReadSetting("Renderer", v.uberhar_cpu_vertex_only);
    assert(v.uberhar_cpu_vertex_only.GetValue() && !v.uberhar_cpu_vertex_only.GetValue(true));
    cfg.ReadSetting("Audio", v.volume);
    assert(v.volume.GetValue() == 0.7f); // CodexAstraLocal: malformed override inherits typed global
    cfg.ReadSetting("System", v.is_new_3ds);
    assert(v.is_new_3ds.GetValue()); // CodexAstraLocal: device model remains global
    cfg.ReadSetting("Layout", v.large_screen_proportion, 2.25f);
    assert(v.large_screen_proportion.GetValue() == 2.25f); // CodexAstraLocal: preserve Android fallback
    cfg.runtime_reload = true;
    cfg.title_config = Parse("[Renderer]\nresolution_factor=3\nuberhar_cpu_vertex_only=false\n");
    cfg.ReadSetting("Renderer", v.uberhar_cpu_vertex_only);
    assert(v.uberhar_cpu_vertex_only.GetValue()); // CodexAstraLocal: restart-only resource unchanged
    cfg.ReadSetting("Renderer", v.resolution_factor);
    assert(v.resolution_factor.GetValue() == 3); // CodexAstraLocal: ordinary live resolution still works
    cfg.runtime_reload = false;
    FileUtil::test_file = "[Testing]\nenabled=true\npreset=1\nresolution=4\n";
    cfg.ApplyTestingOverlay();
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Automatic);
    assert(v.uberhar_cpu_vertex_only.GetValue() && v.resolution_factor.GetValue() == 4);
    cfg.runtime_reload = true;
    FileUtil::test_file = "[Testing]\nenabled=false\npreset=0\nresolution=2\n";
    cfg.ReadSetting("Renderer", v.resolution_factor);
    cfg.ApplyTestingOverlay();
    assert(v.resolution_factor.GetValue() == 2); // CodexAstraLocal: live Testing resolution
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Automatic);
    assert(v.uberhar_cpu_vertex_only.GetValue() && testing_overlay_active); // model/master stay frozen
    cfg.runtime_reload = false;
    FileUtil::test_file = "[Testing]\nenabled=true\npreset=2\nresolution=2\n";
    cfg.ApplyTestingOverlay();
    assert(!v.uberhar_cpu_vertex_only.GetValue() && v.resolution_factor.GetValue() == 2);
    FileUtil::test_file = "[Testing]\nenabled=true\npreset=0\nresolution=2\n"
                          "[Testing.000400000F700100]\npreset=1\nresolution=4\n";
    cfg.ApplyTestingOverlay();
    assert(v.uberhar_cpu_vertex_only.GetValue() && v.resolution_factor.GetValue() == 4);
    FileUtil::test_file = "[Testing]\nenabled=false\n";
    cfg.ReadSetting("Renderer", v.resolution_factor);
    cfg.ApplyTestingOverlay();
    assert(v.resolution_factor.GetValue() == 3 && !testing_overlay_active);
    cfg.title_config.reset();
    cfg.ReadSetting("Renderer", v.resolution_factor);
    assert(v.resolution_factor.GetValue() == 2 && v.resolution_factor.UsingGlobal());
    FileUtil::test_file = "[Testing]\nenabled=true\npreset=9\nresolution=4\n";
    cfg.ApplyTestingOverlay();
    assert(v.resolution_factor.GetValue() == 4 && testing_overlay_active);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Native);
    // CodexAstraLocal: Native and both Combo presets accept only an enabled explicit title opt-in.
    for (int preset = 0; preset <= 2; ++preset) {
        for (bool requested : {false, true}) {
            FileUtil::test_file = "[Testing]\nenabled=true\ndisable_right_eye_render=true\npreset=" +
                std::to_string(preset) + "\n[Testing.000400000F700100]\ndisable_right_eye_render=" +
                (requested ? "true\n" : "false\n");
            ApplyEffective(cfg);
            assert(v.disable_right_eye_render.GetValue() == requested);
            assert(v.uberhar_test_mode.GetValue() == (preset == 0 ? Settings::UberharTestMode::Native :
                                                                                Settings::UberharTestMode::Automatic));
            assert(v.uberhar_cpu_vertex_only.GetValue() == (preset == 1));
        }
    }
    // CodexAstraLocal: Live resolution reloads must not consume a pending eye/master/preset change.
    cfg.runtime_reload = true;
    FileUtil::test_file = "[Testing]\nenabled=false\npreset=0\nresolution=4\n"
                         "[Testing.000400000F700100]\ndisable_right_eye_render=false\n";
    cfg.ReadSetting("Renderer", v.disable_right_eye_render);
    ApplyEffective(cfg);
    assert(v.disable_right_eye_render.GetValue() && v.resolution_factor.GetValue() == 4);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Automatic);
    for (const auto& missing_or_broken : {"", "[broken"}) {
        FileUtil::test_file = missing_or_broken;
        ApplyEffective(cfg);
        assert(v.disable_right_eye_render.GetValue());
    }
    cfg.runtime_reload = false;
    for (const auto& title_choice : {"", "[Testing.000400000F700100]\ndisable_right_eye_render=broken\n"}) {
        FileUtil::test_file = std::string("[Testing]\nenabled=true\ndisable_right_eye_render=true\n") + title_choice;
        ApplyEffective(cfg);
        assert(!v.disable_right_eye_render.GetValue());
    }
    // CodexAstraLocal: A different title and global-only context cannot inherit another title's opt-in.
    FileUtil::test_file = "[Testing]\nenabled=true\ndisable_right_eye_render=true\n"
                         "[Testing.000400000F700100]\ndisable_right_eye_render=true\n";
    Config other_title{0x400000123400};
    ApplyEffective(other_title);
    assert(!v.disable_right_eye_render.GetValue());
    Config global;
    ApplyEffective(global);
    assert(!testing_overlay_active && !testing_overlay_disable_right_eye_render);
    // CodexAstraLocal: All controls share per-title isolation and a launch latch. CPU-only
    // Combo vetoes hardware vertices without affecting the guest scheduling experiment.
    for (int preset = 0; preset <= 2; ++preset) {
        cfg.runtime_reload = false;
        FileUtil::test_file = "[Testing]\nenabled=true\npreset=" + std::to_string(preset) +
            "\n[Testing.000400000F700100]\nuse_hw_shader=true\ndelay_game_render_thread_us=100\n"
            "filter_mode=false\nfactor_3d=1\n";
        ApplyEffective(cfg);
        assert(v.use_hw_shader.GetValue() == (preset != 1));
        assert(v.delay_game_render_thread_us.GetValue() == 100);
        assert(!v.filter_mode.GetValue() && v.factor_3d.GetValue() == 1);
        assert(v.render_3d.GetValue() == Settings::StereoRenderOption::Off);
        cfg.runtime_reload = true;
        FileUtil::test_file = "[Testing]\nenabled=false\npreset=1\nresolution=4\n"
            "[Testing.000400000F700100]\nuse_hw_shader=false\ndelay_game_render_thread_us=0\n"
            "filter_mode=true\nfactor_3d=0\n";
        ApplyEffective(cfg);
        assert(v.use_hw_shader.GetValue() == (preset != 1));
        assert(v.delay_game_render_thread_us.GetValue() == 100);
        assert(!v.filter_mode.GetValue() && v.factor_3d.GetValue() == 1);
        assert(v.resolution_factor.GetValue() == 4);
        FileUtil::test_file = "";
        ApplyEffective(cfg);
        assert(v.use_hw_shader.GetValue() == (preset != 1));
        assert(v.delay_game_render_thread_us.GetValue() == 100);
        assert(!v.filter_mode.GetValue() && v.factor_3d.GetValue() == 1);
    }
    cfg.runtime_reload = false;
    FileUtil::test_file = "[Testing]\nenabled=true\npreset=0\nuse_hw_shader=true\n"
        "delay_game_render_thread_us=100\nfilter_mode=false\nfactor_3d=1\n";
    ApplyEffective(cfg);
    assert(!v.use_hw_shader.GetValue() && v.delay_game_render_thread_us.GetValue() == 0);
    assert(v.filter_mode.GetValue() && v.factor_3d.GetValue() == 0);
    for (const auto* invalid_delay : {"-1", "16001", "broken", "100garbage", "1garbage", "0x10", "2147483648"}) {
        FileUtil::test_file = std::string("[Testing]\nenabled=true\n[Testing.000400000F700100]\n") +
            "delay_game_render_thread_us=" + invalid_delay + "\nfactor_3d=" + invalid_delay + "\nfilter_mode=broken\n";
        ApplyEffective(cfg);
        assert(v.delay_game_render_thread_us.GetValue() == 0 && v.factor_3d.GetValue() == 0);
        assert(v.filter_mode.GetValue());
    }
    // CodexAstraLocal: Leading zero and '+' values agree with Kotlin decimal parsing, not octal.
    for (const auto* decimal : {"0100", "+100"}) {
        FileUtil::test_file = std::string("[Testing]\nenabled=true\n[Testing.000400000F700100]\n") +
            "delay_game_render_thread_us=" + decimal + "\nfactor_3d=+1\n";
        ApplyEffective(cfg);
        assert(v.delay_game_render_thread_us.GetValue() == 100 && v.factor_3d.GetValue() == 1);
    }
    // CodexAstraLocal: Beta/full builds ignore the entire stale Testing layer, including mode/scale.
    Common::g_uberhar_alpha_testing = false;
    FileUtil::test_file = "[Testing]\nenabled=true\npreset=2\nresolution=4\n"
        "[Testing.000400000F700100]\nuse_hw_shader=true\ndelay_game_render_thread_us=100\n";
    v.uberhar_test_mode = Settings::UberharTestMode::Custom;
    v.resolution_factor = 3;
    v.use_hw_shader = true;
    v.delay_game_render_thread_us = 250;
    v.filter_mode = false;
    v.factor_3d = 7;
    ApplyEffective(cfg);
    assert(!testing_overlay_active && !testing_overlay_hardware_shaders);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    assert(v.resolution_factor.GetValue() == 3 && v.use_hw_shader.GetValue());
    assert(v.delay_game_render_thread_us.GetValue() == 250);
    assert(!v.filter_mode.GetValue() && v.factor_3d.GetValue() == 7);
    Common::g_uberhar_alpha_testing = true;
    // CodexAstraLocal: Testing OFF reloads ordinary saved values; profiles keep their normal OFF policy.
    // CodexAstraLocal: Persisted mode 0 is Custom; 3 is Combo, which correctly forces this flag OFF.
    cfg.android_config = Parse("[Renderer]\nuberhar_test_mode=0\ndisable_right_eye_render=true\n");
    FileUtil::test_file = "[Testing]\nenabled=false\n"
                         "[Testing.000400000F700100]\ndisable_right_eye_render=true\n";
    cfg.ReadSetting("Renderer", v.uberhar_test_mode);
    cfg.ReadSetting("Renderer", v.disable_right_eye_render);
    ApplyEffective(cfg);
    assert(!testing_overlay_active && !testing_overlay_disable_right_eye_render);
    assert(v.uberhar_test_mode.GetValue() == Settings::UberharTestMode::Custom);
    assert(v.disable_right_eye_render.GetValue());
    v.uberhar_test_mode = Settings::UberharTestMode::Native;
    ApplyEffective(cfg);
    assert(!v.disable_right_eye_render.GetValue());
    std::cout << "Android title/testing typed layers and right-eye profile priority: PASS\n";
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
