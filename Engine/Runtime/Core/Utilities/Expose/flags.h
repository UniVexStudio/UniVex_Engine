// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UVE::Utilities {

/// `Flags<TEnum>` is the enum-flags helper: a type-safe bit set over a scoped enum, replacing
/// the two hand-rolled patterns this codebase grew — raw-`uint32` flag fields (see
/// `TypeMetadataMethod::flags`) and per-enum handwritten `operator|` + `Has*` pairs (see
/// `TypeMetadataPropertyFlags`'s old operators, now deleted).
///
/// A single enumerator converts implicitly, so declaration sites keep reading naturally
/// (`flags = MyFlags::ReadOnly;`) while combinations stay typed (`flags = MyFlags::A |
/// MyFlags::B;` is a `Flags`, not an int). Queries go through `Has`/`HasAny`;
/// `Bits()` exposes the raw underlying value at serialization boundaries only.
template <typename TEnum>
class Flags final {
public:
    using Underlying = std::underlying_type_t<TEnum>;
    static_assert(std::is_enum_v<TEnum>, "Flags<TEnum> requires a scoped enum.");

    /// No bits set.
    constexpr Flags() noexcept = default;

    /// Implicit from one enumerator (see above). Deliberately non-explicit: implicit
    /// convertibility from the enumerator is the feature, not an oversight.
    constexpr Flags(const TEnum bit) noexcept : m_bits(static_cast<Underlying>(bit)) {}

    /// True iff `bit` is set.
    [[nodiscard]] constexpr bool Has(const TEnum bit) const noexcept {
        // Compared against a zero of the underlying type (not 0U): narrow underlying types
        // promote the operands to int, and an int-vs-unsigned compare trips -Wsign-compare.
        return (m_bits & static_cast<Underlying>(bit)) != static_cast<Underlying>(0);
    }

    /// True iff any bit of `other` is set here.
    [[nodiscard]] constexpr bool HasAny(const Flags other) const noexcept {
        return (m_bits & other.m_bits) != static_cast<Underlying>(0);
    }

    /// True iff no bit is set.
    [[nodiscard]] constexpr bool None() const noexcept {
        return m_bits == static_cast<Underlying>(0);
    }

    /// The raw bits, for serialization boundaries only.
    [[nodiscard]] constexpr Underlying Bits() const noexcept { return m_bits; }

    /// Sets `bit`.
    constexpr void Set(const TEnum bit) noexcept { m_bits |= static_cast<Underlying>(bit); }

    /// Clears `bit`.
    constexpr void Clear(const TEnum bit) noexcept { m_bits &= ~static_cast<Underlying>(bit); }

    /// Clears every bit.
    constexpr void ClearAll() noexcept { m_bits = static_cast<Underlying>(0); }

    [[nodiscard]] friend constexpr bool operator==(const Flags& lhs,
                                                   const Flags& rhs) noexcept {
        return lhs.m_bits == rhs.m_bits;
    }

    [[nodiscard]] friend constexpr bool operator!=(const Flags& lhs,
                                                   const Flags& rhs) noexcept {
        return !(lhs == rhs);
    }

    [[nodiscard]] friend constexpr Flags operator|(const Flags& lhs,
                                                      const Flags& rhs) noexcept {
        return FromBits(static_cast<Underlying>(lhs.m_bits | rhs.m_bits));
    }

    [[nodiscard]] friend constexpr Flags operator&(const Flags& lhs,
                                                      const Flags& rhs) noexcept {
        return FromBits(static_cast<Underlying>(lhs.m_bits & rhs.m_bits));
    }

    [[nodiscard]] friend constexpr Flags operator^(const Flags& lhs,
                                                      const Flags& rhs) noexcept {
        return FromBits(static_cast<Underlying>(lhs.m_bits ^ rhs.m_bits));
    }

    [[nodiscard]] friend constexpr Flags operator~(const Flags& value) noexcept {
        return FromBits(static_cast<Underlying>(~value.m_bits));
    }

    constexpr Flags& operator|=(const Flags& other) noexcept {
        m_bits |= other.m_bits;
        return *this;
    }

    constexpr Flags& operator&=(const Flags& other) noexcept {
        m_bits &= other.m_bits;
        return *this;
    }

    constexpr Flags& operator^=(const Flags& other) noexcept {
        m_bits ^= other.m_bits;
        return *this;
    }

private:
    [[nodiscard]] static constexpr Flags FromBits(const Underlying bits) noexcept {
        Flags flags;
        flags.m_bits = bits;
        return flags;
    }

    Underlying m_bits = static_cast<Underlying>(0);
};

/// `EnumeratorA | EnumeratorB` combines without naming `Flags` at the call site.
template <typename TEnum>
[[nodiscard]] constexpr Flags<TEnum> operator|(const TEnum lhs, const TEnum rhs) noexcept {
    static_assert(std::is_enum_v<TEnum>, "Flags | requires a scoped enum.");
    return Flags<TEnum>{lhs} | Flags<TEnum>{rhs};
}

} // namespace UVE::Utilities
