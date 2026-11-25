// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "job_graph.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "thread_pool.h"

namespace UVE::Threading::Tests {
namespace {

TEST(JobGraphTest, EmptyGraph_ExecuteAndWaitReturnImmediately) {
    ThreadPool pool(2);
    JobGraph graph;
    graph.Execute(pool);
    graph.Wait();
}

TEST(JobGraphTest, WaitBeforeExecute_ReturnsImmediately) {
    JobGraph graph;
    graph.Wait(); // never executed - must not hang
}

TEST(JobGraphTest, IndependentJobs_AllRunExactlyOnce) {
    ThreadPool pool(4);
    JobGraph graph;
    std::atomic<int> completed{0};
    constexpr int kJobCount = 64;
    for (int i = 0; i < kJobCount; ++i) {
        ASSERT_NE(graph.AddJob([&completed] { completed.fetch_add(1, std::memory_order_relaxed); }),
                   kInvalidJobGraphObjectHandle);
    }

    graph.Execute(pool);
    graph.Wait();

    EXPECT_EQ(completed.load(), kJobCount);
}

TEST(JobGraphTest, LinearChain_RunsInDeclaredOrder) {
    ThreadPool pool(4);
    JobGraph graph;
    std::mutex orderMutex;
    std::vector<int> order;

    const JobGraphObjectHandle first = graph.AddJob([&] {
        const std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(1);
    });
    const JobGraphObjectHandle second = graph.AddJob([&] {
        const std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(2);
    });
    const JobGraphObjectHandle third = graph.AddJob([&] {
        const std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(3);
    });
    ASSERT_TRUE(graph.AddDependency(second, first));
    ASSERT_TRUE(graph.AddDependency(third, second));

    graph.Execute(pool);
    graph.Wait();

    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(JobGraphTest, FanInJoin_DependentRunsOnlyAfterBothPredecessorsFinish) {
    ThreadPool pool(4);
    JobGraph graph;
    std::atomic<int> predecessorsFinished{0};
    std::atomic<bool> joinSawBothPredecessorsFinished{false};

    const JobGraphObjectHandle predecessorA = graph.AddJob([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        predecessorsFinished.fetch_add(1, std::memory_order_release);
    });
    const JobGraphObjectHandle predecessorB = graph.AddJob([&] {
        predecessorsFinished.fetch_add(1, std::memory_order_release);
    });
    const JobGraphObjectHandle join = graph.AddJob([&] {
        joinSawBothPredecessorsFinished.store(predecessorsFinished.load(std::memory_order_acquire) == 2,
                                               std::memory_order_relaxed);
    });
    ASSERT_TRUE(graph.AddDependency(join, predecessorA));
    ASSERT_TRUE(graph.AddDependency(join, predecessorB));

    graph.Execute(pool);
    graph.Wait();

    EXPECT_TRUE(joinSawBothPredecessorsFinished.load());
}

TEST(JobGraphTest, FanOutSplit_BothDependentsRunAfterOnePredecessor) {
    ThreadPool pool(4);
    JobGraph graph;
    std::atomic<int> predecessorRunCount{0};
    std::atomic<int> dependentARunCount{0};
    std::atomic<int> dependentBRunCount{0};

    const JobGraphObjectHandle predecessor =
        graph.AddJob([&] { predecessorRunCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandle dependentA =
        graph.AddJob([&] { dependentARunCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandle dependentB =
        graph.AddJob([&] { dependentBRunCount.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(graph.AddDependency(dependentA, predecessor));
    ASSERT_TRUE(graph.AddDependency(dependentB, predecessor));

    graph.Execute(pool);
    graph.Wait();

    EXPECT_EQ(predecessorRunCount.load(), 1);
    EXPECT_EQ(dependentARunCount.load(), 1);
    EXPECT_EQ(dependentBRunCount.load(), 1);
}

TEST(JobGraphTest, DiamondGraph_AllFourObjectsRunExactlyOnce) {
    // top -> {left, right} -> bottom (bottom depends on both left and right).
    ThreadPool pool(4);
    JobGraph graph;
    std::atomic<int> runCount{0};

    const JobGraphObjectHandle top = graph.AddJob([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandle left = graph.AddJob([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandle right = graph.AddJob([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandle bottom = graph.AddJob([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(graph.AddDependency(left, top));
    ASSERT_TRUE(graph.AddDependency(right, top));
    ASSERT_TRUE(graph.AddDependency(bottom, left));
    ASSERT_TRUE(graph.AddDependency(bottom, right));

    graph.Execute(pool);
    graph.Wait();

    EXPECT_EQ(runCount.load(), 4);
}

TEST(JobGraphTest, AddDependency_DirectCycleIsRejected) {
    JobGraph graph;
    const JobGraphObjectHandle a = graph.AddJob([] {});
    const JobGraphObjectHandle b = graph.AddJob([] {});
    ASSERT_TRUE(graph.AddDependency(b, a)); // b depends on a
    EXPECT_FALSE(graph.AddDependency(a, b)); // a depends on b would cycle back to a
}

TEST(JobGraphTest, AddDependency_IndirectCycleIsRejected) {
    JobGraph graph;
    const JobGraphObjectHandle a = graph.AddJob([] {});
    const JobGraphObjectHandle b = graph.AddJob([] {});
    const JobGraphObjectHandle c = graph.AddJob([] {});
    ASSERT_TRUE(graph.AddDependency(b, a)); // b depends on a
    ASSERT_TRUE(graph.AddDependency(c, b)); // c depends on b
    EXPECT_FALSE(graph.AddDependency(a, c)); // a depends on c would close the a->b->c->a cycle
}

TEST(JobGraphTest, AddDependency_SelfDependencyIsRejected) {
    JobGraph graph;
    const JobGraphObjectHandle a = graph.AddJob([] {});
    EXPECT_FALSE(graph.AddDependency(a, a));
}

TEST(JobGraphTest, AddDependency_InvalidHandleIsRejected) {
    JobGraph graph;
    const JobGraphObjectHandle a = graph.AddJob([] {});
    EXPECT_FALSE(graph.AddDependency(a, kInvalidJobGraphObjectHandle));
    EXPECT_FALSE(graph.AddDependency(kInvalidJobGraphObjectHandle, a));
}

TEST(JobGraphTest, AddJob_AfterExecuteIsRejected) {
    ThreadPool pool(2);
    JobGraph graph;
    static_cast<void>(graph.AddJob([] {}));
    graph.Execute(pool);
    EXPECT_EQ(graph.AddJob([] {}), kInvalidJobGraphObjectHandle);
    graph.Wait();
}

TEST(JobGraphTest, AddDependency_AfterExecuteIsRejected) {
    ThreadPool pool(2);
    JobGraph graph;
    const JobGraphObjectHandle a = graph.AddJob([] {});
    const JobGraphObjectHandle b = graph.AddJob([] {});
    graph.Execute(pool);
    EXPECT_FALSE(graph.AddDependency(b, a));
    graph.Wait();
}

TEST(JobGraphTest, Execute_CalledTwiceIsANoOpSecondTime) {
    ThreadPool pool(2);
    JobGraph graph;
    std::atomic<int> runCount{0};
    static_cast<void>(graph.AddJob([&] { runCount.fetch_add(1, std::memory_order_relaxed); }));

    graph.Execute(pool);
    graph.Wait();
    EXPECT_EQ(runCount.load(), 1);

    graph.Execute(pool); // ignored - already executed
    graph.Wait();
    EXPECT_EQ(runCount.load(), 1);
}

TEST(JobGraphTest, LargeWideAndDeepGraph_StressCompletesWithoutHangOrDoubleRun) {
    // 8 independent chains of 32 sequential jobs each, all sharing one pool - exercises the
    // work-stealing path alongside the cascade-on-completion submission logic.
    ThreadPool pool(4);
    JobGraph graph;
    constexpr int kChainCount = 8;
    constexpr int kChainLength = 32;
    std::atomic<int> totalRunCount{0};
    std::vector<std::vector<int>> chainOrder(kChainCount);
    std::vector<std::mutex> chainMutexes(kChainCount);

    for (int chain = 0; chain < kChainCount; ++chain) {
        JobGraphObjectHandle previous = kInvalidJobGraphObjectHandle;
        for (int step = 0; step < kChainLength; ++step) {
            const JobGraphObjectHandle current = graph.AddJob([&totalRunCount, &chainOrder, &chainMutexes, chain, step] {
                totalRunCount.fetch_add(1, std::memory_order_relaxed);
                const std::lock_guard<std::mutex> lock(chainMutexes[static_cast<std::size_t>(chain)]);
                chainOrder[static_cast<std::size_t>(chain)].push_back(step);
            });
            if (previous != kInvalidJobGraphObjectHandle) {
                ASSERT_TRUE(graph.AddDependency(current, previous));
            }
            previous = current;
        }
    }

    graph.Execute(pool);
    graph.Wait();

    EXPECT_EQ(totalRunCount.load(), kChainCount * kChainLength);
    for (int chain = 0; chain < kChainCount; ++chain) {
        const std::vector<int>& order = chainOrder[static_cast<std::size_t>(chain)];
        ASSERT_EQ(order.size(), static_cast<std::size_t>(kChainLength));
        for (int step = 0; step < kChainLength; ++step) {
            EXPECT_EQ(order[static_cast<std::size_t>(step)], step);
        }
    }
}

TEST(JobGraphTest, ThrowingJobDoesNotDeadlockWaitAndUnblocksSuccessors) {
    ThreadPool pool(4);
    JobGraph graph;
    std::atomic<bool> successorRan{false};

    const JobGraphObjectHandle failing = graph.AddJob([] {
        throw std::runtime_error("Simulated job failure");
    });
    const JobGraphObjectHandle successor = graph.AddJob([&successorRan] {
        successorRan.store(true, std::memory_order_release);
    });

    ASSERT_TRUE(graph.AddDependency(successor, failing));

    graph.Execute(pool);
    graph.Wait(); // Must complete without deadlocking

    EXPECT_TRUE(successorRan.load(std::memory_order_acquire));
}

} // namespace
} // namespace UVE::Threading::Tests
