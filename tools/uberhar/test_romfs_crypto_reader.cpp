// CodexAstraLocal: Exercise production reader/cache code with real AES-CTR and
// transient synthetic key/IV bytes. No console keys, seeds or content are used.
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>
#include <cryptopp/aes.h>
#include <cryptopp/modes.h>
#include "common/alignment.h"
#include "common/common_types.h"
#include "common/static_lru_cache.h"

#define LOG_TRACE(...) ((void)0)

// CodexAstraLocal: This endpoint controls only storage availability. The real
// reader owns offset calculations, cache entries and all AES seek/decryption.
namespace FileUtil {
class IOFileBase {
public:
    explicit IOFileBase(std::vector<u8> bytes_) : bytes(std::move(bytes_)), advertised_size(bytes.size()) {}
    std::vector<u8> bytes;
    std::size_t advertised_size;
    unsigned reads = 0;
    std::size_t GetSize() const { return advertised_size; }
    std::size_t ReadAtBytes(u8* output, std::size_t count, std::size_t offset) {
        ++reads;
        if (offset >= bytes.size()) return 0;
        count = std::min(count, bytes.size() - offset);
        std::memcpy(output, bytes.data() + offset, count);
        return count;
    }
};
}

// CodexAstraLocal: Friend access executes the actual serialize function with
// recording archives, checking field layout without claiming a full Boost stream.
namespace boost::serialization {
template <class Base, class Derived> Base& base_object(Derived& value) { return value; }
class access {
public:
    template <class Archive, class Object>
    static void serialize(Archive& archive, Object& object, unsigned version) {
        object.serialize(archive, version);
    }
    template <class Object> static std::unique_ptr<Object> make() {
        return std::unique_ptr<Object>(new Object);
    }
};
}
namespace FileSys {
// @READER_CLASSES@
// @READ_METHODS@
}

static unsigned assertions = 0;
static void Check(bool ok, const char* description) {
    ++assertions;
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", description);
        std::exit(1);
    }
}

struct RecordingArchive {
    std::vector<std::string> fields;
    bool encrypted = false;
    u64 crypto_offset = 0;
    bool load_legacy = false;
    std::vector<u8> legacy_bytes;
    template <class Value> RecordingArchive& operator&(Value& value) {
        if constexpr (std::is_same_v<Value, FileSys::RomFSReader>) {
            fields.push_back("base");
        } else if constexpr (std::is_same_v<Value, std::unique_ptr<FileUtil::IOFileBase>>) {
            fields.push_back("file");
            if (load_legacy) value = std::make_unique<FileUtil::IOFileBase>(legacy_bytes);
        } else if constexpr (std::is_same_v<Value, bool>) {
            fields.push_back("encrypted"); encrypted = value;
        } else if constexpr (std::is_same_v<Value, std::array<u8, 16>>) {
            fields.push_back("crypto-array");
        } else if constexpr (std::is_same_v<Value, u64>) {
            fields.push_back("crypto-offset"); crypto_offset = value;
        } else {
            Check(false, "unexpected serialization field type");
        }
        return *this;
    }
};

static void ReadExact(FileSys::DirectRomFSReader& reader, const std::vector<u8>& plain,
                      std::size_t offset, std::size_t length, const char* description) {
    std::vector<u8> out(length + 13, 0xA7);
    const auto read = reader.ReadFile(offset, length, out.data());
    const auto expected = offset < plain.size() ? std::min(length, plain.size() - offset) : 0;
    Check(read == expected, "read length matches available bytes");
    Check(expected == 0 || std::equal(out.begin(), out.begin() + expected, plain.begin() + offset), description);
    Check(std::all_of(out.begin() + expected, out.end(), [](u8 byte) { return byte == 0xA7; }),
          "reader preserves bytes beyond actual output");
}

int main() {
    std::vector<u8> plain(40000), cipher(plain.size());
    for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<u8>((i * 73 + i / 43) & 0xFF);
    std::array<u8, 16> key{}, counter{};
    for (std::size_t i = 0; i < key.size(); ++i) {
        key[i] = static_cast<u8>(i * 17 + 29);
        counter[i] = static_cast<u8>(i * 11 + 41);
    }
    constexpr std::size_t romfs_bias = 0x1000;
    CryptoPP::CTR_Mode<CryptoPP::AES>::Encryption encrypt(key.data(), key.size(), counter.data());
    encrypt.Seek(romfs_bias);
    encrypt.ProcessData(cipher.data(), plain.data(), cipher.size());
    auto storage = std::make_unique<FileUtil::IOFileBase>(cipher);
    auto* endpoint = storage.get();
    FileSys::DirectRomFSReader reader(std::move(storage), key, counter, romfs_bias);
    Check(reader.GetSize() == plain.size() && reader.AllowsCachedReads(), "reader size and cache policy");
    ReadExact(reader, plain, 137, 79, "unaligned encrypted bytes");
    const auto after_first_read = endpoint->reads;
    ReadExact(reader, plain, 160, 30, "cached plaintext bytes");
    Check(endpoint->reads == after_first_read, "cache hit avoids underlying read");
    ReadExact(reader, plain, 8192 - 31, 100, "page crossing encrypted bytes");
    Check(endpoint->reads == after_first_read + 1, "page crossing reuses first page");
    ReadExact(reader, plain, 7, 20000, "large encrypted bytes");
    Check(!reader.CacheReady(7, 20000), "large reads bypass cache policy");
    Check(reader.CacheReady(137, 79), "small reads permit cache policy");
    ReadExact(reader, plain, plain.size() - 20, 100, "EOF encrypted bytes");
    const auto before_eof_repeat = endpoint->reads;
    ReadExact(reader, plain, plain.size() - 10, 30, "EOF repeat bytes");
    Check(endpoint->reads == before_eof_repeat + 1, "short final page is not reused");
    ReadExact(reader, plain, plain.size(), 19, "EOF offset returns no bytes");
    ReadExact(reader, plain, std::numeric_limits<std::size_t>::max(), 1, "huge offset returns no bytes");
    const auto before_zero = endpoint->reads;
    ReadExact(reader, plain, 0, 0, "zero length returns no bytes");
    Check(endpoint->reads == before_zero, "zero length avoids underlying read");

    auto truncated_storage = std::make_unique<FileUtil::IOFileBase>(cipher);
    auto* truncated_endpoint = truncated_storage.get();
    truncated_endpoint->bytes.resize(2500);
    FileSys::DirectRomFSReader truncated(std::move(truncated_storage), key, counter, romfs_bias);
    std::array<u8, 200> partial;
    partial.fill(0xA7);
    Check(truncated.ReadFile(2400, partial.size(), partial.data()) == 100, "truncated source returns actual count");
    Check(std::equal(partial.begin(), partial.begin() + 100, plain.begin() + 2400), "truncated source bytes decrypted");
    Check(std::all_of(partial.begin() + 100, partial.end(), [](u8 byte) { return byte == 0xA7; }),
          "truncated output tail untouched");
    truncated_endpoint->bytes = cipher;
    const auto before_retry = truncated_endpoint->reads;
    ReadExact(truncated, plain, 2400, 200, "truncated read is not cached");
    Check(truncated_endpoint->reads == before_retry + 1, "truncated page retries underlying read");
    auto missing_storage = std::make_unique<FileUtil::IOFileBase>(cipher);
    missing_storage->bytes.resize(2500);
    FileSys::DirectRomFSReader missing(std::move(missing_storage), key, counter, romfs_bias);
    partial.fill(0xA7);
    Check(missing.ReadFile(4000, partial.size(), partial.data()) == 0, "short source before requested portion returns zero");
    Check(std::all_of(partial.begin(), partial.end(), [](u8 byte) { return byte == 0xA7; }), "unavailable portion does not copy cache bytes");

    FileSys::DirectRomFSReader plaintext(std::make_unique<FileUtil::IOFileBase>(plain));
    ReadExact(plaintext, plain, 137, 79, "plaintext reader remains unchanged");
    ReadExact(plaintext, plain, 7, 20000, "plaintext large read remains unchanged");
    RecordingArchive version_zero;
    boost::serialization::access::serialize(version_zero, plaintext, 0);
    Check(version_zero.fields == std::vector<std::string>{"base", "file"}, "legacy archive consumes only original fields");
    RecordingArchive version_one;
    boost::serialization::access::serialize(version_one, reader, 1);
    Check(version_one.fields == std::vector<std::string>{"base", "file", "encrypted", "crypto-array", "crypto-array", "crypto-offset"}, "new archive appends crypto fields in order");
    Check(version_one.encrypted && version_one.crypto_offset == romfs_bias, "new archive retains encryption state");
    auto legacy_reader = boost::serialization::access::make<FileSys::DirectRomFSReader>();
    RecordingArchive load_zero;
    load_zero.load_legacy = true; load_zero.legacy_bytes = plain;
    boost::serialization::access::serialize(load_zero, *legacy_reader, 0);
    ReadExact(*legacy_reader, plain, 137, 79, "legacy loaded reader defaults to plaintext");
    std::printf("PASS RomFS crypto reader (%u assertions)\n", assertions);
}
