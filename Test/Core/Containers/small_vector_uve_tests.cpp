// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "uve/containers/small_vector_uve.h"

namespace {

using UVE::Containers::SmallVectorUVE;

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

TEST(SmallVectorUVETest, DefaultConstructed_IsEmptyWithInlineCapacity) {
    const SmallVectorUVE<int, 4> vector;
    EXPECT_EQ(vector.SizeUVE(), 0U);
    EXPECT_EQ(vector.CapacityUVE(), 4U);
    EXPECT_TRUE(vector.EmptyUVE());
}

TEST(SmallVectorUVETest, PushWithinInlineCapacity_NeverSpills) {
    SmallVectorUVE<int, 4> vector;
    vector.PushBackUVE(1);
    vector.PushBackUVE(2);
    vector.PushBackUVE(3);
    vector.PushBackUVE(4);

    EXPECT_EQ(vector.SizeUVE(), 4U);
    EXPECT_EQ(vector.CapacityUVE(), 4U);
    EXPECT_EQ(vector[0], 1);
    EXPECT_EQ(vector.BackUVE(), 4);
}

TEST(SmallVectorUVETest, PushPastInlineCapacity_SpillsWithDoubledCapacity) {
    SmallVectorUVE<int, 4> vector;
    for (int value = 1; value <= 5; ++value) {
        vector.PushBackUVE(value);
    }

    EXPECT_EQ(vector.SizeUVE(), 5U);
    EXPECT_EQ(vector.CapacityUVE(), 8U);
    for (int index = 0; index < 5; ++index) {
        EXPECT_EQ(vector[static_cast<std::size_t>(index)], index + 1);
    }
}

TEST(SmallVectorUVETest, RepeatedGrowth_PreservesAllValuesInOrder) {
    SmallVectorUVE<int, 2> vector;
    for (int value = 0; value < 20; ++value) {
        vector.PushBackUVE(value * 3);
    }

    EXPECT_EQ(vector.SizeUVE(), 20U);
    for (int index = 0; index < 20; ++index) {
        EXPECT_EQ(vector[static_cast<std::size_t>(index)], index * 3);
    }
}

TEST(SmallVectorUVETest, Clear_EmptiesAndStaysReusableAfterSpill) {
    SmallVectorUVE<int, 2> vector;
    vector.PushBackUVE(1);
    vector.PushBackUVE(2);
    vector.PushBackUVE(3);
    vector.ClearUVE();

    EXPECT_TRUE(vector.EmptyUVE());
    EXPECT_EQ(vector.SizeUVE(), 0U);

    vector.PushBackUVE(9);
    EXPECT_EQ(vector.SizeUVE(), 1U);
    EXPECT_EQ(vector[0], 9);
}

TEST(SmallVectorUVETest, CopyConstruct_PreservesInlineOrSpilledContents) {
    SmallVectorUVE<std::string, 4> inlineSource;
    inlineSource.PushBackUVE("x");
    const SmallVectorUVE<std::string, 4> inlineCopy(inlineSource);
    EXPECT_EQ(inlineCopy.SizeUVE(), 1U);
    EXPECT_EQ(inlineCopy.CapacityUVE(), 4U);
    EXPECT_EQ(inlineCopy[0], "x");

    SmallVectorUVE<int, 2> spilledSource;
    for (int value = 0; value < 6; ++value) {
        spilledSource.PushBackUVE(value);
    }
    const SmallVectorUVE<int, 2> spilledCopy(spilledSource);
    EXPECT_EQ(spilledCopy.SizeUVE(), 6U);
    for (int index = 0; index < 6; ++index) {
        EXPECT_EQ(spilledCopy[static_cast<std::size_t>(index)], index);
    }
    // The source keeps its own buffer — mutating the copy must not alias it.
    EXPECT_EQ(spilledSource[0], 0);
}

TEST(SmallVectorUVETest, MoveConstruct_SpilledSourceStealsTheBuffer) {
    SmallVectorUVE<int, 2> source;
    for (int value = 0; value < 6; ++value) {
        source.PushBackUVE(value * 2);
    }
    const std::size_t spilledCapacity = source.CapacityUVE();
    EXPECT_GT(spilledCapacity, 2U);

    const SmallVectorUVE<int, 2> moved(std::move(source));
    EXPECT_EQ(moved.SizeUVE(), 6U);
    EXPECT_EQ(moved.CapacityUVE(), spilledCapacity);
    EXPECT_EQ(moved[5], 10);
    EXPECT_TRUE(source.EmptyUVE());
    EXPECT_EQ(source.CapacityUVE(), 2U);
}

TEST(SmallVectorUVETest, MoveAssign_InlineSourceMovesElements) {
    SmallVectorUVE<int, 4> source;
    source.PushBackUVE(7);
    source.PushBackUVE(8);
    SmallVectorUVE<int, 4> destination;
    destination.PushBackUVE(1);

    destination = std::move(source);
    EXPECT_EQ(destination.SizeUVE(), 2U);
    EXPECT_EQ(destination[0], 7);
    EXPECT_EQ(destination[1], 8);
    EXPECT_TRUE(source.EmptyUVE());
}

TEST(SmallVectorUVETest, RangeForAndSpan_CoverLiveElementsAcrossSpill) {
    SmallVectorUVE<int, 2> vector;
    vector.PushBackUVE(1);
    vector.PushBackUVE(2);
    vector.PushBackUVE(3);

    int sum = 0;
    for (const int value : vector) {
        sum += value;
    }
    EXPECT_EQ(sum, 6);

    const std::span<const int> view = vector.AsSpanUVE();
    EXPECT_EQ(view.size(), 3U);
    EXPECT_EQ(view[2], 3);
}

TEST(SmallVectorUVETest, ElementLifetime_SpillDestroysInlineOriginalsExactlyOnce) {
    LifetimeProbeUVE::ResetUVE();
    {
        SmallVectorUVE<LifetimeProbeUVE, 2> vector;
        vector.PushBackUVE(LifetimeProbeUVE(1));
        vector.PushBackUVE(LifetimeProbeUVE(2));
        // Each push: 1 temporary + 1 container move = 2 constructed, 1 temporary destroyed.
        EXPECT_EQ(LifetimeProbeUVE::constructed, 4);
        EXPECT_EQ(LifetimeProbeUVE::destroyed, 2);

        vector.PushBackUVE(LifetimeProbeUVE(3));
        // The temporary (1 constructed) plus the spill's 2 inline-to-heap moves plus the
        // temporary's move into its heap slot: 4 new constructions. Destroyed: the 2 moved-from
        // inline originals plus the temporary.
        EXPECT_EQ(LifetimeProbeUVE::constructed, 8);
        EXPECT_EQ(LifetimeProbeUVE::destroyed, 5);
    }
    EXPECT_EQ(LifetimeProbeUVE::constructed, LifetimeProbeUVE::destroyed);
}

TEST(SmallVectorUVETest, InitializerList_BuildsInlineWhenItFits) {
    const SmallVectorUVE<int, 4U> values{1, 2, 3};
    EXPECT_EQ(values.SizeUVE(), 3U);
    EXPECT_EQ(values.CapacityUVE(), 4U);
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 3);
}

TEST(SmallVectorUVETest, InitializerList_SpillsPastInlineCapacity) {
    const SmallVectorUVE<int, 2U> values{1, 2, 3, 4, 5};
    EXPECT_EQ(values.SizeUVE(), 5U);
    EXPECT_GT(values.CapacityUVE(), 2U);
    for (int index = 0; index < 5; ++index) {
        EXPECT_EQ(values[static_cast<std::size_t>(index)], index + 1);
    }
}

}  // namespace
