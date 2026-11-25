// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "sparse_set.h"

namespace UVE::Containers::Tests {
namespace {

TEST(SparseSetTest, InsertContainsGet_RoundTrip) {
    SparseSet<std::string> set;
    EXPECT_TRUE(set.Empty());
    EXPECT_TRUE(set.Insert(3U, "three"));
    EXPECT_TRUE(set.Insert(7U, "seven"));
    EXPECT_FALSE(set.Insert(3U, "duplicate"));
    EXPECT_EQ(set.Size(), 2U);
    EXPECT_TRUE(set.Contains(3U));
    EXPECT_FALSE(set.Contains(4U));
    EXPECT_EQ(set.Get(3U), "three");
    EXPECT_EQ(set.Get(7U), "seven");
}

TEST(SparseSetTest, Remove_SwapCompactsAndKeepsOthersReachable) {
    SparseSet<int> set;
    set.Insert(1U, 10);
    set.Insert(2U, 20);
    set.Insert(3U, 30);
    EXPECT_TRUE(set.Remove(2U));
    EXPECT_FALSE(set.Remove(2U));
    EXPECT_EQ(set.Size(), 2U);
    EXPECT_FALSE(set.Contains(2U));
    EXPECT_EQ(set.Get(1U), 10);
    EXPECT_EQ(set.Get(3U), 30);
    // Reinsertion after removal reuses the id cleanly.
    EXPECT_TRUE(set.Insert(2U, 200));
    EXPECT_EQ(set.Get(2U), 200);
}

TEST(SparseSetTest, Iteration_CoversEveryLiveValueExactlyOnce) {
    SparseSet<int> set;
    for (std::uint32_t id = 0U; id < 50U; ++id) {
        set.Insert(id * 2U, static_cast<int>(id));
    }
    set.Remove(10U);
    set.Remove(20U);
    int sum = 0;
    std::size_t count = 0U;
    for (const int value : set) {
        sum += value;
        ++count;
    }
    EXPECT_EQ(count, 48U);
    EXPECT_EQ(sum, (49 * 50 / 2) - 5 - 10); // 0..49 minus the removed 5 and 10.
    // Ids run parallel to iteration: Ids()[i] owns the i-th value.
    ASSERT_EQ(set.Ids().size(), 48U);
    std::size_t index = 0U;
    for (const int value : set) {
        EXPECT_EQ(set.Get(set.Ids()[index]), value);
        ++index;
    }
}

TEST(SparseSetTest, Clear_EmptiesButKeepsTheSpine) {
    SparseSet<int> set;
    set.Insert(1000U, 1);
    set.Clear();
    EXPECT_TRUE(set.Empty());
    EXPECT_FALSE(set.Contains(1000U));
    EXPECT_TRUE(set.Insert(1000U, 2));
    EXPECT_EQ(set.Get(1000U), 2);
}

} // namespace
} // namespace UVE::Containers::Tests
