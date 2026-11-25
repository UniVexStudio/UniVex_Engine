// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "i_thread_pool.h"

namespace UVE::Threading {

/// ThreadPool is the concrete, engine-standard implementation of IThreadPool: a
/// fixed-size pool of worker threads, each with its own job deque, sharing one mutex +
/// condition_variable rather than a lock-free structure (no ThreadSanitizer/data-race detector
/// is wired into this project yet, so correct-by-construction was chosen over peak throughput —
/// see docs/CODING_STANDARDS.md). A worker pops its own queue from the back (LIFO, cache-
/// friendly); when empty, it steals from the front of another worker's queue (FIFO, minimizing
/// contention with that worker's own back-pops) — the "work-stealing queue" the spec asks for.
/// Fixed worker count, chosen at construction — does not grow or shrink.
/// Thread-safety: thread-safe. Every public method may be called concurrently from any thread,
/// including from inside a currently-running job (recursive/nested submission is fully
/// supported — a job's own Submit() call never deadlocks against the pool's mutex, since the
/// mutex is never held while a job is executing).
class ThreadPool final : public IThreadPool {
public:
    /// Spawns `workerCount` worker threads immediately. `0` (the default) means "auto":
    /// `max(1, hardware_concurrency() - 1)`, reserving one core for the calling/main thread.
    explicit ThreadPool(std::size_t workerCount = 0);

    /// Requests every worker to stop, then joins them all, blocking until they do. Workers
    /// drain whatever is already queued (their own and anything stealable) before honoring the
    /// stop request — no silently dropped work on teardown. Precondition: no new top-level
    /// Submit() call is made once destruction begins (a submission racing the very start of
    /// destruction is not guaranteed to run — this is not a distributed-shutdown-consensus
    /// mechanism); must not be called from within one of this pool's own jobs (a worker thread
    /// cannot join itself).
    ~ThreadPool() override;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void Submit(Job job) override;
    void Submit(Job job, JobCounter& counter) override;

    [[nodiscard]] std::size_t GetWorkerCount() const noexcept override;
    [[nodiscard]] std::size_t GetPendingJobCount() const noexcept override;
    [[nodiscard]] std::size_t GetActiveWorkerCount() const noexcept override;
    [[nodiscard]] std::uint64_t GetStolenJobCount() const noexcept override;
    [[nodiscard]] bool IsWorkerThread() const noexcept override;

private:
    struct QueuedJob {
        Job function;
        JobCounter* counter = nullptr;
    };

    void SubmitInternal(QueuedJob job);
    void WorkerLoop(std::size_t workerIndex);
    void RunJob(QueuedJob& job);

    /// Requires m_mutex already held by the caller.
    [[nodiscard]] bool HasAnyWorkLocked() const noexcept;
    /// Requires m_mutex already held by the caller, and that some queue other than
    /// `workerIndex`'s own is non-empty (checked by the caller before calling this).
    [[nodiscard]] std::size_t FindStealVictimLocked(std::size_t workerIndex) const noexcept;

    mutable std::mutex m_mutex;
    std::condition_variable m_condVar;
    std::vector<std::deque<QueuedJob>> m_queues;
    std::vector<std::thread> m_workers;
    bool m_stopRequested = false;

    std::atomic<std::size_t> m_nextSubmitIndex{0};
    std::atomic<std::uint64_t> m_stolenJobCount{0};
    std::atomic<std::size_t> m_pendingJobCount{0};
    std::atomic<std::size_t> m_activeWorkerCount{0};
};

} // namespace UVE::Threading
