#!/usr/bin/env python3
"""CodexAstraLocal: Compile the real bounded capture owner with modeled host IO.

PICA state, capture policy, snapshots, serialization and phase/submission handling
remain production code. File-provider and Vulkan mapping plumbing are modeled;
this does not execute a GPU draw or establish Android storage compatibility.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import vertex_capture as vc


def main():
    root = Path("build/uberhar-probe/vertex-capture-session")
    stubs = root / "stubs"
    (stubs / "common").mkdir(parents=True, exist_ok=True)
    (stubs / "video_core/renderer_vulkan").mkdir(parents=True, exist_ok=True)
    # CodexAstraLocal: Real local files permit independent artifact inspection and
    # injected provider failures; counters expose unintended disabled-path reads.
    (stubs / "common/file_util.h").write_text(r'''
#pragma once
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include "common/common_types.h"
namespace FileUtil {
enum class UserPath { ConfigDir, DumpDir };
inline std::string root;
inline unsigned exists_calls{}, reads{}, writes{};
inline bool fail_write{}, throw_exists{}, fail_directory{};
inline std::string GetUserPath(UserPath p) {
    return root + (p == UserPath::ConfigDir ? "/config/" : "/dump/");
}
inline bool Exists(const std::string& p) {
    ++exists_calls;
    if (throw_exists) throw std::runtime_error("injected provider failure");
    return std::filesystem::exists(p);
}
inline bool CreateFullPath(const std::string& p) {
    if (fail_directory) return false;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path());
    return true;
}
class IOFile {
public:
    IOFile(const std::string& p, const char* mode, int = 0) : f(std::fopen(p.c_str(), mode)) {}
    ~IOFile() { if (f) std::fclose(f); }
    bool IsOpen() const { return f; }
    u64 GetSize() const {
        if (!f) return 0;
        const long pos = std::ftell(f); std::fseek(f, 0, SEEK_END);
        const long size = std::ftell(f); std::fseek(f, pos, SEEK_SET);
        return size;
    }
    std::size_t ReadBytes(void* p, std::size_t n) { ++reads; return f ? std::fread(p, 1, n, f) : 0; }
    std::size_t WriteBytes(const void* p, std::size_t n) {
        ++writes; return f && !fail_write ? std::fwrite(p, 1, n, f) : 0;
    }
    bool Flush() { return f && std::fflush(f) == 0; }
    bool Close() { if (!f) return false; auto* old=f; f=nullptr; return std::fclose(old)==0; }
private:
    FILE* f{};
};
}
''')
    # CodexAstraLocal: The production owner must read exactly the selected UBO
    # semantic ranges, including reused offsets; no Map/Commit API is provided.
    (stubs / "video_core/renderer_vulkan/vk_stream_buffer.h").write_text(r'''
#pragma once
#include <array>
#include <cstring>
#include <span>
#include "common/common_types.h"
namespace Vulkan {
class StreamBuffer {
public:
    std::array<u8,8192> bytes{};
    mutable unsigned copies{};
    bool CopyHostWrittenBytes(u64 offset, std::span<u8> out) const noexcept {
        ++copies;
        if (offset > bytes.size() || out.size() > bytes.size()-offset) return false;
        std::memcpy(out.data(),bytes.data()+offset,out.size()); return true;
    }
};
}
''')
    command = [os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-pthread",
               "-DFMT_HEADER_ONLY", "-DXXH_INLINE_ALL", f"-I{stubs}", "-Isrc",
               "-Iexternals/fmt/include", "-Iexternals/boost", "-Iexternals/xxHash",
               "-Iexternals/nihstro/include", "-Iexternals/vulkan-headers/include",
               "-Iexternals/json", "tools/uberhar/test_vertex_capture.cpp",
               "src/video_core/renderer_vulkan/vk_vertex_capture.cpp",
               "src/video_core/pica/shader_setup.cpp", "-o", str(root / "test-session")]
    subprocess.run(command, check=True, timeout=120)
    with tempfile.TemporaryDirectory(prefix="fixtures-", dir=root) as fixtures:
        subprocess.run([str(root / "test-session"), fixtures], check=True, timeout=30)
        # CodexAstraLocal: Validate actual production Writer output using the
        # independent Python protocol reader, not only matching synthetic JSON.
        artifacts = sorted((Path(fixtures) / "dump/uberhar_vertex_capture").glob("*.uvc"))
        for artifact in artifacts:
            vc.read(artifact)
        captured = vc.read(Path(fixtures) / "dump/uberhar_vertex_capture/immutable.uvc")
        indices = vc.replay_packet(captured, captured.manifest["packets"][0])
        assert indices == [60000, 60001, 60002]
        print(f"PASS: {len(artifacts)} actual producer artifacts accepted by independent reader; "
              "indexed full-layout packet passes reconstruction checks")


if __name__ == "__main__":
    main()
