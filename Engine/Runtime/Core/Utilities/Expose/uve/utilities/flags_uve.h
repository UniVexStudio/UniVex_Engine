// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <type_traits>

namespace UVE::Utilities {

/// `FlagsUVE<TEnum>` is the enum-flags helper: a type-safe bit set over a scoped enum, replacing
/// the two hand-rolled patterns this codebase grew — raw-`uint32` flag fields (see
/// `TypeMetadataMethodUVE::flags`) and per-enum handwritten `operator|` + `Has*` pairs (see
/// `TypeMetadataPropertyFlagsUVE`'s old operators, now deleted).
///
/// A single enumerator converts implicitly, so declaration sites keep reading naturally
/// (`flags = MyFlagsUVE::ReadOnly;`) while combinations stay typed (`flags = MyFlagsUVE::A |
/// MyFlagsUVE::B;` is a `FlagsUVE`, not an int). Queries go through `HasUVE`/`HasAnyUVE`;
/// `BitsUVE()` exposes the raw underlying value at serialization boundaries only.
template <typename TEnum>
class FlagsUVE final {
public:
    using UnderlyingUVE = std::underlying_type_t<TEnum>;
    static_assert(std::is_enum_v<TEnum>, "FlagsUVE<TEnum> requires a scoped enum.");

    /// No bits set.
    constexpr FlagsUVE() noexcept = default;

    /// Implicit from one enumerator (see above). Deliberately non-explicit: implicit
    /// convertibility from the enumerator is the feature, not an oversight.
    constexpr FlagsUVE(const TEnum bit) noexcept : m_bits(static_cast<UnderlyingUVE>(bit)) {}

    /// True iff `bit` is set.
    [[nodiscard]] constexpr bool HasUVE(const TEnum bit) const noexcept {
        // Compared against a zero of the underlying type (not 0U): narrow underlying types
        // promote the operands to int, and an int-vs-unsigned compare trips -Wsign-compare.
        return (m_bits & static_cast<UnderlyingUVE>(bit)) != static_cast<UnderlyingUVE>(0);
    }

    /// True iff any bit of `other` is set here.
    [[nodiscard]] constexpr bool HasAnyUVE(const FlagsUVE other) const noexcept {
        return (m_bits & other.m_bits) != static_cast<UnderlyingUVE>(0);
    }

    /// True iff no bit is set.
    [[nodiscard]] constexpr bool NoneUVE() const noexcept {
        return m_bits == static_cast<UnderlyingUVE>(0);
    }

    /// The raw bits, for serialization boundaries only.
    [[nodiscard]] constexpr UnderlyingUVE BitsUVE() const noexcept { return m_bits; }

    /// Sets `bit`.
    constexpr void SetUVE(const TEnum bit) noexcept { m_bits |= static_cast<UnderlyingUVE>(bit); }

    /// Clears `bit`.
    constexpr void ClearUVE(const TEnum bit) noexcept { m_bits &= ~static_cast<UnderlyingUVE>(bit); }

    /// Clears every bit.
    constexpr void ClearAllUVE() noexcept { m_bits = static_cast<UnderlyingUVE>(0); }

    [[nodiscard]] friend constexpr bool operator==(const FlagsUVE& lhs,
                                                   const FlagsUVE& rhs) noexcept {
        return lhs.m_bits == rhs.m_bits;
    }

    [[nodiscard]] friend constexpr bool operator!=(const FlagsUVE& lhs,
                                                   const FlagsUVE& rhs) noexcept {
        return !(lhs == rhs);
    }

    [[nodiscard]] friend constexpr FlagsUVE operator|(const FlagsUVE& lhs,
                                                      const FlagsUVE& rhs) noexcept {
        return FromBitsUVE(static_cast<UnderlyingUVE>(lhs.m_bits | rhs.m_bits));
    }

    [[nodiscard]] friend constexpr FlagsUVE operator&(const FlagsUVE& lhs,
                                                      const FlagsUVE& rhs) noexcept {
        return FromBitsUVE(static_cast<UnderlyingUVE>(lhs.m_bits & rhs.m_bits));
    }

    [[nodiscard]] friend constexpr FlagsUVE operator^(const FlagsUVE& lhs,
                                                      const FlagsUVE& rhs) noexcept {
        return FromBitsUVE(static_cast<UnderlyingUVE>(lhs.m_bits ^ rhs.m_bits));
    }

    [[nodiscard]] friend constexpr FlagsUVE operator~(const FlagsUVE& value) noexcept {
        return FromBitsUVE(static_cast<UnderlyingUVE>(~value.m_bits));
    }

    constexpr FlagsUVE& operator|=(const FlagsUVE& other) noexcept {
        m_bits |= other.m_bits;
        return *this;
    }

    constexpr FlagsUVE& operator&=(const FlagsUVE& other) noexcept {
        m_bits &= other.m_bits;
        return *this;
    }

    constexpr FlagsUVE& operator^=(const FlagsUVE& other) noexcept {
        m_bits ^= other.m_bits;
        return *this;
    }

private:
    [[nodiscard]] static constexpr FlagsUVE FromBitsUVE(const UnderlyingUVE bits) noexcept {
        FlagsUVE flags;
        flags.m_bits = bits;
        return flags;
    }

    UnderlyingUVE m_bits = static_cast<UnderlyingUVE>(0);
};

/// `EnumeratorA | EnumeratorB` combines without naming `FlagsUVE` at the call site.
template <typename TEnum>
[[nodiscard]] constexpr FlagsUVE<TEnum> operator|(const TEnum lhs, const TEnum rhs) noexcept {
    static_assert(std::is_enum_v<TEnum>, "FlagsUVE | requires a scoped enum.");
    return FlagsUVE<TEnum>{lhs} | FlagsUVE<TEnum>{rhs};
}

} // namespace UVE::Utilities
