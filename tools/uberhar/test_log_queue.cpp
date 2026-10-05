// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Check the actual timed queue used by the logger, including idle and
// shutdown cases.
#include "common/bounded_threadsafe_queue.h"
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
  release.set_value();
  producer.get();
  const bool inserted = attempt.get();
  assert(returned_without_producer && !inserted);
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

int main() {
  TestContendedProducer();
  TestSaturatedQueue();
  TestTryOwnershipAndOrder();
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
  puts("PASS: production logger queue handles bounded barrier insertion, "
       "saturation, ownership, FIFO, idle, wakeup, cancellation and drain");
}
