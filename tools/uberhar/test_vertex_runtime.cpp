// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
// AstraEH: Compare replacement policy with the inherited linear FIFO and control
// stacks with boost, including repeated eviction, collisions and overflow/pop/reuse.
#include <cstdio>
#include <random>
#include <stdexcept>
#include <boost/circular_buffer.hpp>
#include "video_core/pica/uberhar_vertex_cache.h"
#include "video_core/shader/uberhar_interpreter_stack.h"

void Check(bool value, const char* text) {
    if (!value)
        throw std::runtime_error(text);
}
struct LinearReference {
    std::array<bool, 64> valid{};
    std::array<u16, 64> keys{};
    unsigned cursor{};
    int Find(u16 key) const {
        for (unsigned i = 0; i < 64; ++i)
            if (valid[i] && keys[i] == key)
                return i;
        return -1;
    }
    unsigned Insert(u16 key) {
        const auto slot = cursor;
        valid[slot] = true;
        keys[slot] = key;
        cursor = (cursor + 1) % 64;
        return slot;
    }
};
template <unsigned N>
void Stacks() {
    Pica::Shader::InterpreterStack<int, N> actual;
    boost::circular_buffer<int> expected(N);
    std::mt19937 rng(1100 + N);
    for (unsigned i = 0; i < 100000; ++i) {
        if (expected.empty() || rng() % 4 != 0) {
            const int value = rng() & 0xffff;
            actual.push_back(value);
            expected.push_back(value);
        } else {
            actual.pop_back();
            expected.pop_back();
        }
        Check(actual.empty() == expected.empty() && actual.size() == expected.size(),
              "stack size mismatch");
        if (!expected.empty()) {
            Check(actual.back() == expected.back(), "stack overflow/order mismatch");
            // Loop counters are modified through back(), so also test mutable references.
            ++actual.back();
            ++expected.back();
        }
    }
    while (!expected.empty()) {
        Check(actual.back() == expected.back(), "stack drain mismatch");
        actual.pop_back();
        expected.pop_back();
    }
}
int main() {
    std::mt19937 rng(1100);
    for (unsigned pattern = 0; pattern < 8; ++pattern) {
        Pica::VertexCacheIndex actual;
        LinearReference expected;
        for (unsigned i = 0; i < 125000; ++i) {
            u16 key;
            switch (pattern) {
            case 0:
                key = i % 64;
                break;
            case 1:
                key = i % 65;
                break;
            case 2:
                key = rng() % 8;
                break;
            case 3:
                key = rng() % 256;
                break;
            case 4:
                key = (rng() % 64) * 128;
                break;
            case 5:
                key = 65535 - (i % 64);
                break;
            case 6:
                key = (i / 4) % 1024;
                break;
            default:
                key = rng();
                break;
            }
            const int slot = actual.Find(key);
            Check(slot == expected.Find(key), "vertex hit/slot differs from original FIFO");
            if (slot < 0)
                Check(actual.Insert(key) == expected.Insert(key), "vertex eviction differs");
        }
    }
    Stacks<4>();
    Stacks<8>();
    std::puts("PASS: 1000000 vertex FIFO comparisons and 200000 circular-stack operations");
}
