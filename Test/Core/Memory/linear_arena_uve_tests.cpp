// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "uve/memory/heap_allocator_uve.h"
#include "uve/memory/linear_arena_uve.h"

namespace UVE::Memory::Tests {
namespace {

TEST(LinearArenaUVETest, SequentialAllocations_BumpWithoutOverlap) {
    LinearArenaUVE arena{256U};
    void* const first = arena.AllocateUVE(64U, 8U, __FILE__, __LINE__);
    void* const second = arena.AllocateUVE(64U, 8U, __FILE__, __LINE__);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_GE(static_cast<const std::byte*>(second) - static_cast<const std::byte*>(first), 64);
    EXPECT_EQ(arena.GetAllocatedBytesUVE(), 128U);
    EXPECT_EQ(arena.CapacityUVE(), 256U);
}

TEST(LinearArenaUVETest, Alignment_IsHonoredPerAllocation) {
    LinearArenaUVE arena{256U};
    (void)arena.AllocateUVE(1U, 1U, __FILE__, __LINE__);
    void* const aligned = arena.AllocateUVE(16U, 16U, __FILE__, __LINE__);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % 16U, 0U);
}

TEST(LinearArenaUVETest, Deallocate_IsANoOpThatKeepsTheBytesOutstanding) {
    LinearArenaUVE arena{256U};
    void* const block = arena.AllocateUVE(64U, 8U, __FILE__, __LINE__);
    arena.DeallocateUVE(block); // Frees nothing: the arena frees only via ResetUVE().
    EXPECT_EQ(arena.GetAllocatedBytesUVE(), 64U);
}

TEST(LinearArenaUVETest, Reset_ReleasesEverythingForReuse) {
    LinearArenaUVE arena{128U};
    void* const before = arena.AllocateUVE(128U, 8U, __FILE__, __LINE__);
    arena.ResetUVE();
    EXPECT_EQ(arena.GetAllocatedBytesUVE(), 0U);
    void* const after = arena.AllocateUVE(128U, 8U, __FILE__, __LINE__);
    EXPECT_EQ(after, before); // The same storage, handed out again.
}

TEST(LinearArenaUVEDeathTest, Exhaustion_Asserts) {
    LinearArenaUVE arena{64U};
    (void)arena.AllocateUVE(64U, 8U, __FILE__, __LINE__);
    EXPECT_DEATH({ (void)arena.AllocateUVE(1U, 1U, __FILE__, __LINE__); }, "");
}

TEST(LinearArenaUVEDeathTest, DeallocateForeignPointer_Asserts) {
    LinearArenaUVE arena{64U};
    HeapAllocatorUVE heap;
    void* const foreign = heap.AllocateUVE(8U, 8U, __FILE__, __LINE__);
    EXPECT_DEATH({ arena.DeallocateUVE(foreign); }, "");
    heap.DeallocateUVE(foreign);
}

} // namespace
} // namespace UVE::Memory::Tests
