// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "alignment_utils.h"

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "heap_allocator.h"
#include "pool_allocator.h"
#include "stack_allocator.h"

namespace UVE::Memory::Tests {
namespace {

class AlignmentTest : public ::testing::TestWithParam<std::size_t> {};

TEST_P(AlignmentTest, HeapAllocator_ReturnsCorrectlyAlignedPointer) {
    const std::size_t alignment = GetParam();
    HeapAllocator allocator;
    void* const pointer = allocator.Allocate(64, alignment, __FILE__, __LINE__);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0U);
    allocator.Deallocate(pointer);
}

TEST_P(AlignmentTest, PoolAllocator_ReturnsCorrectlyAlignedPointer) {
    const std::size_t alignment = GetParam();
    PoolAllocator pool(64, alignment, 4);
    void* const pointer = pool.Allocate(64, alignment, __FILE__, __LINE__);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0U);
    pool.Deallocate(pointer);
}

TEST_P(AlignmentTest, StackAllocator_ReturnsCorrectlyAlignedPointer) {
    const std::size_t alignment = GetParam();
    StackAllocator stack(1024);
    void* const pointer = stack.Allocate(64, alignment, __FILE__, __LINE__);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(pointer) % alignment, 0U);
}

INSTANTIATE_TEST_SUITE_P(VariousAlignments, AlignmentTest,
                          ::testing::Values<std::size_t>(8, 16, 32, 64, 128));

TEST(IsValidAlignmentTest, PowersOfTwo_AreValid) {
    EXPECT_TRUE(IsValidAlignment(1));
    EXPECT_TRUE(IsValidAlignment(2));
    EXPECT_TRUE(IsValidAlignment(8));
    EXPECT_TRUE(IsValidAlignment(128));
}

TEST(IsValidAlignmentTest, ZeroAndNonPowersOfTwo_AreInvalid) {
    EXPECT_FALSE(IsValidAlignment(0));
    EXPECT_FALSE(IsValidAlignment(3));
    EXPECT_FALSE(IsValidAlignment(6));
    EXPECT_FALSE(IsValidAlignment(100));
}

} // namespace
} // namespace UVE::Memory::Tests
