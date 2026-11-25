// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "threading_semaphore.h"

namespace UVE::Threading::Tests {
namespace {

TEST(SemaphoreTest, TryAcquire_TracksPermitsWithoutBlocking) {
    Semaphore gate;
    EXPECT_FALSE(gate.TryAcquire());
    gate.Release(2U);
    EXPECT_TRUE(gate.TryAcquire());
    EXPECT_TRUE(gate.TryAcquire());
    EXPECT_FALSE(gate.TryAcquire());
}

TEST(SemaphoreTest, Gate_ReleasesEveryWaiter) {
    Semaphore gate;
    constexpr int kWaiters = 4;
    std::atomic<int> passed{0};
    std::vector<std::thread> waiters;
    for (int waiter = 0; waiter < kWaiters; ++waiter) {
        waiters.emplace_back([&gate, &passed] {
            gate.Acquire();
            passed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    // Give the waiters a chance to block (not load-bearing: Release wakes whoever holds).
    gate.Release(kWaiters);
    for (std::thread& waiter : waiters) {
        waiter.join();
    }
    EXPECT_EQ(passed.load(), kWaiters);
}

} // namespace
} // namespace UVE::Threading::Tests
