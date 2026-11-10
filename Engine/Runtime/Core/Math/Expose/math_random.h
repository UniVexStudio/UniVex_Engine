// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>

namespace UVE::Math {

/// PCG32 (XSH-RR, 64-bit state): the engine-owned deterministic random source. Two generators
/// built from the same seed produce the same sequence on every platform — replays, particle
/// determinism (`ParticleEmissionUVE::seed` finally has a generator to consume it), and
/// procedural placement all hang off this guarantee, so the seeding and the first outputs are
/// pinned against the PCG reference vectors in the tests.
/// Not a security source: PCG is predictable by design. Anything needing unpredictability
/// (session tokens, save checksums against tampering) must not use this.
/// Thread-safety: none — one generator per thread, or externally synchronize.
class RandomUVE final {
public:
    /// Builds a generator from `seed` on the default stream. The stream selects one of 2^63
    /// independent sequences: two generators with the same seed but different streams never
    /// overlap, which is how parallel jobs each get a reproducible private sequence.
    explicit RandomUVE(const std::uint64_t seed,
                       const std::uint64_t stream = kDefaultStreamUVE) noexcept
        : m_state(0U), m_increment((stream << 1U) | 1U) {
        static_cast<void>(NextU32UVE());
        m_state += seed;
        static_cast<void>(NextU32UVE());
    }

    /// Next 32 bits of the sequence.
    [[nodiscard]] std::uint32_t NextU32UVE() noexcept {
        const std::uint64_t oldState = m_state;
        m_state = oldState * kMultiplierUVE + m_increment;
        const std::uint32_t xorShifted =
            static_cast<std::uint32_t>(((oldState >> 18U) ^ oldState) >> 27U);
        const std::uint32_t rotation = static_cast<std::uint32_t>(oldState >> 59U);
        return (xorShifted >> rotation) | (xorShifted << ((32U - rotation) & 31U));
    }

    /// Next 64 bits, drawn as two 32-bit outputs (high half first).
    [[nodiscard]] std::uint64_t NextU64UVE() noexcept {
        const std::uint64_t high = NextU32UVE();
        return (high << 32U) | NextU32UVE();
    }

    /// Uniform float in [0, 1): 24 random bits over 2^24, so every result is exactly
    /// representable and 1.0 is never returned.
    [[nodiscard]] float NextFloatUVE() noexcept {
        return static_cast<float>(NextU32UVE() >> 8U) * (1.0F / 16777216.0F);
    }

    /// Uniform float in [low, high). Caller ensures `low <= high`.
    [[nodiscard]] float RangeUVE(const float low, const float high) noexcept {
        return low + (high - low) * NextFloatUVE();
    }

    /// Uniform integer in [low, high) via rejection sampling — no modulo bias however small
    /// the range. Caller ensures `low < high`.
    [[nodiscard]] std::int32_t RangeUVE(const std::int32_t low, const std::int32_t high) noexcept {
        const std::uint32_t bound = static_cast<std::uint32_t>(high - low);
        const std::uint32_t threshold = (0U - bound) % bound;
        std::uint32_t drawn = 0U;
        do {
            drawn = NextU32UVE();
        } while (drawn < threshold);
        return low + static_cast<std::int32_t>(drawn % bound);
    }

    /// True with probability `probability` (caller ensures [0, 1]).
    [[nodiscard]] bool ChanceUVE(const float probability) noexcept {
        return NextFloatUVE() < probability;
    }

private:
    static constexpr std::uint64_t kMultiplierUVE = 6364136223846793005ULL;
    static constexpr std::uint64_t kDefaultStreamUVE = 0U;

    std::uint64_t m_state;
    std::uint64_t m_increment;
};

} // namespace UVE::Math
