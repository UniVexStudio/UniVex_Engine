// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <mutex>

#include "uve/memory/i_allocator_uve.h"

namespace UVE::Memory {

/// `ThreadSafeAllocatorUVE` is the bridge the module's own docs anticipate: every concrete
/// allocator is single-threaded by contract, yet `IMemoryTrackerUVE` explicitly anticipates
/// worker-thread allocation. This decorator serializes one inner allocator behind a mutex, so
/// any allocator becomes shareable without changing its contract or its tests.
///
/// The inner allocator is a reference: it must outlive the wrapper, and it must not be used
/// directly while shared (direct use bypasses the lock). The lock is uncontended-cheap but it
/// is still one lock per allocation — hot per-thread allocation wants per-thread arenas, not
/// one of these.
class ThreadSafeAllocatorUVE final : public IAllocatorUVE {
public:
    explicit ThreadSafeAllocatorUVE(IAllocatorUVE& inner) noexcept : m_inner(inner) {}

    [[nodiscard]] void* AllocateUVE(const std::size_t sizeBytes, const std::size_t alignment,
                                    const char* sourceFile, const int sourceLine) override {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_inner.AllocateUVE(sizeBytes, alignment, sourceFile, sourceLine);
    }

    void DeallocateUVE(void* pointer) override {
        const std::lock_guard<std::mutex> lock(m_mutex);
        m_inner.DeallocateUVE(pointer);
    }

    [[nodiscard]] std::size_t GetAllocatedBytesUVE() const override {
        const std::lock_guard<std::mutex> lock(m_mutex);
        return m_inner.GetAllocatedBytesUVE();
    }

private:
    IAllocatorUVE& m_inner;
    mutable std::mutex m_mutex;
};

} // namespace UVE::Memory
