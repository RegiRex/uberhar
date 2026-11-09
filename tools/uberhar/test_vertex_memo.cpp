// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version. Refer to license.txt.
#include <array>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "common/logging/log.h"
#include "video_core/pica/uberhar_vertex_parallel_batch.h"
namespace Common::Log {
void Stop() {}
void FmtLogMessageImpl(Class, Level level, const char*, unsigned, const char*,
                       fmt::string_view, const fmt::format_args&) {
    if (level >= Level::Error) throw std::runtime_error("unexpected runtime error");
}
}
using namespace Pica;
// CodexAstraLocal: Real planner/scheduler and FIFO, immutable independently
// computed bytes as the shader boundary. Duplicate indices must keep exact output,
// FIFO order and chunk lifetime while reducing only certified repeated invocations.
int main() {
    for (unsigned cores : {1U, 4U}) for (bool indexed : {false, true})
        for (unsigned count : {127U, 128U, 512U, 4097U, 8193U})
            for (unsigned pattern : {0U, 1U, 2U}) {
                std::vector<OutputVertex> baseline, memo;
                std::array<NativeVertexCounts, 2> counts;
                NativeParallelBatch::Work work;
                for (unsigned on = 0; on < 2; ++on) {
                    NativeParallelBatch batch{cores, nullptr, false, bool(on)};
                    assert(batch.Prepare(count));
                    const auto vertex = [&](u32 i) {
                        return pattern == 0 ? i % 128 : pattern == 1 ? (i % 128) * 257 : i;
                    };
                    counts[on] = batch.Run(count, indexed, vertex,
                        [&](auto inputs, auto outputs) {
                            for (u32 i = 0; i < inputs.size(); ++i) {
                                std::array<u32, 24> bytes;
                                for (u32 j = 0; j < bytes.size(); ++j)
                                    bytes[j] = inputs[i].vertex * 31 + j;
                                std::memcpy(&outputs[i], bytes.data(), sizeof(OutputVertex));
                            }
                        }, [&](const OutputVertex& v) { (on ? memo : baseline).push_back(v); }, true);
                    if (on) work = batch.LastWork();
                }
                assert(baseline.size() == count && memo.size() == count);
                assert(std::memcmp(baseline.data(), memo.data(), count * sizeof(OutputVertex)) == 0);
                assert(counts[0].hits == counts[1].hits);
                assert(counts[1].invocations + work.memo_hits == counts[0].invocations);
                assert(work.owner_invocations + work.worker_invocations == counts[1].invocations);
                if (indexed && count >= 512 && pattern == 0) assert(work.memo_hits > 0);
                if (!indexed || count < 128) assert(work.memo_checks == 0);
            }
}
