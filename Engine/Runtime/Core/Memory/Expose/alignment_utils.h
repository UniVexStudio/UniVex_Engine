// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>

namespace UVE::Memory {

/// Returns true iff `alignment` is a valid allocation alignment: non-zero and a power of two.
/// Every allocator's Allocate() (HeapAllocator/PoolAllocator/StackAllocator) opens
/// with `UVE_ASSERT(IsValidAlignment(alignment));`; PoolAllocator's constructor applies
/// the same check to its fixed `blockAlignment` up front.
/// Rounds `address` up to the next multiple of `alignment`. Caller ensures
/// `IsValidAlignment(alignment)` — the mask derivation is only correct for powers of two.
[[nodiscard]] constexpr std::uintptr_t AlignUp(const std::uintptr_t address,
                                                  const std::size_t alignment) noexcept {
    const std::uintptr_t mask = static_cast<std::uintptr_t>(alignment) - 1U;
    return (address + mask) & ~mask;
}

[[nodiscard]] constexpr bool IsValidAlignment(std::size_t alignment) noexcept {
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

} // namespace UVE::Memory
