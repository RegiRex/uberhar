#!/usr/bin/env python3
"""CodexAstraLocal: Run real finite timing owner/loop tests with modeled file IO.

No device operation or guest payload is used. Fake clocks test accounting and
failure containment; these tests cannot measure Android timer cost.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import vertex_timing


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=Path("build/uberhar-probe/vertex-timing"))
    parser.add_argument("--keep-fixtures", action="store_true")
    args = parser.parse_args()
    target = args.output
    stubs = target / "stubs/common"
    stubs.mkdir(parents=True, exist_ok=True)
    # CodexAstraLocal: Only file-provider plumbing is modeled. Exclusive fopen,
    # actual config parsing, bounded formatting and owner teardown are exercised.
    (stubs / "file_util.h").write_text(r'''
#pragma once
#include <cstdio>
#include <filesystem>
#include <string>
#include "common/common_types.h"
namespace FileUtil {
enum class UserPath {ConfigDir,DumpDir};
inline std::string root;
inline unsigned writes{};
inline bool short_write{},fail_flush{},fail_directory{};
inline std::string GetUserPath(UserPath p) {return root+(p==UserPath::ConfigDir?"/config/":"/dump/");}
inline bool Exists(const std::string& p) {return std::filesystem::exists(p);}
inline bool CreateFullPath(const std::string& p) {
 if(fail_directory)throw std::runtime_error("injected directory failure");
 std::filesystem::create_directories(std::filesystem::path(p).parent_path());return true;
}
class IOFile {
 FILE* f{};
public:
 IOFile(const std::string& p,const char* mode):f(std::fopen(p.c_str(),mode)){}
 ~IOFile(){if(f)std::fclose(f);}
 bool IsOpen()const{return f;}
 u64 GetSize()const{if(!f)return 0;auto pos=std::ftell(f);std::fseek(f,0,SEEK_END);
  auto size=std::ftell(f);std::fseek(f,pos,SEEK_SET);return size;}
 std::size_t ReadBytes(void* p,std::size_t n){return f?std::fread(p,1,n,f):0;}
 std::size_t WriteBytes(const void*p,std::size_t n){++writes;return f?std::fwrite(p,1,short_write?n/2:n,f):0;}
 bool Flush(){return !fail_flush&&f&&std::fflush(f)==0;}
 bool Close(){if(!f)return false;auto* old=f;f=nullptr;return std::fclose(old)==0;}
};
}
''')
    command = [os.environ.get("CXX", "c++"), "-std=c++20", "-O2", "-pthread",
               "-DFMT_HEADER_ONLY", f"-I{target / 'stubs'}", "-Isrc",
               "-Iexternals/fmt/include", "-Iexternals/boost", "-Iexternals/json",
               "tools/uberhar/test_vertex_timing.cpp",
               "src/video_core/pica/uberhar_vertex_timing.cpp",
               "src/video_core/pica/shader_unit.cpp",
               "src/video_core/pica/primitive_assembly.cpp",
               "-o", str(target / "test-timing")]
    subprocess.run(command, check=True, timeout=120)
    def run(fixture):
        subprocess.run([str(target / "test-timing"), str(fixture)], check=True, timeout=60)
        artifacts = sorted(Path(fixture).glob("*.json"))
        for artifact in artifacts:
            vertex_timing.read_report(artifact)
        print(f"PASS: {len(artifacts)} actual producer reports accepted by independent reader")
    if args.keep_fixtures:
        fixture = Path(tempfile.mkdtemp(prefix="fixtures-", dir=target))
        run(fixture)
        print(f"Retained fixtures: {fixture}")
    else:
        with tempfile.TemporaryDirectory(prefix="fixtures-", dir=target) as fixture:
            run(fixture)


if __name__ == "__main__":
    main()
