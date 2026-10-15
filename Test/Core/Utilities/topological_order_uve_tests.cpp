// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/utilities/topological_order_uve.h"
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Utilities::Tests {
TEST(TopologicalOrderUVETest, StableReadyOrderAndDuplicateEdges) {
    const std::vector<std::vector<std::uint32_t>> edges{{2U, 2U}, {}, {}};
    std::vector<std::uint32_t> order;
    ASSERT_EQ(BuildTopologicalOrderUVE(edges, order), TopologicalOrderCodeUVE::Ordered);
    EXPECT_EQ(order, (std::vector<std::uint32_t>{1U, 2U, 0U}));
}
TEST(TopologicalOrderUVETest, EmptyGraphIsValid) {
    std::vector<std::uint32_t> order{42U};
    EXPECT_EQ(BuildTopologicalOrderUVE({}, order), TopologicalOrderCodeUVE::Ordered);
    EXPECT_TRUE(order.empty());
}
TEST(TopologicalOrderUVETest, UnknownDependencyAndCyclesPreserveOutput) {
    std::vector<std::uint32_t> order{42U};
    const std::vector<std::vector<std::uint32_t>> unknown{{1U}}, self{{0U}}, cycle{{1U}, {0U}};
    EXPECT_EQ(BuildTopologicalOrderUVE(unknown, order), TopologicalOrderCodeUVE::UnknownDependency);
    EXPECT_EQ(BuildTopologicalOrderUVE(self, order), TopologicalOrderCodeUVE::Cycle);
    EXPECT_EQ(BuildTopologicalOrderUVE(cycle, order), TopologicalOrderCodeUVE::Cycle);
    EXPECT_EQ(order, (std::vector<std::uint32_t>{42U}));
}
} // namespace UVE::Utilities::Tests
