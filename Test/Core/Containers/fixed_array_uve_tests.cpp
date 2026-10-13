// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "uve/containers/fixed_array_uve.h"

namespace {

using UVE::Containers::FixedArrayUVE;

// Construction/destruction counter: proves the manual union-slot lifetime neither leaks nor
// double-destroys. Counters are reset per test; aggregate accounting (constructed == destroyed
// after the container dies) is the leak check.
struct LifetimeProbeUVE {
    static inline int constructed = 0;
    static inline int destroyed = 0;

    static void ResetUVE() {
        constructed = 0;
        destroyed = 0;
    }

    int value = 0;

    explicit LifetimeProbeUVE(int valueIn = 0) : value(valueIn) { ++constructed; }
    LifetimeProbeUVE(const LifetimeProbeUVE& other) : value(other.value) { ++constructed; }
    LifetimeProbeUVE(LifetimeProbeUVE&& other) noexcept : value(other.value) { ++constructed; }
    LifetimeProbeUVE& operator=(const LifetimeProbeUVE& other) = default;
    LifetimeProbeUVE& operator=(LifetimeProbeUVE&& other) noexcept = default;
    ~LifetimeProbeUVE() { ++destroyed; }
};

TEST(FixedArrayUVETest, DefaultConstructed_IsEmptyWithZeroSize) {
    const FixedArrayUVE<int, 4> array;
    EXPECT_EQ(array.SizeUVE(), 0U);
    EXPECT_EQ(array.CapacityUVE(), 4U);
    EXPECT_TRUE(array.EmptyUVE());
    EXPECT_FALSE(array.FullUVE());
}

TEST(FixedArrayUVETest, PushBack_GrowsSizeAndPreservesValues) {
    FixedArrayUVE<int, 4> array;
    array.PushBackUVE(10);
    array.PushBackUVE(20);
    array.PushBackUVE(30);

    EXPECT_EQ(array.SizeUVE(), 3U);
    EXPECT_FALSE(array.EmptyUVE());
    EXPECT_FALSE(array.FullUVE());
    EXPECT_EQ(array[0], 10);
    EXPECT_EQ(array[1], 20);
    EXPECT_EQ(array[2], 30);
    EXPECT_EQ(array.FrontUVE(), 10);
    EXPECT_EQ(array.BackUVE(), 30);
}

TEST(FixedArrayUVETest, PushBack_RValueOverloadAcceptsTemporaries) {
    FixedArrayUVE<std::string, 2> array;
    array.PushBackUVE(std::string("hello"));

    EXPECT_EQ(array.SizeUVE(), 1U);
    EXPECT_EQ(array[0], "hello");
    EXPECT_EQ(array.FrontUVE(), array.BackUVE());
}

TEST(FixedArrayUVETest, PushBack_FillsToCapacityExactly) {
    FixedArrayUVE<int, 3> array;
    array.PushBackUVE(1);
    array.PushBackUVE(2);
    array.PushBackUVE(3);

    EXPECT_EQ(array.SizeUVE(), 3U);
    EXPECT_TRUE(array.FullUVE());
    // NOTE: pushing a 4th element asserts (UVE_ASSERT) by design — the overflow contract is
    // "check FullUVE() first", and FrameTaskGraphUVE's CapacityExceeded path is the covered
    // integration test of that contract. No death test here: UVE_DEBUG_BREAK is a trap, not a
    // portable exit.
}

TEST(FixedArrayUVETest, PopBack_RemovesLastElement) {
    FixedArrayUVE<int, 4> array;
    array.PushBackUVE(1);
    array.PushBackUVE(2);
    array.PopBackUVE();

    EXPECT_EQ(array.SizeUVE(), 1U);
    EXPECT_EQ(array.BackUVE(), 1);
}

TEST(FixedArrayUVETest, Clear_EmptiesAndStaysReusable) {
    FixedArrayUVE<int, 4> array;
    array.PushBackUVE(1);
    array.PushBackUVE(2);
    array.ClearUVE();

    EXPECT_TRUE(array.EmptyUVE());
    EXPECT_EQ(array.SizeUVE(), 0U);

    array.PushBackUVE(3);
    EXPECT_EQ(array.SizeUVE(), 1U);
    EXPECT_EQ(array[0], 3);
}

TEST(FixedArrayUVETest, CopyConstruct_CopiesElementsIndependently) {
    FixedArrayUVE<int, 4> original;
    original.PushBackUVE(7);
    original.PushBackUVE(8);

    const FixedArrayUVE<int, 4> copy(original);
    EXPECT_EQ(copy.SizeUVE(), 2U);
    EXPECT_EQ(copy[0], 7);
    EXPECT_EQ(copy[1], 8);

    original[0] = 99;
    EXPECT_EQ(copy[0], 7);
}

TEST(FixedArrayUVETest, CopyAssign_ReplacesContentsAndToleratesSelfAssign) {
    FixedArrayUVE<int, 4> source;
    source.PushBackUVE(5);
    FixedArrayUVE<int, 4> destination;
    destination.PushBackUVE(1);
    destination.PushBackUVE(2);
    destination.PushBackUVE(3);

    destination = source;
    EXPECT_EQ(destination.SizeUVE(), 1U);
    EXPECT_EQ(destination[0], 5);

    destination = destination;
    EXPECT_EQ(destination.SizeUVE(), 1U);
    EXPECT_EQ(destination[0], 5);
}

TEST(FixedArrayUVETest, MoveConstruct_TransfersElementsAndEmptiesSource) {
    FixedArrayUVE<std::string, 4> source;
    source.PushBackUVE("a");
    source.PushBackUVE("b");

    const FixedArrayUVE<std::string, 4> moved(std::move(source));
    EXPECT_EQ(moved.SizeUVE(), 2U);
    EXPECT_EQ(moved[0], "a");
    EXPECT_EQ(moved[1], "b");
    EXPECT_TRUE(source.EmptyUVE());
}

TEST(FixedArrayUVETest, MoveAssign_TransfersElementsAndEmptiesSource) {
    FixedArrayUVE<int, 4> source;
    source.PushBackUVE(11);
    FixedArrayUVE<int, 4> destination;
    destination.PushBackUVE(1);
    destination.PushBackUVE(2);

    destination = std::move(source);
    EXPECT_EQ(destination.SizeUVE(), 1U);
    EXPECT_EQ(destination[0], 11);
    EXPECT_TRUE(source.EmptyUVE());
}

TEST(FixedArrayUVETest, RangeFor_IteratesLiveElementsOnly) {
    FixedArrayUVE<int, 8> array;
    array.PushBackUVE(1);
    array.PushBackUVE(2);
    array.PushBackUVE(3);

    int sum = 0;
    for (const int value : array) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);
}

TEST(FixedArrayUVETest, AsSpan_ReflectsLiveRangeAndWritesThrough) {
    FixedArrayUVE<int, 8> array;
    array.PushBackUVE(4);
    array.PushBackUVE(5);

    const std::span<const int> view = array.AsSpanUVE();
    EXPECT_EQ(view.size(), 2U);
    EXPECT_EQ(view[0], 4);

    array.AsSpanUVE()[1] = 50;
    EXPECT_EQ(array[1], 50);
}

TEST(FixedArrayUVETest, ElementLifetime_NoLeaksNoDoubleDestroys) {
    LifetimeProbeUVE::ResetUVE();
    {
        FixedArrayUVE<LifetimeProbeUVE, 4> array;
        array.PushBackUVE(LifetimeProbeUVE(1));
        array.PushBackUVE(LifetimeProbeUVE(2));
        array.PopBackUVE();
        // 2 temporaries + 2 container moves constructed; 2 temporaries + 1 pop destroyed.
        EXPECT_EQ(LifetimeProbeUVE::constructed, 4);
        EXPECT_EQ(LifetimeProbeUVE::destroyed, 3);
    }
    EXPECT_EQ(LifetimeProbeUVE::constructed, LifetimeProbeUVE::destroyed);
}

}  // namespace
