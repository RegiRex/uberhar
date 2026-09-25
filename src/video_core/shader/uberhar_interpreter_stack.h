// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#pragma once
#include <array>
#include <cassert>
#include <cstddef>
#include <type_traits>

namespace Pica::Shader {
// AstraEH: Preserve boost::circular_buffer's overwrite-oldest stack behavior,
// including overflow, without three heap allocations for every interpreted vertex.
template <typename T, std::size_t Capacity>
class InterpreterStack {
    static_assert(Capacity > 0);
    static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);

public:
    bool empty() const {
        return count == 0;
    }
    std::size_t size() const {
        return count;
    }
    T& back() {
        assert(!empty());
        return data[(tail + Capacity - 1) % Capacity];
    }
    void push_back(const T& value) {
        data[tail] = value;
        tail = (tail + 1) % Capacity;
        if (count < Capacity)
            ++count;
    }
    void pop_back() {
        assert(!empty());
        tail = (tail + Capacity - 1) % Capacity;
        --count;
    }

private:
    std::array<T, Capacity> data;
    std::size_t tail{}, count{};
};
} // namespace Pica::Shader
