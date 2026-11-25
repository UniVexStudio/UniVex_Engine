// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "small_vector.h"

namespace {

using UVE::Containers::SmallVector;

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

TEST(SmallVectorTest, DefaultConstructed_IsEmptyWithInlineCapacity) {
    const SmallVector<int, 4> vector;
    EXPECT_EQ(vector.Size(), 0U);
    EXPECT_EQ(vector.Capacity(), 4U);
    EXPECT_TRUE(vector.Empty());
}

TEST(SmallVectorTest, PushWithinInlineCapacity_NeverSpills) {
    SmallVector<int, 4> vector;
    vector.PushBack(1);
    vector.PushBack(2);
    vector.PushBack(3);
    vector.PushBack(4);

    EXPECT_EQ(vector.Size(), 4U);
    EXPECT_EQ(vector.Capacity(), 4U);
    EXPECT_EQ(vector[0], 1);
    EXPECT_EQ(vector.Back(), 4);
}

TEST(SmallVectorTest, PushPastInlineCapacity_SpillsWithDoubledCapacity) {
    SmallVector<int, 4> vector;
    for (int value = 1; value <= 5; ++value) {
        vector.PushBack(value);
    }

    EXPECT_EQ(vector.Size(), 5U);
    EXPECT_EQ(vector.Capacity(), 8U);
    for (int index = 0; index < 5; ++index) {
        EXPECT_EQ(vector[static_cast<std::size_t>(index)], index + 1);
    }
}

TEST(SmallVectorTest, RepeatedGrowth_PreservesAllValuesInOrder) {
    SmallVector<int, 2> vector;
    for (int value = 0; value < 20; ++value) {
        vector.PushBack(value * 3);
    }

    EXPECT_EQ(vector.Size(), 20U);
    for (int index = 0; index < 20; ++index) {
        EXPECT_EQ(vector[static_cast<std::size_t>(index)], index * 3);
    }
}

TEST(SmallVectorTest, Clear_EmptiesAndStaysReusableAfterSpill) {
    SmallVector<int, 2> vector;
    vector.PushBack(1);
    vector.PushBack(2);
    vector.PushBack(3);
    vector.Clear();

    EXPECT_TRUE(vector.Empty());
    EXPECT_EQ(vector.Size(), 0U);

    vector.PushBack(9);
    EXPECT_EQ(vector.Size(), 1U);
    EXPECT_EQ(vector[0], 9);
}

TEST(SmallVectorTest, CopyConstruct_PreservesInlineOrSpilledContents) {
    SmallVector<std::string, 4> inlineSource;
    inlineSource.PushBack("x");
    const SmallVector<std::string, 4> inlineCopy(inlineSource);
    EXPECT_EQ(inlineCopy.Size(), 1U);
    EXPECT_EQ(inlineCopy.Capacity(), 4U);
    EXPECT_EQ(inlineCopy[0], "x");

    SmallVector<int, 2> spilledSource;
    for (int value = 0; value < 6; ++value) {
        spilledSource.PushBack(value);
    }
    const SmallVector<int, 2> spilledCopy(spilledSource);
    EXPECT_EQ(spilledCopy.Size(), 6U);
    for (int index = 0; index < 6; ++index) {
        EXPECT_EQ(spilledCopy[static_cast<std::size_t>(index)], index);
    }
    // The source keeps its own buffer — mutating the copy must not alias it.
    EXPECT_EQ(spilledSource[0], 0);
}

TEST(SmallVectorTest, MoveConstruct_SpilledSourceStealsTheBuffer) {
    SmallVector<int, 2> source;
    for (int value = 0; value < 6; ++value) {
        source.PushBack(value * 2);
    }
    const std::size_t spilledCapacity = source.Capacity();
    EXPECT_GT(spilledCapacity, 2U);

    const SmallVector<int, 2> moved(std::move(source));
    EXPECT_EQ(moved.Size(), 6U);
    EXPECT_EQ(moved.Capacity(), spilledCapacity);
    EXPECT_EQ(moved[5], 10);
    EXPECT_TRUE(source.Empty());
    EXPECT_EQ(source.Capacity(), 2U);
}

TEST(SmallVectorTest, MoveAssign_InlineSourceMovesElements) {
    SmallVector<int, 4> source;
    source.PushBack(7);
    source.PushBack(8);
    SmallVector<int, 4> destination;
    destination.PushBack(1);

    destination = std::move(source);
    EXPECT_EQ(destination.Size(), 2U);
    EXPECT_EQ(destination[0], 7);
    EXPECT_EQ(destination[1], 8);
    EXPECT_TRUE(source.Empty());
}

TEST(SmallVectorTest, RangeForAndSpan_CoverLiveElementsAcrossSpill) {
    SmallVector<int, 2> vector;
    vector.PushBack(1);
    vector.PushBack(2);
    vector.PushBack(3);

    int sum = 0;
    for (const int value : vector) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);

    const std::span<const int> view = vector.AsSpan();
    EXPECT_EQ(view.size(), 3U);
    EXPECT_EQ(view[2], 3);
}

TEST(SmallVectorTest, ElementLifetime_SpillDestroysInlineOriginalsExactlyOnce) {
    LifetimeProbe::Reset();
    {
        SmallVector<LifetimeProbe, 2> vector;
        vector.PushBack(LifetimeProbe(1));
        vector.PushBack(LifetimeProbe(2));
        // Each push: 1 temporary + 1 container move = 2 constructed, 1 temporary destroyed.
        EXPECT_EQ(LifetimeProbe::constructed, 4);
        EXPECT_EQ(LifetimeProbe::destroyed, 2);

        vector.PushBack(LifetimeProbe(3));
        // The temporary (1 constructed) plus the spill's 2 inline-to-heap moves plus the
        // temporary's move into its heap slot: 4 new constructions. Destroyed: the 2 moved-from
        // inline originals plus the temporary.
        EXPECT_EQ(LifetimeProbe::constructed, 8);
        EXPECT_EQ(LifetimeProbe::destroyed, 5);
    }
    EXPECT_EQ(LifetimeProbe::constructed, LifetimeProbe::destroyed);
}

TEST(SmallVectorTest, InitializerList_BuildsInlineWhenItFits) {
    const SmallVector<int, 4U> values{1, 2, 3};
    EXPECT_EQ(values.Size(), 3U);
    EXPECT_EQ(values.Capacity(), 4U);
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 3);
}

TEST(SmallVectorTest, InitializerList_SpillsPastInlineCapacity) {
    const SmallVector<int, 2U> values{1, 2, 3, 4, 5};
    EXPECT_EQ(values.Size(), 5U);
    EXPECT_GT(values.Capacity(), 2U);
    for (int index = 0; index < 5; ++index) {
        EXPECT_EQ(values[static_cast<std::size_t>(index)], index + 1);
    }
}

}  // namespace
