// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "pool_allocator.h"

#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "platform.h"

namespace UVE::Memory::Tests {
namespace {

TEST(PoolAllocatorTest, AllocateUpToCapacity_Succeeds) {
    PoolAllocator pool(32, 8, 4);
    std::vector<void*> pointers;
    for (int i = 0; i < 4; ++i) {
        void* const pointer = pool.Allocate(32, 8, __FILE__, __LINE__);
        ASSERT_NE(pointer, nullptr);
        pointers.push_back(pointer);
    }
    EXPECT_EQ(pool.GetUsedBlocks(), 4U);
    EXPECT_EQ(pool.GetFreeBlocks(), 0U);
}

TEST(PoolAllocatorTest, ConstructWithOverflowedAlignmentRoundUp_ThrowsBadAlloc) {
    EXPECT_THROW({ PoolAllocator pool(std::numeric_limits<std::size_t>::max(), 64U, 1U); }, std::bad_alloc);
}

TEST(PoolAllocatorTest, ConstructWithOverflowedTotalFootprint_ThrowsBadAlloc) {
    const std::size_t blockCount = std::numeric_limits<std::size_t>::max() / 64U + 1U;
    EXPECT_THROW({ PoolAllocator pool(64U, 64U, blockCount); }, std::bad_alloc);
}

#if UVE_DEBUG
TEST(PoolAllocatorDeathTest, AllocateBeyondCapacity_Asserts) {
    PoolAllocator pool(32, 8, 1);
    (void)pool.Allocate(32, 8, __FILE__, __LINE__);
    EXPECT_DEATH({ (void)pool.Allocate(32, 8, __FILE__, __LINE__); }, "");
}

TEST(PoolAllocatorDeathTest, ConstructWithInvalidAlignment_Asserts) {
    EXPECT_DEATH({ PoolAllocator pool(32, 3, 4); }, "");
}

TEST(PoolAllocatorDeathTest, DeallocateForeignPointer_Asserts) {
    PoolAllocator pool(32, 8, 2);
    int foreign = 0;
    EXPECT_DEATH({ pool.Deallocate(&foreign); }, "");
}

TEST(PoolAllocatorDeathTest, AllocateExceedingBlockContract_Asserts) {
    PoolAllocator pool(16, 8, 2);
    EXPECT_DEATH({ (void)pool.Allocate(64, 8, __FILE__, __LINE__); }, "");
}
#else
TEST(PoolAllocatorTest, AllocateBeyondCapacity_ThrowsBadAllocInsteadOfCorruptingFreeList) {
    PoolAllocator pool(32, 8, 1);
    (void)pool.Allocate(32, 8, __FILE__, __LINE__);
    EXPECT_THROW({ (void)pool.Allocate(32, 8, __FILE__, __LINE__); }, std::bad_alloc);
    EXPECT_EQ(pool.GetUsedBlocks(), 1U);
}

TEST(PoolAllocatorTest, AllocateExceedingBlockContract_ThrowsInvalidArgument) {
    PoolAllocator pool(16, 8, 2);
    EXPECT_THROW({ (void)pool.Allocate(64, 8, __FILE__, __LINE__); }, std::invalid_argument);
    EXPECT_EQ(pool.GetUsedBlocks(), 0U);
}
#endif

TEST(PoolAllocatorTest, DeallocateThenReallocate_ReusesFreedBlock) {
    PoolAllocator pool(32, 8, 2);
    void* const first = pool.Allocate(32, 8, __FILE__, __LINE__);
    pool.Deallocate(first);
    EXPECT_EQ(pool.GetUsedBlocks(), 0U);

    void* const second = pool.Allocate(32, 8, __FILE__, __LINE__);
    EXPECT_EQ(second, first);
    EXPECT_EQ(pool.GetUsedBlocks(), 1U);
}

TEST(PoolAllocatorTest, AllocatedPointers_AreWithinRangeAndBlockAligned) {
    PoolAllocator pool(24, 16, 5);
    std::unordered_set<void*> seen;
    for (int i = 0; i < 5; ++i) {
        void* const pointer = pool.Allocate(24, 16, __FILE__, __LINE__);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % 16, 0U);
        EXPECT_TRUE(seen.insert(pointer).second) << "pool returned the same block twice";
    }
}

TEST(PoolAllocatorTest, BlockStats_UpdateAcrossAllocateAndDeallocate) {
    PoolAllocator pool(16, 8, 8);
    EXPECT_EQ(pool.GetCapacityBlocks(), 8U);
    EXPECT_EQ(pool.GetUsedBlocks(), 0U);
    EXPECT_EQ(pool.GetFreeBlocks(), 8U);

    std::vector<void*> pointers;
    for (int i = 0; i < 3; ++i) {
        pointers.push_back(pool.Allocate(16, 8, __FILE__, __LINE__));
    }
    EXPECT_EQ(pool.GetUsedBlocks(), 3U);
    EXPECT_EQ(pool.GetFreeBlocks(), 5U);

    pool.Deallocate(pointers.front());
    EXPECT_EQ(pool.GetUsedBlocks(), 2U);
    EXPECT_EQ(pool.GetFreeBlocks(), 6U);
}

} // namespace
} // namespace UVE::Memory::Tests
