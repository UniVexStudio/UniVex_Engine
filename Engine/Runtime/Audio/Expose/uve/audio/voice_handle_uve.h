// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <functional>

#include "uve/logging/assert_uve.h"

namespace UVE::Audio {

/// Opaque handle to an audio voice created via IAudioDeviceUVE::CreateVoiceUVE() (and, one level
/// up, IAudioSystemUVE::CreateSourceUVE() — the two layers deliberately share this one handle
/// type rather than each minting their own, since AudioSystemUVE's voices and the underlying
/// device's voices are always 1:1). Mirrors Render::BufferHandleUVE's "small wrapper struct, not
/// a bare uint32_t" precedent.
/// Thread-safety: value type; safe to copy/compare/hash freely, no shared state.
struct VoiceHandleUVE {
    std::uint32_t value = 0;

    explicit constexpr VoiceHandleUVE(std::uint32_t valueIn = 0U) noexcept : value(valueIn) {}

    /// Generational packing for `Containers::HandleTableUVE`, which backs both audio devices’
    /// voice tables: the low 20 bits are the slot index, the high 12 bits the generation. One
    /// million simultaneously-live voices is unreachable while 4096 same-slot reuses is the
    /// wrap horizon — the split favors the realistic axis. Value 0 (`{index 0, generation 0}`)
    /// stays the invalid handle: the table never mints generation 0 (see `NextGenerationUVE`).
    [[nodiscard]] static VoiceHandleUVE FromIndexAndGenerationUVE(std::uint32_t index,
                                                                 std::uint32_t generation) {
        UVE_ASSERT(index <= kIndexMaskUVE);
        UVE_ASSERT(generation <= kGenerationMaskUVE);
        return VoiceHandleUVE{(generation << kIndexBitsUVE) | index};
    }

    [[nodiscard]] constexpr std::uint32_t IndexUVE() const noexcept {
        return value & kIndexMaskUVE;
    }

    [[nodiscard]] constexpr std::uint32_t GenerationUVE() const noexcept {
        return value >> kIndexBitsUVE;
    }

    [[nodiscard]] static constexpr std::uint32_t NextGenerationUVE(std::uint32_t current) noexcept {
        const std::uint32_t next = (current + 1U) & kGenerationMaskUVE;
        return next == 0U ? 1U : next;
    }

private:
    static constexpr std::uint32_t kIndexBitsUVE = 20U;
    static constexpr std::uint32_t kIndexMaskUVE = 0x000FFFFFU;
    static constexpr std::uint32_t kGenerationMaskUVE = 0x00000FFFU;
};

/// The sentinel "no voice" value. Never returned by a successful CreateVoiceUVE()/CreateSourceUVE()
/// call.
inline constexpr VoiceHandleUVE kInvalidVoiceHandleUVE{};

[[nodiscard]] constexpr bool operator==(const VoiceHandleUVE& lhs, const VoiceHandleUVE& rhs) noexcept {
    return lhs.value == rhs.value;
}

[[nodiscard]] constexpr bool operator!=(const VoiceHandleUVE& lhs, const VoiceHandleUVE& rhs) noexcept {
    return !(lhs == rhs);
}

} // namespace UVE::Audio

template <>
struct std::hash<UVE::Audio::VoiceHandleUVE> {
    [[nodiscard]] std::size_t operator()(const UVE::Audio::VoiceHandleUVE& handle) const noexcept {
        return std::hash<std::uint32_t>{}(handle.value);
    }
};
