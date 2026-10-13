// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/containers/ring_buffer_uve.h"

namespace UVE::Containers::Tests {
namespace {

TEST(RingBufferUVETest, FifoOrder_PushBackPopFront) {
    RingBufferUVE<int, 4U> ring;
    EXPECT_TRUE(ring.EmptyUVE());
    ring.PushBackUVE(1);
    ring.PushBackUVE(2);
    ring.PushBackUVE(3);
    EXPECT_EQ(ring.SizeUVE(), 3U);
    EXPECT_EQ(ring.FrontUVE(), 1);
    EXPECT_EQ(ring.BackUVE(), 3);
    ring.PopFrontUVE();
    EXPECT_EQ(ring.FrontUVE(), 2);
    EXPECT_EQ(ring.SizeUVE(), 2U);
}

TEST(RingBufferUVETest, Overflow_OverwritesOldestAndCountsDrops) {
    RingBufferUVE<int, 3U> ring;
    ring.PushBackUVE(1);
    ring.PushBackUVE(2);
    ring.PushBackUVE(3);
    EXPECT_TRUE(ring.FullUVE());
    EXPECT_EQ(ring.DroppedCountUVE(), 0U);
    ring.PushBackUVE(4);
    ring.PushBackUVE(5);
    EXPECT_EQ(ring.SizeUVE(), 3U);
    EXPECT_EQ(ring.DroppedCountUVE(), 2U);
    EXPECT_EQ(ring.FrontUVE(), 3);
    EXPECT_EQ(ring.BackUVE(), 5);
    EXPECT_EQ(ring.AtUVE(0U), 3);
    EXPECT_EQ(ring.AtUVE(1U), 4);
    EXPECT_EQ(ring.AtUVE(2U), 5);
}

TEST(RingBufferUVETest, Iteration_WalksOldestToNewestAcrossTheWrap) {
    RingBufferUVE<std::string, 4U> ring;
    ring.PushBackUVE("a");
    ring.PushBackUVE("b");
    ring.PopFrontUVE();
    ring.PushBackUVE("c");
    ring.PushBackUVE("d");
    ring.PushBackUVE("e"); // Wraps: live are b, c, d, e.
    std::vector<std::string> seen;
    for (const std::string& value : ring) {
        seen.push_back(value);
    }
    ASSERT_EQ(seen.size(), 4U);
    EXPECT_EQ(seen[0U], "b");
    EXPECT_EQ(seen[1U], "c");
    EXPECT_EQ(seen[2U], "d");
    EXPECT_EQ(seen[3U], "e");
}

TEST(RingBufferUVETest, Clear_EmptiesAndZeroesDrops) {
    RingBufferUVE<int, 2U> ring;
    ring.PushBackUVE(1);
    ring.PushBackUVE(2);
    ring.PushBackUVE(3);
    ring.ClearUVE();
    EXPECT_TRUE(ring.EmptyUVE());
    EXPECT_EQ(ring.DroppedCountUVE(), 0U);
    ring.PushBackUVE(4);
    EXPECT_EQ(ring.FrontUVE(), 4);
}

} // namespace
} // namespace UVE::Containers::Tests
