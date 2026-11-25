// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#include "job_counter.h"

namespace UVE::Threading {

/// Job is the engine's one callable-job type: a zero-argument, no-return unit of work
/// submitted to an IThreadPool. Used consistently across the public API and internal queue
/// storage — a seam for a future non-std::function job representation without changing any
/// call site's signature.
using Job = std::function<void()>;

/// IThreadPool is the interface for the engine's job system. Exists so future builds
/// (dedicated server, editor, tests) can substitute a different implementation without changing
/// any call site that consumes an IThreadPool&. Unlike IEventSystem, no per-call type
/// parameter is needed here — every job is already uniformly Job — so these are plain
/// virtuals with no type-erased-primitive layer underneath.
/// Thread-safety: implementations must support every method being called concurrently from any
/// thread, including Submit() called from inside a currently-running job (recursive/nested
/// submission — see ThreadPool).
class IThreadPool {
public:
    virtual ~IThreadPool() = default;

    /// Submits `job` for execution on some worker thread. Fire-and-forget: there is no way to
    /// wait on this specific job via this overload — use the counter overload below if the
    /// caller needs to know when it (and/or a batch of others) has finished.
    virtual void Submit(Job job) = 0;

    /// Submits `job` for execution on some worker thread, incrementing `counter` before the job
    /// can possibly start running and decrementing it (waking anyone blocked in
    /// `counter.Wait()`) once the job finishes — whether it completed normally or threw.
    /// `counter` must outlive the job's execution.
    virtual void Submit(Job job, JobCounter& counter) = 0;

    /// Number of worker threads this pool owns.
    [[nodiscard]] virtual std::size_t GetWorkerCount() const noexcept = 0;

    /// Number of jobs accepted (via either Submit overload) but not yet finished executing.
    /// Debugging/query only.
    [[nodiscard]] virtual std::size_t GetPendingJobCount() const noexcept = 0;

    /// Number of worker threads currently executing a job (as opposed to idle/waiting/stealing).
    /// Debugging/query only; never exceeds GetWorkerCount().
    [[nodiscard]] virtual std::size_t GetActiveWorkerCount() const noexcept = 0;

    /// Total number of jobs ever picked up via the work-stealing path (as opposed to a worker
    /// popping from its own queue). Debugging/query only.
    [[nodiscard]] virtual std::uint64_t GetStolenJobCount() const noexcept = 0;

    /// True iff called from one of this pool's own worker threads. Detection only — nothing in
    /// this increment changes behavior based on it, but future systems can use it to, for
    /// example, avoid a blocking call from within a worker.
    [[nodiscard]] virtual bool IsWorkerThread() const noexcept = 0;
};

} // namespace UVE::Threading
