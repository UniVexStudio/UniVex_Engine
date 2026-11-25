// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <condition_variable>
#include <mutex>

namespace UVE::Threading {

/// JobCounter is a small, reusable "pending-count latch": a batch of jobs submitted to
/// IThreadPool alongside the same counter can be waited on as a group via Wait(), without
/// the pool needing to know anything about futures or return values. A plain concrete utility
/// type (like UVE::Core::FrameStats or UVE::Memory::AllocationRecord) — not a service, so
/// it is not reachable via EngineServices and has no interface.
/// Typically stack-allocated by the code submitting a batch of jobs; non-copyable and
/// non-movable because ThreadPool workers hold a raw pointer to it for the lifetime of the
/// in-flight jobs, so its address must stay stable.
/// Thread-safety: thread-safe. Every method may be called concurrently from any thread —
/// Increment()/DecrementAndNotify() are called by worker threads, Wait() by whichever
/// thread wants to block on the batch.
class JobCounter final {
public:
    JobCounter() = default;

    JobCounter(const JobCounter&) = delete;
    JobCounter& operator=(const JobCounter&) = delete;
    JobCounter(JobCounter&&) = delete;
    JobCounter& operator=(JobCounter&&) = delete;

    /// Increments the pending-job count. Called once per job submitted against this counter,
    /// before the job can possibly start running.
    void Increment();

    /// Decrements the pending-job count and wakes any thread blocked in Wait(). UVE_ASSERTs
    /// the count was greater than zero beforehand — a call without a matching prior
    /// Increment() is a programming error, not a condition to silently tolerate. In Release,
    /// an extra call is logged as an error and ignored (the count is not decremented past zero)
    /// rather than underflowing and leaving a permanently non-zero count that would hang every
    /// future Wait() on this counter.
    void DecrementAndNotify();

    /// Blocks the calling thread until the pending-job count reaches zero. Returns immediately
    /// if it is already zero (e.g. a freshly constructed counter, or one whose jobs already all
    /// completed).
    void Wait();

private:
    std::mutex m_mutex;
    std::condition_variable m_condVar;
    int m_pendingCount = 0;
};

} // namespace UVE::Threading
