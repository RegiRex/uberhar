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

  // CodexAstraUlt: Exercise cadence, forced export and writes racing new drops.
  // A failed/unavailable sink does not acknowledge its snapshot and can retry.
  const auto now = Common::Log::DiagnosticDropCounter::Clock::now();
  const auto snapshot = omissions.Due(now, false);
  assert(snapshot == 2);
  omissions.RecordDrop();
  omissions.Acknowledge(snapshot);
  assert(omissions.Pending() == 1);
  assert(omissions.Due(now + 1s, false) == 0);
  assert(omissions.Due(now + 1s, true) == 1);
  assert(omissions.Pending() == 1);
  assert(omissions.Due(now + 6s, false) == 1);
  omissions.Acknowledge(1);
  assert(omissions.Pending() == 0 && omissions.Due(now + 7s, true) == 0);
}

int main() {
  TestContendedProducer();
  TestSaturatedQueue();
  TestTryOwnershipAndOrder();
  TestDiagnosticDelivery(); // CodexAstraUlt: Opt-in loss never changes reliable delivery.
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
       "optional diagnostics, omission accounting, synchronous policy, saturation, "
       "ownership, FIFO, idle, wakeup, cancellation and drain");
}
