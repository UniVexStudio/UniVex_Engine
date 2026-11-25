// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <atomic>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "parallel_for.h"
#include "thread_pool.h"

namespace UVE::Threading::Tests {
namespace {

TEST(ParallelForTest, CoversEveryIndexExactlyOnce) {
    ThreadPool pool{4U};
    constexpr std::size_t kItems = 1000U;
    // Parentheses, not braces: braced init would build ONE atomic holding 1000U.
    std::vector<std::atomic<int>> hits(kItems);
    for (auto& hit : hits) {
        hit.store(0, std::memory_order_relaxed);
    }
    ParallelFor(pool, 0U, kItems, [&hits](const std::size_t index) {
        hits[index].fetch_add(1, std::memory_order_relaxed);
    });
    for (const auto& hit : hits) {
        EXPECT_EQ(hit.load(std::memory_order_relaxed), 1);
    }
}

TEST(ParallelForTest, EmptyRange_RunsNothing) {
    ThreadPool pool{4U};
    bool ran = false;
    ParallelFor(pool, 5U, 5U, [&ran](const std::size_t) { ran = true; });
    ParallelFor(pool, 9U, 5U, [&ran](const std::size_t) { ran = true; });
    EXPECT_FALSE(ran);
}

TEST(ParallelForTest, FewerItemsThanWorkers_StillCoversAll) {
    ThreadPool pool{8U};
    std::atomic<int> sum{0};
    ParallelFor(pool, 0U, 3U, [&sum](const std::size_t index) {
        sum.fetch_add(static_cast<int>(index), std::memory_order_relaxed);
    });
    EXPECT_EQ(sum.load(), 3);
}

TEST(ParallelForTest, NestedParallelFor_DoesNotDeadlock) {
    // A chunk that itself parallel-fors over the same pool: safe here because the two outer
    // chunks leave two workers free to drain the inner jobs (plus work-stealing covers inner
    // jobs queued behind a waiting worker). Four outer chunks on four workers would deadlock —
    // every worker waiting, none free — which is why the header forbids saturating nests.
    ThreadPool pool{4U};
    std::atomic<int> sum{0};
    ParallelFor(pool, 0U, 2U, [&pool, &sum](const std::size_t outer) {
        ParallelFor(pool, 0U, 10U, [&sum, outer](const std::size_t inner) {
            sum.fetch_add(static_cast<int>(outer * 10U + inner), std::memory_order_relaxed);
        });
    });
    EXPECT_EQ(sum.load(), 190);
}

} // namespace
} // namespace UVE::Threading::Tests
