// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <functional>

#include "engine_assert.h"

namespace UVE::Audio {

/// Opaque handle to an audio voice created via IAudioDevice::CreateVoice() (and, one level
/// up, IAudioSystem::CreateSource() — the two layers deliberately share this one handle
/// type rather than each minting their own, since AudioSystem's voices and the underlying
/// device's voices are always 1:1). Mirrors Render::BufferHandle's "small wrapper struct, not
/// a bare uint32_t" precedent.
/// Thread-safety: value type; safe to copy/compare/hash freely, no shared state.
struct VoiceHandle {
    std::uint32_t value = 0;

    explicit constexpr VoiceHandle(std::uint32_t valueIn = 0U) noexcept : value(valueIn) {}

    /// Generational packing for `Containers::HandleTable`, which backs both audio devices’
    /// voice tables: the low 20 bits are the slot index, the high 12 bits the generation. One
    /// million simultaneously-live voices is unreachable while 4096 same-slot reuses is the
    /// wrap horizon — the split favors the realistic axis. Value 0 (`{index 0, generation 0}`)
    /// stays the invalid handle: the table never mints generation 0 (see `NextGeneration`).
    [[nodiscard]] static VoiceHandle FromIndexAndGeneration(std::uint32_t index,
                                                                 std::uint32_t generation) {
        UVE_ASSERT(index <= kIndexMask);
        UVE_ASSERT(generation <= kGenerationMask);
        return VoiceHandle{(generation << kIndexBits) | index};
    }

    [[nodiscard]] constexpr std::uint32_t Index() const noexcept {
        return value & kIndexMask;
    }

    [[nodiscard]] constexpr std::uint32_t Generation() const noexcept {
        return value >> kIndexBits;
    }

    [[nodiscard]] static constexpr std::uint32_t NextGeneration(std::uint32_t current) noexcept {
        const std::uint32_t next = (current + 1U) & kGenerationMask;
        return next == 0U ? 1U : next;
    }

private:
    static constexpr std::uint32_t kIndexBits = 20U;
    static constexpr std::uint32_t kIndexMask = 0x000FFFFFU;
    static constexpr std::uint32_t kGenerationMask = 0x00000FFFU;
};

/// The sentinel "no voice" value. Never returned by a successful CreateVoice()/CreateSource()
/// call.
inline constexpr VoiceHandle kInvalidVoiceHandle{};

[[nodiscard]] constexpr bool operator==(const VoiceHandle& lhs, const VoiceHandle& rhs) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator!=(const VoiceHandle& lhs, const VoiceHandle& rhs) noexcept {
    return !(lhs == rhs);
}

} // namespace UVE::Audio

template <>
struct std::hash<UVE::Audio::VoiceHandle> {
    [[nodiscard]] std::size_t operator()(const UVE::Audio::VoiceHandle& handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.value);
    }
};
