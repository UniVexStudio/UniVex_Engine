// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/scheduling/frame_scheduler_uve.h"
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>
namespace {
using namespace UVE::Core;
TEST(FrameSerialUVETest, StableTopologyUsesInsertionOrderAmongReadyTasks) {
    std::vector<int> calls;
    FrameTaskGraphUVE graph;
    ASSERT_TRUE(graph.AddTaskUVE({9, FrameTaskDomainUVE::ECS, "dependent", {7}, [&] { calls.push_back(9); }})
                    .IsAcceptedUVE());
    ASSERT_TRUE(graph.AddTaskUVE({2, FrameTaskDomainUVE::ECS, "independent", {}, [&] { calls.push_back(2); }})
                    .IsAcceptedUVE());
    ASSERT_TRUE(
        graph.AddTaskUVE({7, FrameTaskDomainUVE::ECS, "prerequisite", {}, [&] { calls.push_back(7); }})
            .IsAcceptedUVE());
    std::vector<FrameTaskIdUVE> order;
    ASSERT_TRUE(graph.BuildExecutionOrderUVE(order).IsValidUVE());
    EXPECT_TRUE(calls.empty());
    EXPECT_EQ(order, (std::vector<FrameTaskIdUVE>{2, 7, 9}));
    const auto result = FrameSchedulerUVE::ExecuteSerialUVE(graph);
    EXPECT_TRUE(result.IsCompletedUVE());
    EXPECT_EQ(result.completedTaskCount, 3U);
    EXPECT_EQ(calls, (std::vector<int>{2, 7, 9}));
}
TEST(FrameSerialUVETest, InvalidGraphPreservesPlanAndRunsNoActions) {
    int calls = 0;
    FrameTaskGraphUVE graph;
    ASSERT_TRUE(
        graph.AddTaskUVE({1, FrameTaskDomainUVE::ECS, "missing", {2}, [&] { ++calls; }}).IsAcceptedUVE());
    std::vector<FrameTaskIdUVE> output{42};
    EXPECT_FALSE(graph.BuildExecutionOrderUVE(output).IsValidUVE());
    EXPECT_EQ(output, (std::vector<FrameTaskIdUVE>{42}));
    EXPECT_EQ(FrameSchedulerUVE::ExecuteSerialUVE(graph).code, FrameScheduleResultCodeUVE::InvalidGraph);
    EXPECT_EQ(calls, 0);
}
TEST(FrameSerialUVETest, TaskFailureStopsDependentAndSubsequentActions) {
    int calls = 0;
    FrameTaskGraphUVE graph;
    ASSERT_TRUE(
        graph
            .AddTaskUVE(
                {1, FrameTaskDomainUVE::ECS, "throws", {}, [] { throw std::runtime_error("failure"); }})
            .IsAcceptedUVE());
    ASSERT_TRUE(
        graph.AddTaskUVE({2, FrameTaskDomainUVE::ECS, "later", {1}, [&] { ++calls; }}).IsAcceptedUVE());
    const auto result = FrameSchedulerUVE::ExecuteSerialUVE(graph);
    EXPECT_EQ(result.code, FrameScheduleResultCodeUVE::TaskFailed);
    EXPECT_EQ(result.completedTaskCount, 0U);
    EXPECT_EQ(result.message, "failure");
    EXPECT_EQ(calls, 0);
}
} // namespace
