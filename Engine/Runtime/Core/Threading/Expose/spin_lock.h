// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <atomic>

namespace UVE::Threading {

/// `SpinLockUVE` is a test-and-set spinlock for critical sections measured in nanoseconds: a
/// flag flip, a counter bump, a pointer swing. It never sleeps, so holding it across anything
/// that blocks (allocation, I/O, another lock) burns a core for the duration — that is the
/// documented misuse, and anything longer wants `std::mutex`.
///
/// The member names are lowercase `lock`/`unlock`/`try_lock` deliberately: that is the
/// `BasicLockable` shape `std::lock_guard`/`std::unique_lock` require, and a lock type that
/// cannot be guarded is a bug farm. Same rationale as the containers' lowercase `begin`/`end`.
///
/// Not recursive: locking twice from the same thread deadlocks. Copy/move deleted — a lock has
/// identity (its address is what threads agree on), like `JobCounterUVE`.
class SpinLockUVE final {
public:
    SpinLockUVE() = default;
    SpinLockUVE(const SpinLockUVE&) = delete;
    SpinLockUVE& operator=(const SpinLockUVE&) = delete;
    SpinLockUVE(SpinLockUVE&&) = delete;
    SpinLockUVE& operator=(SpinLockUVE&&) = delete;

    void lock() noexcept {
        while (m_flag.test_and_set(std::memory_order_acquire)) {
            // Spin. No backoff: backoff helps contended locks, and a contended spinlock is
            // already the wrong primitive (see above).
        }
    }

    [[nodiscard]] bool try_lock() noexcept { return !m_flag.test_and_set(std::memory_order_acquire); }

    void unlock() noexcept { m_flag.clear(std::memory_order_release); }

private:
    std::atomic_flag m_flag = ATOMIC_FLAG_INIT;
};

} // namespace UVE::Threading
