// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "ring_buffer.h"

namespace UVE::Containers::Tests {
namespace {

TEST(RingBufferTest, FifoOrder_PushBackPopFront) {
    RingBuffer<int, 4U> ring;
    EXPECT_TRUE(ring.Empty());
    ring.PushBack(1);
    ring.PushBack(2);
    ring.PushBack(3);
    EXPECT_EQ(ring.Size(), 3U);
    EXPECT_EQ(ring.Front(), 1);
    EXPECT_EQ(ring.Back(), 3);
    ring.PopFront();
    EXPECT_EQ(ring.Front(), 2);
    EXPECT_EQ(ring.Size(), 2U);
}

TEST(RingBufferTest, Overflow_OverwritesOldestAndCountsDrops) {
    RingBuffer<int, 3U> ring;
    ring.PushBack(1);
    ring.PushBack(2);
    ring.PushBack(3);
    EXPECT_TRUE(ring.Full());
    EXPECT_EQ(ring.DroppedCount(), 0U);
    ring.PushBack(4);
    ring.PushBack(5);
    EXPECT_EQ(ring.Size(), 3U);
    EXPECT_EQ(ring.DroppedCount(), 2U);
    EXPECT_EQ(ring.Front(), 3);
    EXPECT_EQ(ring.Back(), 5);
    EXPECT_EQ(ring.At(0U), 3);
    EXPECT_EQ(ring.At(1U), 4);
    EXPECT_EQ(ring.At(2U), 5);
}

TEST(RingBufferTest, Iteration_WalksOldestToNewestAcrossTheWrap) {
    RingBuffer<std::string, 4U> ring;
    ring.PushBack("a");
    ring.PushBack("b");
    ring.PopFront();
    ring.PushBack("c");
    ring.PushBack("d");
    ring.PushBack("e"); // Wraps: live are b, c, d, e.
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

TEST(RingBufferTest, Clear_EmptiesAndZeroesDrops) {
    RingBuffer<int, 2U> ring;
    ring.PushBack(1);
    ring.PushBack(2);
    ring.PushBack(3);
    ring.Clear();
    EXPECT_TRUE(ring.Empty());
    EXPECT_EQ(ring.DroppedCount(), 0U);
    ring.PushBack(4);
    EXPECT_EQ(ring.Front(), 4);
}

} // namespace
} // namespace UVE::Containers::Tests
