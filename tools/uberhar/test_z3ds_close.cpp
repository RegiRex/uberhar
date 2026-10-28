// CodexAstraLocal: Actual writer lifecycle and save methods operate through
// controlled I/O faults; real zstd/SHA code produces and verifies synthetic bytes.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <cryptopp/sha.h>
#include <zstd.h>
#include <zstd_seekable.h>
#include "common/alignment.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/ticket.h"
#include "core/file_sys/title_metadata.h"

#define LOG_ERROR(...) ((void)0)
#define LOG_DEBUG(...) ((void)0)
#define ASSERT_MSG(ok, ...) do { if (!(ok)) std::abort(); } while (0)
#define UNIMPLEMENTED() ((void)0)
namespace boost::serialization { class access; }
namespace Common { const std::string g_build_fullname = "fixture"; }
namespace Loader { enum class ResultStatus { Success, Error, ErrorInvalidFormat }; }
static unsigned assertions;
static void Check(bool ok, const char* description) {
    ++assertions;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", description); std::exit(1); }
}
// CodexAstraLocal: LSan cannot scan threads under the sandbox ptrace boundary.
// Count actual zstd stream creation/destruction while forwarding to the real
// library, so ownership on every tested error path remains executable evidence.
static unsigned live_streams;
static ZSTD_seekable_CStream* CountedCreate() {
    auto* stream = ZSTD_seekable_createCStream();
    if (stream) ++live_streams;
    return stream;
}
static size_t CountedFree(ZSTD_seekable_CStream* stream) {
    if (stream) {
        Check(live_streams != 0, "compressor released only once");
        --live_streams;
    }
    return ZSTD_seekable_freeCStream(stream);
}

namespace FileUtil {
struct IOType { enum class Type { NullIOFile, Z3DSWriteIOFile }; bool HasCompressedType() const { return false; } };
class IOFileBase {
public:
    virtual ~IOFileBase() = default;
    virtual bool Close() { return false; }
    virtual bool IsOpen() const { return true; }
    virtual bool Open() { return false; }
    virtual u64 GetSize() const { return 0; }
    virtual bool Resize(u64) { return false; }
    virtual bool Seek(s64, int) { return false; }
    virtual u64 Tell() const { return 0; }
    virtual std::unique_ptr<IOFileBase> OpenCopy() const { return {}; }
    IOType GetType() const { return {}; }
    std::unique_ptr<IOFileBase>& Child() { return child; }
    const std::unique_ptr<IOFileBase>& Child() const { return child; }
    std::size_t WriteBytes(const void* bytes, std::size_t size) { return WriteImpl(bytes, size, 1); }
protected:
    virtual std::size_t ReadImpl(void*, std::size_t, std::size_t) { return 0; }
    virtual std::size_t ReadAtImpl(void*, std::size_t, std::size_t) { return 0; }
    virtual std::size_t WriteImpl(const void*, std::size_t, std::size_t) { return 0; }
    virtual IOType::Type MyType() const { return IOType::Type::NullIOFile; }
private:
    std::unique_ptr<IOFileBase> child;
};
class NullIOFile : public IOFileBase {};
struct Endpoint {
    std::vector<u8> bytes;
    std::size_t position = 0;
    unsigned writes = 0, closes = 0, seeks = 0;
    unsigned fail_write_call = 0, fail_after_write_call = 0;
    bool open = true, close_ok = true, fail_seek = false, fail_header_write = false;
    bool fail_next_nonempty = false;
};
class MemoryFile : public IOFileBase {
public:
    explicit MemoryFile(std::shared_ptr<Endpoint> state_) : state(std::move(state_)) {}
    bool Close() override { ++state->closes; return state->close_ok; }
    bool IsOpen() const override { return state->open; }
    bool Seek(s64 offset, int origin) override {
        ++state->seeks;
        if (state->fail_seek || origin != SEEK_SET || offset < 0) return false;
        state->position = static_cast<std::size_t>(offset);
        return true;
    }
    u64 GetSize() const override { return state->bytes.size(); }
protected:
    std::size_t WriteImpl(const void* data, std::size_t length, std::size_t data_size) override {
        ++state->writes;
        auto size = length * data_size;
        const bool fail = size && (state->writes == state->fail_write_call ||
            (state->fail_after_write_call && state->writes > state->fail_after_write_call) ||
            state->fail_next_nonempty || (state->fail_header_write && state->position == 0));
        if (fail) { --size; state->fail_next_nonempty = false; }
        state->bytes.resize(std::max(state->bytes.size(), state->position + size));
        if (size) std::memcpy(state->bytes.data() + state->position, data, size);
        state->position += size;
        return size;
    }
    std::shared_ptr<Endpoint> state;
};
static std::shared_ptr<Endpoint> save_endpoint;
class IOFile : public MemoryFile {
public:
    IOFile(const std::string&, const char*) : MemoryFile(save_endpoint) {}
};
// @WRITER_CLASSES@
// @METADATA_WRITER@
#define ZSTD_seekable_createCStream CountedCreate
#define ZSTD_seekable_freeCStream CountedFree
// @WRITER_IMPL@
// @WRITER_METHODS@
#undef ZSTD_seekable_createCStream
#undef ZSTD_seekable_freeCStream
}
namespace FileSys {
// @PARSER_METHODS@
}

enum class InstallStatus { Success, ErrorEncrypted };
struct FakeTicket {
    bool fixup_ok = false, key_available = false;
    unsigned fixups = 0, key_reads = 0;
    Loader::ResultStatus DoTitlekeyFixup() { ++fixups; return fixup_ok ? Loader::ResultStatus::Success : Loader::ResultStatus::Error; }
    bool GetTitleKey() { ++key_reads; return key_available; }
};
struct FakeContainer {
    FakeTicket ticket;
    bool encrypted = false;
    bool HasEncryptedContent(void*) const { return encrypted; }
    void* GetHeader() { return nullptr; }
    FakeTicket& GetTicket() { return ticket; }
};
struct FakeInstall {
    bool called = false, needs_key = false;
    void EnableExternalInstall(bool value) { called = true; needs_key = value; }
};
static InstallStatus KeyGate(FakeContainer& container, FakeInstall& installFile) {
    const auto& tmd = container;
    // @TITLE_KEY_GATE@
    return InstallStatus::Success;
}

static std::vector<u8> Payload() {
    std::vector<u8> bytes(200000);
    u32 seed = 0x6A91B3;
    for (auto& byte : bytes) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; byte = static_cast<u8>(seed); }
    return bytes;
}
static void Decode(const FileUtil::Endpoint& state, const std::vector<u8>& expected) {
    FileUtil::Z3DSFileHeader header{};
    Check(state.bytes.size() >= sizeof(header), "Z3DS header present");
    std::memcpy(&header, state.bytes.data(), sizeof(header));
    Check(header.magic == FileUtil::Z3DSFileHeader::EXPECTED_MAGIC && header.uncompressed_size == expected.size(), "final header identity and size");
    const auto start = header.header_size + header.metadata_size;
    Check(start <= state.bytes.size() && header.compressed_size == state.bytes.size() - start, "compressed extent exact");
    std::unique_ptr<ZSTD_seekable, decltype(&ZSTD_seekable_free)> decoder(ZSTD_seekable_create(), &ZSTD_seekable_free);
    Check(!ZSTD_isError(ZSTD_seekable_initBuff(decoder.get(), state.bytes.data() + start, header.compressed_size)), "produced seekable stream initializes");
    std::vector<u8> decoded(expected.size());
    const auto result = ZSTD_seekable_decompress(decoder.get(), decoded.data(), decoded.size(), 0);
    Check(result == decoded.size() && decoded == expected, "compressed bytes independently decode exactly");
    std::array<u8, 53> partial{};
    if (expected.size() > 4096 + partial.size()) {
        Check(ZSTD_seekable_decompress(decoder.get(), partial.data(), partial.size(), 4096) == partial.size() &&
              std::equal(partial.begin(), partial.end(), expected.begin() + 4096), "seek table supports offset decode");
    }
}
static void CheckSuccess(const std::vector<u8>& payload) {
    auto state = std::make_shared<FileUtil::Endpoint>();
    {
        FileUtil::Z3DSWriteIOFile file(std::make_unique<FileUtil::MemoryFile>(state), {'N','C','C','H'}, 4096);
        Check(file.WriteBytes(payload.data(), payload.size()) == payload.size(), "writer accepts complete payload");
        Check(file.Close(), "explicit Close succeeds");
        const auto writes = state->writes;
        Check(file.Close(), "repeated Close preserves success");
        Check(state->closes == 1 && state->writes == writes, "repeated Close does not touch child or stream");
        Check(file.WriteBytes(payload.data(), 1) == 0, "write after Close rejected");
    }
    Check(state->closes == 1, "destructor after explicit Close is harmless");
    Check(live_streams == 0, "explicit close releases compressor");
    Decode(*state, payload);
    auto automatic = std::make_shared<FileUtil::Endpoint>();
    {
        FileUtil::Z3DSWriteIOFile file(std::make_unique<FileUtil::MemoryFile>(automatic), {'C','I','A',' '}, 4096);
        Check(file.WriteBytes(payload.data(), payload.size()) == payload.size(), "automatic writer accepts payload");
    }
    Check(automatic->closes == 1, "destructor alone closes once");
    Check(live_streams == 0, "destructor close releases compressor");
    Decode(*automatic, payload);
}
static void CheckFailures(const std::vector<u8>& payload) {
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        auto state = std::make_shared<FileUtil::Endpoint>();
        if (scenario == 0) state->fail_write_call = 1;
        if (scenario == 1) state->fail_seek = true;
        {
            FileUtil::Z3DSWriteIOFile file(std::make_unique<FileUtil::MemoryFile>(state), {'N','C','C','H'}, 4096);
            if (scenario == 2) state->fail_write_call = 2;
            if (scenario == 3) state->fail_after_write_call = 3;
            const auto written = file.WriteBytes(payload.data(), payload.size());
            Check(scenario < 4 ? written == 0 : written == payload.size(), "failed writes report zero and healthy writes complete");
            if (scenario == 4) state->fail_next_nonempty = true;
            if (scenario == 5) state->fail_header_write = true;
            if (scenario == 6) state->fail_seek = true;
            if (scenario == 7) state->close_ok = false;
            const bool closed = file.Close();
            Check(!closed, scenario == 4 ? "footer failure is retained" : "writer I/O failure is retained");
            const auto writes = state->writes;
            Check(!file.Close() && state->writes == writes && state->closes == 1, "failed Close reentry does not retry freed stream");
            Check(file.WriteBytes(payload.data(), 1) == 0, "failed closed writer rejects more bytes");
        }
        Check(state->closes == 1, "failed writer destructor does not close twice");
        Check(live_streams == 0, "failed writer releases compressor");
    }
}

template <class Body> static std::vector<u8> SignedData(const Body& body, std::span<const u8> tail) {
    const u32_be signature = FileSys::Rsa2048Sha256;
    std::vector<u8> bytes(0x140 + sizeof(body) + tail.size());
    std::memcpy(bytes.data(), &signature, sizeof(signature));
    std::memcpy(bytes.data() + 0x140, &body, sizeof(body));
    if (!tail.empty()) std::memcpy(bytes.data() + 0x140 + sizeof(body), tail.data(), tail.size());
    return bytes;
}
static void CheckSaves() {
    FileSys::Ticket::Body ticket_body{};
    FileSys::Ticket::ContentIndex::MainHeader index{};
    index.context_index_size = sizeof(index);
    FileSys::Ticket ticket;
    auto ticket_data = SignedData(ticket_body, std::span(reinterpret_cast<u8*>(&index), sizeof(index)));
    Check(ticket.Load(ticket_data) == Loader::ResultStatus::Success, "synthetic ticket parses");
    for (unsigned failure = 0; failure < 4; ++failure) {
        auto state = std::make_shared<FileUtil::Endpoint>();
        FileUtil::save_endpoint = state;
        state->open = failure != 1;
        state->fail_write_call = failure == 2 ? 1 : 0;
        state->close_ok = failure != 3;
        const auto result = ticket.Save("synthetic-ticket");
        Check(result == (failure ? Loader::ResultStatus::Error : Loader::ResultStatus::Success),
              failure == 2 ? "ticket short write rejected" : "ticket open/close result propagated");
        if (!failure) Check(state->bytes == ticket.Serialize() && state->closes == 1, "ticket exact serialized bytes saved and closed");
    }
    FileSys::TitleMetadata::Body body{};
    body.content_count = 1;
    FileSys::TitleMetadata::ContentChunk chunk{};
    chunk.index = 7; chunk.size = 64;
    const auto tmd_data = SignedData(body, std::span(reinterpret_cast<u8*>(&chunk), sizeof(chunk)));
    for (unsigned failure = 0; failure < 8; ++failure) {
        FileSys::TitleMetadata tmd;
        Check(tmd.Load(tmd_data) == Loader::ResultStatus::Success, "synthetic TMD parses");
        auto state = std::make_shared<FileUtil::Endpoint>();
        FileUtil::save_endpoint = state;
        state->open = failure != 1;
        state->fail_write_call = failure >= 2 && failure <= 5 ? failure - 1 : 0;
        state->fail_seek = failure == 6;
        state->close_ok = failure != 7;
        const auto result = tmd.Save("synthetic-tmd");
        Check(result == (failure ? Loader::ResultStatus::Error : Loader::ResultStatus::Success), "TMD exact write/seek/close errors propagated");
        if (!failure) {
            FileSys::TitleMetadata reopened;
            Check(reopened.Load(state->bytes) == Loader::ResultStatus::Success && reopened.GetContentCount() == 1,
                  "saved TMD reparses complete content table");
        }
    }
    FileUtil::save_endpoint.reset();
}
static void CheckKeyGate() {
    FakeContainer plain;
    FakeInstall plain_install;
    Check(KeyGate(plain, plain_install) == InstallStatus::Success && plain_install.called && !plain_install.needs_key &&
          plain.ticket.fixups == 0 && plain.ticket.key_reads == 0,
          "plaintext personalized CIA bypasses unused key fixup");
    for (unsigned scenario = 0; scenario < 3; ++scenario) {
        FakeContainer encrypted;
        FakeInstall install;
        encrypted.encrypted = true;
        encrypted.ticket.fixup_ok = scenario != 0;
        encrypted.ticket.key_available = scenario == 2;
        Check(KeyGate(encrypted, install) == (scenario == 2 ? InstallStatus::Success : InstallStatus::ErrorEncrypted),
              "encrypted CIA requires successful fixup and title key");
        Check(encrypted.ticket.fixups == 1 && encrypted.ticket.key_reads == (scenario == 0 ? 0 : 1), "encrypted key gate order retained");
        Check(install.called == (scenario == 2) && install.needs_key == (scenario == 2), "encrypted install enabled only after key availability");
    }
}
int main() {
    const auto payload = Payload();
    CheckSuccess(payload);
    CheckFailures(payload);
    CheckSaves();
    CheckKeyGate();
    std::printf("PASS Z3DS close/save (%u assertions)\n", assertions);
}
