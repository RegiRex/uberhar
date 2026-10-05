#!/usr/bin/env python3
"""CodexAstraUlt-2: Fault-inject production readback and debug-scope cleanup.

Execute the actual Surface::Download control flow and DebugScope destructor with
mock scheduler/storage plumbing. Replace only Vulkan copy-command construction
and rectangle constants: no GPU/device behavior is claimed by this host test.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess


def extract(source: str, signature: str) -> str:
    start = source.index(signature)
    return source[start:source.index("\n}\n", start) + 3]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path,
                        default=Path("src/video_core/renderer_vulkan/vk_texture_runtime.cpp"))
    parser.add_argument("--output", type=Path,
                        default=Path("build/uberhar-probe/download-recovery"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    source = args.source.read_text()
    download = extract(source, "void Surface::Download(")
    debug = extract(source, "DebugScope::~DebugScope()")
    # CodexAstraUlt-2: Omit Vulkan payload construction, preserving its one Record
    # call and every production branch/completion/exception boundary around it.
    begin = download.index("    const RecordParams params = {")
    end = download.index("\n        });", begin) + len("\n        });")
    download = download[:begin] + "    scheduler.Record([](vk::CommandBuffer) {});" + download[end:]
    download, replacements = re.subn(
        r"const VideoCore::TextureBlit blit = \{.*?\n        \};",
        "const VideoCore::TextureBlit blit{};", download, flags=re.S)
    if replacements != 1:
        raise RuntimeError("Readback blit construction changed; review the extraction")

    harness = r'''
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/scope_exit.h"
#include "video_core/shader_build_failure.h"

namespace vk {
struct CommandBuffer { void endDebugUtilsLabelEXT() {} };
}
namespace VideoCore {
struct BufferTextureCopy {};
struct StagingData { unsigned size{64}; };
struct TextureBlit {};
}
namespace Vulkan {
struct Scheduler {
    VideoCore::ShaderFailureState failure;
    std::string fail_at;
    std::vector<std::string> events;
    void Visit(const char* name) {
        events.emplace_back(name);
        if (fail_at == name) failure.Fail();
        failure.Check();
    }
    void Finish() { Visit("finish"); }
    template<class F> void Record(F&& function) {
        Visit("record");
        function(vk::CommandBuffer{});
    }
};
struct RenderManager {
    Scheduler& scheduler;
    void EndRendering() { scheduler.Visit("begin"); }
};
struct Buffer {
    Scheduler& scheduler;
    unsigned committed{};
    int Handle() const { return 1; }
    void Commit(unsigned size) {
        scheduler.events.emplace_back("commit");
        committed += size;
    }
};
struct BlitHelper {
    Scheduler& scheduler;
    template<class Surface> void DepthToBuffer(Surface&, int, const VideoCore::BufferTextureCopy&) {
        scheduler.Visit("depth");
    }
};
struct Runtime {
    RenderManager renderpass_cache;
    Buffer download_buffer;
    BlitHelper blit_helper;
    explicit Runtime(Scheduler& scheduler)
        : renderpass_cache{scheduler}, download_buffer{scheduler}, blit_helper{scheduler} {}
};
enum class PixelFormat { RGBA8, D24S8 };
struct Surface {
    Scheduler& scheduler;
    Runtime runtime;
    PixelFormat pixel_format;
    unsigned res_scale;
    Surface(Scheduler& scheduler_, PixelFormat format, unsigned scale)
        : scheduler{scheduler_}, runtime{scheduler_}, pixel_format{format}, res_scale{scale} {}
    void BlitScale(const VideoCore::TextureBlit&, bool) { scheduler.Visit("blit"); }
    void Download(const VideoCore::BufferTextureCopy&, const VideoCore::StagingData&);
};
struct DebugScope {
    Scheduler& scheduler;
    bool has_debug_tool;
    ~DebugScope();
};
'''
    tests = r'''
} // namespace Vulkan

void Check(bool result, const char* message) {
    if (!result) throw std::runtime_error{message};
}
int main() {
    using namespace Vulkan;
    unsigned cases = 0;
    for (const bool depth : {false, true}) {
        for (const unsigned scale : {1U, 2U}) {
            std::vector<std::string> healthy{"begin"};
            if (depth) healthy.emplace_back("depth");
            else {
                if (scale != 1) healthy.emplace_back("blit");
                healthy.emplace_back("record");
            }
            healthy.emplace_back("finish");
            healthy.emplace_back("commit");
            // CodexAstraUlt-2: Inject at every actual scheduling/sync point,
            // including the D24S8 early-return path and failed readback Finish.
            for (std::size_t fault = 0; fault < healthy.size(); ++fault) {
                Scheduler scheduler;
                const bool succeeds = fault == healthy.size() - 1;
                scheduler.fail_at = succeeds ? "" : healthy[fault];
                Surface surface{scheduler, depth ? PixelFormat::D24S8 : PixelFormat::RGBA8, scale};
                bool caught = false;
                try { surface.Download({}, {}); }
                catch (const VideoCore::ShaderRecoveryError&) { caught = true; }
                Check(caught != succeeds, "Readback failure did not reach its caller");
                const auto end = succeeds ? healthy.size() : fault + 1;
                Check(scheduler.events == std::vector<std::string>(healthy.begin(), healthy.begin() + end),
                      "Readback continued or committed after failure");
                Check(surface.runtime.download_buffer.committed == (succeeds ? 64U : 0U),
                      "Readback committed stale staging data");
                ++cases;
            }
        }
    }
    {
        Scheduler scheduler;
        { DebugScope scope{scheduler, false}; }
        Check(scheduler.events.empty(), "Disabled debug scope recorded a command");
        { DebugScope scope{scheduler, true}; }
        Check(scheduler.events == std::vector<std::string>{"record"}, "Healthy label was not closed");
    }
    {
        Scheduler scheduler;
        scheduler.fail_at = "record";
        { DebugScope scope{scheduler, true}; }
        Check(scheduler.failure.Failed(), "Debug cleanup erased the terminal error");
        bool caught = false;
        try { scheduler.failure.Check(); }
        catch (const VideoCore::ShaderRecoveryError&) { caught = true; }
        Check(caught, "Debug cleanup hid terminal error from producer");
    }
    {
        Scheduler scheduler;
        bool caught = false;
        try {
            DebugScope scope{scheduler, true};
            scheduler.failure.Fail();
            scheduler.failure.Check();
        } catch (const VideoCore::ShaderRecoveryError&) { caught = true; }
        Check(caught, "Debug destructor replaced the original exception");
    }
    std::printf("PASS: %u production readback paths/faults; debug cleanup retains terminal error without termination\n", cases);
}
'''
    args.output.mkdir(parents=True, exist_ok=True)
    generated = args.output / "download_recovery.cpp"
    binary = args.output / "download_recovery"
    generated.write_text(harness + download + "\n" + debug + tests)
    command = [os.environ.get("CXX", "c++"), "-std=c++20", "-O1", "-g", "-pthread", "-Isrc"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    subprocess.run(command + [str(generated), "-o", str(binary)], check=True)
    subprocess.run([str(binary.resolve())], check=True, timeout=15)


if __name__ == "__main__":
    main()
