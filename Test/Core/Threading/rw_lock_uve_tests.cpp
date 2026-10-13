// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/threading/rw_lock_uve.h"

namespace UVE::Threading::Tests {
namespace {

TEST(RwLockUVETest, ReadersShareAndWriterExcludes) {
    RwLockUVE lock;
    int value = 0;
    // A held writer blocks a shared try-lock — deterministic (no timing involved).
    lock.lock();
    std::atomic<bool> sharedWon{true};
    std::thread reader([&lock, &sharedWon] { sharedWon.store(lock.try_lock_shared()); });
    reader.join();
    EXPECT_FALSE(sharedWon.load());
    lock.unlock();
    // And a held reader blocks an exclusive try-lock.
    lock.lock_shared();
    std::atomic<bool> exclusiveWon{true};
    std::thread writer([&lock, &exclusiveWon] { exclusiveWon.store(lock.try_lock()); });
    writer.join();
    EXPECT_FALSE(exclusiveWon.load());
    lock.unlock_shared();
    // Full cycle through the STL guards.
    {
        const std::unique_lock<RwLockUVE> write{lock};
        value = 42;
    }
    {
        const std::shared_lock<RwLockUVE> read{lock};
        EXPECT_EQ(value, 42);
    }
}

TEST(RwLockUVETest, ConcurrentReaders_AllSeeThePublishedValue) {
    RwLockUVE lock;
    int value = 0;
    {
        const std::unique_lock<RwLockUVE> write{lock};
        value = 7;
    }
    constexpr int kReadersUVE = 8;
    std::vector<std::thread> readers;
    std::atomic<int> sum{0};
    for (int reader = 0; reader < kReadersUVE; ++reader) {
        readers.emplace_back([&lock, &value, &sum] {
            const std::shared_lock<RwLockUVE> read{lock};
            sum.fetch_add(value, std::memory_order_relaxed);
        });
    }
    for (std::thread& reader : readers) {
        reader.join();
    }
    EXPECT_EQ(sum.load(), kReadersUVE * 7);
}

} // namespace
} // namespace UVE::Threading::Tests
