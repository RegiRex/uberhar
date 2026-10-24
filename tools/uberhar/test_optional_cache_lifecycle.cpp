// CodexAstraLocal: Exercise production renewal, negative-key identity and disk
// envelopes independently of Vulkan; actual shader output gates remain separate.
#include <iostream>
#include <stdexcept>
#include "video_core/renderer_vulkan/uberhar_compile_budget.h"
#include "video_core/renderer_vulkan/uberhar_optional_disk_cache.h"
#include "video_core/renderer_vulkan/uberhar_optional_failure_cache.h"
namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Key { unsigned a{}, b{}; bool operator==(const Key&) const = default; };
}
int main() {
    try {
        Vulkan::CompileBudget budget{4, 500};
        budget.Refill(1000);
        for (unsigned i = 0; i < 4; ++i) Require(budget.Spend(), "startup credit");
        Require(!budget.Spend(), "exhaustion blocks new work");
        budget.Refill(1499);
        Require(!budget.Available(), "no early credit");
        budget.Refill(1500);
        Require(budget.Credits() == 1 && budget.Spend(), "bounded time renewal");
        budget.Refill(1000000);
        Require(budget.Credits() == 4, "idle credit cannot grow unbounded");
        budget.Refill(999999);
        Require(budget.Credits() == 4, "clock reversal cannot generate credit");

        Vulkan::OptionalFailureCache<Key, 4> failures;
        const Key exact{1, 2}, collision{1, 3};
        failures.Remember(9, exact, 2, false, 5000);
        Require(failures.Find(9, exact) && failures.Find(9, exact)->retries == 2,
                "failed metadata survives owner reclamation");
        Require(!failures.Find(9, collision), "full negative-key equality");
        failures.Forget(9, collision);
        Require(failures.Find(9, exact), "collision cannot erase exact failure");
        failures.Forget(9, exact);
        Require(!failures.Find(9, exact), "explicit exact reset");

        const std::vector<u32> identity{1, 2, 3, 4};
        const std::vector<u32> code{0x07230203, 0x10300, 0, 1, 0, 0x10000};
        const auto record = Vulkan::OptionalDiskCache::Encode(identity, 77, code);
        Require(!record.empty(), "framed program persisted");
        Require(Vulkan::OptionalDiskCache::Decode(identity, 77, record) == code,
                "compatible optional program reconstructs");
        Require(Vulkan::OptionalDiskCache::Decode(identity, 78, record).empty(),
                "compiler generation rejects stale program");
        auto other = identity; other[2] ^= 1;
        Require(Vulkan::OptionalDiskCache::Decode(other, 77, record).empty(),
                "every semantic identity word validated");
        auto corrupt = record; corrupt.back() ^= 1;
        Require(Vulkan::OptionalDiskCache::Decode(identity, 77, corrupt).empty(),
                "corrupt program rejected");
        auto truncated = record; truncated.pop_back();
        Require(Vulkan::OptionalDiskCache::Decode(identity, 77, truncated).empty(),
                "truncated program rejected");
        auto oversized = code; oversized.resize(Vulkan::OptionalDiskCache::MaxProgramWords + 1);
        Require(Vulkan::OptionalDiskCache::Encode(identity, 77, oversized).empty(),
                "persistent program size bounded");
        std::cout << "Optional cache lifecycle checks passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
