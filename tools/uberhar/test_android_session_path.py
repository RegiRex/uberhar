#!/usr/bin/env python3
# AstraEH: Exercise the actual Android IOFile::Open and JNI logger setup with real POSIX
# descriptors. Stub only Android services and the logger dispatcher; reproduce 0.1.14's
# absolute-path relocation, then verify append, descriptor ownership and failure handling.
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
def function(path, signature):
    source = (root / path).read_text()
    start = source.index(signature)
    end = source.index('\n}', start) + 2
    return source[start:end]
translate = function('src/common/android_utils.cpp', 'std::string TranslateFilePath(')
opening = function('src/common/file_util.cpp', 'bool IOFile::Open()')
initialize = function('src/android/app/src/main/jni/native.cpp',
                      'void Java_org_citra_citra_1emu_NativeLibrary_createLogFile(')
prefix = r'''
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <fcntl.h>
#include <unistd.h>
#include <boost/algorithm/string/replace.hpp>
#include "common/scope_exit.h"
#define ANDROID 1
#define DUP_FD dup
#define CLOSE_FD close
#define FDOPEN fdopen
#define FOPEN fopen
#define LOG_ERROR(...) ((void)0)
#define LOG_INFO(...) ((void)0)
namespace fs = std::filesystem;
namespace AndroidUtils {
std::string user_dir;
bool raw = true;
int provider_calls = 0;
std::optional<std::string> GetUserDirectory() { return user_dir; }
bool CanUseRawFS() { return raw; }
enum class AndroidOpenMode { READ, WRITE, READ_WRITE, WRITE_APPEND, WRITE_TRUNCATE,
    READ_WRITE_TRUNCATE, READ_WRITE_APPEND };
AndroidOpenMode ParseOpenmode(const std::string&) { return AndroidOpenMode::WRITE_APPEND; }
bool CreateFile(const std::string&, const std::string&) { ++provider_calls; return false; }
int OpenContentUri(const std::string&, AndroidOpenMode) { ++provider_calls; return -1; }
'''
prefix += translate + '\n}\n'
prefix += r'''
namespace FileUtil {
bool Exists(const std::string& p) { return fs::exists(p); }
std::string GetParentPath(const std::string& p) { return fs::path(p).parent_path().string(); }
std::string GetFilename(const std::string& p) { return fs::path(p).filename().string(); }
class IOFile {
public:
    FILE* m_file=nullptr;
    int m_fd=-1;
    bool m_good=false;
    std::string filename, openmode="a";
    explicit IOFile(std::string name) : filename{std::move(name)} { Open(); }
    ~IOFile() { Close(); }
    void Close() { if(m_file) fclose(m_file); m_file=nullptr; }
    bool Open();
};
'''
prefix += opening + '\n}\n'
prefix += r'''
struct JNIEnv {};
using jobject = void*;
using jstring = const char*;
std::string GetJString(JNIEnv*, jstring text) { return text ? text : ""; }
namespace Common::Log {
std::unique_ptr<FileUtil::IOFile> journal;
int supplied_fd=-1;
void Initialize(std::string_view, std::string_view path) {
    supplied_fd = path.empty() ? -1 : std::stoi(std::string{path.substr(5)});
    journal = path.empty() ? nullptr : std::make_unique<FileUtil::IOFile>(std::string{path});
}
void Start() {}
}
'''
prefix += initialize
suffix = r'''
std::string read(const fs::path& p) { std::ifstream in(p); return {std::istreambuf_iterator<char>{in},{}}; }
int main(int argc, char** argv) {
    const fs::path root{argv[1]};
    fs::create_directories(root/"private");
    fs::create_directories(root/"game-data");
    AndroidUtils::user_dir=(root/"game-data").string();
    const auto path=(root/"private"/"log.txt").string();
    { std::ofstream out(path); out << "existing\n"; }
    // AstraEH: Verify the old unmarked absolute path fails through the production Android branch.
    { FileUtil::IOFile old{path}; assert(!old.m_good); }
    for (bool raw : {true, false}) {
        AndroidUtils::raw=raw;
        Java_org_citra_citra_1emu_NativeLibrary_createLogFile(nullptr,nullptr,path.c_str());
        assert(Common::Log::journal && Common::Log::journal->m_good);
        const int original=Common::Log::supplied_fd;
        assert(original>=0 && fcntl(original,F_GETFD)==-1 && errno==EBADF);
        const int owned=fileno(Common::Log::journal->m_file);
        assert(fputs("new record\n",Common::Log::journal->m_file)>=0);
        assert(fflush(Common::Log::journal->m_file)==0);
        Common::Log::journal.reset();
        assert(fcntl(owned,F_GETFD)==-1 && errno==EBADF);
    }
    assert(read(path)=="existing\nnew record\nnew record\n");
    assert(AndroidUtils::provider_calls==0);
    assert(fs::is_empty(root/"game-data"));
    Java_org_citra_citra_1emu_NativeLibrary_createLogFile(nullptr,nullptr,"");
    assert(!Common::Log::journal);
    const auto missing=(root/"missing"/"log.txt").string();
    Java_org_citra_citra_1emu_NativeLibrary_createLogFile(nullptr,nullptr,missing.c_str());
    assert(!Common::Log::journal);
    puts("PASS: actual Android path relocation reproduced; JNI descriptor path appends under raw and provider modes, owns/closes descriptors, handles missing journal");
}
'''
with tempfile.TemporaryDirectory(prefix='uberhar-android-log-') as temp:
    directory=Path(temp)
    (directory/'test.cpp').write_text(prefix+suffix)
    subprocess.run(['c++','-std=c++20','-O2','-I'+str(root/'src'),
                    '-I'+str(root/'externals/boost'),str(directory/'test.cpp'),
                    '-o',str(directory/'test')],check=True)
    subprocess.run([str(directory/'test'),str(directory/'files')],check=True,timeout=15)
