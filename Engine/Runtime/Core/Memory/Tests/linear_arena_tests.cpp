// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cstddef>
#include <cstdint>

#include <gtest/gtest.h>

#include "heap_allocator.h"
#include "linear_arena.h"
#include "build_variant.h"

namespace UVE::Memory::Tests {
namespace {

TEST(LinearArenaTest, SequentialAllocations_BumpWithoutOverlap) {
    LinearArena arena{256U};
    void* const first = arena.Allocate(64U, 8U, __FILE__, __LINE__);
    void* const second = arena.Allocate(64U, 8U, __FILE__, __LINE__);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_GE(static_cast<const std::byte*>(second) - static_cast<const std::byte*>(first), 64);
    EXPECT_EQ(arena.GetAllocatedBytes(), 128U);
    EXPECT_EQ(arena.Capacity(), 256U);
}

TEST(LinearArenaTest, Alignment_IsHonoredPerAllocation) {
    LinearArena arena{256U};
    (void)arena.Allocate(1U, 1U, __FILE__, __LINE__);
    void* const aligned = arena.Allocate(16U, 16U, __FILE__, __LINE__);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(aligned) % 16U, 0U);
}

TEST(LinearArenaTest, Deallocate_IsANoOpThatKeepsTheBytesOutstanding) {
    LinearArena arena{256U};
    void* const block = arena.Allocate(64U, 8U, __FILE__, __LINE__);
    arena.Deallocate(block); // Frees nothing: the arena frees only via Reset().
    EXPECT_EQ(arena.GetAllocatedBytes(), 64U);
}

TEST(LinearArenaTest, Reset_ReleasesEverythingForReuse) {
    LinearArena arena{128U};
    void* const before = arena.Allocate(128U, 8U, __FILE__, __LINE__);
    arena.Reset();
    EXPECT_EQ(arena.GetAllocatedBytes(), 0U);
    void* const after = arena.Allocate(128U, 8U, __FILE__, __LINE__);
    EXPECT_EQ(after, before); // The same storage, handed out again.
}

TEST(LinearArenaDeathTest, Exhaustion_Asserts) {
    // A profile that compiles assertions out has nothing to observe here: the allocation would
    // simply succeed past the arena's capacity instead of stopping the process.
    if (!UVE::Build::kAssertionsEnabled) {
        GTEST_SKIP() << "Assertions are compiled out of this build profile";
    }
    LinearArena arena{64U};
    (void)arena.Allocate(64U, 8U, __FILE__, __LINE__);
    EXPECT_DEATH({ (void)arena.Allocate(1U, 1U, __FILE__, __LINE__); }, "");
}

TEST(LinearArenaDeathTest, DeallocateForeignPointer_Asserts) {
    if (!UVE::Build::kAssertionsEnabled) {
        GTEST_SKIP() << "Assertions are compiled out of this build profile";
    }
    LinearArena arena{64U};
    HeapAllocator heap;
    void* const foreign = heap.Allocate(8U, 8U, __FILE__, __LINE__);
    EXPECT_DEATH({ arena.Deallocate(foreign); }, "");
    heap.Deallocate(foreign);
}

} // namespace
} // namespace UVE::Memory::Tests
