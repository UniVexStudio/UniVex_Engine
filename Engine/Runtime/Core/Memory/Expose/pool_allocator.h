// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "i_allocator.h"
#include "i_memory_tracker.h"

namespace UVE::Memory {

/// PoolAllocator is a fixed-size-block pool allocator: a fixed number of same-size,
/// same-alignment blocks carved from one contiguous buffer allocated at construction, handed
/// out and reclaimed in O(1) via an intrusive free-list threaded through the free blocks
/// themselves (no separate free-list storage). Fixed-capacity only — Allocate() UVE_ASSERTs
/// if the pool is exhausted; it never grows. Ideal for many same-size, same-lifetime-shape
/// objects (e.g. one pool per ECS component type).
/// Thread-safety: not thread-safe. Wrap external synchronization around a shared instance if
/// multiple threads allocate from the same pool concurrently.
class PoolAllocator final : public IAllocator {
public:
    /// Allocates one contiguous buffer sized for `blockCount` blocks of `blockSizeBytes` bytes
    /// each (rounded up internally to at least `sizeof(void*)` so the free-list pointer always
    /// fits, and to a multiple of `blockAlignment` so every block stays aligned), freed in the
    /// destructor. `blockAlignment` must satisfy IsValidAlignment() (UVE_ASSERTs otherwise).
    /// `tracker`, if non-null, must outlive this PoolAllocator. Throws `std::bad_alloc` when
    /// alignment rounding or the total block footprint cannot be represented by `std::size_t`.
    PoolAllocator(std::size_t blockSizeBytes, std::size_t blockAlignment,
                      std::size_t blockCount, IMemoryTracker* tracker = nullptr,
                      std::string_view allocatorTag = "PoolAllocator");
    ~PoolAllocator() override;

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;

    /// `sizeBytes` must be <= the pool's configured block size and `alignment` must be <= the
    /// pool's configured block alignment (UVE_ASSERTs otherwise, and throws
    /// `std::invalid_argument` in Release) — every returned block is exactly one pool block
    /// regardless of the exact requested size. UVE_ASSERTs if the pool is exhausted
    /// (fixed-capacity — never grows), and throws `std::bad_alloc` in Release, so Allocate()
    /// never returns without a valid block.
    [[nodiscard]] void* Allocate(std::size_t sizeBytes, std::size_t alignment,
                                     const char* sourceFile, int sourceLine) override;

    /// `pointer` must have been returned by Allocate() on this same instance — UVE_ASSERTs
    /// on an out-of-range or mid-block (foreign) pointer.
    void Deallocate(void* pointer) override;

    /// Physical footprint of every currently-handed-out block (GetUsedBlocks() * the
    /// internal, alignment/pointer-rounded block stride — not the nominal `blockSizeBytes`).
    [[nodiscard]] std::size_t GetAllocatedBytes() const override;

    /// Number of blocks currently handed out. Debugging/query only — O(1), backed by a single
    /// counter maintained alongside Allocate()/Deallocate(); adds no scanning overhead.
    [[nodiscard]] std::size_t GetUsedBlocks() const noexcept;
    /// Number of blocks currently free: `GetCapacityBlocks() - GetUsedBlocks()`.
    [[nodiscard]] std::size_t GetFreeBlocks() const noexcept;
    /// Total number of blocks this pool was constructed with.
    [[nodiscard]] std::size_t GetCapacityBlocks() const noexcept;

private:
    [[nodiscard]] std::byte* BufferBytes() const noexcept;
    [[nodiscard]] bool OwnsPointer(const void* pointer) const noexcept;

    std::size_t m_blockSizeBytes;
    std::size_t m_blockAlignment;
    std::size_t m_blockStride;
    std::size_t m_blockCount;
    std::size_t m_usedBlockCount = 0;
    void* m_buffer;
    void* m_freeListHead;
    IMemoryTracker* m_tracker;
    std::string m_allocatorTag;
};

} // namespace UVE::Memory
