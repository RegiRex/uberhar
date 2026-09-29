// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.

#pragma once

#include <cstring>
#include <type_traits>
#include <utility>
#include "common/common_types.h"

namespace Vulkan {

// AstraEH: Command-worker-owned, exact whole-block reuse for one immutable layout
// and stage range. Invalidate before reuse after a new command buffer or a foreign
// push write. Pipeline/descriptor binds alone do not alter these Vulkan values.
template <typename Value> class ExactPushConstants {
    static_assert(std::is_trivially_copyable_v<Value>);
    static_assert(std::has_unique_object_representations_v<Value>);

public:
    struct Counters {
        u64 requests{}, uploads{}, reuses{}, invalidations{};
    };

    template <typename Upload> bool UploadIfChanged(const Value& value, Upload&& upload) {
        ++counters.requests;
        if (valid && std::memcmp(&previous, &value, sizeof(Value)) == 0) {
            ++counters.reuses;
            return false;
        }
        // AstraEH: Commit only after the actual upload succeeds. A failed call
        // cannot authorize suppression of a subsequent retry.
        std::forward<Upload>(upload)(value);
        std::memcpy(&previous, &value, sizeof(Value));
        valid = true;
        ++counters.uploads;
        return true;
    }

    void Invalidate() {
        valid = false;
        ++counters.invalidations;
    }

    // AstraEH: Caller must drain the command worker before a title/lifetime reset.
    void Reset() {
        valid = false;
        counters = {};
    }

    const Counters& Stats() const {
        return counters;
    }

private:
    Value previous{};
    bool valid{};
    Counters counters;
};

} // namespace Vulkan
