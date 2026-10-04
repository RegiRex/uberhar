// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version; see license.txt.
// AstraEH: Check the actual timed queue used by the logger, including idle and
// shutdown cases.
#include "common/bounded_threadsafe_queue.h"
#include <array>
#include <cassert>
#include <chrono>
#include <future>
using namespace std::chrono_literals;
int main() {
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
  puts("PASS: production logger queue handles idle, wakeup, cancellation and "
       "drain");
}
