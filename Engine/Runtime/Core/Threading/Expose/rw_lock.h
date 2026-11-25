// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <mutex>
#include <shared_mutex>

namespace UVE::Threading {

/// `RwLock` is a readers-writer lock over `std::shared_mutex`: any number of concurrent
/// readers (`lock_shared`) or one exclusive writer (`lock`). The shape registries and caches
/// want — read-mostly, occasionally rebuilt — without serializing every lookup behind a plain
/// mutex.
///
/// Member names are lowercase for the STL guard interop (`std::shared_lock` needs
/// `lock_shared`/`unlock_shared`; `std::lock_guard`/`std::unique_lock` need `lock`/`unlock`),
/// the same rationale as `SpinLock`. Not recursive in either mode. Copy/move deleted: a
/// lock has identity.
class RwLock final {
public:
    RwLock() = default;
    RwLock(const RwLock&) = delete;
    RwLock& operator=(const RwLock&) = delete;
    RwLock(RwLock&&) = delete;
    RwLock& operator=(RwLock&&) = delete;

    /// Exclusive (writer) lock. Blocks until no reader or writer holds the lock.
    void lock() { m_mutex.lock(); }

    /// Tries the exclusive lock, returning false immediately when held.
    [[nodiscard]] bool try_lock() { return m_mutex.try_lock(); }

    /// Releases the exclusive lock.
    void unlock() { m_mutex.unlock(); }

    /// Shared (reader) lock. Blocks only while a writer holds the lock.
    void lock_shared() { m_mutex.lock_shared(); }

    /// Tries the shared lock, returning false immediately when a writer holds it.
    [[nodiscard]] bool try_lock_shared() { return m_mutex.try_lock_shared(); }

    /// Releases one shared hold.
    void unlock_shared() { m_mutex.unlock_shared(); }

private:
    std::shared_mutex m_mutex;
};

} // namespace UVE::Threading
