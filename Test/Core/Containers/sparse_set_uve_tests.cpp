// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/containers/sparse_set_uve.h"

namespace UVE::Containers::Tests {
namespace {

TEST(SparseSetUVETest, InsertContainsGet_RoundTrip) {
    SparseSetUVE<std::string> set;
    EXPECT_TRUE(set.EmptyUVE());
    EXPECT_TRUE(set.InsertUVE(3U, "three"));
    EXPECT_TRUE(set.InsertUVE(7U, "seven"));
    EXPECT_FALSE(set.InsertUVE(3U, "duplicate"));
    EXPECT_EQ(set.SizeUVE(), 2U);
    EXPECT_TRUE(set.ContainsUVE(3U));
    EXPECT_FALSE(set.ContainsUVE(4U));
    EXPECT_EQ(set.GetUVE(3U), "three");
    EXPECT_EQ(set.GetUVE(7U), "seven");
}

TEST(SparseSetUVETest, Remove_SwapCompactsAndKeepsOthersReachable) {
    SparseSetUVE<int> set;
    set.InsertUVE(1U, 10);
    set.InsertUVE(2U, 20);
    set.InsertUVE(3U, 30);
    EXPECT_TRUE(set.RemoveUVE(2U));
    EXPECT_FALSE(set.RemoveUVE(2U));
    EXPECT_EQ(set.SizeUVE(), 2U);
    EXPECT_FALSE(set.ContainsUVE(2U));
    EXPECT_EQ(set.GetUVE(1U), 10);
    EXPECT_EQ(set.GetUVE(3U), 30);
    // Reinsertion after removal reuses the id cleanly.
    EXPECT_TRUE(set.InsertUVE(2U, 200));
    EXPECT_EQ(set.GetUVE(2U), 200);
}

TEST(SparseSetUVETest, Iteration_CoversEveryLiveValueExactlyOnce) {
    SparseSetUVE<int> set;
    for (std::uint32_t id = 0U; id < 50U; ++id) {
        set.InsertUVE(id * 2U, static_cast<int>(id));
    }
    set.RemoveUVE(10U);
    set.RemoveUVE(20U);
    int sum = 0;
    std::size_t count = 0U;
    for (const int value : set) {
        sum += value;
        ++count;
    }
    EXPECT_EQ(count, 48U);
    EXPECT_EQ(sum, (49 * 50 / 2) - 5 - 10); // 0..49 minus the removed 5 and 10.
    // Ids run parallel to iteration: IdsUVE()[i] owns the i-th value.
    ASSERT_EQ(set.IdsUVE().size(), 48U);
    std::size_t index = 0U;
    for (const int value : set) {
        EXPECT_EQ(set.GetUVE(set.IdsUVE()[index]), value);
        ++index;
    }
}

TEST(SparseSetUVETest, Clear_EmptiesButKeepsTheSpine) {
    SparseSetUVE<int> set;
    set.InsertUVE(1000U, 1);
    set.ClearUVE();
    EXPECT_TRUE(set.EmptyUVE());
    EXPECT_FALSE(set.ContainsUVE(1000U));
    EXPECT_TRUE(set.InsertUVE(1000U, 2));
    EXPECT_EQ(set.GetUVE(1000U), 2);
}

} // namespace
} // namespace UVE::Containers::Tests
