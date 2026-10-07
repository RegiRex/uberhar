// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// CodexAstraLocal: Exercise diagnostic boundaries independently of the renderer.
// Evidence may be absent or censored; it must never fabricate a submitted draw.
#include <array>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <thread>
#include "common/uberhar_activity.h"
#include "video_core/renderer_vulkan/uberhar_vertex_capture_policy.h"

namespace {
using namespace Vulkan::VertexCapture;
constexpr auto Gameplay = static_cast<u32>(Common::UberharActivity::Phase::Gameplay);
static_assert(Gameplay == 3);
unsigned checks{};

void Check(bool condition, const char* failure) {
    ++checks;
    if (!condition)
        throw std::runtime_error(failure);
}

// CodexAstraLocal: Sparse indices can have few elements but require an enormous
// upload; compact high-base meshes instead need only their rebased span.
void CheckBounds() {
    Check(ValidShape(3, 60000, 60002), "compact high-base mesh rejected");
    Check(!ValidShape(2, 0, 65535), "sparse index span escaped cap");
    Check(!ValidShape(4097, 0, 1), "count cap replaced by span cap");
    Check(!ValidShape(0, 0, 0), "empty draw accepted for payload");
    Check(!ValidShape(1, 1, 0), "reversed bounds accepted");
    constexpr u32 top = std::numeric_limits<u32>::max();
    Check(!ValidShape(1, 0, top), "full u32 span wrapped to zero");
    Check(ValidShape(4096, top - 4095, top), "bounded high span rejected");
    Check(!ValidShape(4096, top - 4096, top), "one excess vertex escaped cap");
    for (u32 count : {1U, 3U, 4096U, 4097U}) {
        for (u32 base : {0U, 60000U, top - 5000}) {
            for (u32 span : {1U, 4095U, 4096U, 4097U}) {
                const bool reference = count <= 4096 && span <= 4096;
                Check(ValidShape(count, base, base + span - 1) == reference,
                      "independent count/span boundary mismatch");
            }
        }
    }
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    for (std::size_t capacity : {std::size_t{0}, MaxPacketBytes, MaxPayloadBytes, maximum}) {
        Check(CanAppend(0, capacity, capacity), "exact capacity not usable");
        Check(CanAppend(capacity, 0, capacity), "zero append to full budget rejected");
        Check(!CanAppend(capacity, 1, capacity), "one-byte excess escaped cap");
        if (capacity < maximum)
            Check(!CanAppend(capacity + 1, 0, capacity), "invalid used budget accepted");
    }
    Check(!CanAppend(maximum, maximum, MaxPayloadBytes), "sum overflow authorized copy");
    Check(MaxPayloadBytes + MetadataReserve == MaxBytes, "metadata not budgeted");
    Check(MaxPacketBytes < MaxPayloadBytes && MaxManifestBytes < MetadataReserve,
          "metadata/payload budget separation lost");
}

// CodexAstraLocal: Recording, queue acceptance and completion are distinct facts.
// A later high-water cannot rescue a packet whose actual draw never executed.
void CheckSubmissionEvidence() {
    RecordedState unassigned;
    unassigned.MarkRecorded();
    Check(!unassigned.Accepted(100), "zero/unassigned tick fabricated acceptance");
    Check(!unassigned.Completed(100,100), "zero/unassigned tick fabricated completion");
    RecordedState packet;
    packet.tick = 17;
    Check(!packet.Accepted(100), "unrecorded draw reported accepted");
    Check(!packet.Completed(100, 100), "unrecorded draw reported completed");
    packet.MarkRecorded();
    Check(packet.WasRecorded(), "executed draw missing recorded state");
    Check(!packet.Accepted(std::nullopt), "contended/unknown submit became acceptance");
    Check(!packet.Accepted(16), "recorded but unsubmitted draw accepted");
    Check(packet.Accepted(17), "successful submission not acknowledged");
    Check(!packet.Completed(17, 16), "queue acceptance became GPU completion");
    Check(packet.Completed(17, 17), "known completed draw missing completion");
    Check(!packet.Completed(std::nullopt, 100), "completion bypassed missing submit proof");
    Check(!packet.Completed(16, 100), "failed submission rescued by unrelated completion");
    RecordedState canceled;
    canceled.tick = 17;
    Check(!canceled.Accepted(1000), "canceled command rescued by later submission");
    // CodexAstraLocal: Separate owner instances cannot inherit another session's
    // flag; production integration must also scope the supplied tick high-water.
    RecordedState another_session;
    another_session.tick = 17;
    Check(!another_session.Accepted(1000), "recorded flag leaked across owners");
    RecordedState worker_packet;
    worker_packet.tick = 31;
    std::thread worker([&] { worker_packet.MarkRecorded(); });
    worker.join();
    Check(worker_packet.Accepted(31), "worker execution was not visible to owner");
}

// CodexAstraLocal: Test real phase tokens so startup-generation changes, already
// Gameplay state and a short finite swap interval cannot silently arm/rearm.
void CheckPhaseAndWindow() {
    using namespace Common::UberharActivity;
    Reset(7);
    Window startup{0, 4, ManualPhase()};
    SetStartup(true);
    startup.NextSwap(ManualPhase(), (Capture().token & 4U) != 0);
    SetStartup(false);
    startup.NextSwap(ManualPhase(), (Capture().token & 4U) != 0);
    Check(!startup.Active(), "startup token generation armed capture");
    SetManualPhase(Gameplay);
    startup.NextSwap(ManualPhase(), false);
    Check(startup.Active() && startup.Interval() == 0, "manual gameplay edge did not arm");

    Window already_gameplay{0, 1, Gameplay};
    already_gameplay.NextSwap(Gameplay, false);
    Check(!already_gameplay.Active(), "initial Gameplay auto-armed");
    already_gameplay.NextSwap(0, false);
    already_gameplay.NextSwap(Gameplay, false);
    Check(already_gameplay.Active(), "observed phase reset failed to arm");
    already_gameplay.NextSwap(Gameplay, false);
    Check(already_gameplay.CurrentState() == Window::State::Closed,
          "one-swap capture remained active");

    Window masked_edge{0, 2, 0};
    masked_edge.NextSwap(Gameplay, true);
    masked_edge.NextSwap(Gameplay, false);
    Check(!masked_edge.Active(), "startup-masked edge armed after startup ended");
    masked_edge.NextSwap(0, false);
    masked_edge.NextSwap(Gameplay, false);
    Check(masked_edge.Active(), "fresh post-startup edge failed");

    for (u32 delay : {0U, 1U, 120U}) {
        for (u32 width : {1U, 4U, 8U}) {
            Window window{delay, width, 0};
            unsigned active_intervals{};
            u64 first{};
            for (u32 boundary = 0; boundary < delay + width + 3; ++boundary) {
                window.NextSwap(Gameplay, false);
                if (window.Active()) {
                    if (!first)
                        first = boundary + 1;
                    Check(window.Interval() == active_intervals,
                          "captured intervals are not consecutive");
                    ++active_intervals;
                }
            }
            Check(first == delay + 1, "first interval violates requested delay");
            Check(active_intervals == width, "finite capture window length wrong");
            window.NextSwap(0, false);
            window.NextSwap(Gameplay, false);
            Check(window.CurrentState() == Window::State::Closed,
                  "second phase transition rearmed completed capture");
        }
    }
    Window canceled{120, 8, 0};
    canceled.NextSwap(Gameplay, false);
    canceled.Close();
    for (unsigned i = 0; i < 140; ++i)
        canceled.NextSwap(i % 2 ? Gameplay : 0, false);
    Check(canceled.CurrentState() == Window::State::Closed, "closed window rearmed");
}
} // CodexAstraLocal: private policy regression fixtures.

int main() {
    try {
        CheckBounds();
        CheckSubmissionEvidence();
        CheckPhaseAndWindow();
        std::printf("PASS: vertex capture policy (%u checks)\n", checks);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
