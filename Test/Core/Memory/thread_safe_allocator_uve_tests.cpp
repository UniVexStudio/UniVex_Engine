// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/memory/heap_allocator_uve.h"
#include "uve/memory/thread_safe_allocator_uve.h"

namespace UVE::Memory::Tests {
namespace {

TEST(ThreadSafeAllocatorUVETest, ConcurrentAllocFree_BalancesExactly) {
    HeapAllocatorUVE heap;
    ThreadSafeAllocatorUVE shared{heap};
    constexpr int kThreadsUVE = 4;
    constexpr int kRoundsUVE = 500;
    std::vector<std::thread> workers;
    workers.reserve(kThreadsUVE);
    for (int thread = 0; thread < kThreadsUVE; ++thread) {
        workers.emplace_back([&shared] {
            for (int round = 0; round < kRoundsUVE; ++round) {
                void* const block = shared.AllocateUVE(32U, 8U, __FILE__, __LINE__);
                shared.DeallocateUVE(block);
            }
        });
    }
    for (std::thread& worker : workers) {
        worker.join();
    }
    EXPECT_EQ(shared.GetAllocatedBytesUVE(), 0U);
    EXPECT_EQ(heap.GetAllocatedBytesUVE(), 0U);
}

} // namespace
} // namespace UVE::Memory::Tests
