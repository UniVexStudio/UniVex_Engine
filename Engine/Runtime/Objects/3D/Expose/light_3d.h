// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "entity.h"
#include "light_component.h"
#include "color.h"
#include "quaternion.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the Light3D scene object: the component set and defaults a freshly
/// created Light3D entity attaches — one directional light.
struct Light3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Directional Light";

    LightComponent light = MakeDefaultLight();

    [[nodiscard]] static LightComponent MakeDefaultLight() noexcept {
        LightComponent light{};
        light.type = LightType::Directional;
        return light;
    }
};

[[nodiscard]] bool IsLight3DObjectDefinitionValid(const Light3DObjectDefinition& value) noexcept;

void ApplyLight3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                     const Light3DObjectDefinition& value);

/// Node policy for a `Light3D` object.
///
/// Owns the Rec.709 luminance, forward-axis world direction, view-contribution ranking, and spot
/// cone evaluation shared by `LightSystem` and light inspection tools.
class Light3D final {
public:
    Light3D() = delete;

    static constexpr Math::Vector3 kLocalForwardDirection{0.0F, 0.0F, -1.0F};

    /// True when `light` is valid and has strictly positive intensity.
    [[nodiscard]] static bool IsEmitting(const LightComponent& light) noexcept;

    /// Computes the non-negative Rec.709 perceptual luminance of `color`.
    [[nodiscard]] static float ComputeRec709Luminance(const Math::Color& color) noexcept;

    /// Rotates the light's local `-Z` forward axis by `normalizedWorldRotation`.
    [[nodiscard]] static Math::Vector3 ResolveWorldDirection(
        const Math::Quaternion& normalizedWorldRotation) noexcept;

    /// Estimates how strongly a light of `(type, color, intensity, range)` at `lightPosition`
    /// contributes at `viewPosition` for `LightSystem`'s top-K light selection.
    /// Directional lights return `+infinity` so they are never dropped for a nearby point light.
    [[nodiscard]] static float EstimateViewContribution(
        LightType type, const Math::Color& color, float intensity, float range,
        const Math::Vector3& lightPosition, const Math::Vector3& viewPosition) noexcept;

    /// Evaluates the spot cone weight in `[0, 1]` for a unit vector `lightToTargetDirection`
    /// against `lightWorldDirection` and `spotAngleDegrees` (full cone angle in degrees).
    [[nodiscard]] static float EvaluateSpotConeWeight(
        float spotAngleDegrees, const Math::Vector3& lightWorldDirection,
        const Math::Vector3& lightToTargetDirection) noexcept;
};

} // namespace UVE::Scene
