// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "entity.h"
#include "light_component.h"
#include "color.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Light3D scene object: the component set and defaults a freshly
/// created Light3D entity attaches — one directional light.
struct Light3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Directional Light";

    LightComponentUVE light = MakeDefaultLightUVE();

    [[nodiscard]] static LightComponentUVE MakeDefaultLightUVE() noexcept {
        LightComponentUVE light{};
        light.type = LightTypeUVE::Directional;
        return light;
    }
};

[[nodiscard]] bool IsLight3DObjectDefinitionValidUVE(const Light3DObjectDefinitionUVE& value) noexcept;

void ApplyLight3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                     const Light3DObjectDefinitionUVE& value);

/// Node policy for a `Light3D` object.
///
/// Owns the Rec.709 luminance, forward-axis world direction, view-contribution ranking, and spot
/// cone evaluation shared by `LightSystemUVE` and light inspection tools.
class Light3DUVE final {
public:
    Light3DUVE() = delete;

    static constexpr Math::Vector3UVE kLocalForwardDirectionUVE{0.0F, 0.0F, -1.0F};

    /// True when `light` is valid and has strictly positive intensity.
    [[nodiscard]] static bool IsEmittingUVE(const LightComponentUVE& light) noexcept;

    /// Computes the non-negative Rec.709 perceptual luminance of `color`.
    [[nodiscard]] static float ComputeRec709LuminanceUVE(const Math::ColorUVE& color) noexcept;

    /// Rotates the light's local `-Z` forward axis by `normalizedWorldRotation`.
    [[nodiscard]] static Math::Vector3UVE ResolveWorldDirectionUVE(
        const Math::QuaternionUVE& normalizedWorldRotation) noexcept;

    /// Estimates how strongly a light of `(type, color, intensity, range)` at `lightPosition`
    /// contributes at `viewPosition` for `LightSystemUVE`'s top-K light selection.
    /// Directional lights return `+infinity` so they are never dropped for a nearby point light.
    [[nodiscard]] static float EstimateViewContributionUVE(
        LightTypeUVE type, const Math::ColorUVE& color, float intensity, float range,
        const Math::Vector3UVE& lightPosition, const Math::Vector3UVE& viewPosition) noexcept;

    /// Evaluates the spot cone weight in `[0, 1]` for a unit vector `lightToTargetDirection`
    /// against `lightWorldDirection` and `spotAngleDegrees` (full cone angle in degrees).
    [[nodiscard]] static float EvaluateSpotConeWeightUVE(
        float spotAngleDegrees, const Math::Vector3UVE& lightWorldDirection,
        const Math::Vector3UVE& lightToTargetDirection) noexcept;
};

} // namespace UVE::Scene
