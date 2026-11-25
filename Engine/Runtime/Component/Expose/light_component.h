// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>
#include <cstdint>

#include "color.h"

namespace UVE::Scene {

/// Which lighting behavior a LightComponent-bearing entity represents (Increment 25).
/// `range`/`spotAngleDegrees` are only meaningful for Point/Spot respectively — ignored
/// otherwise, following the same "always a full record, never branch on which fields are
/// meaningful" philosophy Render::LightData uses on the extraction side.
enum class LightType : std::uint8_t { Directional = 0, Point = 1, Spot = 2 };

/// One of the master spec's named built-in components (Part 7.3). Originally a deliberately
/// minimal placeholder (Increment 5) — only color/intensity. Extended in Increment 25 with
/// `type`/`range`/`spotAngleDegrees` to support Point and Spot lights alongside Directional,
/// per Render::LightSystem (Part 7.2). A light's *position*/*direction* are still not stored
/// here — they're derived from the entity's WorldTransformComponent
/// (`worldPosition`/`worldRotation`), exactly as Increment 23 established for direction.
struct LightComponent final {
    // color/intensity stay first (their original Increment-5 position) so every existing 2-arg
    // aggregate-init call site (LightComponent{color, intensity}) keeps its shape (color is a
    // linear Math::Color since Tier 1.3),
    // matching the established backward-compatible-field-addition convention (see
    // ColliderComponent's own later-added fields for the same trailing-default pattern).
    Math::Color color{1.0F, 1.0F, 1.0F};
    float intensity = 1.0F;

    LightType type = LightType::Directional;

    /// Point/Spot only: the distance at which this light's contribution falls to (effectively)
    /// zero. Ignored for Directional (infinitely far away, no falloff).
    float range = 10.0F;

    /// Spot only: the cone's half-angle, in degrees, measured from the light's forward direction.
    /// Ignored for Directional/Point.
    float spotAngleDegrees = 45.0F;
};

[[nodiscard]] bool IsLightTypeValid(const LightType type) noexcept;

/// Validates the value-only lighting component before persistence or renderer extraction. Color and
/// intensity permit HDR values but reject negatives/non-finite data; range and cone angle remain
/// finite positive policy values even when a given light type ignores one of them.
[[nodiscard]] bool IsLightComponentValid(const LightComponent& light) noexcept;

/// Sanitizes `light` so enum, color, intensity, range, and spot cone angle are valid.
[[nodiscard]] LightComponent SanitizeLightComponent(const LightComponent& light) noexcept;

/// Computes the radiant linear RGB color (`color * max(0, intensity)`).
[[nodiscard]] Math::Color EvaluateLightRadiantColor(const LightComponent& light) noexcept;

/// Computes smooth distance attenuation in `[0, 1]` (`1` for directional lights, `0` beyond `range`).
[[nodiscard]] float EvaluateLightDistanceAttenuation(const LightComponent& light,
                                                        float distanceMeters) noexcept;

/// Returns the cosine of the spot cone angle (`cos(DegToRad(spotAngleDegrees))`).
[[nodiscard]] float ResolveSpotLightConeCosine(const LightComponent& light) noexcept;

} // namespace UVE::Scene
