// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_values.h"

#include <algorithm>
#include <cmath>

#include "math_random.h"
#include "scalar.h"

namespace UVE::VFX {

float VfxCurveTable::Sample(const float t) const noexcept {
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    const float scaled = clamped * static_cast<float>(kVfxLookupTableSize - 1U);
    const std::size_t index = std::min(static_cast<std::size_t>(scaled), kVfxLookupTableSize - 2U);
    const float fraction = scaled - static_cast<float>(index);
    return Math::Lerp(samples[index], samples[index + 1U], fraction);
}

VfxLinearColorAlpha VfxGradientTable::Sample(const float t) const noexcept {
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    const float scaled = clamped * static_cast<float>(kVfxLookupTableSize - 1U);
    const std::size_t index = std::min(static_cast<std::size_t>(scaled), kVfxLookupTableSize - 2U);
    const float fraction = scaled - static_cast<float>(index);
    const VfxLinearColorAlpha& a = samples[index];
    const VfxLinearColorAlpha& b = samples[index + 1U];
    return VfxLinearColorAlpha{
        Math::Color{
            Math::Lerp(a.color.r, b.color.r, fraction),
            Math::Lerp(a.color.g, b.color.g, fraction),
            Math::Lerp(a.color.b, b.color.b, fraction),
        },
        Math::Lerp(a.alpha, b.alpha, fraction),
    };
}

float EvaluateVfxCurveExact(const Asset::ParticleCurve& curve, const float t) noexcept {
    if (curve.keys.empty()) {
        return 0.0F;
    }
    if (curve.keys.size() == 1U || t <= curve.keys.front().time) {
        return curve.keys.front().value;
    }
    if (t >= curve.keys.back().time) {
        return curve.keys.back().value;
    }

    for (std::size_t index = 1U; index < curve.keys.size(); ++index) {
        const Asset::ParticleCurveKey& k1 = curve.keys[index];
        if (t <= k1.time) {
            const Asset::ParticleCurveKey& k0 = curve.keys[index - 1U];
            const float dt = k1.time - k0.time;
            if (dt <= 1e-7F) {
                return k1.value;
            }
            const float u = std::clamp((t - k0.time) / dt, 0.0F, 1.0F);
            if (!curve.smooth) {
                return Math::Lerp(k0.value, k1.value, u);
            }
            const float u2 = u * u;
            const float u3 = u2 * u;
            const float h00 = 2.0F * u3 - 3.0F * u2 + 1.0F;
            const float h10 = u3 - 2.0F * u2 + u;
            const float h01 = -2.0F * u3 + 3.0F * u2;
            const float h11 = u3 - u2;
            return h00 * k0.value + h10 * (k0.outTangent * dt) + h01 * k1.value + h11 * (k1.inTangent * dt);
        }
    }
    return curve.keys.back().value;
}

VfxCurveTable BakeVfxCurveTable(const Asset::ParticleCurve& curve, const float scale) noexcept {
    VfxCurveTable table;
    for (std::size_t index = 0U; index < kVfxLookupTableSize; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kVfxLookupTableSize - 1U);
        table.samples[index] = EvaluateVfxCurveExact(curve, t) * scale;
    }
    return table;
}

VfxLinearColorAlpha EvaluateVfxGradientExact(const Asset::ParticleGradient& gradient,
                                                   const float t) noexcept {
    if (gradient.stops.empty()) {
        return VfxLinearColorAlpha{{1.0F, 1.0F, 1.0F}, 1.0F};
    }
    if (gradient.stops.size() == 1U || t <= gradient.stops.front().time) {
        return VfxLinearColorAlpha{gradient.stops.front().color, gradient.stops.front().alpha};
    }
    if (t >= gradient.stops.back().time) {
        return VfxLinearColorAlpha{gradient.stops.back().color, gradient.stops.back().alpha};
    }

    for (std::size_t index = 1U; index < gradient.stops.size(); ++index) {
        const Asset::ParticleGradientStop& s1 = gradient.stops[index];
        if (t <= s1.time) {
            const Asset::ParticleGradientStop& s0 = gradient.stops[index - 1U];
            const float dt = s1.time - s0.time;
            if (dt <= 1e-7F) {
                return VfxLinearColorAlpha{s1.color, s1.alpha};
            }
            const float u = std::clamp((t - s0.time) / dt, 0.0F, 1.0F);
            return VfxLinearColorAlpha{
                Math::Color{
                    Math::Lerp(s0.color.r, s1.color.r, u),
                    Math::Lerp(s0.color.g, s1.color.g, u),
                    Math::Lerp(s0.color.b, s1.color.b, u),
                },
                Math::Lerp(s0.alpha, s1.alpha, u),
            };
        }
    }
    return VfxLinearColorAlpha{gradient.stops.back().color, gradient.stops.back().alpha};
}

VfxGradientTable BakeVfxGradientTable(const Asset::ParticleGradient& gradient) noexcept {
    VfxGradientTable table;
    for (std::size_t index = 0U; index < kVfxLookupTableSize; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kVfxLookupTableSize - 1U);
        table.samples[index] = EvaluateVfxGradientExact(gradient, t);
    }
    return table;
}

std::uint32_t HashVfxSeed(const std::uint32_t emitterSeed, const std::uint32_t particleId) noexcept {
    std::uint32_t h = emitterSeed * 0x9E3779B1U + 0x85EBCA77U;
    h ^= particleId + 0xC2B2AE3DU + (h << 6U) + (h >> 2U);
    h ^= h >> 16U;
    h *= 0x7FEB352DU;
    h ^= h >> 15U;
    h *= 0x846CA68BU;
    h ^= h >> 16U;
    return h;
}

float EvaluateCompiledFloatInput(const VfxCompiledFloatInput& input,
                                    const float normalizedAge,
                                    Math::Random* const rng,
                                    const std::vector<VfxCurveTable>& curveTables,
                                    const std::vector<VfxParameterValue>& parameters) noexcept {
    switch (input.kind) {
        case Asset::ParticleValueKind::Constant:
            return input.constant;
        case Asset::ParticleValueKind::RandomRange:
            if (rng == nullptr) {
                return 0.5F * (input.min + input.max);
            }
            return rng->Range(input.min, input.max);
        case Asset::ParticleValueKind::Curve:
            if (input.curveTableIndex < curveTables.size()) {
                return curveTables[input.curveTableIndex].Sample(normalizedAge);
            }
            return 0.0F;
        case Asset::ParticleValueKind::UserParameter:
            if (input.parameterIndex < parameters.size()) {
                return parameters[input.parameterIndex].vector.x * input.scale;
            }
            return input.constant;
        case Asset::ParticleValueKind::Wire:
        case Asset::ParticleValueKind::Gradient:
            break;
    }
    return input.constant;
}

Math::Vector3 EvaluateCompiledVectorInput(const VfxCompiledVectorInput& input,
                                              Math::Random* const rng,
                                              const std::vector<VfxParameterValue>& parameters) noexcept {
    switch (input.kind) {
        case Asset::ParticleValueKind::Constant:
            return input.constant;
        case Asset::ParticleValueKind::RandomRange:
            if (rng == nullptr) {
                return (input.min + input.max) * 0.5F;
            }
            return Math::Vector3{
                rng->Range(input.min.x, input.max.x),
                rng->Range(input.min.y, input.max.y),
                rng->Range(input.min.z, input.max.z),
            };
        case Asset::ParticleValueKind::UserParameter:
            if (input.parameterIndex < parameters.size()) {
                return parameters[input.parameterIndex].vector;
            }
            return input.constant;
        case Asset::ParticleValueKind::Curve:
        case Asset::ParticleValueKind::Gradient:
        case Asset::ParticleValueKind::Wire:
            break;
    }
    return input.constant;
}

VfxLinearColorAlpha EvaluateCompiledColorInput(const VfxCompiledColorInput& input,
                                                     const float normalizedAge,
                                                     Math::Random* const rng,
                                                     const std::vector<VfxGradientTable>& gradientTables,
                                                     const std::vector<VfxParameterValue>& parameters) noexcept {
    switch (input.kind) {
        case Asset::ParticleValueKind::Constant:
            return VfxLinearColorAlpha{input.constant, input.alpha};
        case Asset::ParticleValueKind::RandomRange: {
            const float u = rng != nullptr ? rng->NextFloat() : 0.5F;
            return VfxLinearColorAlpha{
                Math::Color{
                    Math::Lerp(input.minColor.r, input.maxColor.r, u),
                    Math::Lerp(input.minColor.g, input.maxColor.g, u),
                    Math::Lerp(input.minColor.b, input.maxColor.b, u),
                },
                input.alpha,
            };
        }
        case Asset::ParticleValueKind::Gradient:
            if (input.gradientTableIndex < gradientTables.size()) {
                return gradientTables[input.gradientTableIndex].Sample(normalizedAge);
            }
            return VfxLinearColorAlpha{{1.0F, 1.0F, 1.0F}, 1.0F};
        case Asset::ParticleValueKind::UserParameter:
            if (input.parameterIndex < parameters.size()) {
                const VfxParameterValue& param = parameters[input.parameterIndex];
                return VfxLinearColorAlpha{
                    Math::Color{param.vector.x, param.vector.y, param.vector.z},
                    param.alpha,
                };
            }
            return VfxLinearColorAlpha{input.constant, input.alpha};
        case Asset::ParticleValueKind::Curve:
        case Asset::ParticleValueKind::Wire:
            break;
    }
    return VfxLinearColorAlpha{input.constant, input.alpha};
}

} // namespace UVE::VFX
