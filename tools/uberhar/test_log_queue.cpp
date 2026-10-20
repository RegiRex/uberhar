// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Check the actual timed queue used by the logger, including idle and
// shutdown cases.
#include "common/bounded_threadsafe_queue.h"
#include "common/logging/diagnostic_queue.h" // CodexAstraUlt: Test actual optional delivery.
#include <array>
#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <string> // CodexAstraLocal: Exercise borrowed production message ownership.
using namespace std::chrono_literals;

// CodexAstraUlt-2: Pause a producer inside construction, while it owns the queue's
// writer lock, without depending on thread scheduling to establish contention.
struct GatedEntry {
  int value = 0;

  GatedEntry() = default;
  GatedEntry(int value_, std::promise<void>* entered = nullptr,
             std::shared_future<void> release = {})
      : value(value_) {
    if (entered) {
      entered->set_value();
      release.wait();
    }
  }
};

// CodexAstraUlt-2: A failed barrier enqueue must finish before the blocked normal
// producer is released, and must neither overtake nor replace that producer.
static void TestContendedProducer() {
  Common::MPSCQueue<GatedEntry, 2> queue;
  std::promise<void> entered;
  std::promise<void> release;
  auto resumed = release.get_future().share();
  auto producer = std::async(std::launch::async, [&] {
    queue.EmplaceWait(7, &entered, resumed);
  });
  entered.get_future().wait();
  auto attempt =
      std::async(std::launch::async, [&] { return queue.TryEmplace(11); });
  const bool returned_without_producer =
      attempt.wait_for(1s) == std::future_status::ready;
  // CodexAstraUlt: Optional diagnostics must also return while a producer owns
  // the mutex, including when the synchronous debug option would normally write.
  Common::Log::DiagnosticDropCounter omissions;
  auto diagnostic = std::async(std::launch::async, [&] {
    Common::Log::SubmitEntry(queue, GatedEntry{12}, Common::Log::Delivery::Diagnostic,
                            true, omissions, [](const auto&) { assert(false); });
  });
  const bool diagnostic_returned =
      diagnostic.wait_for(1s) == std::future_status::ready;
  release.set_value();
  producer.get();
  diagnostic.get();
  const bool inserted = attempt.get();
  assert(returned_without_producer && !inserted);
  assert(diagnostic_returned && omissions.Pending() == 1);
  GatedEntry value;
  assert(queue.TryPop(value) && value.value == 7);
  assert(!queue.TryPop(value));
}

// CodexAstraUlt-2: Reproduce a saturated logger with another normal producer
// awaiting space. The flush attempt must return while the consumer remains stopped.
static void TestSaturatedQueue() {
  Common::MPSCQueue<int, 1> queue;
  queue.EmplaceWait(1);
  std::promise<void> entered;
  auto producer = std::async(std::launch::async, [&] {
    entered.set_value();
    queue.EmplaceWait(2);
  });
  entered.get_future().wait();
  assert(producer.wait_for(50ms) == std::future_status::timeout);
  auto attempt =
      std::async(std::launch::async, [&] { return queue.TryEmplace(3); });
  const bool returned_without_consumer =
      attempt.wait_for(1s) == std::future_status::ready;
  int value = 0;
  assert(queue.TryPop(value) && value == 1);
  producer.get();
  const bool inserted = attempt.get();
  assert(returned_without_consumer && !inserted);
  assert(queue.TryPop(value) && value == 2);
  assert(!queue.TryPop(value));
}

// CodexAstraUlt-2: Failure retains the caller's move-only barrier; successful
// retries preserve FIFO with ordinary entries and do not drop normal messages.
static void TestTryOwnershipAndOrder() {
  Common::MPSCQueue<std::unique_ptr<int>, 2> queue;
  queue.EmplaceWait(std::make_unique<int>(1));
  auto barrier = std::make_unique<int>(2);
  assert(queue.TryEmplace(std::move(barrier)) && !barrier);
  auto pending = std::make_unique<int>(3);
  assert(!queue.TryEmplace(std::move(pending)) && pending && *pending == 3);
  std::unique_ptr<int> value;
  assert(queue.TryPop(value) && *value == 1);
  assert(queue.TryEmplace(std::move(pending)) && !pending);
  assert(queue.TryPop(value) && *value == 2);
  assert(queue.TryPop(value) && *value == 3);
  assert(!queue.TryPop(value));
}

// CodexAstraUlt: Use the production routing policy against a stopped consumer;
// only opted-in records are omitted, while reliable records/barriers retain FIFO.
static void TestDiagnosticDelivery() {
  using Common::Log::Delivery;
  using Common::Log::SubmitEntry;
  Common::MPSCQueue<int, 2> queue;
  Common::Log::DiagnosticDropCounter omissions;
  int synchronous_value = 0;
  auto synchronous_write = [&](int value) { synchronous_value = value; };
  SubmitEntry(queue, 1, Delivery::Reliable, false, omissions, synchronous_write);
  SubmitEntry(queue, 2, Delivery::Diagnostic, false, omissions, synchronous_write);
  SubmitEntry(queue, 3, Delivery::Diagnostic, false, omissions, synchronous_write);
  assert(omissions.Pending() == 1);

  // CodexAstraUlt: Reliable records still wait rather than disappearing when full;
  // optional attempts and unsuccessful flush barriers cannot steal their entries.
  std::promise<void> entered;
  auto reliable = std::async(std::launch::async, [&] {
    entered.set_value();
    SubmitEntry(queue, 4, Delivery::Reliable, false, omissions, synchronous_write);
  });
  entered.get_future().wait();
  assert(reliable.wait_for(50ms) == std::future_status::timeout);
  auto optional = std::async(std::launch::async, [&] {
    SubmitEntry(queue, 5, Delivery::Diagnostic, true, omissions, synchronous_write);
  });
  const bool returned = optional.wait_for(1s) == std::future_status::ready;
  assert(!queue.TryEmplace(6));
  int value = 0;
  assert(queue.TryPop(value) && value == 1);
  reliable.get();
  optional.get();
  assert(returned && omissions.Pending() == 2 && synchronous_value == 0);
  assert(queue.TryPop(value) && value == 2);
  assert(queue.TryPop(value) && value == 4);
  assert(!queue.TryPop(value));

  // CodexAstraUlt: Synchronous mode remains an explicit reliable-message option;
  // diagnostics always use the bounded queue, even when there is room to enqueue.
  SubmitEntry(queue, 7, Delivery::Reliable, true, omissions, synchronous_write);
  assert(synchronous_value == 7 && !queue.TryPop(value));
  SubmitEntry(queue, 8, Delivery::Diagnostic, true, omissions, synchronous_write);
  assert(synchronous_value == 7 && queue.TryPop(value) && value == 8);
  assert(queue.TryEmplace(9));
  assert(queue.TryPop(value) && value == 9);

  // CodexAstraLocal: Preserve cadence/forced export with the new exact-bin snapshots.
  // CodexAstraUlt: Exercise cadence, forced export and writes racing new drops.
  // A failed/unavailable sink does not acknowledge its snapshot and can retry.
  const auto now = Common::Log::DiagnosticDropCounter::Clock::now();
  const auto snapshot = omissions.Due(now, false);
  assert(snapshot.Total() == 2 && snapshot[Common::Log::DiagnosticFamily::Other] == 2);
  omissions.RecordDrop();
  omissions.Acknowledge(snapshot);
  assert(omissions.Pending() == 1);
  assert(omissions.Due(now + 1s, false).Total() == 0);
  assert(omissions.Due(now + 1s, true).Total() == 1);
  assert(omissions.Pending() == 1);
  const auto retry = omissions.Due(now + 6s, false);
  assert(retry.Total() == 1);
  omissions.Acknowledge(retry);
  assert(omissions.Pending() == 0 && omissions.Due(now + 7s, true).Total() == 0);
}

// CodexAstraLocal: Cover actual differently prefixed producers and specificity,
// plus unknown rows. A family counts whole omitted records, not contained events.
static void TestDiagnosticFamilies() {
  using namespace Common::Log;
  struct Expected { std::string_view message; DiagnosticFamily family; };
  const std::array cases{
      Expected{"Uberhar CPU draw features progress: schema=3", DiagnosticFamily::CpuDraw},
      Expected{"Uberhar deferred CPU draws progress:", DiagnosticFamily::CpuDraw},
      Expected{"Uberhar CPU parallel progress:", DiagnosticFamily::Prefix},
      Expected{"Uberhar texture decode: final=false", DiagnosticFamily::Decode},
      Expected{"Uberhar CPU raw copy: final=false", DiagnosticFamily::Decode},
      Expected{"Uberhar CPU transfer: feature=15", DiagnosticFamily::Decode},
      Expected{"Uberhar CPU adaptive history progress:", DiagnosticFamily::Adaptive},
      Expected{"Uberhar texture adaptation: feature=8", DiagnosticFamily::Adaptive},
      Expected{"Uberhar vertex scheduling progress:", DiagnosticFamily::Adaptive},
      Expected{"Uberhar fragment selected progress:", DiagnosticFamily::Fragment},
      Expected{"Uberhar shader preparation progress:", DiagnosticFamily::Fragment},
      Expected{"Uberhar push constants progress:", DiagnosticFamily::Fragment},
      Expected{"Uberhar CPU PSO census: schema=1", DiagnosticFamily::Fragment},
      Expected{"Uberhar adaptive CPU PSOs progress:", DiagnosticFamily::Fragment},
      Expected{"Uberhar GPU shader admission progress:", DiagnosticFamily::Fragment},
      Expected{"Uberhar GPU graphics detail: feature=gpu_identity.v1", DiagnosticFamily::GpuIdentity},
      Expected{"Uberhar GPU cost coverage: feature=gpu_work.v2", DiagnosticFamily::GpuCost},
      Expected{"Uberhar host cost queue: feature=host_wait.v2", DiagnosticFamily::GpuCost},
      Expected{"Software feature cost schema=1", DiagnosticFamily::Software},
      Expected{"Software clipping cost schema=1", DiagnosticFamily::Software},
      Expected{"Software row detail schema=1", DiagnosticFamily::Software},
      Expected{"Software constant scope schema=1", DiagnosticFamily::Software},
      Expected{"Uberhar software scheduling totals:", DiagnosticFamily::Software},
      Expected{"Uberhar Vulkan memory progress:", DiagnosticFamily::Other},
      Expected{"prefix Uberhar fragment selected progress:", DiagnosticFamily::Other},
      Expected{"Uberhar GPU graphics detailer:", DiagnosticFamily::Other},
      Expected{"", DiagnosticFamily::Other},
  };
  for (const auto& item : cases) {
    assert(ClassifyDiagnosticFamily(item.message) == item.family);
  }

  Common::MPSCQueue<std::string, 1> queue;
  DiagnosticDropCounter omissions;
  auto unexpected_write = [](const auto&) { assert(false); };
  std::string accepted = "Uberhar GPU graphics detail: accepted";
  SubmitEntry(queue, std::move(accepted), Delivery::Diagnostic, true, omissions,
              unexpected_write, accepted);
  assert(omissions.Pending() == 0);
  std::string dropped = "Uberhar GPU graphics detail: dropped";
  SubmitEntry(queue, std::move(dropped), Delivery::Diagnostic, true, omissions,
              unexpected_write, dropped);
  assert(dropped == "Uberhar GPU graphics detail: dropped");
  const auto now = DiagnosticDropCounter::Clock::now();
  const auto snapshot = omissions.Due(now, false);
  assert(snapshot.Total() == 1 && snapshot[DiagnosticFamily::GpuIdentity] == 1);
  // CodexAstraLocal: Same-family and different-family arrivals during sink output
  // survive exact-bin acknowledgement; a failed sink simply retries its snapshot.
  omissions.RecordDrop(DiagnosticFamily::GpuIdentity);
  omissions.RecordDrop(DiagnosticFamily::Software);
  omissions.Acknowledge(snapshot);
  const auto retry = omissions.Due(now + 1s, true);
  assert(retry.Total() == 2 && retry[DiagnosticFamily::GpuIdentity] == 1 &&
         retry[DiagnosticFamily::Software] == 1);
  assert(omissions.Due(now + 1s, true).families == retry.families);
  omissions.Acknowledge(retry);
  assert(omissions.Pending() == 0);
}

// CodexAstraLocal: Race multiple real failed enqueue producers with snapshot/ACK
// cycles. Every omission must appear once in its family, with no underflow/loss.
static void TestConcurrentDiagnosticFamilies() {
  using namespace Common::Log;
  Common::MPSCQueue<int, 1> queue;
  queue.EmplaceWait(0);
  DiagnosticDropCounter omissions;
  constexpr std::size_t attempts = 4000;
  const std::array<std::string_view, 3> messages{
      "Uberhar texture decode: test", "Uberhar fragment selected progress: test",
      "Uberhar GPU graphics detail: test"};
  std::array<std::future<void>, messages.size()> producers;
  std::atomic<std::size_t> remaining{messages.size()};
  for (std::size_t i = 0; i < producers.size(); ++i) {
    producers[i] = std::async(std::launch::async, [&, i] {
      for (std::size_t j = 0; j < attempts; ++j) {
        SubmitEntry(queue, 1, Delivery::Diagnostic, false, omissions,
                    [](int) { assert(false); }, messages[i]);
      }
      remaining.fetch_sub(1, std::memory_order_release);
    });
  }
  DiagnosticDropCounter::Snapshot total;
  const auto report = [&] {
    const auto snapshot = omissions.Due(DiagnosticDropCounter::Clock::now(), true);
    for (std::size_t i = 0; i < total.families.size(); ++i)
      total.families[i] += snapshot.families[i];
    omissions.Acknowledge(snapshot);
  };
  while (remaining.load(std::memory_order_acquire)) {
    report();
    std::this_thread::yield();
  }
  for (auto& producer : producers) producer.get();
  report();
  assert(total.Total() == attempts * messages.size());
  assert(total[DiagnosticFamily::Decode] == attempts);
  assert(total[DiagnosticFamily::Fragment] == attempts);
  assert(total[DiagnosticFamily::GpuIdentity] == attempts);
  assert(omissions.Pending() == 0);
  int value = -1;
  assert(queue.TryPop(value) && value == 0 && !queue.TryPop(value));
}

int main() {
  TestContendedProducer();
  TestSaturatedQueue();
  TestTryOwnershipAndOrder();
  TestDiagnosticDelivery(); // CodexAstraUlt: Opt-in loss never changes reliable delivery.
  TestDiagnosticFamilies(); // CodexAstraLocal: Actual row prefixes and retained ownership.
  TestConcurrentDiagnosticFamilies(); // CodexAstraLocal: Exact per-family concurrent ACK.
  Common::MPSCQueue<int> queue;
  std::stop_source stop;
  int value = 42;
  assert(!queue.PopWaitFor(value, stop.get_token(), 5ms));
  assert(value == 42);
  queue.EmplaceWait(7);
  assert(queue.PopWaitFor(value, stop.get_token(), 100ms));
  assert(value == 7);
  std::promise<void> started;
  auto ready = started.get_future();
  auto consumer = std::async(std::launch::async, [&] {
    started.set_value();
    int next = 0;
    assert(queue.PopWaitFor(next, stop.get_token(), 1s));
    return next;
  });
  ready.wait();
  queue.EmplaceWait(11);
  assert(consumer.get() == 11);
  stop.request_stop();
  queue.EmplaceWait(19);
  assert(!queue.PopWaitFor(value, stop.get_token(), 1s));
  assert(queue.TryPop(value) &&
         value == 19); // Shutdown drain retains pending messages.
  // CodexAstraUlt: Extend the unchanged -2 queue guarantees with optional-delivery checks.
  puts("PASS: production logger queue handles bounded barrier insertion, "
       "optional diagnostics, per-family concurrent omission accounting, synchronous policy, saturation, "
       "ownership, FIFO, idle, wakeup, cancellation and drain");
}
