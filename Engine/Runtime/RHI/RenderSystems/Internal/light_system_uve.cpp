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
#include "uve/math/vector3_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/light_component_uve.h"
#include "uve/component/light_emitter_component_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"
#include "uve/objects/3d/directional_light_3d_uve.h"
#include "uve/objects/3d/light_3d_uve.h"
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
    return Scene::Light3DUVE::EstimateViewContributionUVE(
        light.type, light.color, light.intensity, light.range, light.position, viewPosition);
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
    outLight.direction = Scene::Light3DUVE::ResolveWorldDirectionUVE(normalizedRotation);
    outLight.rotation = normalizedRotation;
    outLight.color = light.color;
    outLight.intensity = light.intensity;
    outLight.range = light.range;
    outLight.spotAngleDegrees = light.spotAngleDegrees;
    return true;
}

/// A DirectionalLight3D: its colour, energy and shadow switch are LightEmitter3D's.
[[nodiscard]] bool TryBuildDirectionalLightDataUVE(const Scene::WorldTransformComponentUVE& worldTransform,
                                                   const Scene::DirectionalLight3DComponentUVE& directional,
                                                   const Scene::LightEmitterComponentUVE& emitter,
                                                   LightDataUVE& outLight) {
    if (!Scene::IsDirectionalLight3DComponentValidUVE(directional) || !Scene::IsLightEmitterComponentValidUVE(emitter)) {
        UVE_ERROR("LightSystemUVE: invalid DirectionalLight3D skipped");
        return false;
    }
    Scene::LightComponentUVE light{};
    light.type = Scene::LightTypeUVE::Directional;
    light.color = emitter.color;
    // Energy, and nothing else, decides whether it lights: the forward shader only adds, so there
    // is no subtractive light to switch to - turning one off is energy zero, in plain sight.
    light.intensity = emitter.energy;
    if (!TryBuildLightDataUVE(worldTransform, light, outLight)) {
        return false;
    }
    if (emitter.cullMask == 0U) {
        return false;
    }
    outLight.castsShadows = emitter.shadowEnabled;
    outLight.shadowMaxDistance = directional.shadowMaxDistance;
    outLight.shadowSplitBlend = directional.shadowSplitBlend;
    outLight.shadowDistanceFadeRange = directional.shadowDistanceFadeRange;
    outLight.discAngularDiameterDegrees = directional.discAngularDiameterDegrees;
    outLight.discIntensity = directional.discIntensity;
    outLight.discLimbDarkening = directional.discLimbDarkening;
    outLight.cullMask = emitter.cullMask;
    outLight.specular = emitter.specular;
    // Negative component values preserve the inherit sentinel for Renderer3DUVE's configured defaults;
    // authored depth-bias values use the engine's established 0.025 shader-unit scale.
    outLight.shadowBias = emitter.shadowBias >= 0.0F ? emitter.shadowBias * 0.025F : -1.0F;
    outLight.shadowNormalBias = emitter.shadowNormalBias >= 0.0F ? emitter.shadowNormalBias : -1.0F;
    outLight.shadowOpacity = emitter.shadowOpacity;
    outLight.shadowBlur = emitter.shadowBlur;
    outLight.volumetricFogEnergy = emitter.volumetricFogEnergy;
    outLight.distanceFadeEnabled = emitter.distanceFadeEnabled;
    outLight.distanceFadeBegin = emitter.distanceFadeBegin;
    outLight.distanceFadeShadow = emitter.distanceFadeShadow;
    outLight.distanceFadeLength = emitter.distanceFadeLength;
    return true;
}

void ApplyLightEmitter3DViewFadeUVE(LightDataUVE& light, const Math::Vector3UVE& viewPosition) noexcept {
    if (!light.distanceFadeEnabled) {
        return;
    }
    const Math::Vector3UVE toLight{light.position.x - viewPosition.x, light.position.y - viewPosition.y,
                                   light.position.z - viewPosition.z};
    const float distance = Math::LengthUVE(toLight);
    const float lightWeight =
        Scene::LightEmitter3DDistanceFadeWeightUVE(distance, light.distanceFadeBegin, light.distanceFadeLength);
    light.intensity *= lightWeight;
    const float shadowWeight =
        Scene::LightEmitter3DDistanceFadeWeightUVE(distance, light.distanceFadeShadow, light.distanceFadeLength);
    light.shadowOpacity *= shadowWeight;
    if (shadowWeight <= 0.0F) {
        light.castsShadows = false;
    }
}

/// Every light the frame can use, in entity order: Light3D and DirectionalLight3D alike.
template <typename Visit>
void ForEachLightDataUVE(Scene::IEntityManagerUVE& entityManager, const Visit& visit) {
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::LightComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::LightComponentUVE& light) {
            LightDataUVE slot;
            if (TryBuildLightDataUVE(worldTransform, light, slot)) {
                slot.isEditorInternal =
                    entityManager.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(entity);
                if (slot.isEditorInternal) {
                    slot.castsShadows = false;
                }
                visit(slot);
            }
        });
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::DirectionalLight3DComponentUVE,
                             Scene::LightEmitterComponentUVE>(
        [&](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::DirectionalLight3DComponentUVE& directional, const Scene::LightEmitterComponentUVE& emitter) {
            LightDataUVE slot;
            if (TryBuildDirectionalLightDataUVE(worldTransform, directional, emitter, slot)) {
                slot.isEditorInternal =
                    entityManager.HasComponentUVE<Scene::EditorInternalEntityComponentUVE>(entity);
                if (slot.isEditorInternal) {
                    slot.castsShadows = false;
                }
                visit(slot);
            }
        });
}

} // namespace

LightListUVE LightSystemUVE::ExtractActiveLightsUVE(Scene::IEntityManagerUVE& entityManager) const {
    LightListUVE result;

    ForEachLightDataUVE(entityManager, [&](const LightDataUVE& light) {
        // A light contributing nothing (zero energy, a zero cull mask, an invalid component) is left
        // out of the list rather than occupying a slot: the shader would skip it anyway, and
        // holding a slot open costs a real light its place. Same rule as the ranked path below.
        if (light.intensity <= 0.0F || result.FullUVE()) {
            return;
        }
        result.PushBackUVE(light);
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

    ForEachLightDataUVE(entityManager, [&](LightDataUVE light) {
        ApplyLightEmitter3DViewFadeUVE(light, viewPosition);
        candidates.push_back(RankedLightUVE{light, EstimateLightContributionUVE(light, viewPosition)});
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
        // A light contributing nothing is left out of the list rather than occupying a slot: the
        // shader would skip it anyway, and holding a slot open costs a real light its place.
        if (candidates[index].contribution <= 0.0F) {
            break;
        }
        result.PushBackUVE(candidates[index].light);
    }
    return result;
}

} // namespace UVE::Render
