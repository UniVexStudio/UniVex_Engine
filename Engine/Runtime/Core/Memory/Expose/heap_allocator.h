// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>

#include "i_allocator.h"
#include "i_memory_tracker.h"

namespace UVE::Memory {

/// HeapAllocator is a tracked, general-purpose allocator wrapping the C++17 aligned
/// `::operator new`/`::operator delete` pair. It has no fixed capacity — this is the engine's
/// "default" allocator (see MemoryManager::GetDefaultAllocator()), not a hot-path
/// allocator; PoolAllocator/StackAllocator exist for hot-path use. Optionally reports
/// every allocation/deallocation to an IMemoryTracker (`nullptr` = untracked), so it stays
/// independently usable/testable without a MemoryManager.
/// Thread-safety: not thread-safe. `m_allocatedBytes` and the internal outstanding-allocation
/// bookkeeping are plain (non-atomic/non-mutex-guarded) state — wrap external synchronization
/// around a shared instance if multiple threads allocate through the same HeapAllocator
/// concurrently.
class HeapAllocator final : public IAllocator {
public:
    /// `tracker`, if non-null, must outlive this HeapAllocator. `allocatorTag` is the tag
    /// reported to `tracker` for every allocation made through this instance — defaults to
    /// "HeapAllocator"; pass a caller-specific name to distinguish multiple instances in
    /// leak reports (e.g. "AnimationPool" was the master-spec's own example tag).
    explicit HeapAllocator(IMemoryTracker* tracker = nullptr,
                               std::string_view allocatorTag = "HeapAllocator");

    [[nodiscard]] void* Allocate(std::size_t sizeBytes, std::size_t alignment,
                                     const char* sourceFile, int sourceLine) override;
    /// `pointer` must have been returned by Allocate() on this same instance and not yet
    /// freed — UVE_ASSERTs on a pointer this allocator never allocated (double-free or
    /// wrong-allocator bug), and logs an error and safely no-ops in Release rather than
    /// dereferencing bookkeeping this allocator never recorded.
    void Deallocate(void* pointer) override;
    [[nodiscard]] std::size_t GetAllocatedBytes() const override;

private:
    struct OutstandingAllocation {
        std::size_t sizeBytes;
        std::size_t alignment;
    };

    IMemoryTracker* m_tracker;
    std::string m_allocatorTag;
    std::size_t m_allocatedBytes = 0;
    std::unordered_map<void*, OutstandingAllocation> m_outstandingAllocations;
};

} // namespace UVE::Memory
