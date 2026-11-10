// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>

namespace UVE::Math {

/// Deterministic 2D noise: value noise plus Perlin-style gradient noise, both pure functions of
/// integer lattice + seed — no tables, no stored permutation, the same inputs produce the same
/// outputs on every platform. Procedural placement and terrain-height sampling hang off this
/// file; fractal octaves are the caller's loop over it (sample at doubled frequency, halve the
/// amplitude), not a second API.
namespace Detail {

/// One 32-bit hash from lattice coordinates and seed: an xxhash-style avalanche over the mixed
/// inputs. The lattice coordinates are offset-xored rather than added so negative coordinates
/// hash distinctly from positive ones.
[[nodiscard]] inline std::uint32_t HashLatticeUVE(const std::int32_t x, const std::int32_t y,
                                                  const std::uint32_t seed) noexcept {
    std::uint32_t hash = seed + 0x9E3779B9U + static_cast<std::uint32_t>(x) * 0x85EBCA6BU;
    hash ^= static_cast<std::uint32_t>(y) * 0xC2B2AE35U + 0x9E3779B9U + (hash << 6U) + (hash >> 2U);
    hash ^= hash >> 16U;
    hash *= 0x7FEB352DU;
    hash ^= hash >> 15U;
    hash *= 0x846CA68BU;
    hash ^= hash >> 16U;
    return hash;
}

/// Perlin's smootherstep fade: zero first and second derivatives at the lattice points, which is
/// what keeps gradient noise free of grid-aligned creases.
[[nodiscard]] inline float FadeUVE(const float t) noexcept {
    return t * t * t * (t * (t * 6.0F - 15.0F) + 10.0F);
}

} // namespace Detail

/// Value noise at a lattice point, in [0, 1]: the lattice hash scaled to a unit float.
[[nodiscard]] inline float ValueNoise2DUVE(const std::int32_t x, const std::int32_t y,
                                           const std::uint32_t seed) noexcept {
    return static_cast<float>(Detail::HashLatticeUVE(x, y, seed)) * (1.0F / 4294967296.0F);
}

/// Smooth value noise at a real position, in [0, 1]: the four surrounding lattice values
/// interpolated with the smootherstep fade.
[[nodiscard]] inline float ValueNoise2DUVE(const float x, const float y, const std::uint32_t seed) noexcept {
    const std::int32_t x0 = static_cast<std::int32_t>(x >= 0.0F ? x : x - 1.0F);
    const std::int32_t y0 = static_cast<std::int32_t>(y >= 0.0F ? y : y - 1.0F);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float u = Detail::FadeUVE(fx);
    const float v = Detail::FadeUVE(fy);
    const float a = ValueNoise2DUVE(x0, y0, seed);
    const float b = ValueNoise2DUVE(x0 + 1, y0, seed);
    const float c = ValueNoise2DUVE(x0, y0 + 1, seed);
    const float d = ValueNoise2DUVE(x0 + 1, y0 + 1, seed);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}

/// Perlin-style gradient noise at a real position, roughly in [-1, 1]: each lattice corner
/// contributes a hashed unit-diagonal gradient dotted with the offset, faded together. The 8
/// gradients are the axis/diagonal set, so the range bound is √2/2 · √2 = 1 in the worst
/// alignment and smaller in practice; callers needing a hard bound scale by 1/√2.
[[nodiscard]] inline float GradientNoise2DUVE(const float x, const float y,
                                              const std::uint32_t seed) noexcept {
    const std::int32_t x0 = static_cast<std::int32_t>(x >= 0.0F ? x : x - 1.0F);
    const std::int32_t y0 = static_cast<std::int32_t>(y >= 0.0F ? y : y - 1.0F);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float u = Detail::FadeUVE(fx);
    const float v = Detail::FadeUVE(fy);
    // One gradient per corner: the hash's low 3 bits pick from (±1, ±1) and (±1, 0)/(0, ±1).
    float corners[4];
    const std::int32_t xs[2] = {x0, x0 + 1};
    const std::int32_t ys[2] = {y0, y0 + 1};
    for (std::int32_t cy = 0; cy < 2; ++cy) {
        for (std::int32_t cx = 0; cx < 2; ++cx) {
            const std::uint32_t pick = Detail::HashLatticeUVE(xs[cx], ys[cy], seed) & 7U;
            const float gx = (pick & 1U) != 0U ? 1.0F : -1.0F;
            const float gy = (pick & 2U) != 0U ? ((pick & 4U) != 0U ? 1.0F : -1.0F) : 0.0F;
            const float dx = fx - static_cast<float>(cx);
            const float dy = fy - static_cast<float>(cy);
            corners[static_cast<std::size_t>(cy * 2 + cx)] = gx * dx + gy * dy;
        }
    }
    const float top = corners[0] + (corners[1] - corners[0]) * u;
    const float bottom = corners[2] + (corners[3] - corners[2]) * u;
    return top + (bottom - top) * v;
}

} // namespace UVE::Math
