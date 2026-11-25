// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "spin_lock.h"

namespace UVE::Threading::Tests {
namespace {

TEST(SpinLockTest, GuardsACounterAcrossThreads) {
    SpinLock lock;
    int counter = 0;
    constexpr int kThreads = 4;
    constexpr int kRounds = 2000;
    std::vector<std::thread> workers;
    for (int thread = 0; thread < kThreads; ++thread) {
        workers.emplace_back([&lock, &counter] {
            for (int round = 0; round < kRounds; ++round) {
                const std::lock_guard<SpinLock> guard{lock};
                ++counter;
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(counter, kThreads * kRounds);
}

TEST(SpinLockTest, TryLock_SucceedsWhenFreeFailsWhenHeld) {
    SpinLock lock;
    EXPECT_TRUE(lock.try_lock());
    EXPECT_FALSE(lock.try_lock()); // Not recursive: the held lock refuses itself.
    lock.unlock();
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

} // namespace
} // namespace UVE::Threading::Tests
