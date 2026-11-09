// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Test the production lease, not a mirrored queue. Vulkan
// range/tick/order/publication is separately exercised by stream-order extracts.
#include <array>
#include <cassert>
#include <cstring>
#include <thread>
#include "video_core/uberhar_owned_vertex_upload.h"
struct Vertex { std::array<std::uint8_t, 88> bytes{}; };
using Pool = VideoCore::OwnedVertexUpload<Vertex>;
std::vector<Vertex> Make(std::size_t n, std::uint8_t value) {
    std::vector<Vertex> vertices(n);
    for (auto& vertex : vertices) vertex.bytes.fill(value);
    return vertices;
}
int main() {
    Pool pool;
    auto source = Make(1000, 0x53);
    auto* identity = source.data();
    auto first = pool.TryAcquire(source);
    assert(first && source.empty() && first.Bytes().data() == reinterpret_cast<std::uint8_t*>(identity));
    assert(first.Bytes().size() == 88000 && first.Bytes()[87999] == 0x53);
    source = Make(1000, 0xa9);
    assert(first.Bytes()[87999] == 0x53); // Owner's next draw cannot overwrite payload.
    std::array<Pool::Lease, 3> others;
    for (auto& lease : others) { auto input = Make(1000, 0x31); lease = pool.TryAcquire(input); assert(lease); }
    auto refusal = Make(1000, 0x7e); auto* refusal_id = refusal.data();
    assert(!pool.TryAcquire(refusal) && refusal.data() == refusal_id && refusal[0].bytes[0] == 0x7e);
    std::thread recorder([lease = std::move(first)]() mutable {
        assert(lease.Bytes()[87999] == 0x53);
        lease.Published(true, true, 37);
    });
    recorder.join();
    auto reuse = pool.TryAcquire(source);
    assert(reuse && source.empty() && source.data() == identity); // Actual recycled allocation.
    auto statistics = pool.GetStatistics();
    assert(statistics.acquired == 5 && statistics.refused == 1 && statistics.published == 1 &&
           statistics.copy_samples == 1 && statistics.copy_ns == 37 && statistics.copy_failures == 0);
    Pool::Lease surviving;
    { Pool local; auto input = Make(2000, 0x44); surviving = local.TryAcquire(input); }
    assert(surviving && surviving.Bytes()[175999] == 0x44);
    surviving.Published(false, false, 0); surviving = {}; // Cancellation also releases safely.
    Pool bounded; std::array<Pool::Lease, 4> large;
    for (unsigned n = 0; n < 3; ++n) { auto input = Make(30000, 0x60); large[n] = bounded.TryAcquire(input); assert(large[n]); }
    auto over_budget = Make(30000, 0x61); auto* before = over_budget.data();
    assert(!bounded.TryAcquire(over_budget) && over_budget.data() == before && over_budget.size() == 30000);
    assert(bounded.GetStatistics().peak_resident_bytes <= 8 * 1024 * 1024);
    auto oversize = Make(100000, 0x62);
    assert(!bounded.TryAcquire(oversize) && oversize.size() == 100000);
    std::vector<Vertex> empty; assert(!bounded.TryAcquire(empty));
}
