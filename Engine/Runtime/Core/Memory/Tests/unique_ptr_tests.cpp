// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <utility>

#include <gtest/gtest.h>

#include "heap_allocator.h"
#include "unique_ptr.h"

namespace UVE::Memory::Tests {
namespace {

struct DestructionCounter final {
    explicit DestructionCounter(int* counterOut) : counter(counterOut) {}
    ~DestructionCounter() { ++(*counter); }
    int value = 0;
    int* counter;
};

TEST(UniquePtrTest, MakeUnique_ConstructsAndDestroysThroughTheAllocator) {
    HeapAllocator heap;
    int destructions = 0;
    {
        UniquePtr<DestructionCounter> owned =
            UVE_MAKE_UNIQUE(DestructionCounter, heap, &destructions);
        ASSERT_TRUE(static_cast<bool>(owned));
        EXPECT_EQ(heap.GetAllocatedBytes(), sizeof(DestructionCounter));
        owned->value = 42;
        EXPECT_EQ((*owned).value, 42);
    }
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(heap.GetAllocatedBytes(), 0U);
}

TEST(UniquePtrTest, Move_TransfersOwnershipAndAllocatorTogether) {
    HeapAllocator heap;
    int destructions = 0;
    UniquePtr<DestructionCounter> moved;
    {
        UniquePtr<DestructionCounter> owned =
            UVE_MAKE_UNIQUE(DestructionCounter, heap, &destructions);
        moved = std::move(owned);
        EXPECT_FALSE(static_cast<bool>(owned));
    }
    EXPECT_EQ(destructions, 0); // Still alive under the moved-to pointer.
    EXPECT_TRUE(static_cast<bool>(moved));
    moved.Reset();
    EXPECT_EQ(destructions, 1);
    EXPECT_EQ(heap.GetAllocatedBytes(), 0U);
}

TEST(UniquePtrTest, Release_DetachesWithoutDestroying) {
    HeapAllocator heap;
    int destructions = 0;
    UniquePtr<DestructionCounter> owned =
        UVE_MAKE_UNIQUE(DestructionCounter, heap, &destructions);
    DestructionCounter* const raw = owned.Release();
    EXPECT_FALSE(static_cast<bool>(owned));
    EXPECT_EQ(destructions, 0);
    Destroy(heap, raw); // The caller took the hand-pairing back.
    EXPECT_EQ(destructions, 1);
}

} // namespace
} // namespace UVE::Memory::Tests
