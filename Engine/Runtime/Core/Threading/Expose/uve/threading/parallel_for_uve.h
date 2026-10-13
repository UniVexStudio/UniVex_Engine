// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <algorithm>
#include <cstddef>
#include <utility>

#include "uve/threading/i_thread_pool_uve.h"
#include "uve/threading/job_counter_uve.h"

namespace UVE::Threading {

/// Runs `fn(i)` for every `i` in [`begin`, `end`) across the pool's workers, blocking until all
/// iterations complete. The range splits into at most one chunk per worker (fewer when the range
/// is smaller than the pool); an empty range runs nothing and returns without touching the pool.
///
/// This is the primitive every caller hand-rolled before it existed: submit N jobs plus a
/// `JobCounterUVE` plus a wait. The functor may itself submit nested jobs — the pool guarantees
/// nested submission never deadlocks — but nesting a second `ParallelForUVE` over the SAME pool
/// deadlocks when the outer chunks occupy every worker (each waits for inner jobs no free
/// worker can run). Nest only with outer chunks strictly below the worker count, or fan the
/// inner work out to a second pool.
///
/// Exceptions: a throwing `fn` propagates out of the WORKER, not out of this call — jobs are
/// `std::function<void()>` and the pool does not transport exceptions. Throwing functors are a
/// caller bug; fail closed inside `fn` instead.
template <typename TFunc>
void ParallelForUVE(IThreadPoolUVE& pool, const std::size_t begin, const std::size_t end, TFunc&& fn) {
    if (begin >= end) {
        return;
    }
    const std::size_t items = end - begin;
    const std::size_t chunks = std::min(pool.GetWorkerCountUVE(), items);
    JobCounterUVE counter;
    std::size_t chunkBegin = begin;
    for (std::size_t chunk = 0U; chunk < chunks; ++chunk) {
        const std::size_t remaining = end - chunkBegin;
        const std::size_t left = chunks - chunk;
        const std::size_t take = (remaining + left - 1U) / left; // Ceiling: the last chunk is never empty.
        const std::size_t chunkEnd = chunkBegin + take;
        pool.SubmitUVE(
            [chunkBegin, chunkEnd, &fn] {
                for (std::size_t index = chunkBegin; index < chunkEnd; ++index) {
                    fn(index);
                }
            },
            counter);
        chunkBegin = chunkEnd;
    }
    counter.WaitUVE();
}

} // namespace UVE::Threading
