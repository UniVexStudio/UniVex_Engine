// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/memory/std_allocator_uve.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/memory/memory_manager_uve.h"
#include "uve/memory/pool_allocator_uve.h"

namespace UVE::Memory::Tests {
namespace {

TEST(StdAllocatorUVETest, VectorAllocatesThroughPool_TrackerSeesAllocationAndRelease) {
    MemoryManagerUVE manager;
    PoolAllocatorUVE pool(256, alignof(int), 4, &manager, "VectorPool");
    {
        std::vector<int, StdAllocatorUVE<int>> values(
            StdAllocatorUVE<int>(pool, __FILE__, __LINE__));
        values.reserve(16);
        for (int index = 0; index < 16; ++index) {
            values.push_back(index * 2);
        }

        // One vector buffer: one pool block, one tracked allocation of exactly the bytes asked.
        EXPECT_EQ(pool.GetUsedBlocksUVE(), 1U);
        EXPECT_EQ(manager.GetActiveAllocationCountUVE(), 1U);
        EXPECT_EQ(manager.GetActiveBytesUVE(), 16U * sizeof(int));
        EXPECT_EQ(values.size(), 16U);
        EXPECT_EQ(values[15], 30);

        // The live record carries the pool's tag plus the adaptor's construction-site label.
        const std::vector<AllocationRecordUVE> live = manager.GetLeakedAllocationsUVE();
        ASSERT_EQ(live.size(), 1U);
        EXPECT_EQ(live[0].allocatorTag, "VectorPool");
        EXPECT_STREQ(live[0].sourceFile, __FILE__);
        EXPECT_GT(live[0].sourceLine, 0);
    }

    EXPECT_EQ(pool.GetUsedBlocksUVE(), 0U);
    EXPECT_EQ(manager.GetActiveAllocationCountUVE(), 0U);
    EXPECT_EQ(manager.GetActiveBytesUVE(), 0U);
    EXPECT_FALSE(manager.HasLeaksUVE());
}

TEST(StdAllocatorUVETest, ReboundAllocator_SharesTheSameBackingPool) {
    MemoryManagerUVE manager;
    PoolAllocatorUVE pool(256, 8, 4, &manager, "RebindPool");

    const StdAllocatorUVE<int> ints(pool);
    StdAllocatorUVE<double> doubles(ints);
    double* const block = doubles.allocate(4);
    ASSERT_NE(block, nullptr);
    EXPECT_EQ(pool.GetUsedBlocksUVE(), 1U);
    EXPECT_EQ(manager.GetActiveAllocationCountUVE(), 1U);

    doubles.deallocate(block, 4);
    EXPECT_EQ(pool.GetUsedBlocksUVE(), 0U);
    EXPECT_FALSE(manager.HasLeaksUVE());
}

TEST(StdAllocatorUVETest, EqualityComparesBackingAllocatorIdentityOnly) {
    PoolAllocatorUVE poolA(64, 8, 4);
    PoolAllocatorUVE poolB(64, 8, 4);

    const StdAllocatorUVE<int> first(poolA);
    const StdAllocatorUVE<int> second(poolA, "elsewhere.cpp", 1);
    const StdAllocatorUVE<int> other(poolB);
    EXPECT_TRUE(first == second);
    EXPECT_FALSE(first != second);
    EXPECT_FALSE(first == other);
    EXPECT_TRUE(first != other);

    // Cross-type comparison works through the same identity rule.
    const StdAllocatorUVE<double> crossType(poolA);
    EXPECT_TRUE(first == crossType);
}

TEST(StdAllocatorUVETest, AllocateBeyondMaxSize_ThrowsBeforeTouchingThePool) {
    PoolAllocatorUVE pool(64, 8, 4);
    StdAllocatorUVE<int> allocator(pool);

    EXPECT_THROW(static_cast<void>(allocator.allocate(allocator.max_size() + 1U)),
                 std::bad_array_new_length);
    EXPECT_EQ(pool.GetUsedBlocksUVE(), 0U);
    // NOTE: a request that fits max_size() but exceeds the pool block (or arrives exhausted)
    // surfaces the pool's own assert/throw contract by design — no death test here for the same
    // reason FixedArrayUVE has none (UVE_DEBUG_BREAK is a trap, not a portable exit).
}

TEST(StdAllocatorUVETest, VectorCopy_SharesPoolAndFreesEachBufferIndependently) {
    MemoryManagerUVE manager;
    PoolAllocatorUVE pool(512, 8, 8, &manager, "CopyPool");
    {
        std::vector<int, StdAllocatorUVE<int>> values{StdAllocatorUVE<int>(pool)};
        values.reserve(8);
        for (int index = 0; index < 8; ++index) {
            values.push_back(index);
        }

        // Copy construction copies the adaptor, so the copy allocates its own block from the
        // same pool (propagate traits are false: assignment never re-points a container).
        const std::vector<int, StdAllocatorUVE<int>> copy(values);
        EXPECT_EQ(pool.GetUsedBlocksUVE(), 2U);
        EXPECT_EQ(manager.GetActiveAllocationCountUVE(), 2U);
        ASSERT_EQ(copy.size(), 8U);
        EXPECT_EQ(copy[7], 7);
        EXPECT_EQ(values[7], 7);
    }

    EXPECT_EQ(pool.GetUsedBlocksUVE(), 0U);
    EXPECT_FALSE(manager.HasLeaksUVE());
}

} // namespace
} // namespace UVE::Memory::Tests
