#!/usr/bin/env python3
# AstraEH: Run the production FileBackend against real stdio files, including a killed
# helper process. This does not claim Android SAF/OS-exit API device coverage.
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/common/logging/backend.cpp').read_text()
start = source.index('class FileBackend final : public Backend {')
end = source.index('\n};', start) + 3
production = source[start:end]
prefix = r'''
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include "common/logging/uberhar_log_retention.h"
using u64 = unsigned long long;
namespace fs = std::filesystem;
namespace Common::Literals {
constexpr unsigned long long operator""_MiB(unsigned long long n) { return n * 1024 * 1024; }
}
namespace FileUtil {
bool Exists(const std::string& p) { return fs::exists(p); }
bool IsDirectory(const std::string& p) { return fs::is_directory(p); }
u64 GetSize(const std::string& p) { return fs::file_size(p); }
bool Delete(const std::string& p) { return !fs::exists(p) || fs::remove(p); }
bool Rename(const std::string& a, const std::string& b) { fs::rename(a,b); return true; }
struct IOFile {
    FILE* file;
    std::string name;
    IOFile(const std::string& p, const char* mode, int) : file{fopen(p.c_str(), mode)}, name{p} {}
    ~IOFile() { Close(); }
    bool IsOpen() const { return file != nullptr; }
    size_t GetSize() const { return file && fs::is_regular_file(name) ? fs::file_size(name) : 0; }
    size_t WriteString(const std::string& s) { return file ? fwrite(s.data(),1,s.size(),file) : 0; }
    bool Flush() { return file && fflush(file)==0; }
    void Close() { if(file) fclose(file); file=nullptr; }
};
}
namespace Common::Log {
enum class Level { Info, Error };
struct Entry { Level log_level; std::chrono::microseconds timestamp; std::string message; };
std::string FormatLogMessage(const Entry& entry) { return entry.message; }
struct Backend {
    virtual ~Backend() = default;
    virtual void Write(const Entry&)=0;
    virtual void Flush()=0;
    virtual void Close()=0;
    virtual void EnableForStacktrace()=0;
};
constexpr int _SH_DENYWR = 0;
'''
suffix = r'''
}
using namespace Common::Log;
std::string read(const fs::path& p) { std::ifstream f(p); return {std::istreambuf_iterator<char>{f}, {}}; }
int main(int argc, char** argv) {
    const fs::path root{argv[1]};
    fs::create_directories(root);
    const auto legacy = (root/"legacy.txt").string();
    const auto journal = (root/"session-a.txt").string();
    {
        FileBackend a{legacy};
        FileBackend b{journal, false};
        Entry entry{Level::Info, std::chrono::microseconds{1}, "session A"};
        a.Write(entry); b.Write(entry); a.Flush(); b.Flush();
        assert(a.Healthy() && b.Healthy());
    }
    for (int i=0; i<6; ++i) {
        FileBackend normal{legacy};
        FileBackend unique{(root/("session-"+std::to_string(i)+".txt")).string(), false};
        Entry entry{Level::Info, std::chrono::microseconds{1}, "other session"};
        normal.Write(entry); unique.Write(entry);
    }
    assert(read(journal)=="session A\n");
    assert(!fs::exists(root/"session-a.old.txt"));
    {
        FileBackend appended{journal, false};
        appended.Write({Level::Error, std::chrono::microseconds{1}, "preserved append"});
    }
    assert(read(journal)=="session A\npreserved append\n");
    int ready[2]; assert(pipe(ready)==0);
    pid_t child=fork(); assert(child>=0);
    const auto killed=(root/"killed-session.txt").string();
    if(child==0) {
        close(ready[0]);
        FileBackend writer{killed, false};
        writer.Write({Level::Info, std::chrono::microseconds{1}, "written before kill"});
        writer.Flush();
        assert(writer.Healthy());
        char c=1; assert(write(ready[1],&c,1)==1);
        for(;;) pause();
    }
    close(ready[1]); char c; assert(read(ready[0],&c,1)==1); close(ready[0]);
    assert(kill(child,SIGKILL)==0); int status; assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && WTERMSIG(status)==SIGKILL);
    assert(read(killed)=="written before kill\n");
    {
        FileBackend missing{(root/"absent"/"log.txt").string(), false};
        assert(!missing.Healthy());
        FileBackend full{"/dev/full", false};
        full.Write({Level::Error, std::chrono::microseconds{1}, "storage error"});
        assert(!full.Healthy());
    }
    const auto capped=root/"capped.txt";
    std::ofstream(capped).close(); fs::resize_file(capped,100ULL*1024*1024);
    { FileBackend writer{capped.string(),false}; assert(!writer.Healthy()); }
    puts("PASS: production session backend survives six rotations and SIGKILL; append, open/write/flush failures and size cap checked");
}
'''
with tempfile.TemporaryDirectory(prefix='uberhar-session-backend-') as temporary:
    directory = Path(temporary)
    cpp = directory / 'test.cpp'
    cpp.write_text(prefix + production + suffix)
    subprocess.run(['c++', '-std=c++20', '-O2', '-I' + str(root / 'src'), str(cpp), '-o', str(directory / 'test')], check=True)
    subprocess.run([str(directory / 'test'), str(directory / 'files')], check=True, timeout=15)
