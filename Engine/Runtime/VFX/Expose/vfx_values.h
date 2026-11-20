// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "color.h"
#include "math_random.h"
#include "particle_system_asset.h"
#include "vector3.h"

namespace UVE::VFX {

inline constexpr std::uint32_t kInvalidVfxParameterIndexUVE = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t kVfxLookupTableSizeUVE = 256U;

/// Baked 256-entry lookup table for normalized-age curve sampling in tight update loops.
struct VfxCurveTableUVE final {
    std::array<float, kVfxLookupTableSizeUVE> samples{};
    [[nodiscard]] float SampleUVE(float t) const noexcept;
};

/// Single RGBA sample stored in linear color space with separate alpha.
struct VfxLinearColorAlphaUVE final {
    Math::ColorUVE color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    [[nodiscard]] bool operator==(const VfxLinearColorAlphaUVE&) const = default;
};

/// Baked 256-entry lookup table for normalized-age gradient sampling in linear color space.
struct VfxGradientTableUVE final {
    std::array<VfxLinearColorAlphaUVE, kVfxLookupTableSizeUVE> samples{};
    [[nodiscard]] VfxLinearColorAlphaUVE SampleUVE(float t) const noexcept;
};

/// Exact evaluation of a float curve (cubic Hermite with tangents when `smooth`, piecewise linear otherwise),
/// clamped outside [first.time, last.time].
[[nodiscard]] float EvaluateVfxCurveExactUVE(const Asset::ParticleCurveUVE& curve, float t) noexcept;

/// Bakes `curve * scale` over [0, 1] into a 256-entry lookup table.
[[nodiscard]] VfxCurveTableUVE BakeVfxCurveTableUVE(const Asset::ParticleCurveUVE& curve, float scale = 1.0F) noexcept;

/// Exact evaluation of a color gradient in linear RGB color space with separate linear alpha.
[[nodiscard]] VfxLinearColorAlphaUVE EvaluateVfxGradientExactUVE(
    const Asset::ParticleGradientUVE& gradient, float t) noexcept;

/// Bakes `gradient` over [0, 1] into a 256-entry lookup table.
[[nodiscard]] VfxGradientTableUVE BakeVfxGradientTableUVE(const Asset::ParticleGradientUVE& gradient) noexcept;

/// Deterministic 32-bit hash combining an emitter seed and per-particle spawn index.
[[nodiscard]] std::uint32_t HashVfxSeedUVE(std::uint32_t emitterSeed, std::uint32_t particleId) noexcept;

/// Runtime parameter storage slot resolved from `ParticleUserParameterUVE`.
struct VfxParameterValueUVE final {
    Asset::ParticleUserParameterTypeUVE type = Asset::ParticleUserParameterTypeUVE::Float;
    Math::Vector3UVE vector{};
    float alpha = 1.0F;
};

/// Pre-resolved float input referencing baked tables / parameter indices instead of asset maps.
struct VfxCompiledFloatInputUVE final {
    Asset::ParticleValueKindUVE kind = Asset::ParticleValueKindUVE::Constant;
    float constant = 0.0F;
    float min = 0.0F;
    float max = 0.0F;
    float scale = 1.0F;
    std::uint32_t curveTableIndex = 0U;
    std::uint32_t parameterIndex = kInvalidVfxParameterIndexUVE;
    std::uint32_t wireNode = 0U;
};

/// Pre-resolved vector input referencing parameter indices instead of asset maps.
struct VfxCompiledVectorInputUVE final {
    Asset::ParticleValueKindUVE kind = Asset::ParticleValueKindUVE::Constant;
    Math::Vector3UVE constant{};
    Math::Vector3UVE min{};
    Math::Vector3UVE max{};
    std::uint32_t parameterIndex = kInvalidVfxParameterIndexUVE;
    std::uint32_t wireNode = 0U;
};

/// Pre-resolved color input referencing baked gradient tables / parameter indices.
struct VfxCompiledColorInputUVE final {
    Asset::ParticleValueKindUVE kind = Asset::ParticleValueKindUVE::Constant;
    Math::ColorUVE constant{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    Math::ColorUVE minColor{1.0F, 1.0F, 1.0F};
    Math::ColorUVE maxColor{1.0F, 1.0F, 1.0F};
    std::uint32_t gradientTableIndex = 0U;
    std::uint32_t parameterIndex = kInvalidVfxParameterIndexUVE;
};

/// Evaluates a compiled float input at normalized age `t`.
[[nodiscard]] float EvaluateCompiledFloatInputUVE(
    const VfxCompiledFloatInputUVE& input,
    float normalizedAge,
    Math::RandomUVE* rng,
    const std::vector<VfxCurveTableUVE>& curveTables,
    const std::vector<VfxParameterValueUVE>& parameters) noexcept;

/// Evaluates a compiled vector input.
[[nodiscard]] Math::Vector3UVE EvaluateCompiledVectorInputUVE(
    const VfxCompiledVectorInputUVE& input,
    Math::RandomUVE* rng,
    const std::vector<VfxParameterValueUVE>& parameters) noexcept;

/// Evaluates a compiled color input at normalized age `t`.
[[nodiscard]] VfxLinearColorAlphaUVE EvaluateCompiledColorInputUVE(
    const VfxCompiledColorInputUVE& input,
    float normalizedAge,
    Math::RandomUVE* rng,
    const std::vector<VfxGradientTableUVE>& gradientTables,
    const std::vector<VfxParameterValueUVE>& parameters) noexcept;

} // namespace UVE::VFX
