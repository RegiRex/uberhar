// Copyright 2026 Uberhar contributors
// Licensed under GPLv2 or any later version
// AstraEH: Exercise the production completion primitive used by Vulkan
// first-ready draws.

#include <cassert>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>

#include "common/async_handle.h"

using namespace std::chrono_literals;

int main() {
  // AstraEH: A ready alternative must not wait for an unfinished
  // specialization.
  {
    Common::AsyncCompletion signal;
    Common::AsyncHandle primary{false, &signal}, fallback{true, &signal};
    assert(!signal.WaitAny(primary, fallback));
    primary.MarkDone();
    assert(signal.WaitAny(primary, fallback));
  }

  // AstraEH: Completion after waiting begins must wake either path without
  // spinning.
  for (bool primary_wins : {false, true}) {
    Common::AsyncCompletion signal;
    Common::AsyncHandle primary{false, &signal}, fallback{false, &signal};
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    auto waiter = std::async(std::launch::async, [&] {
      entered.set_value();
      return signal.WaitAny(primary, fallback);
    });
    entered_future.wait();
    assert(waiter.wait_for(10ms) == std::future_status::timeout);
    signal.Notify(); // An unrelated pipeline completion must not release this
                     // draw.
    assert(waiter.wait_for(10ms) == std::future_status::timeout);
    (primary_wins ? primary : fallback).MarkDone();
    assert(waiter.wait_for(2s) == std::future_status::ready);
    assert(waiter.get() == primary_wins);
  }

  // AstraEH: Published data must be visible after first-ready selection.
  // Repetition races notification with predicate evaluation to exercise the
  // lost-wakeup boundary.
  for (int i = 0; i < 1000; ++i) {
    Common::AsyncCompletion signal;
    Common::AsyncHandle primary{false, &signal}, fallback{false, &signal};
    int published_value = 0;
    std::thread producer([&] {
      published_value = i + 1;
      fallback.MarkDone();
    });
    assert(!signal.WaitAny(primary, fallback));
    assert(published_value == i + 1);
    producer.join();
  }

  // AstraEH: The existing single-handle wait still works for shaders and forced
  // mode.
  {
    Common::AsyncHandle handle;
    std::thread producer([&] { handle.MarkDone(); });
    handle.WaitDone();
    assert(handle.IsDone());
    producer.join();
  }
  std::cout << "PASS: first-ready ordering, delayed completion, unrelated "
               "notifications, "
               "1000 publication races, and standalone waits\n";
}
