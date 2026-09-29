// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Execute queued draws against an independent command-state model.
// This tests actual production value reuse, not Vulkan driver speed or GPU pixels.
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "video_core/renderer_vulkan/uberhar_push_constants.h"
#include "video_core/shader/generator/glsl_fs_shader_gen.h"

namespace {
using Value = Pica::Shader::Generator::GLSL::DynamicTevState;
using Cache = Vulkan::ExactPushConstants<Value>;
static_assert(sizeof(Value) == 128);

void Require(bool condition, const char* reason) {
    if (!condition) {
        throw std::runtime_error(reason);
    }
}
bool Equal(const Value& a, const Value& b) {
    return std::memcmp(&a, &b, sizeof(Value)) == 0;
}

// AstraEH: Model the API state independently. A foreign partial write changes
// values/layout; new command buffers start undefined. Compute/descriptor binds
// leave fragment values alone. No model state is used to decide cache reuse.
struct DriverModel {
    Value fragment{};
    bool defined{}, compatible{};
    u64 uploads{}, draws{};
    void Upload(const Value& value) {
        fragment = value;
        defined = compatible = true;
        ++uploads;
    }
    void Draw(const Value& expected) {
        Require(defined && compatible && Equal(fragment, expected),
                "Draw saw stale/undefined constants");
        ++draws;
    }
    void ForeignWrite() {
        std::memset(&fragment, 0xCC, 16);
        compatible = false;
    }
    void NewBuffer() {
        defined = compatible = false;
    }
};

enum class Kind { Draw, ForeignWrite, NewBuffer, Compute, DescriptorBind, Report };
struct Command {
    Kind kind;
    Value constants{};
    bool dirty{}, fallback{};
};
struct Producer {
    bool pipeline_dirty{true}, fragment_dirty{true};
    std::vector<Command> commands;
    void Draw(const Value& value, bool selected_fallback) {
        commands.push_back(
            {Kind::Draw, value, pipeline_dirty || fragment_dirty, selected_fallback});
        pipeline_dirty = fragment_dirty = false;
    }
    void Event(Kind kind) {
        commands.push_back({kind});
        if (kind == Kind::ForeignWrite) {
            fragment_dirty = true;
        } else if (kind == Kind::NewBuffer) {
            pipeline_dirty = fragment_dirty = true;
        }
    }
};

// AstraEH: Delayed execution is deliberate: all later register changes have
// happened before the first command runs, so reference-capture mistakes matter.
void Execute(const Producer& producer, Cache& cache, DriverModel& driver) {
    for (const auto& command : producer.commands) {
        switch (command.kind) {
        case Kind::Draw:
            if (command.dirty) {
                cache.Invalidate();
            }
            if (command.fallback) {
                cache.UploadIfChanged(command.constants, [&](const Value& v) { driver.Upload(v); });
                driver.Draw(command.constants);
            }
            break;
        case Kind::ForeignWrite:
            driver.ForeignWrite();
            break;
        case Kind::NewBuffer:
            driver.NewBuffer();
            break;
        case Kind::Compute:
        case Kind::DescriptorBind:
            break;
        case Kind::Report: {
            const auto& s = cache.Stats();
            Require(s.requests == s.uploads + s.reuses, "Ordered report has inconsistent counters");
            Require(s.uploads == driver.uploads, "Report counted a suppressed upload");
            break;
        }
        }
    }
}
} // namespace

int main() {
    Cache cache;
    DriverModel driver;
    Value original{};
    cache.UploadIfChanged(original, [&](const Value& v) { driver.Upload(v); });
    driver.Draw(original);
    Require(!cache.UploadIfChanged(original, [&](const Value& v) { driver.Upload(v); }),
            "Equal full block uploaded twice");
    // AstraEH: Every ABI byte, including high lighting words, must cause an upload.
    for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
        auto changed = original;
        reinterpret_cast<unsigned char*>(&changed)[byte] ^= 0x80;
        Require(cache.UploadIfChanged(changed, [&](const Value& v) { driver.Upload(v); }),
                "Changed ABI byte suppressed");
        driver.Draw(changed);
        Require(!cache.UploadIfChanged(changed, [&](const Value& v) { driver.Upload(v); }),
                "Repeated changed block uploaded twice");
        cache.UploadIfChanged(original, [&](const Value& v) { driver.Upload(v); });
    }
    // AstraEH: Failed emission must not commit the new shadow value.
    auto changed = original;
    changed.lighting_ops_hi = 1;
    bool failed{};
    try {
        cache.UploadIfChanged(changed,
                              [](const Value&) { throw std::runtime_error("test upload"); });
    } catch (const std::runtime_error&) {
        failed = true;
    }
    Require(failed, "Failure probe did not throw");
    Require(cache.UploadIfChanged(changed, [&](const Value& v) { driver.Upload(v); }),
            "Failed upload incorrectly authorized reuse");
    driver.Draw(changed);

    cache.Reset();
    driver = {};
    Producer directed;
    directed.Draw(original, true);
    directed.Draw(original, true);
    // AstraEH: Foreign state -> specialized winner -> generic is the subtle case:
    // specialization consumes the dirty flag but must leave the shadow invalid.
    directed.Event(Kind::ForeignWrite);
    directed.Draw(changed, false);
    directed.Draw(original, true);
    directed.Event(Kind::NewBuffer);
    directed.Draw(changed, false);
    directed.Draw(original, true);
    directed.Event(Kind::Compute);
    directed.Event(Kind::DescriptorBind);
    directed.Draw(changed, false);
    directed.Draw(original, true);
    directed.Draw(changed, true); // a late generic winner uses this command's own copy
    directed.Draw(original, true);
    directed.Event(Kind::Report);
    Execute(directed, cache, driver);
    Require(cache.Stats().uploads == 5 && cache.Stats().reuses == 2,
            "Directed invalidation/selection sequence changed");

    // AstraEH: Reproducible mixed streams include chunk-like reports (no reset),
    // new buffers, foreign layout writes, repeated bytes and selected-path changes.
    u32 rng = 0x160916;
    const auto next = [&] {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    };
    Producer stream;
    Value state{};
    for (u32 i = 0; i < 200000; ++i) {
        const u32 r = next();
        if ((r & 15) == 0) {
            reinterpret_cast<unsigned char*>(&state)[(r >> 8) % sizeof(Value)] ^= 1;
        }
        if (i % 127 == 0)
            stream.Event(Kind::NewBuffer);
        if (i % 89 == 0)
            stream.Event(Kind::ForeignWrite);
        if (i % 31 == 0)
            stream.Event(Kind::Compute);
        if (i % 19 == 0)
            stream.Event(Kind::DescriptorBind);
        if (i % 4096 == 0)
            stream.Event(Kind::Report);
        stream.Draw(state, (r & 7) != 0);
    }
    Execute(stream, cache, driver);
    const auto stats = cache.Stats();
    Require(stats.requests == stats.uploads + stats.reuses && stats.reuses > 100000,
            "Mixed stream did not exercise exact suppression");
    // AstraEH: Simulate a fully drained title switch. Equal bytes still need the
    // first new upload, and counters start over with the new title.
    cache.Reset();
    driver = {};
    Require(cache.Stats().requests == 0, "Title reset retained counters");
    cache.UploadIfChanged(state, [&](const Value& v) { driver.Upload(v); });
    driver.Draw(state);
    Require(cache.Stats().uploads == 1 && cache.Stats().reuses == 0,
            "Title reset retained validity");
    std::cout << "PASS: 128 ABI-byte mutations, failed-upload retry, delayed queue/selection, "
              << "foreign writes, buffer/title resets; " << stats.requests << " modeled draws, "
              << stats.uploads << " uploads, " << stats.reuses << " exact reuses\n";
}
