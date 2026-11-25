// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "engine_assert.h"
#include "alignment_utils.h"
#include "i_allocator.h"

namespace UVE::Memory {

/// `LinearArena` is the frame/linear arena: a bump allocator over one owned buffer where
/// allocation is a pointer add and freeing is `Reset()`. The standard "allocate freely during
/// a frame, reset at end of frame" discipline — `StackAllocator` is close but its LIFO
/// ordering is stricter than a frame arena needs (a frame's allocations have no nesting to
/// rewind to).
///
/// `Deallocate` is a deliberate no-op: single blocks are never reclaimed, only the whole
/// arena at once. It still asserts the pointer came from this arena, so a mismatched
/// allocator pair fails loudly instead of leaking silently. `Reset` runs NO destructors —
/// callers destroy their objects (via `Destroy`) before resetting, exactly like the pool.
///
/// Exhaustion asserts like `PoolAllocator`'s: capacity is fixed at construction and running
/// dry is a sizing bug, not a runtime condition. Size the arena from `GetAllocatedBytes()`
/// peaks observed in practice.
///
/// Thread-safety: none, like every other concrete allocator here — wrap in
/// `ThreadSafeAllocator` for shared use.
class LinearArena final : public IAllocator {
public:
    explicit LinearArena(const std::size_t capacityBytes) : m_storage(capacityBytes) {
        UVE_ASSERT(capacityBytes > 0U);
    }

    [[nodiscard]] void* Allocate(const std::size_t sizeBytes, const std::size_t alignment,
                                    const char* /*sourceFile*/, const int /*sourceLine*/) override {
        UVE_ASSERT(IsValidAlignment(alignment));
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(m_storage.data());
        const std::uintptr_t aligned = AlignUp(base + m_offset, alignment);
        const std::size_t alignedOffset = static_cast<std::size_t>(aligned - base);
        UVE_ASSERT(alignedOffset + sizeBytes <= m_storage.size());
        if (alignedOffset + sizeBytes > m_storage.size()) {
            UVE_ERROR("LinearArena: arena exhausted ({} of {} bytes in use)", m_offset,
                      m_storage.size());
            throw std::bad_alloc{};
        }
        m_offset = alignedOffset + sizeBytes;
        return m_storage.data() + alignedOffset;
    }

    void Deallocate(void* pointer) override {
        // No-op by design (see above) — but the pointer must be ours.
        const std::byte* const begin = m_storage.data();
        const std::byte* const end = begin + m_storage.size();
        const auto* const candidate = static_cast<const std::byte*>(pointer);
        UVE_ASSERT(pointer == nullptr || (candidate >= begin && candidate < end));
        static_cast<void>(candidate);
    }

    [[nodiscard]] std::size_t GetAllocatedBytes() const noexcept override { return m_offset; }

    /// Frees every allocation at once. Runs no destructors (see above).
    void Reset() noexcept { m_offset = 0U; }

    /// The fixed capacity in bytes.
    [[nodiscard]] std::size_t Capacity() const noexcept { return m_storage.size(); }

private:
    std::vector<std::byte> m_storage;
    std::size_t m_offset = 0U;
};

} // namespace UVE::Memory
