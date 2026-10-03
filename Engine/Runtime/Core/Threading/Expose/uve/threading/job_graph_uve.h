// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "uve/threading/i_thread_pool_uve.h"
#include "uve/threading/job_counter_uve.h"

namespace UVE::Threading {

using JobGraphObjectHandleUVE = std::size_t;

inline constexpr JobGraphObjectHandleUVE kInvalidJobGraphObjectHandleUVE =
    static_cast<JobGraphObjectHandleUVE>(-1);

/// JobGraphUVE is a dependency-scheduled task graph built on top of IThreadPoolUVE/JobCounterUVE:
/// unlike submitting a batch of independent jobs against one JobCounterUVE (the pool's own
/// fan-out/fan-in primitive, still the right tool when nothing depends on anything else), a
/// JobGraphUVE lets a caller declare "this job must not start until that job finishes" and have
/// the graph itself submit each job the instant its dependencies are satisfied - no caller-side
/// WaitUVE() between phases, no missed parallelism from over-serializing independent work.
///
/// Usage: AddJobUVE() each unit of work (order does not matter), AddDependencyUVE() to declare
/// ordering between them, then ExecuteUVE(pool) once to submit every dependency-free job (their
/// completions cascade automatically), then WaitUVE() to block until the whole graph is done.
/// A JobGraphUVE is single-use: AddJobUVE()/AddDependencyUVE() are only valid before ExecuteUVE(),
/// and ExecuteUVE() itself only once. Build a new instance for the next frame/batch.
///
/// Thread-safety: AddJobUVE()/AddDependencyUVE() are main-thread-only (or at least
/// single-threaded, and only before ExecuteUVE()). Once ExecuteUVE() has been called, the graph's
/// internal state is only ever touched by worker threads running its own submitted jobs, so no
/// external synchronization is needed for WaitUVE() itself. Non-copyable and non-movable — like
/// JobCounterUVE, submitted jobs capture `this`, so its address must stay stable for the graph's
/// entire in-flight lifetime.
class JobGraphUVE final {
public:
    JobGraphUVE() = default;
    ~JobGraphUVE() = default;

    JobGraphUVE(const JobGraphUVE&) = delete;
    JobGraphUVE& operator=(const JobGraphUVE&) = delete;
    JobGraphUVE(JobGraphUVE&&) = delete;
    JobGraphUVE& operator=(JobGraphUVE&&) = delete;

    /// Adds one job to the graph and returns a handle identifying it, for use with
    /// AddDependencyUVE(). No-op (returns kInvalidJobGraphObjectHandleUVE, logs an error) if called
    /// after ExecuteUVE().
    [[nodiscard]] JobGraphObjectHandleUVE AddJobUVE(JobUVE job);

    /// Declares that `dependent` must not start running until `dependency` has finished. Returns
    /// false and changes nothing if either handle is invalid/unknown, `dependent == dependency`,
    /// adding this edge would create a cycle, or the graph has already been executed - a
    /// programming error the caller should treat as such (e.g. via an assert at the call site),
    /// not a condition JobGraphUVE recovers from silently.
    [[nodiscard]] bool AddDependencyUVE(JobGraphObjectHandleUVE dependent, JobGraphObjectHandleUVE dependency);

    /// Submits every job with no unmet dependencies to `pool`; each job's completion decrements
    /// its dependents' remaining-dependency counts and submits any that reach zero, cascading
    /// through the whole graph without further caller involvement. `pool` must outlive this call
    /// through to the matching WaitUVE(). May be called at most once per instance; a second call
    /// is a no-op (logged as an error).
    void ExecuteUVE(IThreadPoolUVE& pool);

    /// Blocks the calling thread until every job in the graph has finished. Safe to call even if
    /// the graph is empty or ExecuteUVE() has not been called (returns immediately in both cases).
    void WaitUVE();

private:
    struct ObjectUVE {
        JobUVE job;
        std::vector<JobGraphObjectHandleUVE> successors;
        std::atomic<int> remainingDependencies{0};
    };

    [[nodiscard]] bool CanReachUVE(JobGraphObjectHandleUVE from, JobGraphObjectHandleUVE to) const;
    void SubmitObjectUVE(IThreadPoolUVE& pool, JobGraphObjectHandleUVE index);

    std::vector<std::unique_ptr<ObjectUVE>> m_objects;
    JobCounterUVE m_completionCounter;
    bool m_executed = false;
};

} // namespace UVE::Threading
