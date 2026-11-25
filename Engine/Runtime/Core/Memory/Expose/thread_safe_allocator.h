// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <mutex>

#include "i_allocator.h"

namespace UVE::Memory {

/// `ThreadSafeAllocator` is the bridge the module's own docs anticipate: every concrete
/// allocator is single-threaded by contract, yet `IMemoryTracker` explicitly anticipates
/// worker-thread allocation. This decorator serializes one inner allocator behind a mutex, so
/// any allocator becomes shareable without changing its contract or its tests.
///
/// The inner allocator is a reference: it must outlive the wrapper, and it must not be used
/// directly while shared (direct use bypasses the lock). The lock is uncontended-cheap but it
/// is still one lock per allocation — hot per-thread allocation wants per-thread arenas, not
/// one of these.
class ThreadSafeAllocator final : public IAllocator {
public:
    explicit ThreadSafeAllocator(IAllocator& inner) noexcept : m_inner(inner) {}

    [[nodiscard]] void* Allocate(const std::size_t sizeBytes, const std::size_t alignment,
                                    const char* sourceFile, const int sourceLine) override {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_inner.Allocate(sizeBytes, alignment, sourceFile, sourceLine);
    }

    void Deallocate(void* pointer) override {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_inner.Deallocate(pointer);
    }

    [[nodiscard]] std::size_t GetAllocatedBytes() const override {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_inner.GetAllocatedBytes();
    }

private:
    IAllocator& m_inner;
    mutable std::mutex m_mutex;
};

} // namespace UVE::Memory
