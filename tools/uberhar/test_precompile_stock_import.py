#!/usr/bin/env python3
"""CodexAstraUlt-2: Execute the production read-only stock-cache import with fault fixtures.

The actual method and disk framing declarations are extracted. File/device shells and
semantic validators are mocked here; the separate recipe-codec test exercises real
FSConfig and pipeline validation. Zstd compression/decompression uses the host library.
"""

import argparse
import pathlib
import subprocess
import tempfile


def method(source: str) -> str:
    start = source.index("std::vector<Precompile::Recipe> ShaderDiskCache::ImportCompatibleRecipes(")
    end = source.index("void ShaderDiskCache::LoadPrecompileRecipes(", start)
    return source[start:end]


PREFIX = r'''
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>
#include <zstd.h>
using u8=uint8_t; using u16=uint16_t; using u32=uint32_t; using u64=uint64_t;
using u32_le=u32;
template<class T, bool=std::is_enum_v<T>> struct Underlying { using type=T; };
template<class T> struct Underlying<T,true> { using type=std::underlying_type_t<T>; };
template<unsigned O, unsigned N, class T> struct BitField {
    using U=typename Underlying<T>::type;
    U storage;
    T Value() const { return static_cast<T>((storage>>O)&((U{1}<<N)-1)); }
    operator T() const { return Value(); }
    void Assign(T v) { constexpr U mask=((U{1}<<N)-1)<<O;
        storage=(storage&~mask)|((static_cast<U>(v)<<O)&mask); }
};
static u64 hash_bytes(const void* p,size_t n) {
    u64 h=1469598103934665603ULL;
    for (auto b:std::span{static_cast<const u8*>(p),n}) h=(h^b)*1099511628211ULL;
    return h;
}
struct FSConfig {
    std::array<u32,68> words;
    explicit FSConfig(u32 id):words{} { words[0]=id; }
    u64 Hash() const { return hash_bytes(this,sizeof(*this)); }
    static constexpr u64 StructHash() { return 123; }
    bool operator==(const FSConfig&) const=default;
};
struct StaticPipelineInfo {
    std::array<u64,3> shader_ids;
    std::array<u32,30> words;
    static constexpr u64 StructHash() { return 456; }
    bool ExecutionEquals(const StaticPipelineInfo& x,bool) const {
        return words==x.words;
    }
};
struct PipelineInfo { StaticPipelineInfo state;
    u64 Hash() const { return hash_bytes(&state,sizeof(state)); }
};
struct Profile { u32 value{}; bool operator==(const Profile&) const=default; };
namespace Precompile {
constexpr size_t MaxRecipes=8192;
struct Recipe { FSConfig config; StaticPipelineInfo pipeline; u64 uses; };
bool IsValidConfig(const FSConfig& fs) { return fs.words[0]!=0xffffffffU; }
bool IsEligible(const FSConfig& fs,const StaticPipelineInfo& p) {
    return IsValidConfig(fs) && p.shader_ids==std::array<u64,3>{} &&
        p.words[0]==88 && p.words[1]==8 && p.words[2]==0;
}
}
static int logs{}, decompressions{};
static std::string last_status,last_phase,last_reason;
template<class... Rest> static void record_log(const char*,u64,const char* status,
    const char* phase,const char* reason,const Rest&...) {
    ++logs;last_status=status;last_phase=phase;last_reason=reason;
}
static std::atomic_bool* cancel_on_read{};
static int reads_before_cancel{};
static bool fail_read{};
#define LOG_INFO(category,...) record_log(__VA_ARGS__)
#define LOG_WARNING(...) (++logs)
#define LOG_ERROR(...) (++logs)
namespace FileUtil {
struct Source { std::vector<u8> bytes; u64 reported_size{}; };
static std::map<std::string,Source> files;
static bool Exists(const std::string& path) { return files.contains(path); }
struct IOFile {
    Source* src{};
    IOFile(const std::string& path,const char* mode) {
        assert(std::string(mode)=="rb");
        auto it=files.find(path); if(it!=files.end())src=&it->second;
    }
    explicit operator bool() const { return src; }
    u64 GetSize() const { return src->reported_size?src->reported_size:src->bytes.size(); }
    size_t ReadAtBytes(u8* dst,size_t n,size_t off) {
        if(fail_read)throw std::runtime_error("read fault");
        if(cancel_on_read && --reads_before_cancel==0)cancel_on_read->store(true);
        if(off>src->bytes.size() || n>src->bytes.size()-off)return 0;
        if(n)std::memcpy(dst,src->bytes.data()+off,n);
        return n;
    }
};
}
namespace Common::Compression {
size_t GetDecompressedSize(std::span<const u8> p) {
    return ZSTD_getFrameContentSize(p.data(),p.size());
}
std::vector<u8> DecompressDataZSTD(std::span<const u8> p) {
    ++decompressions;
    auto n=GetDecompressedSize(p); if(n>1024*1024)return {};
    std::vector<u8> out(n);
    auto got=ZSTD_decompress(out.data(),out.size(),p.data(),p.size());
    if(ZSTD_isError(got)||got!=n)return {};
    return out;
}
}
constexpr int VS=0,FS=1,GS=2;
class ShaderDiskCache {
public:
    using SourceFileCacheVersionHash=std::array<u8,64>;
    enum class CacheFileType:u32 { VS_CACHE,FS_CACHE,GS_CACHE,PL_CACHE,MAX };
    enum class CacheEntryType:u16 { FILE_INFO,VS_CONFIG,VS_PROGRAM,VS_SPIRV,
        FS_CONFIG,FS_SPIRV,GS_CONFIG,GS_SPIRV,PL_CONFIG,MAX };
    struct FileInfoEntry {
        static constexpr u32 CACHE_FILE_MAGIC=0x48434B56,CACHE_FILE_VERSION=0;
        u32_le cache_magic; u32 file_version; u64 config_struct_hash;
        CacheFileType file_type; SourceFileCacheVersionHash source_hash;
        std::array<char,0x20> build_name;
        union { u8 reserved[0x400]; Profile profile; };
    };
    struct FSConfigEntry {
        static constexpr u8 EXPECTED_VERSION=0;
        u64 version; FSConfig fs_config;
    };
    struct PLConfigEntry {
        static constexpr u8 EXPECTED_VERSION=0;
        u64 version; StaticPipelineInfo pl_info;
    };
    class CacheEntry {
    public:
        static constexpr u32 MAX_ENTRY_SIZE=4*1024*1024;
'''

MIDDLE = r'''
    };
    struct Parent { Profile profile{7}; } parent;
    u64 title_id=1;
    std::string GetFSFile(u64,bool)const{return "fs";}
    std::string GetPLFile(u64,bool)const{return "pl";}
    std::vector<Precompile::Recipe> ImportCompatibleRecipes(const std::atomic_bool* stop_loading=nullptr);
};
static_assert(sizeof(ShaderDiskCache::FileInfoEntry)==1144);
static_assert(sizeof(ShaderDiskCache::FSConfigEntry)==280);
static_assert(sizeof(ShaderDiskCache::PLConfigEntry)==152);
'''

TESTS = r'''
using Type=ShaderDiskCache::CacheEntryType;
using Kind=ShaderDiskCache::CacheFileType;
using Header=ShaderDiskCache::CacheEntry::CacheEntryHeader;
using Footer=ShaderDiskCache::CacheEntry::CacheEntryFooter;
using Info=ShaderDiskCache::FileInfoEntry;
using FSEntry=ShaderDiskCache::FSConfigEntry;
using PLEntry=ShaderDiskCache::PLConfigEntry;
template<class T> std::vector<u8> bytes(const T& value) {
    const auto* p=reinterpret_cast<const u8*>(&value);return {p,p+sizeof(value)};
}
static std::vector<u8> compressed(std::span<const u8> p) {
    std::vector<u8> out(ZSTD_compressBound(p.size()));
    auto n=ZSTD_compress(out.data(),out.size(),p.data(),p.size(),1);assert(!ZSTD_isError(n));
    out.resize(n);return out;
}
static std::vector<u8> frame(Type t,u64 id,std::vector<u8> payload,u32 ordinal,bool zip=false) {
    if(zip)payload=compressed(payload);
    Header h{};h.entry_version=Header::ENTRY_VERSION;h.type=t;h.id=id;
    h.entry_size=sizeof(Header)+payload.size()+sizeof(Footer);h.zstd_compressed.Assign(zip);
    Footer f{};f.version.Assign(Footer::ENTRY_VERSION);f.entry_id.Assign(ordinal);f.entry_size=h.entry_size;
    auto out=bytes(h);out.insert(out.end(),payload.begin(),payload.end());auto end=bytes(f);
    out.insert(out.end(),end.begin(),end.end());return out;
}
static std::vector<u8> info(Kind kind,u8 source=11,u32 profile=7) {
    Info i{};i.cache_magic=Info::CACHE_FILE_MAGIC;i.file_type=kind;
    i.config_struct_hash=kind==Kind::FS_CACHE?FSConfig::StructHash():StaticPipelineInfo::StructHash();
    i.source_hash.fill(source);i.profile.value=profile;return frame(Type::FILE_INFO,0,bytes(i),0);
}
static void append(std::vector<u8>& a,const std::vector<u8>& b){a.insert(a.end(),b.begin(),b.end());}
static std::vector<u8> pipeline(const FSConfig& fs,u32 ordinal=1,u64 vs=0,u64 gs=0,u32 stride=88,u64 fsid=0) {
    StaticPipelineInfo p{};p.shader_ids={vs,fsid?fsid:fs.Hash(),gs};p.words[0]=stride;p.words[1]=8;
    PipelineInfo pi{p};return frame(Type::PL_CONFIG,pi.Hash(),bytes(PLEntry{0,p}),ordinal);
}
static auto good() {
    std::map<std::string,FileUtil::Source> f;
    FSConfig fs{3};auto a=info(Kind::FS_CACHE);append(a,frame(Type::FS_CONFIG,fs.Hash(),bytes(FSEntry{0,fs}),1));
    auto b=info(Kind::PL_CACHE);append(b,pipeline(fs));f["fs"].bytes=a;f["pl"].bytes=b;return f;
}
static void setword(std::vector<u8>& b,size_t off,u32 v){std::memcpy(b.data()+off,&v,4);}
static int cases{};
static void expect(size_t n,const char* name) {
    ShaderDiskCache cache;const auto before=FileUtil::files;const int previous_logs=logs;
    auto out=cache.ImportCompatibleRecipes();
    if(out.size()!=n){std::cerr<<"FAIL "<<name<<": "<<out.size()<<" expected "<<n<<"\n";std::exit(1);}
    for(const auto& [key,value]:before)assert(value.bytes==FileUtil::files.at(key).bytes);
    for(const auto& r:out){assert(r.uses==1);assert((r.pipeline.shader_ids==std::array<u64,3>{}));}
    assert(logs==previous_logs+1);
    ++cases;
}
int main(){
    const size_t first=sizeof(Header)+sizeof(Info)+sizeof(Footer);
    const size_t fsentry=sizeof(Header)+sizeof(FSEntry)+sizeof(Footer);
    FileUtil::files=good();expect(1,"valid");assert(last_status=="complete");
    FileUtil::files=good();FileUtil::files.erase("fs");expect(0,"missing");assert(last_status=="missing");
    for(size_t n:{0u,1u,15u,16u,31u}){FileUtil::files=good();FileUtil::files["fs"].bytes.resize(n);expect(0,"short");}
    FileUtil::files=good();FileUtil::files["fs"].reported_size=513ULL*1024*1024;expect(0,"file cap");
    FileUtil::files=good();FileUtil::files["fs"].bytes.back()=1;expect(0,"footer reserved");assert(last_status=="rejected");
    FileUtil::files=good();setword(FileUtil::files["fs"].bytes,first+fsentry-sizeof(Footer)+4,99);expect(0,"footer size");
    FileUtil::files=good();setword(FileUtil::files["fs"].bytes,first+fsentry-sizeof(Footer),0x224);expect(0,"ordinal");
    FileUtil::files=good();FileUtil::files["fs"].bytes[first+1]=2;expect(0,"reserved flags");
    FileUtil::files=good();setword(FileUtil::files["fs"].bytes,first+4,0xffffffff);expect(0,"large frame");
    FileUtil::files=good();FileUtil::files["fs"].bytes.push_back(0);expect(0,"trailing byte");
    FSConfig fs{3};
    FileUtil::files=good();FileUtil::files["fs"].bytes=info(Kind::FS_CACHE,11,8);
    append(FileUtil::files["fs"].bytes,frame(Type::FS_CONFIG,fs.Hash(),bytes(FSEntry{0,fs}),1));
    FileUtil::files["pl"].bytes=info(Kind::PL_CACHE,11,8);append(FileUtil::files["pl"].bytes,pipeline(fs));expect(0,"profile mismatch");assert(last_status=="mismatched");
    FileUtil::files=good();FileUtil::files["pl"].bytes=info(Kind::PL_CACHE,12);append(FileUtil::files["pl"].bytes,pipeline(fs));expect(0,"source mismatch");
    FileUtil::files=good();setword(FileUtil::files["fs"].bytes,sizeof(Header)+8,999);expect(0,"schema mismatch");
    FileUtil::files=good();FileUtil::files["fs"].bytes=info(Kind::FS_CACHE);
    append(FileUtil::files["fs"].bytes,frame(Type::FS_CONFIG,1234,bytes(FSEntry{0,fs}),1));
    FileUtil::files["pl"].bytes=info(Kind::PL_CACHE);append(FileUtil::files["pl"].bytes,pipeline(fs,1,0,0,88,1234));expect(0,"FS identity");
    FileUtil::files=good();FileUtil::files["pl"].bytes=info(Kind::PL_CACHE);append(FileUtil::files["pl"].bytes,pipeline(fs,1,9));expect(0,"GPU VS excluded");
    FileUtil::files=good();FileUtil::files["pl"].bytes=info(Kind::PL_CACHE);append(FileUtil::files["pl"].bytes,pipeline(fs,1,0,9));expect(0,"GPU GS excluded");
    FileUtil::files=good();FileUtil::files["pl"].bytes=info(Kind::PL_CACHE);append(FileUtil::files["pl"].bytes,pipeline(fs,1,0,0,80));expect(0,"layout excluded");
    FileUtil::files=good();append(FileUtil::files["pl"].bytes,pipeline(fs,2));expect(1,"dedup");
    FileUtil::files=good();FileUtil::files["pl"].bytes[first+8]^=1;expect(0,"PL identity");
    FileUtil::files=good();append(FileUtil::files["pl"].bytes,pipeline(fs,2));FileUtil::files["pl"].bytes.back()=1;expect(0,"no partial result");
    FileUtil::files=good();FileUtil::files["fs"].bytes=info(Kind::FS_CACHE);
    append(FileUtil::files["fs"].bytes,frame(Type::FS_CONFIG,fs.Hash(),bytes(FSEntry{0,fs}),1,true));
    decompressions=0;expect(1,"compressed config");assert(decompressions==1);
    FileUtil::files=good();append(FileUtil::files["fs"].bytes,frame(Type::FS_SPIRV,9,{0xde,0xad},2));
    FileUtil::files["fs"].bytes[first+fsentry+1]=1;decompressions=0;expect(1,"skip opaque SPIRV");assert(decompressions==0);
    FileUtil::files=good();FileUtil::files["fs"].bytes=info(Kind::FS_CACHE);
    append(FileUtil::files["fs"].bytes,frame(Type::FS_CONFIG,fs.Hash(),std::vector<u8>(65536),1,true));
    decompressions=0;expect(0,"oversize decompression");assert(decompressions==0);
    FileUtil::files=good();fail_read=true;expect(0,"read exception");fail_read=false;assert(last_status=="failed" && last_reason=="read fault");
    FileUtil::files=good();std::atomic_bool stop{true};ShaderDiskCache cache;
    assert(cache.ImportCompatibleRecipes(&stop).empty());assert(last_status=="cancelled");++cases;
    stop=false;cancel_on_read=&stop;reads_before_cancel=5;
    assert(cache.ImportCompatibleRecipes(&stop).empty());assert(last_status=="cancelled");cancel_on_read=nullptr;++cases;
    FileUtil::files=good();FileUtil::files["fs"].bytes=info(Kind::FS_CACHE);
    for(u32 i=0;i<Precompile::MaxRecipes+1;++i){FSConfig item{i+1};
        append(FileUtil::files["fs"].bytes,frame(Type::FS_CONFIG,item.Hash(),bytes(FSEntry{0,item}),i+1));}
    FileUtil::files["pl"].bytes=info(Kind::PL_CACHE);append(FileUtil::files["pl"].bytes,pipeline(FSConfig{8193}));expect(0,"FS inventory cap");
    FileUtil::files=good();FileUtil::files["fs"].bytes=info(Kind::FS_CACHE);
    for(u32 i=1;i<=131072;++i)append(FileUtil::files["fs"].bytes,frame(Type::FS_SPIRV,0,{},i));
    expect(0,"entry count cap");
    std::cout<<"PASS production stock import: "<<cases<<" cases\n";
}
'''


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    source = (root / "src/video_core/renderer_vulkan/vk_shader_disk_cache.cpp").read_text()
    header = (root / "src/video_core/renderer_vulkan/vk_shader_disk_cache.h").read_text()
    start = header.index("struct CacheEntryFooter {")
    marker = "static_assert(sizeof(CacheEntryHeader) == 0x10);"
    end = header.index(marker, start) + len(marker)
    framing = header[start:end]
    production = method(source)
    output = args.output or pathlib.Path(tempfile.mkdtemp(prefix="uberhar-stock-import-"))
    output.mkdir(parents=True, exist_ok=True)

    def run(name: str, code: str, negative: bool = False) -> None:
        cpp = output / f"{name}.cpp"
        binary = output / name
        cpp.write_text(PREFIX + framing + MIDDLE + code + TESTS)
        built = subprocess.run(["c++", "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
                                str(cpp), "-lzstd", "-o", str(binary)], capture_output=True, text=True)
        if built.returncode:
            raise RuntimeError(f"{name}: compile failed\n{built.stdout}{built.stderr}")
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
        (output / f"{name}.log").write_text(result.stdout + result.stderr)
        if negative:
            if result.returncode == 0:
                raise RuntimeError(f"{name}: deliberate defect escaped fixtures")
            print(f"PASS deliberate defect rejected: {name}")
        elif result.returncode:
            raise RuntimeError(f"{name}: failed\n{result.stdout}{result.stderr}")
        else:
            print(result.stdout, end="")

    run("stock-import", production)
    controls = {
        "no-footer-size": ("footer.entry_size != header.entry_size", "false"),
        "no-profile-check": ("profile != parent.profile", "false"),
        "no-source-pair-check": ("fs_source != pl_source", "false"),
        "no-fs-hash-check": ("header.id != entry.fs_config.Hash()", "false"),
        "no-cpu-interface-check": ("entry.pl_info.shader_ids[VS] != 0 || entry.pl_info.shader_ids[GS] != 0", "false"),
        "no-decompression-size-check": ("Common::Compression::GetDecompressedSize(payload) != expected", "false"),
    }
    for name, (before, after) in controls.items():
        if production.count(before) != 1:
            raise RuntimeError(f"{name}: production mutation anchor missing or ambiguous")
        run(name, production.replace(before, after), negative=True)


if __name__ == "__main__":
    main()
