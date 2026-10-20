// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include <catch2/catch_test_macros.hpp>
#include <json.hpp>
#include "common/file_util.h"
#include "common/scm_rev.h"
#include "common/uberhar_activity.h"
#include "video_core/renderer_vulkan/compute_failure_packet.h"
#include "video_core/renderer_vulkan/vk_compute_vertex.h"

namespace {
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Packet = Vulkan::ComputeFailurePacket;
using Stage = Vulkan::ComputeFailureStage;

// CodexAstraLocal: Redirect only the writer's dump path, then restore its exact
// prior value even if that directory never existed. UpdateUserPath would refuse
// such a restoration, so swap the underlying non-const FileUtil string back.
// All actual archive writes and the deliberate path blocker stay in this scope.
class DumpScope {
public:
    DumpScope()
        : path{const_cast<std::string&>(FileUtil::GetUserPath(FileUtil::UserPath::DumpDir))},
          saved{path} {
        static std::atomic<u64> next{};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        root = fs::temp_directory_path() /
            ("uberhar-compute-packet-" + std::to_string(stamp) + "-" + std::to_string(next++));
        if (!fs::create_directory(root)) throw std::runtime_error{"temporary directory collision"};
        dump = root / "dump";
        fs::create_directory(dump);
        FileUtil::UpdateUserPath(FileUtil::UserPath::DumpDir, dump.string());
    }
    ~DumpScope() {
        path.swap(saved);
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    std::vector<fs::path> Files() const {
        std::vector<fs::path> result;
        if (!fs::is_directory(dump)) return result;
        for (const auto& item : fs::directory_iterator(dump))
            if (item.path().extension() == ".ucp") result.push_back(item.path());
        std::sort(result.begin(), result.end());
        return result;
    }
    fs::path root, dump;
private:
    std::string& path;
    std::string saved;
};

std::vector<u8> Read(const fs::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) throw std::runtime_error{"retained packet missing"};
    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

template <typename T>
std::span<const u8> BytesOf(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    return {reinterpret_cast<const u8*>(&value), sizeof(value)};
}

// CodexAstraLocal: This is an independent archive reader, not a replacement
// writer. Validate explicit little-endian framing, every section's byte range
// and standard FNV-1a checksum, and the required completion footer. The checksum
// detects payload corruption; it is not authentication of the JSON metadata.
struct Archive {
    Json metadata;
    std::vector<u8> payload;
    std::span<const u8> Section(const std::string& name) const {
        const auto& description = metadata.at("sections").at(name);
        return std::span<const u8>{payload}.subspan(description.at("offset").get<std::size_t>(),
                                                  description.at("bytes").get<std::size_t>());
    }
};

std::string Hash(std::span<const u8> data) {
    u64 value = 0xcbf29ce484222325ULL;
    for (const auto byte : data) {
        value ^= byte;
        value *= 0x100000001b3ULL;
    }
    std::ostringstream text;
    text << std::uppercase << std::hex << std::setfill('0') << std::setw(16) << value;
    return text.str();
}

std::optional<Archive> Decode(std::span<const u8> bytes) {
    if (bytes.size() < 24 || std::memcmp(bytes.data(), "UBCAL001", 8) != 0) return {};
    const auto word = [&](std::size_t offset) {
        u32 value{};
        for (u32 i = 0; i < 4; ++i) value |= u32{bytes[offset + i]} << (8 * i);
        return value;
    };
    const std::size_t json_size = word(8), payload_size = word(12);
    if (json_size > 1024 * 1024 || payload_size > 64 * 1024 * 1024 ||
        24 + json_size + payload_size != bytes.size() ||
        std::memcmp(bytes.data() + bytes.size() - 8, "UCEND001", 8) != 0) return {};
    try {
        Archive result;
        result.metadata = Json::parse(bytes.begin() + 16, bytes.begin() + 16 + json_size);
        if (result.metadata.at("schema") != 1 || result.metadata.at("byte_order") != "little") return {};
        result.payload.assign(bytes.begin() + 16 + json_size, bytes.end() - 8);
        std::vector<std::pair<std::size_t, std::size_t>> ranges;
        for (const auto& section : result.metadata.at("sections").items()) {
            const auto offset = section.value().at("offset").get<std::size_t>();
            const auto size = section.value().at("bytes").get<std::size_t>();
            if (offset > payload_size || size > payload_size - offset ||
                Hash(std::span<const u8>{result.payload}.subspan(offset, size)) !=
                    section.value().at("fnv1a64").get<std::string>()) return {};
            ranges.emplace_back(offset, size);
        }
        std::sort(ranges.begin(), ranges.end());
        std::size_t end{};
        for (const auto& [offset, size] : ranges) {
            if (offset != end) return {};
            end += size;
        }
        if (end != payload_size) return {};
        return result;
    } catch (const std::exception&) {
        return {};
    }
}

void EqualSection(const Archive& archive, const std::string& name, std::span<const u8> expected) {
    REQUIRE(archive.metadata.at("sections").contains(name));
    const auto actual = archive.Section(name);
    REQUIRE(actual.size() == expected.size());
    CHECK(std::equal(actual.begin(), actual.end(), expected.begin()));
}

std::shared_ptr<const Vulkan::ComputeDeviceEvidence> Device() {
    // CodexAstraLocal: Synthetic device identity is serialized by production;
    // no Vulkan instance, driver call, guest replay or GPU work is performed.
    return std::make_shared<Vulkan::ComputeDeviceEvidence>(Vulkan::ComputeDeviceEvidence{
        0x5143, 0x740, 0x12345678, 0x00403000, 1U << 27, 256, 128, "fixture device"});
}

std::shared_ptr<Vulkan::ComputeVertexInput> Input() {
    auto input = std::make_shared<Vulkan::ComputeVertexInput>();
    for (std::size_t i = 0; i < input->metadata.size(); ++i) input->metadata[i] = 0xf0000000U + i;
    for (std::size_t i = 0; i < input->program.size(); ++i) input->program[i] = 0x13572468U ^ i;
    for (std::size_t i = 0; i < input->swizzles.size(); ++i) input->swizzles[i] = 0x89abcdefU ^ (i * 17);
    input->raw = {0, 1, 0xff, 0x80, 0x7f, 0, 0xc0, 0x7f, 0, 0, 0, 0x80, 0x31};
    input->sanitize_mul = true;
    return input;
}

std::array<u32, 64> Status() {
    std::array<u32, 64> result{};
    for (u32 i = 0; i < result.size(); ++i) result[i] = 0xdead0000U + i;
    result[19] = 0x7fc12345; result[20] = 0xff800000; result[63] = 0x80000000;
    return result;
}
} // namespace

TEST_CASE("Calculated failure packet preserves exact evidence and framing", "[compute-failure-packet]") {
    DumpScope scope;
    auto input = Input();
    const auto expected_input = *input;
    const auto status = Status();
    const std::array<u32, 5> expected{0x12345678, 7, 31, 0, 0x80000000};
    const std::array<u32, 6> original{0x7fc12345, 0x80000000, 1, 0xff800000, 0xffffffff, 0x3f800000};
    const std::array<u32, 4> uniforms{0x3f000000, 0x7fa54321, 0x80000000, 0x00000001};
    auto module = std::make_shared<Vulkan::ComputeModuleEvidence>();
    module->role = "guest";
    module->source = "#version 450\n// exact retained source\nvoid main() {}\n";
    module->spirv = {0x07230203, 0x00010600, 0x12345678, 0, 0};
    module->optimizer_disabled = true;
    const auto expected_module = *module;
    Packet packet{Device()};
    const auto session = Common::UberharActivity::Capture().run;
    packet.Stage(Stage::Completion);
    CHECK(packet.Stage() == Stage::Completion);
    packet.Label("title", "synthetic-owned-input");
    packet.Number("draw", 73);
    packet.Input(input, true);
    packet.Value("original.semantic.u32", original);
    packet.Value("uniforms.f32bits", uniforms);
    packet.Status(status); packet.Expected(expected);
    auto superseded = std::make_shared<Vulkan::ComputeModuleEvidence>(*module);
    superseded->source = "superseded source";
    packet.Module(superseded); packet.Module(module);
    // CodexAstraLocal: Owners may release their references before deferred
    // serialization. The packet must keep the original immutable evidence alive.
    input.reset(); module.reset(); superseded.reset();
    CHECK(scope.Files().empty());
    packet.Notice(Stage::Completion, "original failure", -13);
    CHECK(scope.Files().empty());
    packet.FlushNotices();
    const auto paths = scope.Files();
    REQUIRE(paths.size() == 1);
    const auto bytes = Read(paths[0]);
    const auto decoded = Decode(bytes);
    REQUIRE(decoded.has_value());
    const auto& archive = *decoded;
    const auto& meta = archive.metadata;
    CHECK(meta.at("build").get<std::string>() == Common::g_scm_rev);
    CHECK(meta.at("shader_cache_version").get<std::string>() == Common::g_shader_cache_version);
    CHECK(meta.at("session") == session);
    CHECK(meta.at("stage") == "completion");
    CHECK(meta.at("reason") == "original failure");
    CHECK(meta.at("vk_result") == -13);
    CHECK(meta.at("labels").at("title") == "synthetic-owned-input");
    CHECK(meta.at("labels").at("draw") == 73);
    CHECK(meta.at("labels").at("sanitize_mul") == true);
    CHECK(meta.at("labels").at("shader_entry_state") == "fresh_draw_zero_input_output_temporary_address_condition");
    CHECK(meta.at("labels").at("later_vertex_entry_state") == "serial_carry_reconstruct_from_original_packet_not_sampled");
    const Json expected_device{{"vendor",0x5143},{"device",0x740},{"driver_version",0x12345678},
        {"api_version",0x00403000},{"device_name","fixture device"},{"max_storage_buffer_range",1U<<27},
        {"max_workgroup_invocations",256},{"max_push_constants",128}};
    CHECK(meta.at("device") == expected_device);
    CHECK(meta.at("input_complete") == true);
    CHECK(meta.at("status_available") == true);
    CHECK(meta.at("capture_degraded") == false);
    CHECK(meta.at("unavailable").empty());
    CHECK(meta.at("full_capture") == true);
    CHECK(meta.at("incomplete") == false);
    CHECK(meta.at("pixel_replay_complete") == false);
    EqualSection(archive, "input.metadata.u32", BytesOf(expected_input.metadata));
    EqualSection(archive, "input.raw", expected_input.raw);
    EqualSection(archive, "input.program.u32", BytesOf(expected_input.program));
    EqualSection(archive, "input.swizzles.u32", BytesOf(expected_input.swizzles));
    EqualSection(archive, "actual.status.u32", BytesOf(status));
    EqualSection(archive, "expected.completion.u32", BytesOf(expected));
    EqualSection(archive, "original.semantic.u32", BytesOf(original));
    EqualSection(archive, "uniforms.f32bits", BytesOf(uniforms));
    EqualSection(archive, "guest.comp", {reinterpret_cast<const u8*>(expected_module.source.data()), expected_module.source.size()});
    EqualSection(archive, "guest.spv", {reinterpret_cast<const u8*>(expected_module.spirv.data()), expected_module.spirv.size()*4});
    REQUIRE(meta.at("modules").size() == 1);
    const Json expected_module_metadata{{"role","guest"},{"optimizer_disabled",true},{"entry","main"}};
    CHECK(meta.at("modules")[0] == expected_module_metadata);
    CHECK(meta.at("sections").size() == 10);
    // CodexAstraLocal: Challenge the independent reader with real writer bytes;
    // successful parsing alone cannot count as checksum/footer verification.
    auto corrupt = bytes;
    corrupt[corrupt.size()-9] ^= 1;
    CHECK_FALSE(Decode(corrupt).has_value());
    corrupt = bytes; corrupt.back() ^= 1;
    CHECK_FALSE(Decode(corrupt).has_value());
    corrupt = bytes; corrupt.pop_back();
    CHECK_FALSE(Decode(corrupt).has_value());
    corrupt = bytes; corrupt[12] ^= 1;
    CHECK_FALSE(Decode(corrupt).has_value());
}

TEST_CASE("Calculated failure packets deduplicate cancel and preserve earlier incidents", "[compute-failure-packet]") {
    DumpScope scope;
    Packet first{Device()};
    first.Notice(Stage::GuestCompile, "first compile failure", -1);
    first.Notice(Stage::GuestCompile, "must not replace first", -2);
    first.FlushNotices();
    REQUIRE(scope.Files().size() == 1);
    const auto first_path = scope.Files()[0];
    const auto first_bytes = Read(first_path);
    const auto first_archive = Decode(first_bytes);
    REQUIRE(first_archive.has_value());
    CHECK(first_archive->metadata.at("reason") == "first compile failure");
    CHECK(first_archive->metadata.at("vk_result") == -1);
    first.Failure(Stage::GuestCompile, "unwind repeats same stage", -3);
    CHECK(scope.Files().size() == 1);
    first.Failure(Stage::QueueSubmit, "distinct failure stage", -4);
    Packet second{Device()};
    second.Failure(Stage::GuestCompile, "distinct packet", -5);
    const auto paths = scope.Files();
    REQUIRE(paths.size() == 3);
    std::vector<u64> identities;
    std::vector<std::string> reasons;
    for (const auto& path : paths) {
        const auto archive = Decode(Read(path));
        REQUIRE(archive.has_value());
        identities.push_back(archive->metadata.at("packet").get<u64>());
        reasons.push_back(archive->metadata.at("reason").get<std::string>());
        if (archive->metadata.at("reason") == "distinct failure stage")
            CHECK(archive->metadata.at("packet") == first_archive->metadata.at("packet"));
        if (archive->metadata.at("reason") == "distinct packet")
            CHECK(archive->metadata.at("packet") != first_archive->metadata.at("packet"));
    }
    std::sort(identities.begin(), identities.end());
    CHECK(std::unique(identities.begin(), identities.end()) - identities.begin() == 2);
    std::sort(reasons.begin(), reasons.end());
    const std::vector<std::string> expected_reasons{
        "distinct failure stage", "distinct packet", "first compile failure"};
    CHECK(reasons == expected_reasons);
    CHECK(Read(first_path) == first_bytes);
    // CodexAstraLocal: Cancel the new draw whose predecessor already failed,
    // before it reports an incident; never relabel that predecessor as this draw.
    Packet canceled{Device()};
    canceled.CancelDependency();
    CHECK(canceled.DependencyCanceled());
    canceled.Notice(Stage::PreviousUseWait, "must not capture canceled draw");
    canceled.FlushNotices();
    canceled.Failure(Stage::Admission, "must still be suppressed");
    CHECK(scope.Files().size() == 3);

    // CodexAstraLocal: A regular-file parent deterministically rejects creation
    // even under privileged test runners. No logger reconfiguration or permission
    // assumptions: observe nonfatal failure and unchanged retained bytes.
    const auto blocked = scope.root / "blocked";
    fs::create_directory(blocked);
    FileUtil::UpdateUserPath(FileUtil::UserPath::DumpDir, blocked.string());
    fs::remove(blocked);
    { std::ofstream file{blocked, std::ios::binary}; file << "leave this file intact"; }
    const auto sentinel = Read(blocked);
    Packet unwritable{Device()};
    CHECK_NOTHROW(unwritable.Failure(Stage::Allocation, "unwritable diagnostic path"));
    CHECK(fs::is_regular_file(blocked));
    CHECK(Read(blocked) == sentinel);
    CHECK(scope.Files().size() == 3);
    FileUtil::UpdateUserPath(FileUtil::UserPath::DumpDir, scope.dump.string());
    Packet recovery{Device()};
    recovery.Failure(Stage::Allocation, "later independent incident");
    CHECK(scope.Files().size() == 4);
    CHECK(Read(first_path) == first_bytes);
}

TEST_CASE("Calculated failure packet marks missing and bounded evidence incomplete", "[compute-failure-packet]") {
    DumpScope scope;
    Packet missing{nullptr};
    const std::array<u32, 2> wrong_status{0x7fc12345, 7};
    missing.Status(wrong_status);
    missing.Unavailable("readback", "current fence failed; no stale read");
    missing.Failure(Stage::ProducerWait, "no successful current readback");
    REQUIRE(scope.Files().size() == 1);
    auto archive = Decode(Read(scope.Files()[0]));
    REQUIRE(archive.has_value());
    CHECK(archive->metadata.at("status_available") == false);
    CHECK(archive->metadata.at("input_complete") == false);
    CHECK(archive->metadata.at("full_capture") == false);
    CHECK(archive->metadata.at("incomplete") == true);
    CHECK(archive->metadata.at("unavailable").at("status") == "invalid_word_count");
    CHECK(archive->metadata.at("unavailable").at("actual.status.u32") == "no_successful_current_packet_readback");
    CHECK(archive->metadata.at("unavailable").at("input") == "capture_not_completed_no_substitution");
    CHECK(archive->metadata.at("unavailable").at("device") == "device_identity_capture_failed");
    CHECK(archive->metadata.at("unavailable").at("expected.completion.u32") == "validation_not_reached");
    CHECK(archive->metadata.at("unavailable").at("readback") == "current fence failed; no stale read");
    CHECK_FALSE(archive->metadata.at("sections").contains("actual.status.u32"));
    CHECK(archive->payload.empty());

    // CodexAstraLocal: One over-bound owned section is enough to exercise the
    // production cap. Move it into the packet, omit only that section, and retain
    // later small evidence with explicit incompleteness instead of truncation.
    constexpr std::size_t limit = 64 * 1024 * 1024;
    Packet bounded{Device()};
    const auto input = Input();
    const auto status = Status();
    const std::array<u32, 3> expected{1, 2, 3};
    const std::array<u8, 4> tail{7, 0, 0xff, 9};
    bounded.Input(input, true); bounded.Status(status); bounded.Expected(expected);
    bounded.Bytes("oversize.raw", std::vector<u8>(limit + 1, 0xa7));
    bounded.Bytes("tail.raw", tail);
    bounded.Failure(Stage::Completion, "bounded original evidence");
    REQUIRE(scope.Files().size() == 2);
    for (const auto& path : scope.Files()) {
        auto decoded = Decode(Read(path));
        REQUIRE(decoded.has_value());
        if (decoded->metadata.at("reason") == "bounded original evidence") archive = std::move(decoded);
    }
    REQUIRE(archive->metadata.at("reason") == "bounded original evidence");
    CHECK(archive->metadata.at("payload_limit_bytes") == limit);
    CHECK(archive->metadata.at("capture_degraded") == true);
    CHECK(archive->metadata.at("input_complete") == true);
    CHECK(archive->metadata.at("status_available") == true);
    CHECK(archive->metadata.at("full_capture") == false);
    CHECK(archive->metadata.at("incomplete") == true);
    CHECK(archive->metadata.at("pixel_replay_complete") == false);
    CHECK(archive->metadata.at("unavailable").at("oversize.raw") == "packet_64MiB_payload_bound");
    CHECK_FALSE(archive->metadata.at("sections").contains("oversize.raw"));
    CHECK(archive->payload.size() < limit);
    EqualSection(*archive, "input.raw", input->raw);
    EqualSection(*archive, "actual.status.u32", BytesOf(status));
    EqualSection(*archive, "expected.completion.u32", BytesOf(expected));
    EqualSection(*archive, "tail.raw", tail);
}
