// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Exercise production read-only inventory and reuse labels against real
// temporary files, including ambiguity that must never be reported as empty.
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "video_core/renderer_vulkan/uberhar_cache_diagnostics.h"

namespace fs = std::filesystem;
using namespace Vulkan::UberharCacheDiagnostics;
void Require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    Require(std::string_view{Snapshot{}.State()} == "not_observed", "unloaded snapshot mislabeled");
    const auto root = fs::temp_directory_path() /
                      ("uberhar-cache-test-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        fs::path path;
        ~Cleanup() {
            std::error_code error;
            fs::remove_all(path, error);
        }
    } cleanup{root};
    const auto pipeline = root / "pipeline", transfer = root / "transferable";
    const auto driver = pipeline / "0000000000000001-1234.bin";
    constexpr auto title = "0000000000000001";
    auto inspect = [&](bool enabled = true, bool specialized = false) {
        return Inspect(pipeline, driver, transfer, title, enabled, specialized);
    };
    Require(std::string_view{inspect().State()} == "empty_files", "missing namespace not empty");
    fs::create_directories(pipeline);
    fs::create_directories(transfer);
    std::ofstream(driver).close();
    Require(inspect().driver.files == 1 && inspect().driver.bytes == 0,
            "zero-length driver evidence lost");
    Require(std::string_view{inspect().State()} == "empty_files", "zero-byte cache not empty");
    const auto module = pipeline / "0000000000000001-uber-ABC.spv";
    std::ofstream(module) << "old-or-invalid-module";
    std::ofstream(pipeline / "0000000000000002-uber-ABC.spv") << "another title";
    std::ofstream(pipeline / "0000000000000001-uber-ABC.spv.tmp") << "incomplete write";
    auto state = inspect();
    Require(state.generic.files == 1 && state.generic.bytes == 21, "wrong namespace inventory");
    Require(std::string_view{state.State()} == "present_files", "presence silently validated");
    Require(fs::file_size(module) == 21, "inspection mutated cache bytes");
    Require(std::string_view{ReuseState(true, 0, 4)} == "cold_encountered",
            "present stale files must allow cold observed reuse");
    Require(std::string_view{ReuseState(true, 4, 0)} == "warm_encountered", "warm lookup");
    Require(std::string_view{ReuseState(true, 4, 1)} == "mixed", "mixed lookup");
    Require(std::string_view{ReuseState(true, 0, 0)} == "not_observed", "unseen is not cold");
    Require(std::string_view{ReuseState(false, 0, 3)} == "disabled", "disabled is not cold");
    const auto capped = InspectGeneric(pipeline, title, 1);
    Require(capped.capped && !capped.complete && capped.examined == 1, "scan bound not enforced");
    state.generic = capped;
    Require(std::string_view{state.State()} == "unknown", "partial scan claimed empty or warm");
    fs::remove(module);
    std::ofstream(transfer / "0000000000000001_fs.vkch") << "specialized";
    Require(std::string_view{inspect().State()} == "empty_files", "bypassed records affect Native");
    Require(std::string_view{inspect(true, true).State()} == "present_files",
            "active specialized records ignored");
    fs::create_directory(module);
    Require(!inspect().generic.complete && std::string_view{inspect().State()} == "unknown",
            "non-file entry silently ignored");
    const auto bad_directory = root / "not-a-directory";
    std::ofstream(bad_directory) << "file";
    Require(!InspectGeneric(bad_directory, title).complete, "scan error lost");
    Require(std::string_view{inspect(false).State()} == "disabled", "disabled inventory");
    Require(std::string_view{Inspect({}, driver, transfer, title, true, false).State()} ==
                "unknown",
            "unmapped virtual path claimed empty");
    Require(inspect(false).generic.examined == 0, "disabled inventory touched namespace");
    std::cout << "PASS: cache namespace, empty/present/disabled/unknown, scan cap, "
                 "stale-file versus reuse, and read-only inspection\n";
}
