// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <vector>

#include "i_thread_pool.h"
#include "job_counter.h"

namespace UVE::Threading {

using JobGraphObjectHandle = std::size_t;

inline constexpr JobGraphObjectHandle kInvalidJobGraphObjectHandle =
    static_cast<JobGraphObjectHandle>(-1);

/// JobGraph is a dependency-scheduled task graph built on top of IThreadPool/JobCounter:
/// unlike submitting a batch of independent jobs against one JobCounter (the pool's own
/// fan-out/fan-in primitive, still the right tool when nothing depends on anything else), a
/// JobGraph lets a caller declare "this job must not start until that job finishes" and have
/// the graph itself submit each job the instant its dependencies are satisfied - no caller-side
/// Wait() between phases, no missed parallelism from over-serializing independent work.
///
/// Usage: AddJob() each unit of work (order does not matter), AddDependency() to declare
/// ordering between them, then Execute(pool) once to submit every dependency-free job (their
/// completions cascade automatically), then Wait() to block until the whole graph is done.
/// A JobGraph is single-use: AddJob()/AddDependency() are only valid before Execute(),
/// and Execute() itself only once. Build a new instance for the next frame/batch.
///
/// Thread-safety: AddJob()/AddDependency() are main-thread-only (or at least
/// single-threaded, and only before Execute()). Once Execute() has been called, the graph's
/// internal state is only ever touched by worker threads running its own submitted jobs, so no
/// external synchronization is needed for Wait() itself. Non-copyable and non-movable — like
/// JobCounter, submitted jobs capture `this`, so its address must stay stable for the graph's
/// entire in-flight lifetime.
class JobGraph final {
public:
    JobGraph() = default;
    ~JobGraph() = default;

    JobGraph(const JobGraph&) = delete;
    JobGraph& operator=(const JobGraph&) = delete;
    JobGraph(JobGraph&&) = delete;
    JobGraph& operator=(JobGraph&&) = delete;

    /// Adds one job to the graph and returns a handle identifying it, for use with
    /// AddDependency(). No-op (returns kInvalidJobGraphObjectHandle, logs an error) if called
    /// after Execute().
    [[nodiscard]] JobGraphObjectHandle AddJob(Job job);

    /// Declares that `dependent` must not start running until `dependency` has finished. Returns
    /// false and changes nothing if either handle is invalid/unknown, `dependent == dependency`,
    /// adding this edge would create a cycle, or the graph has already been executed - a
    /// programming error the caller should treat as such (e.g. via an assert at the call site),
    /// not a condition JobGraph recovers from silently.
    [[nodiscard]] bool AddDependency(JobGraphObjectHandle dependent, JobGraphObjectHandle dependency);

    /// Submits every job with no unmet dependencies to `pool`; each job's completion decrements
    /// its dependents' remaining-dependency counts and submits any that reach zero, cascading
    /// through the whole graph without further caller involvement. `pool` must outlive this call
    /// through to the matching Wait(). May be called at most once per instance; a second call
    /// is a no-op (logged as an error).
    void Execute(IThreadPool& pool);

    /// Blocks the calling thread until every job in the graph has finished. Safe to call even if
    /// the graph is empty or Execute() has not been called (returns immediately in both cases).
    void Wait();

private:
    struct Object {
        Job job;
        std::vector<JobGraphObjectHandle> successors;
        std::atomic<int> remainingDependencies{0};
    };

    [[nodiscard]] bool CanReach(JobGraphObjectHandle from, JobGraphObjectHandle to) const;
    void SubmitObject(IThreadPool& pool, JobGraphObjectHandle index);

    std::vector<std::unique_ptr<Object>> m_objects;
    JobCounter m_completionCounter;
    bool m_executed = false;
};

} // namespace UVE::Threading
