// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "i_allocator.h"
#include "i_memory_tracker.h"

namespace UVE::Memory {

/// IMemoryManager is the engine's memory-tracking/leak-detection hub interface. Extends
/// IMemoryTracker (so every allocator can report to it through that minimal surface) and
/// adds the fuller stats/leak-report/default-allocator surface EngineServices exposes.
/// Thread-safety: every method must be safe to call concurrently from any thread, anticipating
/// future worker-thread allocation.
class IMemoryManager : public IMemoryTracker {
public:
    /// Returns this manager's own tracked, general-purpose default allocator (a
    /// HeapAllocator wired to report to this manager). Systems without a more specialized
    /// allocator should use this rather than constructing their own untracked HeapAllocator.
    [[nodiscard]] virtual IAllocator& GetDefaultAllocator() = 0;

    /// Number of allocations currently active (recorded but not yet deallocated).
    [[nodiscard]] virtual std::size_t GetActiveAllocationCount() const = 0;
    /// Sum of `sizeBytes` across every currently-active allocation.
    [[nodiscard]] virtual std::size_t GetActiveBytes() const = 0;
    /// The highest GetActiveBytes() has ever reached (high-water mark).
    [[nodiscard]] virtual std::size_t GetPeakBytes() const = 0;
    /// Total number of allocations ever recorded, active or not (monotonically increasing).
    [[nodiscard]] virtual std::uint64_t GetTotalAllocationsEver() const = 0;

    /// True iff any allocation recorded via RecordAllocation() has not been matched by a
    /// RecordDeallocation() call.
    [[nodiscard]] virtual bool HasLeaks() const = 0;

    /// Returns a snapshot copy of every currently-active (never-deallocated) allocation record,
    /// for tests/tooling to assert against directly rather than parsing log output.
    [[nodiscard]] virtual std::vector<AllocationRecord> GetLeakedAllocations() const = 0;

    /// Logs one UVE_ERROR per leaked allocation (including its allocationId, size, allocator
    /// tag, and source location) plus a one-line summary, via the active logger. Does not
    /// abort — leak *detection* is this increment's job, not a forced crash. Typically called
    /// once, from EngineCore::Shutdown(), before this manager is torn down.
    virtual void LogLeakReport() = 0;
};

} // namespace UVE::Memory
