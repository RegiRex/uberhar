// CodexAstraLocal: Source-extracted NCCH/CIA crypto runs with generated synthetic
// inputs and real AES/SHA; supplied-key storage and file endpoints are controlled.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include <cryptopp/sha.h>
#include "common/alignment.h"
#include "common/bit_field.h"
#include "common/common_types.h"
#include "common/swap.h"
#include "core/file_sys/title_metadata.h"

#define LOG_DEBUG(...) ((void)0)
#define LOG_ERROR(...) ((void)0)
#define LOG_WARNING(...) ((void)0)
#define LOG_INFO(...) ((void)0)

namespace Loader { enum class ResultStatus { Success, Error, ErrorEncrypted, ErrorInvalidFormat, ErrorNotUsed }; }
namespace HW::AES {
enum class KeySlotID { NCCHSecure1, NCCHSecure2, NCCHSecure3, NCCHSecure4 };
constexpr std::size_t AES_BLOCK_SIZE = 16;
std::array<bool, 4> available;
std::array<std::array<u8, 16>, 4> supplied_y;
unsigned initializations;
void InitKeys() { ++initializations; }
void SetKeyY(KeySlotID slot, const std::array<u8, 16>& value) { supplied_y[static_cast<unsigned>(slot)] = value; }
bool IsNormalKeyAvailable(KeySlotID slot) { return available[static_cast<unsigned>(slot)]; }
std::array<u8, 16> GetNormalKey(KeySlotID slot) {
    // CodexAstraLocal: Deliberately synthetic slot transformation. This verifies
    // caller selection/availability, never Nintendo's normal-key scrambler.
    auto value = supplied_y[static_cast<unsigned>(slot)];
    for (auto& byte : value) byte ^= static_cast<u8>(0x31 + 23 * static_cast<unsigned>(slot));
    return value;
}
}
namespace FileSys {
std::optional<std::array<u8, 16>> supplied_seed;
u64 seed_program;
std::optional<std::array<u8, 16>> GetSeed(u64 program) { seed_program = program; return supplied_seed; }
constexpr std::size_t CIA_CONTENT_BITS_SIZE = 0x10000 / 8;
// @STRUCTURES@
static_assert(sizeof(NCCH_Header) == 0x200);
static_assert(sizeof(ExeFs_Header) == 0x200);

class MemoryFile {
public:
    explicit MemoryFile(std::vector<u8> bytes_ = {}) : bytes(std::move(bytes_)) {}
    std::vector<u8> bytes;
    std::size_t position = 0;
    bool IsOpen() const { return true; }
    std::size_t GetSize() const { return bytes.size(); }
    bool Seek(std::size_t offset, int) { position = offset; return true; }
    std::size_t ReadBytes(u8* output, std::size_t size) {
        if (position >= bytes.size()) return 0;
        size = std::min(size, bytes.size() - position);
        std::memcpy(output, bytes.data() + position, size);
        position += size;
        return size;
    }
};
static constexpr int kBlockSize = 0x200;
static constexpr int kMaxSections = 8;
class NCCHContainer {
public:
    NCCH_Header ncch_header{};
    ExeFs_Header exefs_header{};
    bool is_encrypted = false, is_proto = false, is_compressed = false, has_exefs = true;
    std::array<u8, 16> primary_key{}, secondary_key{}, exheader_ctr{}, exefs_ctr{}, romfs_ctr{};
    std::unique_ptr<MemoryFile> file = std::make_unique<MemoryFile>();
    std::unique_ptr<MemoryFile> exefs_file = std::make_unique<MemoryFile>();
    Loader::ResultStatus SetupCrypto();
    Loader::ResultStatus LoadSectionExeFS(const char*, std::vector<u8>&);
    Loader::ResultStatus Load() { return Loader::ResultStatus::Success; }
    Loader::ResultStatus LoadOverrideExeFSSection(const char*, std::vector<u8>&) { return Loader::ResultStatus::ErrorNotUsed; }
};
// @SETUP@
// @LZSS@
// @SECTION@

class CIAContainer {
public:
    CIAHeader cia_header{};
    TitleMetadata cia_tmd;
    u64 GetContentSize(std::size_t index) const;
    const TitleMetadata& GetTitleMetadata() const { return cia_tmd; }
};
// @METADATA@
constexpr int ResultFileNotFound = -1, ResultWriteBeyondEnd = -2;
}

struct OutputFinalizer {
    bool success = true;
    bool Finalize() { return success; }
};
struct CIAFile {
    FileSys::CIAContainer container;
    bool external_transaction = true;
    std::size_t current_content_index = 0;
    struct { int result = 0; } current_content_install_result;
    struct DecryptionState {
        std::vector<CryptoPP::CBC_Mode<CryptoPP::AES>::Decryption> content;
        std::vector<std::unique_ptr<CryptoPP::SHA256>> content_hashes;
    };
    std::unique_ptr<DecryptionState> decryption_state = std::make_unique<DecryptionState>();
    std::unique_ptr<OutputFinalizer> current_content_file;
    void Initialize(const std::array<u8, 16>& key) {
        const auto& tmd = container.GetTitleMetadata();
        const auto content_count = tmd.GetContentCount();
        const std::optional<std::array<u8, 16>> title_key = key;
        // @CBC_INIT@
    }
    int ProcessChunk(std::size_t i, std::vector<u8>& temp);
    bool FinishCurrentContent();
};
// @CBC_CHUNK@
// @CIA_FINISH@

static unsigned assertions = 0;
static void Check(bool ok, const char* description) {
    ++assertions;
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", description); std::exit(1); }
}
static void ResetSupplied() {
    HW::AES::available.fill(true);
    HW::AES::supplied_y = {};
    HW::AES::initializations = 0;
    FileSys::seed_program = 0;
    std::array<u8, 16> seed;
    for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = static_cast<u8>(73 + i * 19);
    FileSys::supplied_seed = seed;
}
static void Header(FileSys::NCCHContainer& ncch, u16 version = 2, u8 slot = 1) {
    ncch.ncch_header = {};
    ncch.ncch_header.version = version;
    ncch.ncch_header.secondary_key_slot = slot;
    ncch.ncch_header.program_id = 0x1122334455667788ULL;
    ncch.ncch_header.exefs_offset = 0x123;
    ncch.ncch_header.romfs_offset = 0x456;
    for (unsigned i = 0; i < 8; ++i) ncch.ncch_header.partition_id[i] = static_cast<u8>(17 + i * 13);
    for (unsigned i = 0; i < 16; ++i) ncch.ncch_header.signature[i] = static_cast<u8>(31 + i * 7);
}

static void CheckSetup() {
    ResetSupplied();
    FileSys::NCCHContainer missing_primary;
    Header(missing_primary);
    HW::AES::available[0] = false;
    Check(missing_primary.SetupCrypto() == Loader::ResultStatus::ErrorEncrypted,
          "missing primary key rejected without exheader");
    for (auto [slot, index] : std::array<std::pair<u8, unsigned>, 4>{{{0, 0}, {1, 1}, {10, 2}, {11, 3}}}) {
        ResetSupplied();
        FileSys::NCCHContainer success;
        Header(success, 2, slot);
        Check(success.SetupCrypto() == Loader::ResultStatus::Success, "supported secondary slot accepted");
        Check(success.secondary_key == HW::AES::GetNormalKey(static_cast<HW::AES::KeySlotID>(index)), "secondary slot selected");
        FileSys::NCCHContainer missing;
        Header(missing, 2, slot);
        HW::AES::available[index] = false;
        Check(missing.SetupCrypto() == Loader::ResultStatus::ErrorEncrypted, "missing selected secondary key rejected");
    }
    ResetSupplied();
    FileSys::NCCHContainer missing_seed;
    Header(missing_seed);
    missing_seed.ncch_header.seed_crypto.Assign(1);
    FileSys::supplied_seed.reset();
    Check(missing_seed.SetupCrypto() == Loader::ResultStatus::ErrorEncrypted, "missing seed rejected");
    ResetSupplied();
    FileSys::NCCHContainer seeded;
    Header(seeded);
    seeded.ncch_header.seed_crypto.Assign(1);
    Check(seeded.SetupCrypto() == Loader::ResultStatus::Success, "available seed accepted");
    CryptoPP::SHA256 seed_hash;
    seed_hash.Update(seeded.ncch_header.signature, 16);
    seed_hash.Update(FileSys::supplied_seed->data(), FileSys::supplied_seed->size());
    std::array<u8, 32> expected_hash;
    seed_hash.Final(expected_hash.data());
    Check(std::equal(expected_hash.begin(), expected_hash.begin() + 16, HW::AES::supplied_y[1].begin()), "seeded secondary KeyY derives from signature and seed");
    Check(std::equal(seeded.ncch_header.signature, seeded.ncch_header.signature + 16, HW::AES::supplied_y[0].begin()), "primary KeyY remains unseeded");
    Check(FileSys::seed_program == seeded.ncch_header.program_id, "seed lookup uses program identity");
    for (bool unknown_version : {false, true}) {
        ResetSupplied();
        FileSys::NCCHContainer invalid;
        Header(invalid, unknown_version ? 7 : 2, unknown_version ? 1 : 99);
        Check(invalid.SetupCrypto() == Loader::ResultStatus::ErrorEncrypted, "unknown crypto method rejected");
    }
    ResetSupplied();
    FileSys::NCCHContainer fixed;
    Header(fixed);
    fixed.ncch_header.fixed_key.Assign(1);
    HW::AES::available.fill(false);
    Check(fixed.SetupCrypto() == Loader::ResultStatus::Success && HW::AES::initializations == 0,
          "fixed crypto does not require supplied slots");
    Check(fixed.primary_key == std::array<u8, 16>{} && fixed.secondary_key == std::array<u8, 16>{}, "fixed keys initialized to zero");
    FileSys::NCCHContainer plaintext;
    Header(plaintext, 99, 99);
    plaintext.ncch_header.no_crypto.Assign(1);
    Check(plaintext.SetupCrypto() == Loader::ResultStatus::Success && !plaintext.is_encrypted,
          "plaintext does not require crypto metadata");

    for (u16 version : {0, 2}) {
        ResetSupplied();
        FileSys::NCCHContainer ncch;
        Header(ncch, version);
        Check(ncch.SetupCrypto() == Loader::ResultStatus::Success, "version zero/two accepted");
        std::array<u8, 16> expected{};
        for (unsigned i = 0; i < 8; ++i) expected[i] = ncch.ncch_header.partition_id[7 - i];
        expected[8] = 1; Check(ncch.exheader_ctr == expected, "exheader reverse partition counter");
        expected[8] = 2; Check(ncch.exefs_ctr == expected, "ExeFS reverse partition counter");
        expected[8] = 3; Check(ncch.romfs_ctr == expected, "RomFS reverse partition counter");
    }
    ResetSupplied();
    FileSys::NCCHContainer version_one;
    Header(version_one, 1);
    Check(version_one.SetupCrypto() == Loader::ResultStatus::Success, "version one accepted");
    std::array<u8, 16> expected{};
    std::copy(std::begin(version_one.ncch_header.partition_id), std::end(version_one.ncch_header.partition_id), expected.begin());
    auto expect_counter = [&](const std::array<u8, 16>& actual, u32 offset) {
        for (unsigned byte = 0; byte < 4; ++byte) expected[12 + byte] = static_cast<u8>(offset >> (24 - byte * 8));
        Check(actual == expected, "version one encodes big endian byte offsets");
    };
    expect_counter(version_one.exheader_ctr, 0x200);
    expect_counter(version_one.exefs_ctr, 0x123 * 0x200);
    expect_counter(version_one.romfs_ctr, 0x456 * 0x200);
    for (bool romfs : {false, true}) {
        FileSys::NCCHContainer overflow;
        Header(overflow, 1);
        if (romfs) overflow.ncch_header.romfs_offset = 0x800000;
        else overflow.ncch_header.exefs_offset = 0x800000;
        Check(overflow.SetupCrypto() == Loader::ResultStatus::ErrorEncrypted, "version one offset overflow rejected");
    }
}

static void CheckSections() {
    ResetSupplied();
    FileSys::NCCHContainer ncch;
    Header(ncch);
    Check(ncch.SetupCrypto() == Loader::ResultStatus::Success, "section crypto initialized");
    ncch.exefs_file->bytes.resize(0x3000);
    const std::array<std::string, 4> names{"icon", "banner", ".code", "12345678"};
    std::array<std::vector<u8>, 4> expected;
    for (unsigned i = 0; i < names.size(); ++i) {
        auto& section = ncch.exefs_header.section[i];
        std::memcpy(section.name, names[i].data(), names[i].size());
        section.offset = 37 + i * 257;
        section.size = 64;
        expected[i].resize(section.size);
        for (unsigned j = 0; j < section.size; ++j) expected[i][j] = static_cast<u8>(11 + i * 31 + j * 19);
        const auto& key = i < 2 ? ncch.primary_key : ncch.secondary_key;
        CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption encrypt(key.data(), key.size(), ncch.exefs_ctr.data());
        encrypt.Seek(0x200 + section.offset);
        encrypt.ProcessData(ncch.exefs_file->bytes.data() + 0x200 + section.offset, expected[i].data(), expected[i].size());
    }
    for (unsigned i = 0; i < names.size(); ++i) {
        std::vector<u8> actual;
        Check(ncch.LoadSectionExeFS(names[i].c_str(), actual) == Loader::ResultStatus::Success, "ExeFS section loads");
        Check(actual == expected[i], "ExeFS section plaintext matches");
    }
    ncch.exefs_header.section[0].size = 0xFFFFFFFF;
    std::vector<u8> actual;
    Check(ncch.LoadSectionExeFS("icon", actual) == Loader::ResultStatus::ErrorInvalidFormat,
          "section allocation bounded before decrypt");
}

static std::array<u8, 32> Digest(const std::vector<u8>& bytes) {
    std::array<u8, 32> hash;
    CryptoPP::SHA256().CalculateDigest(hash.data(), bytes.data(), bytes.size());
    return hash;
}
static void AddContent(CIAFile& cia, u16 index, const std::vector<u8>& bytes, bool present = true) {
    FileSys::TitleMetadata::ContentChunk chunk{};
    chunk.index = index;
    chunk.type = FileSys::TMDContentTypeFlag::Encrypted;
    chunk.size = bytes.size();
    chunk.hash = Digest(bytes);
    cia.container.cia_tmd.AddContentChunk(chunk);
    if (present) cia.container.cia_header.SetContentPresent(index);
}
static void CheckCia() {
    std::vector<u8> plain(64);
    for (unsigned i = 0; i < plain.size(); ++i) plain[i] = static_cast<u8>(13 + i * 37);
    CIAFile sparse;
    AddContent(sparse, 0x1234, plain, false);
    AddContent(sparse, 7, std::vector<u8>(32, 0x81));
    Check(sparse.container.GetContentSize(0) == 0 && sparse.container.GetContentSize(1) == 32,
          "sparse CIA uses encoded presence index");
    sparse.container.cia_header.SetContentPresent(0x1234);
    Check(sparse.container.GetContentSize(0) == 64, "sparse CIA selected content size retained");
    const auto ctr = sparse.container.cia_tmd.GetContentCTRByIndex(0);
    std::array<u8, 16> expected_ctr{};
    expected_ctr[0] = 0x12; expected_ctr[1] = 0x34;
    Check(ctr == expected_ctr, "CIA CBC IV uses encoded big endian content index");
    std::array<u8, 16> key{};
    for (unsigned i = 0; i < key.size(); ++i) key[i] = static_cast<u8>(17 + i * 41);
    std::vector<u8> encrypted(plain.size());
    CryptoPP::CBC_Mode<CryptoPP::AES>::Encryption encrypt(key.data(), key.size(), expected_ctr.data());
    encrypt.ProcessData(encrypted.data(), plain.data(), plain.size());
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        CIAFile cia;
        AddContent(cia, 0x1234, plain);
        auto decrypt_key = key;
        if (scenario == 1) decrypt_key[0] ^= 1;
        cia.Initialize(decrypt_key);
        cia.current_content_file = std::make_unique<OutputFinalizer>();
        if (scenario == 3) cia.current_content_file->success = false;
        std::vector<u8> incomplete(15, 0xA7);
        Check(cia.ProcessChunk(0, incomplete) == FileSys::ResultWriteBeyondEnd,
              "incomplete CBC chunk rejected before crypto state changes");
        auto input = encrypted;
        if (scenario == 2) input[23] ^= 1;
        std::vector<u8> decrypted;
        std::size_t offset = 0;
        for (std::size_t size : {16, 32, 16}) {
            std::vector<u8> temp(input.begin() + offset, input.begin() + offset + size);
            Check(cia.ProcessChunk(0, temp) == 0, "complete CBC chunk processed");
            decrypted.insert(decrypted.end(), temp.begin(), temp.end());
            offset += size;
        }
        if (scenario == 0 || scenario == 3) Check(decrypted == plain, "CBC streaming preserves state across chunks");
        const bool finalized = cia.FinishCurrentContent();
        if (scenario == 0) Check(finalized, "correct CBC plaintext passes content hash");
        if (scenario == 1) Check(!finalized, "wrong CBC key rejected by content hash");
        if (scenario == 2) Check(!finalized, "corrupt CBC bytes rejected by content hash");
        if (scenario == 3) Check(!finalized, "writer finalization failure rejects correct plaintext");
        Check(!cia.current_content_file && !cia.decryption_state->content_hashes[0], "finalized resources released");
        Check(scenario == 0 || cia.current_content_install_result.result == FileSys::ResultFileNotFound,
              "finalization failures report install error");
    }
}

int main() {
    CheckSetup();
    CheckSections();
    CheckCia();
    std::printf("PASS NCCH/CIA crypto (%u assertions)\n", assertions);
}
