// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "uve/logging/assert_uve.h"
#include "uve/memory/alignment_utils_uve.h"
#include "uve/memory/i_allocator_uve.h"

namespace UVE::Memory {

/// `LinearArenaUVE` is the frame/linear arena: a bump allocator over one owned buffer where
/// allocation is a pointer add and freeing is `ResetUVE()`. The standard "allocate freely during
/// a frame, reset at end of frame" discipline — `StackAllocatorUVE` is close but its LIFO
/// ordering is stricter than a frame arena needs (a frame's allocations have no nesting to
/// rewind to).
///
/// `DeallocateUVE` is a deliberate no-op: single blocks are never reclaimed, only the whole
/// arena at once. It still asserts the pointer came from this arena, so a mismatched
/// allocator pair fails loudly instead of leaking silently. `ResetUVE` runs NO destructors —
/// callers destroy their objects (via `DestroyUVE`) before resetting, exactly like the pool.
///
/// Exhaustion asserts like `PoolAllocatorUVE`'s: capacity is fixed at construction and running
/// dry is a sizing bug, not a runtime condition. Size the arena from `GetAllocatedBytesUVE()`
/// peaks observed in practice.
///
/// Thread-safety: none, like every other concrete allocator here — wrap in
/// `ThreadSafeAllocatorUVE` for shared use.
class LinearArenaUVE final : public IAllocatorUVE {
public:
    explicit LinearArenaUVE(const std::size_t capacityBytes) : m_storage(capacityBytes) {
        UVE_ASSERT(capacityBytes > 0U);
    }

    [[nodiscard]] void* AllocateUVE(const std::size_t sizeBytes, const std::size_t alignment,
                                    const char* /*sourceFile*/, const int /*sourceLine*/) override {
        UVE_ASSERT(IsValidAlignmentUVE(alignment));
        const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(m_storage.data());
        const std::uintptr_t aligned = AlignUpUVE(base + m_offset, alignment);
        const std::size_t alignedOffset = static_cast<std::size_t>(aligned - base);
        UVE_ASSERT(alignedOffset + sizeBytes <= m_storage.size());
        if (alignedOffset + sizeBytes > m_storage.size()) {
            UVE_ERROR("LinearArenaUVE: arena exhausted ({} of {} bytes in use)", m_offset,
                      m_storage.size());
            throw std::bad_alloc{};
        }
        m_offset = alignedOffset + sizeBytes;
        return m_storage.data() + alignedOffset;
    }

    void DeallocateUVE(void* pointer) override {
        // No-op by design (see above) — but the pointer must be ours.
        const std::byte* const begin = m_storage.data();
        const std::byte* const end = begin + m_storage.size();
        const auto* const candidate = static_cast<const std::byte*>(pointer);
        UVE_ASSERT(pointer == nullptr || (candidate >= begin && candidate < end));
        static_cast<void>(candidate);
    }

    [[nodiscard]] std::size_t GetAllocatedBytesUVE() const noexcept override { return m_offset; }

    /// Frees every allocation at once. Runs no destructors (see above).
    void ResetUVE() noexcept { m_offset = 0U; }

    /// The fixed capacity in bytes.
    [[nodiscard]] std::size_t CapacityUVE() const noexcept { return m_storage.size(); }

private:
    std::vector<std::byte> m_storage;
    std::size_t m_offset = 0U;
};

} // namespace UVE::Memory
