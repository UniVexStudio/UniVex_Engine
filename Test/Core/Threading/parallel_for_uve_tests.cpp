// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <atomic>
#include <cstddef>
#include <vector>

#include <gtest/gtest.h>

#include "uve/threading/parallel_for_uve.h"
#include "uve/threading/thread_pool_uve.h"

namespace UVE::Threading::Tests {
namespace {

TEST(ParallelForUVETest, CoversEveryIndexExactlyOnce) {
    ThreadPoolUVE pool{4U};
    constexpr std::size_t kItemsUVE = 1000U;
    // Parentheses, not braces: braced init would build ONE atomic holding 1000U.
    std::vector<std::atomic<int>> hits(kItemsUVE);
    for (auto& hit : hits) {
        hit.store(0, std::memory_order_relaxed);
    }
    ParallelForUVE(pool, 0U, kItemsUVE, [&hits](const std::size_t index) {
        hits[index].fetch_add(1, std::memory_order_relaxed);
    });
    for (const auto& hit : hits) {
        EXPECT_EQ(hit.load(std::memory_order_relaxed), 1);
    }
}

TEST(ParallelForUVETest, EmptyRange_RunsNothing) {
    ThreadPoolUVE pool{4U};
    bool ran = false;
    ParallelForUVE(pool, 5U, 5U, [&ran](const std::size_t) { ran = true; });
    ParallelForUVE(pool, 9U, 5U, [&ran](const std::size_t) { ran = true; });
    EXPECT_FALSE(ran);
}

TEST(ParallelForUVETest, FewerItemsThanWorkers_StillCoversAll) {
    ThreadPoolUVE pool{8U};
    std::atomic<int> sum{0};
    ParallelForUVE(pool, 0U, 3U, [&sum](const std::size_t index) {
        sum.fetch_add(static_cast<int>(index), std::memory_order_relaxed);
    });
    EXPECT_EQ(sum.load(), 3);
}

TEST(ParallelForUVETest, NestedParallelFor_DoesNotDeadlock) {
    // A chunk that itself parallel-fors over the same pool: safe here because the two outer
    // chunks leave two workers free to drain the inner jobs (plus work-stealing covers inner
    // jobs queued behind a waiting worker). Four outer chunks on four workers would deadlock —
    // every worker waiting, none free — which is why the header forbids saturating nests.
    ThreadPoolUVE pool{4U};
    std::atomic<int> sum{0};
    ParallelForUVE(pool, 0U, 2U, [&pool, &sum](const std::size_t outer) {
        ParallelForUVE(pool, 0U, 10U, [&sum, outer](const std::size_t inner) {
            sum.fetch_add(static_cast<int>(outer * 10U + inner), std::memory_order_relaxed);
        });
    });
    EXPECT_EQ(sum.load(), 190);
}

} // namespace
} // namespace UVE::Threading::Tests
