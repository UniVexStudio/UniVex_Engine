// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "i_allocator.h"
#include "i_memory_tracker.h"

namespace UVE::Memory {

class StackAllocator;

/// Opaque marker into a StackAllocator's allocation history, returned by
/// StackAllocator::GetMarker() and consumed by StackAllocator::RewindToMarker() to
/// bulk-free everything allocated since the marker was taken — the idiomatic "free everything
/// this frame/scope" pattern for a stack allocator. Carries the allocator that produced it so
/// RewindToMarker() can UVE_ASSERT a marker is never used against a different
/// StackAllocator instance than the one that issued it (e.g. a marker from StackAllocatorA
/// passed into StackAllocatorB's RewindToMarker()).
/// Thread-safety: value type, safe to copy/move/compare freely.
struct StackMarker {
    std::size_t offset = 0;
    const StackAllocator* owner = nullptr;
};

/// StackAllocator is a fixed-capacity bump-pointer allocator. Each allocation writes a small
/// trailing header recording its own extent, so the allocator always knows each block's extent
/// without external bookkeeping. Two ways to free, both fully implemented — neither is a stub
/// of the other:
///  - Deallocate(pointer) (the IAllocator-conformance path): strict LIFO — UVE_ASSERTs
///    that `pointer` is exactly the current top of the stack.
///  - GetMarker()/RewindToMarker(marker) (the idiomatic bulk scoped-free path): walks
///    every allocation made since the marker in reverse, reporting each one to the tracker
///    individually, then rewinds in one step.
/// Fixed-capacity only — Allocate() UVE_ASSERTs if the stack is exhausted; it never grows.
/// Thread-safety: not thread-safe. Wrap external synchronization around a shared instance if
/// multiple threads allocate from the same stack concurrently.
class StackAllocator final : public IAllocator {
public:
    /// Allocates one `capacityBytes`-byte buffer up front (freed in the destructor).
    /// `tracker`, if non-null, must outlive this StackAllocator.
    explicit StackAllocator(std::size_t capacityBytes, IMemoryTracker* tracker = nullptr,
                                std::string_view allocatorTag = "StackAllocator");
    ~StackAllocator() override;

    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;

    /// UVE_ASSERTs if the stack does not have room for `sizeBytes` plus alignment padding plus
    /// the per-allocation header (fixed-capacity — never grows). Also throws `std::bad_alloc` in
    /// that case (Release builds included — UVE_ASSERT is compiled out there, but capacity
    /// exhaustion is a real runtime condition this allocator must still refuse safely), so
    /// Allocate() never returns without a valid, capacity-backed pointer.
    [[nodiscard]] void* Allocate(std::size_t sizeBytes, std::size_t alignment,
                                     const char* sourceFile, int sourceLine) override;

    /// Strict-LIFO single-object free: UVE_ASSERTs that `pointer` is exactly the current top of
    /// the stack (the most recently allocated, not-yet-freed block).
    void Deallocate(void* pointer) override;

    /// Total bytes currently committed from the stack's buffer (payloads + alignment padding +
    /// per-allocation headers) — the physical footprint, not the sum of nominal request sizes.
    [[nodiscard]] std::size_t GetAllocatedBytes() const override;

    /// Captures the stack's current position for a later RewindToMarker() call.
    [[nodiscard]] StackMarker GetMarker() const noexcept;

    /// Frees every allocation made since `marker` was captured, in one step, reporting each
    /// freed allocation to the tracker individually along the way. UVE_ASSERTs that
    /// `marker.owner` is this instance and that `marker.offset` does not exceed the stack's
    /// current position — both are usage errors, never silently ignored.
    void RewindToMarker(const StackMarker& marker);

    /// Frees every allocation currently on the stack — equivalent to rewinding to the marker
    /// this instance had immediately after construction.
    void Reset();

private:
    [[nodiscard]] std::byte* BufferBytes() const noexcept;
    void RewindToOffset(std::size_t targetOffset);

    std::size_t m_capacityBytes;
    std::size_t m_currentOffset = 0;
    void* m_buffer;
    IMemoryTracker* m_tracker;
    std::string m_allocatorTag;
};

} // namespace UVE::Memory
