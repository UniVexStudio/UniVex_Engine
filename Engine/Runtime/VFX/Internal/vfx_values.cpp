// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "vfx_values.h"

#include <algorithm>
#include <cmath>

#include "math_random.h"
#include "scalar.h"

namespace UVE::VFX {

float VfxCurveTableUVE::SampleUVE(const float t) const noexcept {
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    const float scaled = clamped * static_cast<float>(kVfxLookupTableSizeUVE - 1U);
    const std::size_t index = std::min(static_cast<std::size_t>(scaled), kVfxLookupTableSizeUVE - 2U);
    const float fraction = scaled - static_cast<float>(index);
    return Math::LerpUVE(samples[index], samples[index + 1U], fraction);
}

VfxLinearColorAlphaUVE VfxGradientTableUVE::SampleUVE(const float t) const noexcept {
    const float clamped = std::clamp(t, 0.0F, 1.0F);
    const float scaled = clamped * static_cast<float>(kVfxLookupTableSizeUVE - 1U);
    const std::size_t index = std::min(static_cast<std::size_t>(scaled), kVfxLookupTableSizeUVE - 2U);
    const float fraction = scaled - static_cast<float>(index);
    const VfxLinearColorAlphaUVE& a = samples[index];
    const VfxLinearColorAlphaUVE& b = samples[index + 1U];
    return VfxLinearColorAlphaUVE{
        Math::ColorUVE{
            Math::LerpUVE(a.color.r, b.color.r, fraction),
            Math::LerpUVE(a.color.g, b.color.g, fraction),
            Math::LerpUVE(a.color.b, b.color.b, fraction),
        },
        Math::LerpUVE(a.alpha, b.alpha, fraction),
    };
}

float EvaluateVfxCurveExactUVE(const Asset::ParticleCurveUVE& curve, const float t) noexcept {
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
        const Asset::ParticleCurveKeyUVE& k1 = curve.keys[index];
        if (t <= k1.time) {
            const Asset::ParticleCurveKeyUVE& k0 = curve.keys[index - 1U];
            const float dt = k1.time - k0.time;
            if (dt <= 1e-7F) {
                return k1.value;
            }
            const float u = std::clamp((t - k0.time) / dt, 0.0F, 1.0F);
            if (!curve.smooth) {
                return Math::LerpUVE(k0.value, k1.value, u);
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

VfxCurveTableUVE BakeVfxCurveTableUVE(const Asset::ParticleCurveUVE& curve, const float scale) noexcept {
    VfxCurveTableUVE table;
    for (std::size_t index = 0U; index < kVfxLookupTableSizeUVE; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kVfxLookupTableSizeUVE - 1U);
        table.samples[index] = EvaluateVfxCurveExactUVE(curve, t) * scale;
    }
    return table;
}

VfxLinearColorAlphaUVE EvaluateVfxGradientExactUVE(const Asset::ParticleGradientUVE& gradient,
                                                   const float t) noexcept {
    if (gradient.stops.empty()) {
        return VfxLinearColorAlphaUVE{{1.0F, 1.0F, 1.0F}, 1.0F};
    }
    if (gradient.stops.size() == 1U || t <= gradient.stops.front().time) {
        return VfxLinearColorAlphaUVE{gradient.stops.front().color, gradient.stops.front().alpha};
    }
    if (t >= gradient.stops.back().time) {
        return VfxLinearColorAlphaUVE{gradient.stops.back().color, gradient.stops.back().alpha};
    }

    for (std::size_t index = 1U; index < gradient.stops.size(); ++index) {
        const Asset::ParticleGradientStopUVE& s1 = gradient.stops[index];
        if (t <= s1.time) {
            const Asset::ParticleGradientStopUVE& s0 = gradient.stops[index - 1U];
            const float dt = s1.time - s0.time;
            if (dt <= 1e-7F) {
                return VfxLinearColorAlphaUVE{s1.color, s1.alpha};
            }
            const float u = std::clamp((t - s0.time) / dt, 0.0F, 1.0F);
            return VfxLinearColorAlphaUVE{
                Math::ColorUVE{
                    Math::LerpUVE(s0.color.r, s1.color.r, u),
                    Math::LerpUVE(s0.color.g, s1.color.g, u),
                    Math::LerpUVE(s0.color.b, s1.color.b, u),
                },
                Math::LerpUVE(s0.alpha, s1.alpha, u),
            };
        }
    }
    return VfxLinearColorAlphaUVE{gradient.stops.back().color, gradient.stops.back().alpha};
}

VfxGradientTableUVE BakeVfxGradientTableUVE(const Asset::ParticleGradientUVE& gradient) noexcept {
    VfxGradientTableUVE table;
    for (std::size_t index = 0U; index < kVfxLookupTableSizeUVE; ++index) {
        const float t = static_cast<float>(index) / static_cast<float>(kVfxLookupTableSizeUVE - 1U);
        table.samples[index] = EvaluateVfxGradientExactUVE(gradient, t);
    }
    return table;
}

std::uint32_t HashVfxSeedUVE(const std::uint32_t emitterSeed, const std::uint32_t particleId) noexcept {
    std::uint32_t h = emitterSeed * 0x9E3779B1U + 0x85EBCA77U;
    h ^= particleId + 0xC2B2AE3DU + (h << 6U) + (h >> 2U);
    h ^= h >> 16U;
    h *= 0x7FEB352DU;
    h ^= h >> 15U;
    h *= 0x846CA68BU;
    h ^= h >> 16U;
    return h;
}

float EvaluateCompiledFloatInputUVE(const VfxCompiledFloatInputUVE& input,
                                    const float normalizedAge,
                                    Math::RandomUVE* const rng,
                                    const std::vector<VfxCurveTableUVE>& curveTables,
                                    const std::vector<VfxParameterValueUVE>& parameters) noexcept {
    switch (input.kind) {
        case Asset::ParticleValueKindUVE::Constant:
            return input.constant;
        case Asset::ParticleValueKindUVE::RandomRange:
            if (rng == nullptr) {
                return 0.5F * (input.min + input.max);
            }
            return rng->RangeUVE(input.min, input.max);
        case Asset::ParticleValueKindUVE::Curve:
            if (input.curveTableIndex < curveTables.size()) {
                return curveTables[input.curveTableIndex].SampleUVE(normalizedAge);
            }
            return 0.0F;
        case Asset::ParticleValueKindUVE::UserParameter:
            if (input.parameterIndex < parameters.size()) {
                return parameters[input.parameterIndex].vector.x * input.scale;
            }
            return input.constant;
        case Asset::ParticleValueKindUVE::Wire:
        case Asset::ParticleValueKindUVE::Gradient:
            break;
    }
    return input.constant;
}

Math::Vector3UVE EvaluateCompiledVectorInputUVE(const VfxCompiledVectorInputUVE& input,
                                              Math::RandomUVE* const rng,
                                              const std::vector<VfxParameterValueUVE>& parameters) noexcept {
    switch (input.kind) {
        case Asset::ParticleValueKindUVE::Constant:
            return input.constant;
        case Asset::ParticleValueKindUVE::RandomRange:
            if (rng == nullptr) {
                return (input.min + input.max) * 0.5F;
            }
            return Math::Vector3UVE{
                rng->RangeUVE(input.min.x, input.max.x),
                rng->RangeUVE(input.min.y, input.max.y),
                rng->RangeUVE(input.min.z, input.max.z),
            };
        case Asset::ParticleValueKindUVE::UserParameter:
            if (input.parameterIndex < parameters.size()) {
                return parameters[input.parameterIndex].vector;
            }
            return input.constant;
        case Asset::ParticleValueKindUVE::Curve:
        case Asset::ParticleValueKindUVE::Gradient:
        case Asset::ParticleValueKindUVE::Wire:
            break;
    }
    return input.constant;
}

VfxLinearColorAlphaUVE EvaluateCompiledColorInputUVE(const VfxCompiledColorInputUVE& input,
                                                     const float normalizedAge,
                                                     Math::RandomUVE* const rng,
                                                     const std::vector<VfxGradientTableUVE>& gradientTables,
                                                     const std::vector<VfxParameterValueUVE>& parameters) noexcept {
    switch (input.kind) {
        case Asset::ParticleValueKindUVE::Constant:
            return VfxLinearColorAlphaUVE{input.constant, input.alpha};
        case Asset::ParticleValueKindUVE::RandomRange: {
            const float u = rng != nullptr ? rng->NextFloatUVE() : 0.5F;
            return VfxLinearColorAlphaUVE{
                Math::ColorUVE{
                    Math::LerpUVE(input.minColor.r, input.maxColor.r, u),
                    Math::LerpUVE(input.minColor.g, input.maxColor.g, u),
                    Math::LerpUVE(input.minColor.b, input.maxColor.b, u),
                },
                input.alpha,
            };
        }
        case Asset::ParticleValueKindUVE::Gradient:
            if (input.gradientTableIndex < gradientTables.size()) {
                return gradientTables[input.gradientTableIndex].SampleUVE(normalizedAge);
            }
            return VfxLinearColorAlphaUVE{{1.0F, 1.0F, 1.0F}, 1.0F};
        case Asset::ParticleValueKindUVE::UserParameter:
            if (input.parameterIndex < parameters.size()) {
                const VfxParameterValueUVE& param = parameters[input.parameterIndex];
                return VfxLinearColorAlphaUVE{
                    Math::ColorUVE{param.vector.x, param.vector.y, param.vector.z},
                    param.alpha,
                };
            }
            return VfxLinearColorAlphaUVE{input.constant, input.alpha};
        case Asset::ParticleValueKindUVE::Curve:
        case Asset::ParticleValueKindUVE::Wire:
            break;
    }
    return VfxLinearColorAlphaUVE{input.constant, input.alpha};
}

} // namespace UVE::VFX
