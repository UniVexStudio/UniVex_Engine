// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "fixed_array.h"

namespace {

using UVE::Containers::FixedArray;

// Construction/destruction counter: proves the manual union-slot lifetime neither leaks nor
// double-destroys. Counters are reset per test; aggregate accounting (constructed == destroyed
// after the container dies) is the leak check.
struct LifetimeProbe {
    static inline int constructed = 0;
    static inline int destroyed = 0;

    static void Reset() {
        constructed = 0;
        destroyed = 0;
    }

    int value = 0;

    explicit LifetimeProbe(int valueIn = 0) : value(valueIn) { ++constructed; }
    LifetimeProbe(const LifetimeProbe& other) : value(other.value) { ++constructed; }
    LifetimeProbe(LifetimeProbe&& other) noexcept : value(other.value) { ++constructed; }
    LifetimeProbe& operator=(const LifetimeProbe& other) = default;
    LifetimeProbe& operator=(LifetimeProbe&& other) noexcept = default;
    ~LifetimeProbe() { ++destroyed; }
};

TEST(FixedArrayTest, DefaultConstructed_IsEmptyWithZeroSize) {
    const FixedArray<int, 4> array;
    EXPECT_EQ(array.Size(), 0U);
    EXPECT_EQ(array.Capacity(), 4U);
    EXPECT_TRUE(array.Empty());
    EXPECT_FALSE(array.Full());
}

TEST(FixedArrayTest, PushBack_GrowsSizeAndPreservesValues) {
    FixedArray<int, 4> array;
    array.PushBack(10);
    array.PushBack(20);
    array.PushBack(30);

    EXPECT_EQ(array.Size(), 3U);
    EXPECT_FALSE(array.Empty());
    EXPECT_FALSE(array.Full());
    EXPECT_EQ(array[0], 10);
    EXPECT_EQ(array[1], 20);
    EXPECT_EQ(array[2], 30);
    EXPECT_EQ(array.Front(), 10);
    EXPECT_EQ(array.Back(), 30);
}

TEST(FixedArrayTest, PushBack_RValueOverloadAcceptsTemporaries) {
    FixedArray<std::string, 2> array;
    array.PushBack(std::string("hello"));

    EXPECT_EQ(array.Size(), 1U);
    EXPECT_EQ(array[0], "hello");
    EXPECT_EQ(array.Front(), array.Back());
}

TEST(FixedArrayTest, PushBack_FillsToCapacityExactly) {
    FixedArray<int, 3> array;
    array.PushBack(1);
    array.PushBack(2);
    array.PushBack(3);

    EXPECT_EQ(array.Size(), 3U);
    EXPECT_TRUE(array.Full());
    // NOTE: pushing a 4th element asserts (UVE_ASSERT) by design — the overflow contract is
    // "check Full() first", and FrameTaskGraph's CapacityExceeded path is the covered
    // integration test of that contract. No death test here: UVE_DEBUG_BREAK is a trap, not a
    // portable exit.
}

TEST(FixedArrayTest, PopBack_RemovesLastElement) {
    FixedArray<int, 4> array;
    array.PushBack(1);
    array.PushBack(2);
    array.PopBack();

    EXPECT_EQ(array.Size(), 1U);
    EXPECT_EQ(array.Back(), 1);
}

TEST(FixedArrayTest, Clear_EmptiesAndStaysReusable) {
    FixedArray<int, 4> array;
    array.PushBack(1);
    array.PushBack(2);
    array.Clear();

    EXPECT_TRUE(array.Empty());
    EXPECT_EQ(array.Size(), 0U);

    array.PushBack(3);
    EXPECT_EQ(array.Size(), 1U);
    EXPECT_EQ(array[0], 3);
}

TEST(FixedArrayTest, CopyConstruct_CopiesElementsIndependently) {
    FixedArray<int, 4> original;
    original.PushBack(7);
    original.PushBack(8);

    const FixedArray<int, 4> copy(original);
    EXPECT_EQ(copy.Size(), 2U);
    EXPECT_EQ(copy[0], 7);
    EXPECT_EQ(copy[1], 8);

    original[0] = 99;
    EXPECT_EQ(copy[0], 7);
}

TEST(FixedArrayTest, CopyAssign_ReplacesContentsAndToleratesSelfAssign) {
    FixedArray<int, 4> source;
    source.PushBack(5);
    FixedArray<int, 4> destination;
    destination.PushBack(1);
    destination.PushBack(2);
    destination.PushBack(3);

    destination = source;
    EXPECT_EQ(destination.Size(), 1U);
    EXPECT_EQ(destination[0], 5);

    destination = destination;
    EXPECT_EQ(destination.Size(), 1U);
    EXPECT_EQ(destination[0], 5);
}

TEST(FixedArrayTest, MoveConstruct_TransfersElementsAndEmptiesSource) {
    FixedArray<std::string, 4> source;
    source.PushBack("a");
    source.PushBack("b");

    const FixedArray<std::string, 4> moved(std::move(source));
    EXPECT_EQ(moved.Size(), 2U);
    EXPECT_EQ(moved[0], "a");
    EXPECT_EQ(moved[1], "b");
    EXPECT_TRUE(source.Empty());
}

TEST(FixedArrayTest, MoveAssign_TransfersElementsAndEmptiesSource) {
    FixedArray<int, 4> source;
    source.PushBack(11);
    FixedArray<int, 4> destination;
    destination.PushBack(1);
    destination.PushBack(2);

    destination = std::move(source);
    EXPECT_EQ(destination.Size(), 1U);
    EXPECT_EQ(destination[0], 11);
    EXPECT_TRUE(source.Empty());
}

TEST(FixedArrayTest, RangeFor_IteratesLiveElementsOnly) {
    FixedArray<int, 8> array;
    array.PushBack(1);
    array.PushBack(2);
    array.PushBack(3);

    int sum = 0;
    for (const int value : array) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);
}

TEST(FixedArrayTest, AsSpan_ReflectsLiveRangeAndWritesThrough) {
    FixedArray<int, 8> array;
    array.PushBack(4);
    array.PushBack(5);

    const std::span<const int> view = array.AsSpan();
    EXPECT_EQ(view.size(), 2U);
    EXPECT_EQ(view[0], 4);

    array.AsSpan()[1] = 50;
    EXPECT_EQ(array[1], 50);
}

TEST(FixedArrayTest, ElementLifetime_NoLeaksNoDoubleDestroys) {
    LifetimeProbe::Reset();
    {
        FixedArray<LifetimeProbe, 4> array;
        array.PushBack(LifetimeProbe(1));
        array.PushBack(LifetimeProbe(2));
        array.PopBack();
        // 2 temporaries + 2 container moves constructed; 2 temporaries + 1 pop destroyed.
        EXPECT_EQ(LifetimeProbe::constructed, 4);
        EXPECT_EQ(LifetimeProbe::destroyed, 3);
    }
    EXPECT_EQ(LifetimeProbe::constructed, LifetimeProbe::destroyed);
}

}  // namespace
