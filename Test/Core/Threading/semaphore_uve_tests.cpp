// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <atomic>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/threading/semaphore_uve.h"

namespace UVE::Threading::Tests {
namespace {

TEST(SemaphoreUVETest, TryAcquire_TracksPermitsWithoutBlocking) {
    SemaphoreUVE gate;
    EXPECT_FALSE(gate.TryAcquireUVE());
    gate.ReleaseUVE(2U);
    EXPECT_TRUE(gate.TryAcquireUVE());
    EXPECT_TRUE(gate.TryAcquireUVE());
    EXPECT_FALSE(gate.TryAcquireUVE());
}

TEST(SemaphoreUVETest, Gate_ReleasesEveryWaiter) {
    SemaphoreUVE gate;
    constexpr int kWaitersUVE = 4;
    std::atomic<int> passed{0};
    std::vector<std::thread> waiters;
    for (int waiter = 0; waiter < kWaitersUVE; ++waiter) {
        waiters.emplace_back([&gate, &passed] {
            gate.AcquireUVE();
            passed.fetch_add(1, std::memory_order_relaxed);
        });
    }
    // Give the waiters a chance to block (not load-bearing: Release wakes whoever holds).
    gate.ReleaseUVE(kWaitersUVE);
    for (std::thread& waiter : waiters) {
        waiter.join();
    }
    EXPECT_EQ(passed.load(), kWaitersUVE);
}

} // namespace
} // namespace UVE::Threading::Tests
