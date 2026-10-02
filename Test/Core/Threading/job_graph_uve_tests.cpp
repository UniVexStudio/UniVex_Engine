// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/threading/job_graph_uve.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/threading/thread_pool_uve.h"

namespace UVE::Threading::Tests {
namespace {

TEST(JobGraphUVETest, EmptyGraph_ExecuteAndWaitReturnImmediately) {
    ThreadPoolUVE pool(2);
    JobGraphUVE graph;
    graph.ExecuteUVE(pool);
    graph.WaitUVE();
}

TEST(JobGraphUVETest, WaitBeforeExecute_ReturnsImmediately) {
    JobGraphUVE graph;
    graph.WaitUVE(); // never executed - must not hang
}

TEST(JobGraphUVETest, IndependentJobs_AllRunExactlyOnce) {
    ThreadPoolUVE pool(4);
    JobGraphUVE graph;
    std::atomic<int> completed{0};
    constexpr int kJobCount = 64;
    for (int i = 0; i < kJobCount; ++i) {
        ASSERT_NE(graph.AddJobUVE([&completed] { completed.fetch_add(1, std::memory_order_relaxed); }),
                   kInvalidJobGraphObjectHandleUVE);
    }

    graph.ExecuteUVE(pool);
    graph.WaitUVE();

    EXPECT_EQ(completed.load(), kJobCount);
}

TEST(JobGraphUVETest, LinearChain_RunsInDeclaredOrder) {
    ThreadPoolUVE pool(4);
    JobGraphUVE graph;
    std::mutex orderMutex;
    std::vector<int> order;

    const JobGraphObjectHandleUVE first = graph.AddJobUVE([&] {
        const std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(1);
    });
    const JobGraphObjectHandleUVE second = graph.AddJobUVE([&] {
        const std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(2);
    });
    const JobGraphObjectHandleUVE third = graph.AddJobUVE([&] {
        const std::lock_guard<std::mutex> lock(orderMutex);
        order.push_back(3);
    });
    ASSERT_TRUE(graph.AddDependencyUVE(second, first));
    ASSERT_TRUE(graph.AddDependencyUVE(third, second));

    graph.ExecuteUVE(pool);
    graph.WaitUVE();

    ASSERT_EQ(order.size(), 3U);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 3);
}

TEST(JobGraphUVETest, FanInJoin_DependentRunsOnlyAfterBothPredecessorsFinish) {
    ThreadPoolUVE pool(4);
    JobGraphUVE graph;
    std::atomic<int> predecessorsFinished{0};
    std::atomic<bool> joinSawBothPredecessorsFinished{false};

    const JobGraphObjectHandleUVE predecessorA = graph.AddJobUVE([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        predecessorsFinished.fetch_add(1, std::memory_order_release);
    });
    const JobGraphObjectHandleUVE predecessorB = graph.AddJobUVE([&] {
        predecessorsFinished.fetch_add(1, std::memory_order_release);
    });
    const JobGraphObjectHandleUVE join = graph.AddJobUVE([&] {
        joinSawBothPredecessorsFinished.store(predecessorsFinished.load(std::memory_order_acquire) == 2,
                                               std::memory_order_relaxed);
    });
    ASSERT_TRUE(graph.AddDependencyUVE(join, predecessorA));
    ASSERT_TRUE(graph.AddDependencyUVE(join, predecessorB));

    graph.ExecuteUVE(pool);
    graph.WaitUVE();

    EXPECT_TRUE(joinSawBothPredecessorsFinished.load());
}

TEST(JobGraphUVETest, FanOutSplit_BothDependentsRunAfterOnePredecessor) {
    ThreadPoolUVE pool(4);
    JobGraphUVE graph;
    std::atomic<int> predecessorRunCount{0};
    std::atomic<int> dependentARunCount{0};
    std::atomic<int> dependentBRunCount{0};

    const JobGraphObjectHandleUVE predecessor =
        graph.AddJobUVE([&] { predecessorRunCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandleUVE dependentA =
        graph.AddJobUVE([&] { dependentARunCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandleUVE dependentB =
        graph.AddJobUVE([&] { dependentBRunCount.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(graph.AddDependencyUVE(dependentA, predecessor));
    ASSERT_TRUE(graph.AddDependencyUVE(dependentB, predecessor));

    graph.ExecuteUVE(pool);
    graph.WaitUVE();

    EXPECT_EQ(predecessorRunCount.load(), 1);
    EXPECT_EQ(dependentARunCount.load(), 1);
    EXPECT_EQ(dependentBRunCount.load(), 1);
}

TEST(JobGraphUVETest, DiamondGraph_AllFourObjectsRunExactlyOnce) {
    // top -> {left, right} -> bottom (bottom depends on both left and right).
    ThreadPoolUVE pool(4);
    JobGraphUVE graph;
    std::atomic<int> runCount{0};

    const JobGraphObjectHandleUVE top = graph.AddJobUVE([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandleUVE left = graph.AddJobUVE([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandleUVE right = graph.AddJobUVE([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    const JobGraphObjectHandleUVE bottom = graph.AddJobUVE([&] { runCount.fetch_add(1, std::memory_order_relaxed); });
    ASSERT_TRUE(graph.AddDependencyUVE(left, top));
    ASSERT_TRUE(graph.AddDependencyUVE(right, top));
    ASSERT_TRUE(graph.AddDependencyUVE(bottom, left));
    ASSERT_TRUE(graph.AddDependencyUVE(bottom, right));

    graph.ExecuteUVE(pool);
    graph.WaitUVE();

    EXPECT_EQ(runCount.load(), 4);
}

TEST(JobGraphUVETest, AddDependencyUVE_DirectCycleIsRejected) {
    JobGraphUVE graph;
    const JobGraphObjectHandleUVE a = graph.AddJobUVE([] {});
    const JobGraphObjectHandleUVE b = graph.AddJobUVE([] {});
    ASSERT_TRUE(graph.AddDependencyUVE(b, a)); // b depends on a
    EXPECT_FALSE(graph.AddDependencyUVE(a, b)); // a depends on b would cycle back to a
}

TEST(JobGraphUVETest, AddDependencyUVE_IndirectCycleIsRejected) {
    JobGraphUVE graph;
    const JobGraphObjectHandleUVE a = graph.AddJobUVE([] {});
    const JobGraphObjectHandleUVE b = graph.AddJobUVE([] {});
    const JobGraphObjectHandleUVE c = graph.AddJobUVE([] {});
    ASSERT_TRUE(graph.AddDependencyUVE(b, a)); // b depends on a
    ASSERT_TRUE(graph.AddDependencyUVE(c, b)); // c depends on b
    EXPECT_FALSE(graph.AddDependencyUVE(a, c)); // a depends on c would close the a->b->c->a cycle
}

TEST(JobGraphUVETest, AddDependencyUVE_SelfDependencyIsRejected) {
    JobGraphUVE graph;
    const JobGraphObjectHandleUVE a = graph.AddJobUVE([] {});
    EXPECT_FALSE(graph.AddDependencyUVE(a, a));
}

TEST(JobGraphUVETest, AddDependencyUVE_InvalidHandleIsRejected) {
    JobGraphUVE graph;
    const JobGraphObjectHandleUVE a = graph.AddJobUVE([] {});
    EXPECT_FALSE(graph.AddDependencyUVE(a, kInvalidJobGraphObjectHandleUVE));
    EXPECT_FALSE(graph.AddDependencyUVE(kInvalidJobGraphObjectHandleUVE, a));
}

TEST(JobGraphUVETest, AddJobUVE_AfterExecuteIsRejected) {
    ThreadPoolUVE pool(2);
    JobGraphUVE graph;
    static_cast<void>(graph.AddJobUVE([] {}));
    graph.ExecuteUVE(pool);
    EXPECT_EQ(graph.AddJobUVE([] {}), kInvalidJobGraphObjectHandleUVE);
    graph.WaitUVE();
}

TEST(JobGraphUVETest, AddDependencyUVE_AfterExecuteIsRejected) {
    ThreadPoolUVE pool(2);
    JobGraphUVE graph;
    const JobGraphObjectHandleUVE a = graph.AddJobUVE([] {});
    const JobGraphObjectHandleUVE b = graph.AddJobUVE([] {});
    graph.ExecuteUVE(pool);
    EXPECT_FALSE(graph.AddDependencyUVE(b, a));
    graph.WaitUVE();
}

TEST(JobGraphUVETest, ExecuteUVE_CalledTwiceIsANoOpSecondTime) {
    ThreadPoolUVE pool(2);
    JobGraphUVE graph;
    std::atomic<int> runCount{0};
    static_cast<void>(graph.AddJobUVE([&] { runCount.fetch_add(1, std::memory_order_relaxed); }));

    graph.ExecuteUVE(pool);
    graph.WaitUVE();
    EXPECT_EQ(runCount.load(), 1);

    graph.ExecuteUVE(pool); // ignored - already executed
    graph.WaitUVE();
    EXPECT_EQ(runCount.load(), 1);
}

TEST(JobGraphUVETest, LargeWideAndDeepGraph_StressCompletesWithoutHangOrDoubleRun) {
    // 8 independent chains of 32 sequential jobs each, all sharing one pool - exercises the
    // work-stealing path alongside the cascade-on-completion submission logic.
    ThreadPoolUVE pool(4);
    JobGraphUVE graph;
    constexpr int kChainCount = 8;
    constexpr int kChainLength = 32;
    std::atomic<int> totalRunCount{0};
    std::vector<std::vector<int>> chainOrder(kChainCount);
    std::vector<std::mutex> chainMutexes(kChainCount);

    for (int chain = 0; chain < kChainCount; ++chain) {
        JobGraphObjectHandleUVE previous = kInvalidJobGraphObjectHandleUVE;
        for (int step = 0; step < kChainLength; ++step) {
            const JobGraphObjectHandleUVE current = graph.AddJobUVE([&totalRunCount, &chainOrder, &chainMutexes, chain, step] {
                totalRunCount.fetch_add(1, std::memory_order_relaxed);
                const std::lock_guard<std::mutex> lock(chainMutexes[static_cast<std::size_t>(chain)]);
                chainOrder[static_cast<std::size_t>(chain)].push_back(step);
            });
            if (previous != kInvalidJobGraphObjectHandleUVE) {
                ASSERT_TRUE(graph.AddDependencyUVE(current, previous));
            }
            previous = current;
        }
    }

    graph.ExecuteUVE(pool);
    graph.WaitUVE();

    EXPECT_EQ(totalRunCount.load(), kChainCount * kChainLength);
    for (int chain = 0; chain < kChainCount; ++chain) {
        const std::vector<int>& order = chainOrder[static_cast<std::size_t>(chain)];
        ASSERT_EQ(order.size(), static_cast<std::size_t>(kChainLength));
        for (int step = 0; step < kChainLength; ++step) {
            EXPECT_EQ(order[static_cast<std::size_t>(step)], step);
        }
    }
}

} // namespace
} // namespace UVE::Threading::Tests
