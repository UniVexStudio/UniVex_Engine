// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/light_system_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "uve/logging/assert_uve.h"
#include "uve/logging/logging_macros_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Render {

namespace {

/// A cheap estimate of how much a light can contribute at `viewPosition`, used only to RANK lights
/// against each other when more exist than fit.
///
/// Deliberately not exact, and deliberately not raw distance. Distance alone says a dim candle at
/// two metres outranks a floodlight at twenty, which is wrong; this uses intensity over squared
/// distance, the same inverse-square falloff the shader itself applies, so the ranking agrees with
/// the shading rather than merely correlating with it. Colour is folded in by luminance so a light
/// whose colour is nearly black cannot outrank a visible one on intensity alone.
///
/// Directional lights return infinity. They have no position to be distant from, they light the
/// entire scene, and the directional light is the only shadow caster - dropping one in favour of a
/// nearby point light would remove the scene's key light and all of its shadows at once.
[[nodiscard]] float EstimateLightContributionUVE(const LightDataUVE& light,
                                                 const Math::Vector3UVE& viewPosition) noexcept {
    if (light.type == Scene::LightTypeUVE::Directional) {
        return std::numeric_limits<float>::infinity();
    }
    // Rec. 709 luminance: the eye's own weighting, so a green light is not ranked equal to a blue
    // one of the same numeric intensity.
    const float luminance =
        0.2126F * light.color.x + 0.7152F * light.color.y + 0.0722F * light.color.z;
    const float emitted = light.intensity * std::max(luminance, 0.0F);
    if (emitted <= 0.0F) {
        return 0.0F;
    }

    const Math::Vector3UVE toLight{light.position.x - viewPosition.x, light.position.y - viewPosition.y,
                                   light.position.z - viewPosition.z};
    const float distanceSquared =
        toLight.x * toLight.x + toLight.y * toLight.y + toLight.z * toLight.z;

    // Outside its own range a light contributes exactly nothing - the shader zeroes it - so it must
    // rank below every light that contributes anything at all, rather than merely ranking low.
    if (light.range > 0.0F && distanceSquared > light.range * light.range) {
        return 0.0F;
    }
    // One metre floor: inside that radius the estimate saturates rather than exploding, which keeps
    // a light the camera is sitting on top of from being infinitely important.
    return emitted / std::max(distanceSquared, 1.0F);
}

/// The single definition of "is this a usable light, and what are its frame values" - shared by
/// both overloads on purpose. Two copies of this validation would be two chances to disagree about
/// which lights exist, and the selecting overload must reject exactly what the unordered one does.
[[nodiscard]] bool TryBuildLightDataUVE(const Scene::WorldTransformComponentUVE& worldTransform,
                                        const Scene::LightComponentUVE& light,
                                        LightDataUVE& outLight) {
    const bool validLight = Scene::IsLightComponentValidUVE(light);
    UVE_ASSERT(validLight);
    if (!validLight) {
        UVE_ERROR("LightSystemUVE: invalid light component skipped");
        return false;
    }
    const bool finitePosition = std::isfinite(worldTransform.worldPosition.x) &&
                                 std::isfinite(worldTransform.worldPosition.y) &&
                                 std::isfinite(worldTransform.worldPosition.z);
    Math::QuaternionUVE normalizedRotation;
    const bool validTransform =
        finitePosition && Math::TryNormalizeUVE(worldTransform.worldRotation, normalizedRotation);
    UVE_ASSERT(validTransform);
    if (!validTransform) {
        UVE_ERROR("LightSystemUVE: invalid world transform skipped");
        return false;
    }
    outLight.type = light.type;
    outLight.position = worldTransform.worldPosition;
    outLight.direction = Math::RotateVectorUVE(normalizedRotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    outLight.rotation = normalizedRotation;
    outLight.color = light.color;
    outLight.intensity = light.intensity;
    outLight.range = light.range;
    outLight.spotAngleDegrees = light.spotAngleDegrees;
    return true;
}

} // namespace

LightListUVE LightSystemUVE::ExtractActiveLightsUVE(Scene::IEntityManagerUVE& entityManager) const {
    LightListUVE result;
    std::size_t filledCount = 0;

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::LightComponentUVE>(
        [&](Scene::EntityUVE, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::LightComponentUVE& light) {
            if (filledCount >= kMaxLightsUVE) {
                return;
            }
            if (!TryBuildLightDataUVE(worldTransform, light, result[filledCount])) {
                return;
            }
            ++filledCount;
        });

    return result;
}

LightListUVE LightSystemUVE::ExtractActiveLightsForViewUVE(Scene::IEntityManagerUVE& entityManager,
                                                           const Math::Vector3UVE& viewPosition) const {
    // Selection is a two-pass job: the first pass must see EVERY light to rank them, which the
    // single-pass fill above cannot do because it stops at kMaxLightsUVE. The validation rules are
    // reused exactly - a light rejected there must be rejected here, or the two overloads would
    // disagree about what a valid light is.
    struct RankedLightUVE final {
        LightDataUVE light;
        float contribution;
    };
    std::vector<RankedLightUVE> candidates;

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::LightComponentUVE>(
        [&](Scene::EntityUVE, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::LightComponentUVE& light) {
            LightDataUVE slot;
            if (!TryBuildLightDataUVE(worldTransform, light, slot)) {
                return;
            }
            candidates.push_back(RankedLightUVE{slot, EstimateLightContributionUVE(slot, viewPosition)});
        });

    // Partial sort: only the top kMaxLightsUVE matter, and a scene with hundreds of lights should
    // not pay a full sort to discard almost all of them. Stable ordering among equal contributions
    // is not guaranteed and does not need to be - equal contributions are interchangeable by
    // definition.
    const std::size_t keepCount = std::min(candidates.size(), kMaxLightsUVE);
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(keepCount),
                      candidates.end(),
                      [](const RankedLightUVE& lhs, const RankedLightUVE& rhs) {
                          return lhs.contribution > rhs.contribution;
                      });

    LightListUVE result;
    for (std::size_t index = 0U; index < keepCount; ++index) {
        // A light contributing nothing is left as the empty sentinel rather than occupying a slot:
        // the shader would skip it anyway, and holding a slot open costs a real light its place.
        if (candidates[index].contribution <= 0.0F) {
            break;
        }
        result[index] = candidates[index].light;
    }
    return result;
}

} // namespace UVE::Render
