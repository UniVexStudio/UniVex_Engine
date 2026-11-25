// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "heap_allocator.h"
#include "i_memory_manager.h"

namespace UVE::Memory {

/// MemoryManager is the concrete, engine-standard implementation of IMemoryManager. Owns
/// its own tracked HeapAllocator (returned by GetDefaultAllocator()) and maintains a
/// mutex-guarded map of every currently-active allocation reported to it by any allocator
/// (including its own default one), plus running stats (active bytes/count, peak bytes,
/// total-allocations-ever). Every accepted allocation is assigned a unique, monotonically
/// increasing allocationId via an atomic counter at the moment RecordAllocation() accepts
/// it.
/// Thread-safety: thread-safe. RecordAllocation()/RecordDeallocation() and every stats
/// accessor are guarded by an internal mutex (the id counter uses an atomic instead, so it can
/// be incremented without taking the lock) — safe to call concurrently from any thread.
class MemoryManager final : public IMemoryManager {
public:
    MemoryManager();

    void RecordAllocation(void* pointer, std::size_t sizeBytes, std::size_t alignment,
                              std::string_view allocatorTag, const char* sourceFile,
                              int sourceLine) override;
    void RecordDeallocation(void* pointer) override;

    [[nodiscard]] IAllocator& GetDefaultAllocator() override;
    [[nodiscard]] std::size_t GetActiveAllocationCount() const override;
    [[nodiscard]] std::size_t GetActiveBytes() const override;
    [[nodiscard]] std::size_t GetPeakBytes() const override;
    [[nodiscard]] std::uint64_t GetTotalAllocationsEver() const override;
    [[nodiscard]] bool HasLeaks() const override;
    [[nodiscard]] std::vector<AllocationRecord> GetLeakedAllocations() const override;
    void LogLeakReport() override;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<void*, AllocationRecord> m_activeAllocations;
    std::atomic<std::uint64_t> m_nextAllocationId{1};
    std::size_t m_activeBytes = 0;
    std::size_t m_peakBytes = 0;
    std::uint64_t m_totalAllocationsEver = 0;
    HeapAllocator m_defaultAllocator;
};

} // namespace UVE::Memory
