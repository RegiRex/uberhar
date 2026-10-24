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
# CodexAstraLocal: Production methods and real setting classes are compiled; only file/log I/O is substituted.
prefix = r'''
#include <cassert>
#include <iostream>
#include <iomanip>
#include <memory>
#include <ranges>
#include <sstream>
#include <INIReader.h>
#include "common/settings.h"
#include "common/file_util.h"
#define private public
#include "jni/config.h"
#undef private
#define LOG_INFO(...)
#define LOG_ERROR(...)
namespace Settings { Values values; }
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
    std::cout << "Android title/testing typed layers: PASS\n";
}
'''
with tempfile.TemporaryDirectory(prefix='uberhar-settings-') as tmp:
    path = Path(tmp)
    (path / 'fixture.cpp').write_text(prefix + source[start:end] + main)
    command = ['c++', '-std=c++20', '-O0', '-DFMT_HEADER_ONLY', '-DENABLE_VULKAN', '-Isrc',
               '-Isrc/android/app/src/main', '-Ibuild/uberhar-profile',
               '-Iexternals/fmt/include', '-Iexternals/boost', '-I'+str(inih/'cpp'),
               str(path/'fixture.cpp'), str(inih/'cpp/INIReader.cpp'), str(inih/'ini.c'),
               '-o', str(path/'fixture')]
    subprocess.run(command, cwd=root, check=True)
    subprocess.run([str(path/'fixture')], cwd=root, check=True)
