// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>
#include <cstdint>

#include "scalar.h"

namespace UVE::Math {

/// The standard easing set (easings.net): every In/Out/InOut curve animation blending, camera
/// transitions and UI tweens want, in one place. Before this, the only easing in the engine was
/// the UI tween's private 9-case evaluator — `EaseUITween` now maps onto these and owns no
/// formulas of its own.
/// `t` is time in [0, 1]; every curve returns 0 at 0 and 1 at 1 (Back/Elastic/Bounce overshoot
/// between, by design). Callers clamp `t` themselves — the curves extrapolate past the ends.
/// Not constexpr: the trigonometric/exponential cases need runtime `<cmath>`.
enum class EaseKind : std::uint8_t {
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
[[nodiscard]] inline float BounceOut(const float t) noexcept {
    constexpr float kN1 = 7.5625F;
    constexpr float kD1 = 2.75F;
    if (t < 1.0F / kD1) {
        return kN1 * t * t;
    }
    if (t < 2.0F / kD1) {
        const float u = t - 1.5F / kD1;
        return kN1 * u * u + 0.75F;
    }
    if (t < 2.5F / kD1) {
        const float u = t - 2.25F / kD1;
        return kN1 * u * u + 0.9375F;
    }
    const float u = t - 2.625F / kD1;
    return kN1 * u * u + 0.984375F;
}

} // namespace Detail

/// Evaluates the easing curve `kind` at time `t`.
[[nodiscard]] inline float Ease(const EaseKind kind, const float t) noexcept {
    constexpr float kBackOvershoot = 1.70158F;
    switch (kind) {
        case EaseKind::Linear:
            return t;
        case EaseKind::SineIn:
            return 1.0F - std::cos((t * kPi) * 0.5F);
        case EaseKind::SineOut:
            return std::sin((t * kPi) * 0.5F);
        case EaseKind::SineInOut:
            return -(std::cos(kPi * t) - 1.0F) * 0.5F;
        case EaseKind::QuadIn:
            return t * t;
        case EaseKind::QuadOut:
            return 1.0F - (1.0F - t) * (1.0F - t);
        case EaseKind::QuadInOut:
            return t < 0.5F ? 2.0F * t * t : 1.0F - (-2.0F * t + 2.0F) * (-2.0F * t + 2.0F) * 0.5F;
        case EaseKind::CubicIn:
            return t * t * t;
        case EaseKind::CubicOut: {
            const float u = 1.0F - t;
            return 1.0F - u * u * u;
        }
        case EaseKind::CubicInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? 4.0F * t * t * t : 1.0F - u * u * u * 0.5F;
        }
        case EaseKind::QuartIn:
            return t * t * t * t;
        case EaseKind::QuartOut: {
            const float u = 1.0F - t;
            return 1.0F - u * u * u * u;
        }
        case EaseKind::QuartInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? 8.0F * t * t * t * t : 1.0F - u * u * u * u * 0.5F;
        }
        case EaseKind::QuintIn:
            return t * t * t * t * t;
        case EaseKind::QuintOut: {
            const float u = 1.0F - t;
            return 1.0F - u * u * u * u * u;
        }
        case EaseKind::QuintInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? 16.0F * t * t * t * t * t : 1.0F - u * u * u * u * u * 0.5F;
        }
        case EaseKind::ExpoIn:
            return t <= 0.0F ? 0.0F : std::pow(2.0F, 10.0F * t - 10.0F);
        case EaseKind::ExpoOut:
            return t >= 1.0F ? 1.0F : 1.0F - std::pow(2.0F, -10.0F * t);
        case EaseKind::ExpoInOut:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            return t < 0.5F ? std::pow(2.0F, 20.0F * t - 10.0F) * 0.5F
                            : (2.0F - std::pow(2.0F, -20.0F * t + 10.0F)) * 0.5F;
        case EaseKind::CircIn:
            return 1.0F - std::sqrt(1.0F - t * t);
        case EaseKind::CircOut: {
            const float u = t - 1.0F;
            return std::sqrt(1.0F - u * u);
        }
        case EaseKind::CircInOut: {
            const float u = -2.0F * t + 2.0F;
            return t < 0.5F ? (1.0F - std::sqrt(1.0F - (2.0F * t) * (2.0F * t))) * 0.5F
                            : (std::sqrt(1.0F - u * u) + 1.0F) * 0.5F;
        }
        case EaseKind::BackIn: {
            constexpr float kC3 = kBackOvershoot + 1.0F;
            return kC3 * t * t * t - kBackOvershoot * t * t;
        }
        case EaseKind::BackOut: {
            constexpr float kC3 = kBackOvershoot + 1.0F;
            const float u = t - 1.0F;
            return 1.0F + kC3 * u * u * u + kBackOvershoot * u * u;
        }
        case EaseKind::BackInOut: {
            constexpr float kC2 = kBackOvershoot * 1.525F;
            if (t < 0.5F) {
                const float u = 2.0F * t;
                return (u * u * ((kC2 + 1.0F) * u - kC2)) * 0.5F;
            }
            const float u = 2.0F * t - 2.0F;
            return (u * u * ((kC2 + 1.0F) * u + kC2) + 2.0F) * 0.5F;
        }
        case EaseKind::ElasticIn:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            return -std::pow(2.0F, 10.0F * t - 10.0F) *
                   std::sin((10.0F * t - 10.75F) * ((2.0F * kPi) / 3.0F));
        case EaseKind::ElasticOut:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            return std::pow(2.0F, -10.0F * t) * std::sin((10.0F * t - 0.75F) * ((2.0F * kPi) / 3.0F)) +
                   1.0F;
        case EaseKind::ElasticInOut:
            if (t <= 0.0F) {
                return 0.0F;
            }
            if (t >= 1.0F) {
                return 1.0F;
            }
            if (t < 0.5F) {
                return -(std::pow(2.0F, 20.0F * t - 10.0F) *
                         std::sin((20.0F * t - 11.125F) * ((2.0F * kPi) / 4.5F))) *
                       0.5F;
            }
            return (std::pow(2.0F, -20.0F * t + 10.0F) *
                    std::sin((20.0F * t - 11.125F) * ((2.0F * kPi) / 4.5F))) *
                       0.5F +
                   1.0F;
        case EaseKind::BounceIn:
            return 1.0F - Detail::BounceOut(1.0F - t);
        case EaseKind::BounceOut:
            return Detail::BounceOut(t);
        case EaseKind::BounceInOut:
            return t < 0.5F ? (1.0F - Detail::BounceOut(1.0F - 2.0F * t)) * 0.5F
                            : (1.0F + Detail::BounceOut(2.0F * t - 1.0F)) * 0.5F;
    }
    return t;
}

} // namespace UVE::Math
