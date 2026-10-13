// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <utility>

#include <gtest/gtest.h>

#include "uve/memory/heap_allocator_uve.h"
#include "uve/memory/unique_ptr_uve.h"

namespace UVE::Memory::Tests {
namespace {

struct DestructionCounterUVE final {
    explicit DestructionCounterUVE(int* counterOut) : counter(counterOut) {}
    ~DestructionCounterUVE() { ++(*counter); }
    int value = 0;
    int* counter;
};

TEST(UniquePtrUVETest, MakeUnique_ConstructsAndDestroysThroughTheAllocator) {
    HeapAllocatorUVE heap;
    int destructions = 0;
    {
        UniquePtrUVE<DestructionCounterUVE> owned =
            UVE_MAKE_UNIQUE(DestructionCounterUVE, heap, &destructions);
        ASSERT_TRUE(static_cast<bool>(owned));
        EXPECT_EQ(heap.GetAllocatedBytesUVE(), sizeof(DestructionCounterUVE));
        owned->value = 42;
        EXPECT_EQ((*owned).value, 42);
    }
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(heap.GetAllocatedBytesUVE(), 0U);
}

TEST(UniquePtrUVETest, Move_TransfersOwnershipAndAllocatorTogether) {
    HeapAllocatorUVE heap;
    int destructions = 0;
    UniquePtrUVE<DestructionCounterUVE> moved;
    {
        UniquePtrUVE<DestructionCounterUVE> owned =
            UVE_MAKE_UNIQUE(DestructionCounterUVE, heap, &destructions);
        moved = std::move(owned);
        EXPECT_FALSE(static_cast<bool>(owned));
    }
    EXPECT_EQ(destructions, 0); // Still alive under the moved-to pointer.
    EXPECT_TRUE(static_cast<bool>(moved));
    moved.ResetUVE();
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(heap.GetAllocatedBytesUVE(), 0U);
}

TEST(UniquePtrUVETest, Release_DetachesWithoutDestroying) {
    HeapAllocatorUVE heap;
    int destructions = 0;
    UniquePtrUVE<DestructionCounterUVE> owned =
        UVE_MAKE_UNIQUE(DestructionCounterUVE, heap, &destructions);
    DestructionCounterUVE* const raw = owned.ReleaseUVE();
    EXPECT_FALSE(static_cast<bool>(owned));
    EXPECT_EQ(destructions, 0);
    DestroyUVE(heap, raw); // The caller took the hand-pairing back.
    EXPECT_EQ(destructions, 1);
}

} // namespace
} // namespace UVE::Memory::Tests
