// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "frame_scheduler.h"

#include "thread_pool.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <utility>

namespace UVE::Core {
namespace {

FrameTaskDefinition MakeTask(FrameTaskId id, const char* name,
                                  std::vector<FrameTaskId> dependencies,
                                  std::function<void()> action) {
    Containers::SmallVector<FrameTaskId, FrameTaskGraph::kMaximumDependenciesPerTask> deps;
    for (const FrameTaskId dependency : dependencies) {
        deps.PushBack(dependency);
    }
    return FrameTaskDefinition{id, FrameTaskDomain::Animation, name, std::move(deps),
                                 std::move(action)};
}

} // namespace

TEST(FrameTaskGraphTest, AddTask_RejectsDuplicateEdgesAndAcceptsCrossDomainTasks) {
    FrameTaskGraph graph;
    EXPECT_TRUE(graph.AddTask(MakeTask(1U, "animation", {}, [] {})).IsAccepted());
    EXPECT_TRUE(graph.AddTask(FrameTaskDefinition{
        2U, FrameTaskDomain::RenderPreparation, "render", {1U}, [] {}})
                    .IsAccepted());

    const FrameTaskGraphMutationResult duplicate = graph.AddTask(
        MakeTask(3U, "duplicate-edge", {1U, 1U}, [] {}));
    EXPECT_EQ(duplicate.code, FrameTaskGraphMutationCode::DuplicateDependency);
    EXPECT_EQ(graph.GetTaskCount(), 2U);
    EXPECT_TRUE(graph.Validate().IsValid());
}

TEST(FrameTaskGraphTest, Validate_RejectsUnknownAndCyclicDependencies) {
    FrameTaskGraph unknown;
    ASSERT_TRUE(unknown.AddTask(MakeTask(1U, "consumer", {99U}, [] {})).IsAccepted());
    EXPECT_EQ(unknown.Validate().code, FrameTaskGraphValidationCode::UnknownDependency);

    FrameTaskGraph cyclic;
    ASSERT_TRUE(cyclic.AddTask(MakeTask(1U, "first", {2U}, [] {})).IsAccepted());
    ASSERT_TRUE(cyclic.AddTask(MakeTask(2U, "second", {1U}, [] {})).IsAccepted());
    EXPECT_EQ(cyclic.Validate().code, FrameTaskGraphValidationCode::CyclicDependency);
}

TEST(FrameSchedulerTest, Execute_RunsDependenciesBeforeDependentsAcrossDomains) {
    FrameTaskGraph graph;
    std::mutex orderMutex;
    std::vector<FrameTaskId> order;
    const auto record = [&orderMutex, &order](FrameTaskId id) {
        const std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(id);
    };

    ASSERT_TRUE(graph.AddTask(FrameTaskDefinition{
        1U, FrameTaskDomain::Animation, "animation", {}, [&record] { record(1U); }})
                    .IsAccepted());
    ASSERT_TRUE(graph.AddTask(FrameTaskDefinition{
        2U, FrameTaskDomain::Physics, "physics", {1U}, [&record] { record(2U); }})
                    .IsAccepted());
    ASSERT_TRUE(graph.AddTask(FrameTaskDefinition{
        3U, FrameTaskDomain::RenderPreparation, "render", {1U, 2U},
        [&record] { record(3U); }})
                    .IsAccepted());

    Threading::ThreadPool pool(2U);
    const FrameScheduleResult result = FrameScheduler(pool).Execute(graph);

    ASSERT_TRUE(result.IsCompleted());
    EXPECT_EQ(result.completedTaskCount, 3U);
    EXPECT_EQ(result.taskCount, 3U);
    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], 1U);
    EXPECT_EQ(order[1], 2U);
    EXPECT_EQ(order[2], 3U);
}

TEST(FrameSchedulerTest, Execute_ContainsTaskFailureAndDoesNotRunDependents) {
    FrameTaskGraph graph;
    bool dependentRan = false;
    ASSERT_TRUE(graph.AddTask(MakeTask(1U, "failing", {}, [] {
        throw std::runtime_error("synthetic task failure");
    }))
                    .IsAccepted());
    ASSERT_TRUE(graph.AddTask(MakeTask(2U, "dependent", {1U}, [&dependentRan] {
        dependentRan = true;
    }))
                    .IsAccepted());

    Threading::ThreadPool pool(2U);
    const FrameScheduleResult result = FrameScheduler(pool).Execute(graph);

    EXPECT_EQ(result.code, FrameScheduleResultCode::TaskFailed);
    EXPECT_EQ(result.completedTaskCount, 0U);
    EXPECT_EQ(result.taskCount, 2U);
    EXPECT_FALSE(dependentRan);
    EXPECT_EQ(result.message, "synthetic task failure");
}

TEST(FrameSchedulerTest, Execute_RejectsEmptyGraphBeforeSubmittingWork) {
    FrameTaskGraph graph;
    Threading::ThreadPool pool(1U);

    const FrameScheduleResult result = FrameScheduler(pool).Execute(graph);

    EXPECT_EQ(result.code, FrameScheduleResultCode::InvalidGraph);
    EXPECT_EQ(result.completedTaskCount, 0U);
    EXPECT_EQ(result.taskCount, 0U);
}

TEST(FrameTaskGraphTest, AddTask_RejectsTaskBeyondCapacity) {
    FrameTaskGraph graph;
    for (std::size_t index = 0U; index < FrameTaskGraph::kMaximumTasks; ++index) {
        ASSERT_TRUE(graph.AddTask(MakeTask(static_cast<FrameTaskId>(index + 1U), "task", {}, [] {}))
                        .IsAccepted())
            << "task index " << index;
    }
    EXPECT_EQ(graph.GetTaskCount(), FrameTaskGraph::kMaximumTasks);

    const FrameTaskGraphMutationResult overflow =
        graph.AddTask(MakeTask(1000000U, "overflow", {}, [] {}));
    EXPECT_EQ(overflow.code, FrameTaskGraphMutationCode::CapacityExceeded);
    EXPECT_EQ(graph.GetTaskCount(), FrameTaskGraph::kMaximumTasks);
}

TEST(FrameTaskGraphTest, Dependencies_LiveInlineAndOverCapIsRejected) {
    FrameTaskGraph graph;
    std::vector<FrameTaskId> full;
    for (FrameTaskId id = 2U; id <= FrameTaskGraph::kMaximumDependenciesPerTask + 1U; ++id) {
        full.push_back(id);
    }
    ASSERT_TRUE(graph.AddTask(MakeTask(1U, "full", full, [] {})).IsAccepted());
    ASSERT_EQ(graph.GetTaskCount(), 1U);
    const FrameTaskDefinition& stored = graph.GetTasks()[0];
    EXPECT_EQ(stored.dependencies.Size(), FrameTaskGraph::kMaximumDependenciesPerTask);
    // Inline capacity equals the cap: a full-but-accepted list never touched the heap.
    EXPECT_EQ(stored.dependencies.Capacity(), FrameTaskGraph::kMaximumDependenciesPerTask);

    full.push_back(99U);
    const FrameTaskGraphMutationResult overflow =
        graph.AddTask(MakeTask(2U, "overflow", full, [] {}));
    EXPECT_EQ(overflow.code, FrameTaskGraphMutationCode::TooManyDependencies);
    EXPECT_EQ(graph.GetTaskCount(), 1U);
}

} // namespace UVE::Core
