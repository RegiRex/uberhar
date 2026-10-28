// CodexAstraLocal: Synthetic malformed and valid data exercise the actual
// security methods inserted by test_installer_security.py. No keys or titles.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <vector>
#include "common/alignment.h"
#include "common/common_types.h"
#include "core/file_sys/ticket.h"
#include "core/file_sys/title_metadata.h"

#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)

// CodexAstraLocal: Only result transport and class scaffolding are doubles;
// vector storage, offset calculations, copy operations and parser layouts are real.
enum class FixtureError { ReadUnsupported, WriteBeyondEnd };
template <typename T> struct ResultVal {
    bool success;
    T value{};
    FixtureError error{};
    ResultVal(T value_) : success(true), value(value_) {}
    ResultVal(FixtureError error_) : success(false), error(error_) {}
};
constexpr auto ResultReadNotSupported = FixtureError::ReadUnsupported;
namespace FileSys {
constexpr auto ResultWriteBeyondEnd = FixtureError::WriteBeyondEnd;
}
namespace Loader {
enum class ResultStatus { Success, Error, ErrorInvalidFormat };
}

struct CIAFile {
    u64 written{};
    std::vector<u8> data;
    ResultVal<std::size_t> Read(u64, std::size_t, u8*) const;
    ResultVal<std::size_t> BufferMetadataFixture(u64, std::size_t, const u8*, u64);
};
struct ContentFile {
    ResultVal<std::size_t> Read(u64, std::size_t, u8*) const;
};
struct TicketFile {
    u64 written{};
    std::vector<u8> data;
    ResultVal<std::size_t> Read(u64, std::size_t, u8*) const;
    ResultVal<std::size_t> Write(u64, std::size_t, bool, bool, const u8*);
};
struct TMDFile {
    u64 written{};
    std::vector<u8> data;
    ResultVal<std::size_t> Read(u64, std::size_t, u8*) const;
    ResultVal<std::size_t> Write(u64, std::size_t, bool, bool, const u8*);
};

// @AM_METHODS@
// @LZSS_METHODS@
namespace FileSys {
// @PARSER_METHODS@
}

static unsigned assertions = 0;
static void Check(bool ok, const char* description) {
    ++assertions;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", description);
        std::exit(1);
    }
}

template <typename File> static void CheckRead(const char* description) {
    File file;
    std::array<u8, 19> sentinel;
    sentinel.fill(0xA7);
    const auto result = file.Read(std::numeric_limits<u64>::max(), sentinel.size(), sentinel.data());
    Check(!result.success && result.error == FixtureError::ReadUnsupported, description);
    Check(std::all_of(sentinel.begin(), sentinel.end(), [](u8 byte) { return byte == 0xA7; }),
          "unsupported reads preserve caller bytes");
    Check(!file.Read(0, 0, sentinel.data()).success, "zero length read remains unsupported");
}

template <typename File> static void CheckWrites() {
    File file;
    const std::array<u8, 4> payload{0x19, 0x37, 0x55, 0x73};
    auto result = file.Write(7, payload.size(), false, false, payload.data());
    Check(result.success && result.value == payload.size(), "sparse write succeeds");
    Check(file.data.size() == 11 && file.written == 4, "sparse write uses end offset");
    Check(std::all_of(file.data.begin(), file.data.begin() + 7, [](u8 b) { return b == 0; }),
          "sparse unwritten prefix is initialized");
    Check(std::equal(payload.begin(), payload.end(), file.data.begin() + 7), "sparse bytes copied");
    result = file.Write(1, 2, false, false, payload.data());
    Check(result.success && file.data.size() == 11, "overlap does not shrink allocation");
    Check(std::equal(payload.begin(), payload.end(), file.data.begin() + 7), "overlap preserves tail");
    result = file.Write(64, payload.size(), false, false, payload.data());
    Check(result.success && file.data.size() == 68, "second sparse write extends allocation");
    const auto saved = file.data;
    const auto saved_written = file.written;
    for (auto [offset, length] : std::array<std::pair<u64, std::size_t>, 4>{
             std::pair{std::numeric_limits<u64>::max(), std::size_t{2}},
             std::pair{MAX_IMPORT_FILE_SIZE, std::size_t{1}},
             std::pair{MAX_IMPORT_FILE_SIZE + 1, std::size_t{0}},
             std::pair{u64{1}, std::numeric_limits<std::size_t>::max()}}) {
        result = file.Write(offset, length, false, false, payload.data());
        Check(!result.success && result.error == FixtureError::WriteBeyondEnd,
              "oversized or wrapped write rejected");
        Check(file.data == saved && file.written == saved_written, "rejected write has no side effects");
    }
}

static void CheckCiaMetadataBuffer() {
    CIAFile file;
    const std::array<u8, 16> payload{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto result = file.BufferMetadataFixture(5, 10, payload.data(), 9);
    Check(result.success && file.data.size() == 9, "CIA metadata clamped to limit");
    Check(std::equal(payload.begin(), payload.begin() + 4, file.data.begin() + 5),
          "CIA metadata copies exact bounded bytes");
    Check(std::all_of(file.data.begin(), file.data.begin() + 5, [](u8 b) { return b == 0; }),
          "CIA metadata sparse prefix initialized");
    const auto saved = file.data;
    result = file.BufferMetadataFixture(9, 5, payload.data(), 9);
    Check(result.success && file.data == saved, "CIA write at metadata limit is ignored");
    const auto written = file.written;
    result = file.BufferMetadataFixture(std::numeric_limits<u64>::max(), 2, payload.data(), 9);
    Check(!result.success && result.error == FixtureError::WriteBeyondEnd,
          "CIA wrapped metadata write rejected");
    Check(file.data == saved && file.written == written, "CIA wrapped write has no side effects");
}

static void CheckLzss() {
    for (std::size_t size = 0; size < 8; ++size) {
        std::vector<u8> compressed(size, 0), out(16, 0xCC);
        if (size < 4) {
            Check(LZSS_GetDecompressedSize(compressed) == size, "short LZSS size footer bounded");
        }
        Check(!LZSS_Decompress(compressed, out), "short LZSS footer rejected");
        Check(out.front() == 0xCC, "invalid LZSS does not touch output");
    }
    std::vector<u8> compressed(10), out(10, 0xCC);
    compressed[0] = 0x7E;
    compressed[1] = 0;
    u32 footer = (8U << 24) | 10;
    std::memcpy(compressed.data() + 2, &footer, sizeof(footer));
    Check(LZSS_Decompress(compressed, out) && out.back() == 0x7E, "valid LZSS literal decodes");
    for (u32 bad_footer : {(11U << 24) | 10, (8U << 24) | 11}) {
        std::memcpy(compressed.data() + 2, &bad_footer, sizeof(bad_footer));
        Check(!LZSS_Decompress(compressed, out), "LZSS footer bounds rejected");
    }
    std::memcpy(compressed.data() + 2, &footer, sizeof(footer));
    Check(!LZSS_Decompress(compressed, std::span<u8>(out).first(9)), "short LZSS output rejected");
}

using Index = FileSys::Ticket::ContentIndex;
static std::vector<u8> MakeIndex(bool allow) {
    Index::MainHeader header{};
    Index::IndexHeader entry{};
    Index::RightsField field{};
    std::vector<u8> bytes(sizeof(header) + sizeof(entry) + sizeof(field));
    header.always1 = 1;
    header.header_size = sizeof(header);
    header.context_index_size = static_cast<u32>(bytes.size());
    header.index_headers_offset = sizeof(header);
    header.index_headers_count = 1;
    header.index_header_size = sizeof(entry);
    entry.type = 3;
    entry.data_offset = sizeof(header) + sizeof(entry);
    entry.entry_count = 1;
    entry.entry_size = sizeof(field);
    entry.total_size = sizeof(field);
    field.start_index = 300;
    field.rights[0] = allow ? 1 : 0;
    std::memcpy(bytes.data(), &header, sizeof(header));
    std::memcpy(bytes.data() + sizeof(header), &entry, sizeof(entry));
    std::memcpy(bytes.data() + sizeof(header) + sizeof(entry), &field, sizeof(field));
    return bytes;
}

static void CheckRights() {
    Index index;
    Check(!index.HasRights(0), "unloaded rights fail closed");
    index.Load(1, 2, MakeIndex(true));
    Check(index.HasRights(300) && !index.HasRights(301), "valid synthetic rights parsed");
    auto copy = index;
    index.Load(1, 2, MakeIndex(false));
    Check(!index.HasRights(300), "rights reset on reload");
    Check(copy.HasRights(300), "copied rights independent of original");
    auto source = MakeIndex(true);
    Index uninitialized;
    uninitialized.Load(1, 2, source);
    auto copied_before_initialize = uninitialized;
    uninitialized = Index{};
    Check(copied_before_initialize.HasRights(300), "copied uninitialized rights own data");
    for (int defect = 0; defect < 5; ++defect) {
        auto bytes = source;
        Index::MainHeader header;
        Index::IndexHeader entry;
        std::memcpy(&header, bytes.data(), sizeof(header));
        std::memcpy(&entry, bytes.data() + sizeof(header), sizeof(entry));
        if (defect == 0) header.index_headers_offset = 0xFFFFFFFC;
        if (defect == 1) header.index_headers_count = 0xFFFF;
        if (defect == 2) entry.data_offset = 0xFFFFFFFC;
        if (defect == 3) entry.entry_count = 0xFFFFFFFF;
        if (defect == 4) entry.data_offset = static_cast<u32>(bytes.size() - sizeof(Index::RightsField) + 1);
        std::memcpy(bytes.data(), &header, sizeof(header));
        std::memcpy(bytes.data() + sizeof(header), &entry, sizeof(entry));
        index.Load(1, 2, bytes);
        Check(!index.HasRights(300), "out of bounds rights fail closed");
    }
    for (std::size_t size = 0; size < sizeof(Index::MainHeader); ++size) {
        index.Load(1, 2, std::vector<u8>(size));
        Check(!index.HasRights(0), "truncated rights fail closed");
    }
}

template <typename Body> static std::vector<u8> MakeSigned(const Body& body, std::span<const u8> tail) {
    // CodexAstraLocal: ECDSA-sized synthetic signature bytes provide parser
    // framing only. No signature verification or key material is modeled.
    const u32_be type = FileSys::EcdsaSha256;
    std::vector<u8> data(0x40 + sizeof(body) + tail.size());
    std::memcpy(data.data(), &type, sizeof(type));
    std::memcpy(data.data() + 0x40, &body, sizeof(body));
    if (!tail.empty()) std::memcpy(data.data() + 0x40 + sizeof(body), tail.data(), tail.size());
    return data;
}

static void CheckParsers() {
    FileSys::Ticket ticket;
    FileSys::TitleMetadata tmd;
    const std::array<u8, 4> short_data{};
    for (std::size_t offset : {std::size_t{5}, std::numeric_limits<std::size_t>::max()}) {
        Check(ticket.Load(short_data, offset) == Loader::ResultStatus::Error, "ticket offset rejected");
        Check(tmd.Load(short_data, offset) == Loader::ResultStatus::Error, "TMD offset rejected");
    }
    for (std::size_t size = 0; size < 4; ++size) {
        Check(ticket.Load(std::span(short_data).first(size)) == Loader::ResultStatus::Error,
              "truncated ticket signature rejected");
        Check(tmd.Load(std::span(short_data).first(size)) == Loader::ResultStatus::Error,
              "truncated TMD signature rejected");
    }
    FileSys::Ticket::Body ticket_body{};
    const u64_be title_id = 42, ticket_id = 81;
    std::memcpy(&ticket_body.title_id, &title_id, sizeof(title_id));
    std::memcpy(&ticket_body.ticket_id, &ticket_id, sizeof(ticket_id));
    auto ticket_bytes = MakeSigned(ticket_body, MakeIndex(true));
    ticket_bytes.insert(ticket_bytes.begin(), 13, 0xCC);
    Check(ticket.Load(ticket_bytes, 13) == Loader::ResultStatus::Success &&
          ticket.GetTitleID() == 42 && ticket.GetTicketID() == 81 && ticket.HasRights(300),
          "valid ticket at nonzero offset");
    auto copied_ticket = ticket;
    ticket = FileSys::Ticket{};
    Check(copied_ticket.HasRights(300), "ticket copy owns rights metadata");
    FileSys::TitleMetadata::Body body{};
    body.content_count = 1;
    FileSys::TitleMetadata::ContentChunk chunk{};
    chunk.id = 101;
    auto bytes = MakeSigned(body, std::span(reinterpret_cast<const u8*>(&chunk), sizeof(chunk)));
    Check(tmd.Load(bytes) == Loader::ResultStatus::Success && tmd.GetContentCount() == 1 &&
          tmd.GetContentIDByIndex(0) == 101, "valid synthetic TMD parsed");
    chunk.id = 202;
    bytes = MakeSigned(body, std::span(reinterpret_cast<const u8*>(&chunk), sizeof(chunk)));
    bytes.insert(bytes.begin(), 7, 0xCC);
    Check(tmd.Load(bytes, 7) == Loader::ResultStatus::Success && tmd.GetContentCount() == 1 &&
          tmd.GetContentIDByIndex(0) == 202, "TMD reload replaces chunks");
    bytes.pop_back();
    Check(tmd.Load(bytes, 7) == Loader::ResultStatus::ErrorInvalidFormat,
          "truncated TMD chunk rejected");
}

int main() {
    CheckRead<CIAFile>("CIAFile rejects reads");
    CheckRead<TicketFile>("TicketFile rejects reads");
    CheckRead<TMDFile>("TMDFile rejects reads");
    CheckRead<ContentFile>("ContentFile rejects reads");
    CheckWrites<TicketFile>();
    CheckWrites<TMDFile>();
    CheckCiaMetadataBuffer();
    CheckLzss();
    CheckRights();
    CheckParsers();
    std::printf("PASS installer security boundaries (%u assertions)\n", assertions);
}
