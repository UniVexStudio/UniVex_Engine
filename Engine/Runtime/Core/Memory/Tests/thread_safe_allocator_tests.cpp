// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "heap_allocator.h"
#include "thread_safe_allocator.h"

namespace UVE::Memory::Tests {
namespace {

TEST(ThreadSafeAllocatorTest, ConcurrentAllocFree_BalancesExactly) {
    HeapAllocator heap;
    ThreadSafeAllocator shared{heap};
    constexpr int kThreads = 4;
    constexpr int kRounds = 500;
    std::vector<std::thread> workers;
    workers.reserve(kThreads);
    for (int thread = 0; thread < kThreads; ++thread) {
        workers.emplace_back([&shared] {
            for (int round = 0; round < kRounds; ++round) {
                void* const block = shared.Allocate(32U, 8U, __FILE__, __LINE__);
                shared.Deallocate(block);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(shared.GetAllocatedBytes(), 0U);
    EXPECT_EQ(heap.GetAllocatedBytes(), 0U);
}

} // namespace
} // namespace UVE::Memory::Tests
