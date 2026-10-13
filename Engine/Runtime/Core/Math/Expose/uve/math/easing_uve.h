// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>
#include <cstdint>

#include "uve/math/scalar_uve.h"

namespace UVE::Math {

/// The standard easing set (easings.net): every In/Out/InOut curve animation blending, camera
/// transitions and UI tweens want, in one place. Before this, the only easing in the engine was
/// the UI tween's private 9-case evaluator — `EaseUITweenUVE` now maps onto these and owns no
/// formulas of its own.
/// `t` is time in [0, 1]; every curve returns 0 at 0 and 1 at 1 (Back/Elastic/Bounce overshoot
/// between, by design). Callers clamp `t` themselves — the curves extrapolate past the ends.
/// Not constexpr: the trigonometric/exponential cases need runtime `<cmath>`.
enum class EaseKindUVE : std::uint8_t {
    Linear = 0,
    SineIn, SineOut, SineInOut,
    QuadIn, QuadOut, QuadInOut,
    CubicIn, CubicOut, CubicInOut,
    QuartIn, QuartOut, QuartInOut,
    QuintIn, QuintOut, QuintInOut,
    ExpoIn, ExpoOut, ExpoInOut,
    CircIn, CircOut, CircInOut,
    BackIn, BackOut, BackInOut,
    ElasticIn, ElasticOut, ElasticInOut,
    BounceIn, BounceOut, BounceInOut,
};

namespace Detail {

/// The BounceOut polynomial shared by all three bounce curves.
[[nodiscard]] inline float BounceOutUVE(const float t) noexcept {
    constexpr float kN1UVE = 7.5625F;
    constexpr float kD1UVE = 2.75F;
    if (t < 1.0F / kD1UVE) {
        return kN1UVE * t * t;
    }
    if (t < 2.0F / kD1UVE) {
        const float u = t - 1.5F / kD1UVE;
        return kN1UVE * u * u + 0.75F;
    }
    if (t < 2.5F / kD1UVE) {
        const float u = t - 2.25F / kD1UVE;
        return kN1UVE * u * u + 0.9375F;
    }
    const float u = t - 2.625F / kD1UVE;
    return kN1UVE * u * u + 0.984375F;
}

} // namespace Detail

/// Evaluates the easing curve `kind` at time `t`.
[[nodiscard]] inline float EaseUVE(const EaseKindUVE kind, const float t) noexcept {
    constexpr float kBackOvershootUVE = 1.70158F;
    switch (kind) {
        case EaseKindUVE::Linear:
            return t;
        case EaseKindUVE::SineIn:
            return 1.0F - std::cos((t * kPiUVE) * 0.5F);
        case EaseKindUVE::SineOut:
            return std::sin((t * kPiUVE) * 0.5F);
        case EaseKindUVE::SineInOut:
            return -(std::cos(kPiUVE * t) - 1.0F) * 0.5F;
        case EaseKindUVE::QuadIn:
            return t * t;
        case EaseKindUVE::QuadOut:
            return 1.0F - (1.0F - t) * (1.0F - t);
        case EaseKindUVE::QuadInOut:
            return t < 0.5F ? 2.0F * t * t : 1.0F - (-2.0F * t + 2.0F) * (-2.0F * t + 2.0F) * 0.5F;
        case EaseKindUVE::CubicIn:
            return t * t * t;
        case EaseKindUVE::CubicOut: {
            const float u = 1.0F - t;
            return 1.0F - u * u * u;
        }
        case EaseKindUVE::CubicInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? 4.0F * t * t * t : 1.0F - u * u * u * 0.5F;
        }
        case EaseKindUVE::QuartIn:
            return t * t * t * t;
        case EaseKindUVE::QuartOut: {
            const float u = 1.0F - t;
            return 1.0F - u * u * u * u;
        }
        case EaseKindUVE::QuartInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? 8.0F * t * t * t * t : 1.0F - u * u * u * u * 0.5F;
        }
        case EaseKindUVE::QuintIn:
            return t * t * t * t * t;
        case EaseKindUVE::QuintOut: {
            const float u = 1.0F - t;
            return 1.0F - u * u * u * u * u;
        }
        case EaseKindUVE::QuintInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? 16.0F * t * t * t * t * t : 1.0F - u * u * u * u * u * 0.5F;
        }
        case EaseKindUVE::ExpoIn:
            return t <= 0.0F ? 0.0F : std::pow(2.0F, 10.0F * t - 10.0F);
        case EaseKindUVE::ExpoOut:
            return t >= 1.0F ? 1.0F : 1.0F - std::pow(2.0F, -10.0F * t);
        case EaseKindUVE::ExpoInOut:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            return t < 0.5F ? std::pow(2.0F, 20.0F * t - 10.0F) * 0.5F
                            : (2.0F - std::pow(2.0F, -20.0F * t + 10.0F)) * 0.5F;
        case EaseKindUVE::CircIn:
            return 1.0F - std::sqrt(1.0F - t * t);
        case EaseKindUVE::CircOut: {
            const float u = t - 1.0F;
            return std::sqrt(1.0F - u * u);
        }
        case EaseKindUVE::CircInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? (1.0F - std::sqrt(1.0F - (2.0F * t) * (2.0F * t))) * 0.5F
                            : (std::sqrt(1.0F - u * u) + 1.0F) * 0.5F;
        }
        case EaseKindUVE::BackIn: {
            constexpr float kC3UVE = kBackOvershootUVE + 1.0F;
            return kC3UVE * t * t * t - kBackOvershootUVE * t * t;
        }
        case EaseKindUVE::BackOut: {
            constexpr float kC3UVE = kBackOvershootUVE + 1.0F;
            const float u = t - 1.0F;
            return 1.0F + kC3UVE * u * u * u + kBackOvershootUVE * u * u;
        }
        case EaseKindUVE::BackInOut: {
            constexpr float kC2UVE = kBackOvershootUVE * 1.525F;
            if (t < 0.5F) {
                const float u = 2.0F * t;
                return (u * u * ((kC2UVE + 1.0F) * u - kC2UVE)) * 0.5F;
            }
            const float u = 2.0F * t - 2.0F;
            return (u * u * ((kC2UVE + 1.0F) * u + kC2UVE) + 2.0F) * 0.5F;
        }
        case EaseKindUVE::ElasticIn:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            return -std::pow(2.0F, 10.0F * t - 10.0F) *
                   std::sin((10.0F * t - 10.75F) * ((2.0F * kPiUVE) / 3.0F));
        case EaseKindUVE::ElasticOut:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            return std::pow(2.0F, -10.0F * t) * std::sin((10.0F * t - 0.75F) * ((2.0F * kPiUVE) / 3.0F)) +
                   1.0F;
        case EaseKindUVE::ElasticInOut:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            if (t < 0.5F) {
                return -(std::pow(2.0F, 20.0F * t - 10.0F) *
                         std::sin((20.0F * t - 11.125F) * ((2.0F * kPiUVE) / 4.5F))) *
                       0.5F;
            }
            return (std::pow(2.0F, -20.0F * t + 10.0F) *
                    std::sin((20.0F * t - 11.125F) * ((2.0F * kPiUVE) / 4.5F))) *
                       0.5F +
                   1.0F;
        case EaseKindUVE::BounceIn:
            return 1.0F - Detail::BounceOutUVE(1.0F - t);
        case EaseKindUVE::BounceOut:
            return Detail::BounceOutUVE(t);
        case EaseKindUVE::BounceInOut:
            return t < 0.5F ? (1.0F - Detail::BounceOutUVE(1.0F - 2.0F * t)) * 0.5F
                            : (1.0F + Detail::BounceOutUVE(2.0F * t - 1.0F)) * 0.5F;
    }
    return t;
}

} // namespace UVE::Math
