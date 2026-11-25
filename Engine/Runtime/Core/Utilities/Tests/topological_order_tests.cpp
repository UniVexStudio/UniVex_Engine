// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "topological_order.h"
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Utilities::Tests {
TEST(TopologicalOrderTest, StableReadyOrderAndDuplicateEdges) {
    const std::vector<std::vector<std::uint32_t>> edges{{2U, 2U}, {}, {}};
    std::vector<std::uint32_t> order;
    ASSERT_EQ(BuildTopologicalOrder(edges, order), TopologicalOrderCode::Ordered);
    EXPECT_EQ(order, (std::vector<std::uint32_t>{1U, 2U, 0U}));
}
TEST(TopologicalOrderTest, EmptyGraphIsValid) {
    std::vector<std::uint32_t> order{42U};
    EXPECT_EQ(BuildTopologicalOrder({}, order), TopologicalOrderCode::Ordered);
    EXPECT_TRUE(order.empty());
}
TEST(TopologicalOrderTest, UnknownDependencyAndCyclesPreserveOutput) {
    std::vector<std::uint32_t> order{42U};
    const std::vector<std::vector<std::uint32_t>> unknown{{1U}}, self{{0U}}, cycle{{1U}, {0U}};
    EXPECT_EQ(BuildTopologicalOrder(unknown, order), TopologicalOrderCode::UnknownDependency);
    EXPECT_EQ(BuildTopologicalOrder(self, order), TopologicalOrderCode::Cycle);
    EXPECT_EQ(BuildTopologicalOrder(cycle, order), TopologicalOrderCode::Cycle);
    EXPECT_EQ(order, (std::vector<std::uint32_t>{42U}));
}
} // namespace UVE::Utilities::Tests
