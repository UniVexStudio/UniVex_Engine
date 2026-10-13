// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/threading/spin_lock_uve.h"

namespace UVE::Threading::Tests {
namespace {

TEST(SpinLockUVETest, GuardsACounterAcrossThreads) {
    SpinLockUVE lock;
    int counter = 0;
    constexpr int kThreadsUVE = 4;
    constexpr int kRoundsUVE = 2000;
    std::vector<std::thread> workers;
    for (int thread = 0; thread < kThreadsUVE; ++thread) {
        workers.emplace_back([&lock, &counter] {
            for (int round = 0; round < kRoundsUVE; ++round) {
                const std::lock_guard<SpinLockUVE> guard{lock};
                ++counter;
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(counter, kThreadsUVE * kRoundsUVE);
}

TEST(SpinLockUVETest, TryLock_SucceedsWhenFreeFailsWhenHeld) {
    SpinLockUVE lock;
    EXPECT_TRUE(lock.try_lock());
    EXPECT_FALSE(lock.try_lock()); // Not recursive: the held lock refuses itself.
    lock.unlock();
    EXPECT_TRUE(lock.try_lock());
    lock.unlock();
}

} // namespace
} // namespace UVE::Threading::Tests
