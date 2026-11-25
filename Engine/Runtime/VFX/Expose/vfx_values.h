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

inline constexpr std::uint32_t kInvalidVfxParameterIndex = std::numeric_limits<std::uint32_t>::max();
inline constexpr std::size_t kVfxLookupTableSize = 256U;

/// Baked 256-entry lookup table for normalized-age curve sampling in tight update loops.
struct VfxCurveTable final {
    std::array<float, kVfxLookupTableSize> samples{};
    [[nodiscard]] float Sample(float t) const noexcept;
};

/// Single RGBA sample stored in linear color space with separate alpha.
struct VfxLinearColorAlpha final {
    Math::Color color{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    [[nodiscard]] bool operator==(const VfxLinearColorAlpha&) const = default;
};

/// Baked 256-entry lookup table for normalized-age gradient sampling in linear color space.
struct VfxGradientTable final {
    std::array<VfxLinearColorAlpha, kVfxLookupTableSize> samples{};
    [[nodiscard]] VfxLinearColorAlpha Sample(float t) const noexcept;
};

/// Exact evaluation of a float curve (cubic Hermite with tangents when `smooth`, piecewise linear otherwise),
/// clamped outside [first.time, last.time].
[[nodiscard]] float EvaluateVfxCurveExact(const Asset::ParticleCurve& curve, float t) noexcept;

/// Bakes `curve * scale` over [0, 1] into a 256-entry lookup table.
[[nodiscard]] VfxCurveTable BakeVfxCurveTable(const Asset::ParticleCurve& curve, float scale = 1.0F) noexcept;

/// Exact evaluation of a color gradient in linear RGB color space with separate linear alpha.
[[nodiscard]] VfxLinearColorAlpha EvaluateVfxGradientExact(
    const Asset::ParticleGradient& gradient, float t) noexcept;

/// Bakes `gradient` over [0, 1] into a 256-entry lookup table.
[[nodiscard]] VfxGradientTable BakeVfxGradientTable(const Asset::ParticleGradient& gradient) noexcept;

/// Deterministic 32-bit hash combining an emitter seed and per-particle spawn index.
[[nodiscard]] std::uint32_t HashVfxSeed(std::uint32_t emitterSeed, std::uint32_t particleId) noexcept;

/// Runtime parameter storage slot resolved from `ParticleUserParameter`.
struct VfxParameterValue final {
    Asset::ParticleUserParameterType type = Asset::ParticleUserParameterType::Float;
    Math::Vector3 vector{};
    float alpha = 1.0F;
};

/// Pre-resolved float input referencing baked tables / parameter indices instead of asset maps.
struct VfxCompiledFloatInput final {
    Asset::ParticleValueKind kind = Asset::ParticleValueKind::Constant;
    float constant = 0.0F;
    float min = 0.0F;
    float max = 0.0F;
    float scale = 1.0F;
    std::uint32_t curveTableIndex = 0U;
    std::uint32_t parameterIndex = kInvalidVfxParameterIndex;
    std::uint32_t wireNode = 0U;
};

/// Pre-resolved vector input referencing parameter indices instead of asset maps.
struct VfxCompiledVectorInput final {
    Asset::ParticleValueKind kind = Asset::ParticleValueKind::Constant;
    Math::Vector3 constant{};
    Math::Vector3 min{};
    Math::Vector3 max{};
    std::uint32_t parameterIndex = kInvalidVfxParameterIndex;
    std::uint32_t wireNode = 0U;
};

/// Pre-resolved color input referencing baked gradient tables / parameter indices.
struct VfxCompiledColorInput final {
    Asset::ParticleValueKind kind = Asset::ParticleValueKind::Constant;
    Math::Color constant{1.0F, 1.0F, 1.0F};
    float alpha = 1.0F;
    Math::Color minColor{1.0F, 1.0F, 1.0F};
    Math::Color maxColor{1.0F, 1.0F, 1.0F};
    std::uint32_t gradientTableIndex = 0U;
    std::uint32_t parameterIndex = kInvalidVfxParameterIndex;
};

/// Evaluates a compiled float input at normalized age `t`.
[[nodiscard]] float EvaluateCompiledFloatInput(
    const VfxCompiledFloatInput& input,
    float normalizedAge,
    Math::Random* rng,
    const std::vector<VfxCurveTable>& curveTables,
    const std::vector<VfxParameterValue>& parameters) noexcept;

/// Evaluates a compiled vector input.
[[nodiscard]] Math::Vector3 EvaluateCompiledVectorInput(
    const VfxCompiledVectorInput& input,
    Math::Random* rng,
    const std::vector<VfxParameterValue>& parameters) noexcept;

/// Evaluates a compiled color input at normalized age `t`.
[[nodiscard]] VfxLinearColorAlpha EvaluateCompiledColorInput(
    const VfxCompiledColorInput& input,
    float normalizedAge,
    Math::Random* rng,
    const std::vector<VfxGradientTable>& gradientTables,
    const std::vector<VfxParameterValue>& parameters) noexcept;

} // namespace UVE::VFX
