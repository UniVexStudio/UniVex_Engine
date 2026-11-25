// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "frame_scheduler.h"
#include <stdexcept>
#include <vector>
#include <gtest/gtest.h>
namespace {
using namespace UVE::Core;
TEST(FrameSerialTest, StableTopologyUsesInsertionOrderAmongReadyTasks) {
    std::vector<int> calls;
    FrameTaskGraph graph;
    ASSERT_TRUE(graph.AddTask({9, FrameTaskDomain::ECS, "dependent", {7}, [&] { calls.push_back(9); }})
                    .IsAccepted());
    ASSERT_TRUE(graph.AddTask({2, FrameTaskDomain::ECS, "independent", {}, [&] { calls.push_back(2); }})
                    .IsAccepted());
    ASSERT_TRUE(
        graph.AddTask({7, FrameTaskDomain::ECS, "prerequisite", {}, [&] { calls.push_back(7); }})
            .IsAccepted());
    std::vector<FrameTaskId> order;
    ASSERT_TRUE(graph.BuildExecutionOrder(order).IsValid());
    EXPECT_TRUE(calls.empty());
    EXPECT_EQ(order, (std::vector<FrameTaskId>{2, 7, 9}));
    const auto result = FrameScheduler::ExecuteSerial(graph);
    EXPECT_TRUE(result.IsCompleted());
    EXPECT_EQ(result.completedTaskCount, 3U);
    EXPECT_EQ(calls, (std::vector<int>{2, 7, 9}));
}
TEST(FrameSerialTest, InvalidGraphPreservesPlanAndRunsNoActions) {
    int calls = 0;
    FrameTaskGraph graph;
    ASSERT_TRUE(
        graph.AddTask({1, FrameTaskDomain::ECS, "missing", {2}, [&] { ++calls; }}).IsAccepted());
    std::vector<FrameTaskId> output{42};
    EXPECT_FALSE(graph.BuildExecutionOrder(output).IsValid());
    EXPECT_EQ(output, (std::vector<FrameTaskId>{42}));
    EXPECT_EQ(FrameScheduler::ExecuteSerial(graph).code, FrameScheduleResultCode::InvalidGraph);
    EXPECT_EQ(calls, 0);
}
TEST(FrameSerialTest, TaskFailureStopsDependentAndSubsequentActions) {
    int calls = 0;
    FrameTaskGraph graph;
    ASSERT_TRUE(
        graph
            .AddTask(
                {1, FrameTaskDomain::ECS, "throws", {}, [] { throw std::runtime_error("failure"); }})
            .IsAccepted());
    ASSERT_TRUE(
        graph.AddTask({2, FrameTaskDomain::ECS, "later", {1}, [&] { ++calls; }}).IsAccepted());
    const auto result = FrameScheduler::ExecuteSerial(graph);
    EXPECT_EQ(result.code, FrameScheduleResultCode::TaskFailed);
    EXPECT_EQ(result.completedTaskCount, 0U);
    EXPECT_EQ(result.message, "failure");
    EXPECT_EQ(calls, 0);
}
} // namespace
