// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>

namespace UVE::Threading {

/// `SemaphoreUVE` is an unbounded counting semaphore: `AcquireUVE` blocks until the count is
/// positive, then takes one; `ReleaseUVE` adds back. A zero-initialized semaphore is a gate
/// (workers wait, the producer releases N permits); a pool-sized one caps concurrency.
///
/// Implemented on mutex + condition variable rather than `std::counting_semaphore` so the
/// ceiling is `size_t`, not a compile-time template constant nobody can agree on. Members carry
/// the `UVE` suffix (unlike the locks) because no STL guard interoperates with a semaphore —
/// there is no lowercase shape to match.
class SemaphoreUVE final {
public:
    explicit SemaphoreUVE(const std::size_t initial = 0U) : m_count(initial) {}

    SemaphoreUVE(const SemaphoreUVE&) = delete;
    SemaphoreUVE& operator=(const SemaphoreUVE&) = delete;
    SemaphoreUVE(SemaphoreUVE&&) = delete;
    SemaphoreUVE& operator=(SemaphoreUVE&&) = delete;

    /// Takes one permit, blocking until one is available.
    void AcquireUVE() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_condVar.wait(lock, [this] { return m_count > 0U; });
        --m_count;
    }

    /// Takes one permit when available, returning false immediately otherwise.
    [[nodiscard]] bool TryAcquireUVE() {
        const std::lock_guard<std::mutex> lock(m_mutex);
        if (m_count == 0U) {
            return false;
        }
        --m_count;
        return true;
    }

    /// Returns `permits` permits, waking that many waiters (all of them when in doubt: the
    /// predicate re-checks, so a spurious wake costs a mutex round-trip, not correctness).
    void ReleaseUVE(const std::size_t permits = 1U) {
        {
            const std::lock_guard<std::mutex> lock(m_mutex);
            m_count += permits;
        }
        m_condVar.notify_all();
    }

private:
    std::mutex m_mutex;
    std::condition_variable m_condVar;
    std::size_t m_count;
};

} // namespace UVE::Threading
