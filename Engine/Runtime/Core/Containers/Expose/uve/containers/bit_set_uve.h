// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

#include "uve/logging/assert_uve.h"

namespace UVE::Containers {

/// `BitSetUVE<NumBits>` is a fixed-width bit set over `uint64_t` words: layer masks, archetype
/// signatures, feature flags. Every operation is word-parallel; spare bits in the last word are
/// always masked to zero, so two sets compare equal iff their live bits match.
///
/// Bounds contract: bit access asserts `bit < NumBits` in debug and is unchecked in release,
/// matching the module's convention. There is intentionally no `operator[]` proxy — `TestUVE` /
/// `SetUVE` name the direction, and proxies have bitten this codebase's style before (see the
/// `std::vector<bool>` avoidance notes in the container review).
template <std::size_t NumBits>
class BitSetUVE final {
public:
    static_assert(NumBits > 0U, "BitSetUVE with zero bits tests nothing; that is never what a "
                                "caller wants.");

    BitSetUVE() = default;

    /// Sets bit `bit`.
    void SetUVE(const std::size_t bit) {
        UVE_ASSERT(bit < NumBits);
        m_words[bit / 64U] |= (1ULL << (bit % 64U));
    }

    /// Sets bit `bit` to `value`.
    void SetUVE(const std::size_t bit, const bool value) {
        if (value) {
            SetUVE(bit);
        } else {
            ResetUVE(bit);
        }
    }

    /// Clears bit `bit`.
    void ResetUVE(const std::size_t bit) {
        UVE_ASSERT(bit < NumBits);
        m_words[bit / 64U] &= ~(1ULL << (bit % 64U));
    }

    /// Flips bit `bit`.
    void FlipUVE(const std::size_t bit) {
        UVE_ASSERT(bit < NumBits);
        m_words[bit / 64U] ^= (1ULL << (bit % 64U));
    }

    /// True iff bit `bit` is set.
    [[nodiscard]] bool TestUVE(const std::size_t bit) const {
        UVE_ASSERT(bit < NumBits);
        return (m_words[bit / 64U] & (1ULL << (bit % 64U))) != 0U;
    }

    /// Sets every live bit.
    void SetAllUVE() noexcept {
        for (std::uint64_t& word : m_words) {
            word = ~0ULL;
        }
        MaskSpareBitsUVE();
    }

    /// Clears every bit.
    void ResetAllUVE() noexcept {
        for (std::uint64_t& word : m_words) {
            word = 0U;
        }
    }

    /// True iff any live bit is set.
    [[nodiscard]] bool AnyUVE() const noexcept {
        for (const std::uint64_t word : m_words) {
            if (word != 0U) {
                return true;
            }
        }
        return false;
    }

    /// True iff no live bit is set.
    [[nodiscard]] bool NoneUVE() const noexcept { return !AnyUVE(); }

    /// True iff every live bit is set.
    [[nodiscard]] bool AllUVE() const noexcept {
        for (std::size_t word = 0U; word < m_words.size(); ++word) {
            std::uint64_t expect = ~0ULL;
            if (word == m_words.size() - 1U && (NumBits % 64U) != 0U) {
                expect = (1ULL << (NumBits % 64U)) - 1U;
            }
            if (m_words[word] != expect) {
                return false;
            }
        }
        return true;
    }

    /// Number of live bits set.
    [[nodiscard]] std::size_t CountUVE() const noexcept {
        std::size_t count = 0U;
        for (const std::uint64_t word : m_words) {
            count += static_cast<std::size_t>(std::popcount(word));
        }
        return count;
    }

    /// The bit width. A static, so callers can ask without an instance.
    [[nodiscard]] static constexpr std::size_t SizeUVE() noexcept { return NumBits; }

    [[nodiscard]] friend bool operator==(const BitSetUVE& lhs, const BitSetUVE& rhs) noexcept {
        return lhs.m_words == rhs.m_words;
    }

    [[nodiscard]] friend bool operator!=(const BitSetUVE& lhs, const BitSetUVE& rhs) noexcept {
        return !(lhs == rhs);
    }

    [[nodiscard]] friend BitSetUVE operator&(const BitSetUVE& lhs, const BitSetUVE& rhs) noexcept {
        BitSetUVE result;
        for (std::size_t word = 0U; word < result.m_words.size(); ++word) {
            result.m_words[word] = lhs.m_words[word] & rhs.m_words[word];
        }
        return result;
    }

    [[nodiscard]] friend BitSetUVE operator|(const BitSetUVE& lhs, const BitSetUVE& rhs) noexcept {
        BitSetUVE result;
        for (std::size_t word = 0U; word < result.m_words.size(); ++word) {
            result.m_words[word] = lhs.m_words[word] | rhs.m_words[word];
        }
        return result;
    }

    [[nodiscard]] friend BitSetUVE operator^(const BitSetUVE& lhs, const BitSetUVE& rhs) noexcept {
        BitSetUVE result;
        for (std::size_t word = 0U; word < result.m_words.size(); ++word) {
            result.m_words[word] = lhs.m_words[word] ^ rhs.m_words[word];
        }
        return result;
    }

    [[nodiscard]] friend BitSetUVE operator~(const BitSetUVE& value) noexcept {
        BitSetUVE result;
        for (std::size_t word = 0U; word < result.m_words.size(); ++word) {
            result.m_words[word] = ~value.m_words[word];
        }
        result.MaskSpareBitsUVE();
        return result;
    }

private:
    static constexpr std::size_t kWordCountUVE = (NumBits + 63U) / 64U;

    void MaskSpareBitsUVE() noexcept {
        if ((NumBits % 64U) != 0U) {
            m_words[kWordCountUVE - 1U] &= (1ULL << (NumBits % 64U)) - 1U;
        }
    }

    std::array<std::uint64_t, kWordCountUVE> m_words{};
};

} // namespace UVE::Containers
