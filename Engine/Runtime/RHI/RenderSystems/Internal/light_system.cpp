// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "light_system.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "engine_assert.h"
#include "logging_macros.h"
#include "quaternion.h"
#include "vector3.h"
#include "editor_internal_entity_component.h"
#include "light_component.h"
#include "light_emitter_component.h"
#include "abstract_objects_3d.h"
#include "directional_light_3d.h"
#include "light_3d.h"
#include "visibility_component.h"
#include "world_transform_component.h"

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
[[nodiscard]] float EstimateLightContribution(const LightData& light,
                                                 const Math::Vector3& viewPosition) noexcept {
    return Scene::Light3D::EstimateViewContribution(
        light.type, light.color, light.intensity, light.range, light.position, viewPosition);
}

/// The single definition of "is this a usable light, and what are its frame values" - shared by
/// both overloads on purpose. Two copies of this validation would be two chances to disagree about
/// which lights exist, and the selecting overload must reject exactly what the unordered one does.
[[nodiscard]] bool TryBuildLightData(const Scene::WorldTransformComponent& worldTransform,
                                        const Scene::LightComponent& light,
                                        LightData& outLight) {
    const bool validLight = Scene::IsLightComponentValid(light);
    UVE_ASSERT(validLight);
    if (!validLight) {
        UVE_ERROR("LightSystem: invalid light component skipped");
        return false;
    }
    const bool finitePosition = std::isfinite(worldTransform.worldPosition.x) &&
                                 std::isfinite(worldTransform.worldPosition.y) &&
                                 std::isfinite(worldTransform.worldPosition.z);
    Math::Quaternion normalizedRotation;
    const bool validTransform =
        finitePosition && Math::TryNormalize(worldTransform.worldRotation, normalizedRotation);
    UVE_ASSERT(validTransform);
    if (!validTransform) {
        UVE_ERROR("LightSystem: invalid world transform skipped");
        return false;
    }
    outLight.type = light.type;
    outLight.position = worldTransform.worldPosition;
    outLight.direction = Scene::Light3D::ResolveWorldDirection(normalizedRotation);
    outLight.rotation = normalizedRotation;
    outLight.color = light.color;
    outLight.intensity = light.intensity;
    outLight.range = light.range;
    outLight.spotAngleDegrees = light.spotAngleDegrees;
    return true;
}

/// A DirectionalLight3D: its colour, energy and shadow switch are LightEmitter3D's.
[[nodiscard]] bool TryBuildDirectionalLightData(const Scene::WorldTransformComponent& worldTransform,
                                                   const Scene::DirectionalLight3DComponent& directional,
                                                   const Scene::LightEmitterComponent& emitter,
                                                   LightData& outLight) {
    if (!Scene::IsDirectionalLight3DComponentValid(directional) || !Scene::IsLightEmitterComponentValid(emitter)) {
        UVE_ERROR("LightSystem: invalid DirectionalLight3D skipped");
        return false;
    }
    Scene::LightComponent light{};
    light.type = Scene::LightType::Directional;
    light.color = emitter.color;
    // Energy, and nothing else, decides whether it lights: the forward shader only adds, so there
    // is no subtractive light to switch to - turning one off is energy zero, in plain sight.
    light.intensity = emitter.energy;
    if (!TryBuildLightData(worldTransform, light, outLight)) {
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
    // Negative component values preserve the inherit sentinel for Renderer3D's configured defaults;
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

void ApplyLightEmitter3DViewFade(LightData& light, const Math::Vector3& viewPosition) noexcept {
    if (!light.distanceFadeEnabled) {
        return;
    }
    const Math::Vector3 toLight{light.position.x - viewPosition.x, light.position.y - viewPosition.y,
                                   light.position.z - viewPosition.z};
    const float distance = Math::Length(toLight);
    const float lightWeight =
        Scene::LightEmitter3DDistanceFadeWeight(distance, light.distanceFadeBegin, light.distanceFadeLength);
    light.intensity *= lightWeight;
    const float shadowWeight =
        Scene::LightEmitter3DDistanceFadeWeight(distance, light.distanceFadeShadow, light.distanceFadeLength);
    light.shadowOpacity *= shadowWeight;
    if (shadowWeight <= 0.0F) {
        light.castsShadows = false;
    }
}

[[nodiscard]] bool IsLightEntityHidden(Scene::IEntityManager& entityManager,
                                          const Scene::Entity entity) noexcept {
    if (!entityManager.HasComponent<Scene::VisibilityComponent>(entity)) {
        return false;
    }
    const Scene::VisibilityComponent& visibility =
        entityManager.GetComponent<Scene::VisibilityComponent>(entity);
    return !visibility.visible || !visibility.visibleInHierarchy;
}

/// Every light the frame can use, in entity order: DirectionalLight3D and Light3D alike.
template <typename Visit>
void ForEachLightData(Scene::IEntityManager& entityManager, const Visit& visit) {
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::DirectionalLight3DComponent,
                             Scene::LightEmitterComponent>(
        [&](const Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
            const Scene::DirectionalLight3DComponent& directional, const Scene::LightEmitterComponent& emitter) {
            if (IsLightEntityHidden(entityManager, entity)) {
                return;
            }
            LightData slot;
            if (TryBuildDirectionalLightData(worldTransform, directional, emitter, slot)) {
                slot.isEditorInternal =
                    entityManager.HasComponent<Scene::EditorInternalEntityComponent>(entity);
                if (slot.isEditorInternal) {
                    slot.castsShadows = false;
                }
                visit(slot);
            }
        });
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::LightComponent>(
        [&](const Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
            const Scene::LightComponent& light) {
            if (IsLightEntityHidden(entityManager, entity)) {
                return;
            }
            LightData slot;
            if (TryBuildLightData(worldTransform, light, slot)) {
                slot.isEditorInternal =
                    entityManager.HasComponent<Scene::EditorInternalEntityComponent>(entity);
                if (entityManager.HasComponent<Scene::LightEmitterComponent>(entity)) {
                    const Scene::LightEmitterComponent& emitter =
                        entityManager.GetComponent<Scene::LightEmitterComponent>(entity);
                    if (Scene::IsLightEmitterComponentValid(emitter)) {
                        if (emitter.cullMask == 0U) {
                            return;
                        }
                        slot.castsShadows = emitter.shadowEnabled;
                        slot.cullMask = emitter.cullMask;
                        slot.specular = emitter.specular;
                        slot.shadowBias = emitter.shadowBias >= 0.0F ? emitter.shadowBias * 0.025F : -1.0F;
                        slot.shadowNormalBias = emitter.shadowNormalBias >= 0.0F ? emitter.shadowNormalBias : -1.0F;
                        slot.shadowOpacity = emitter.shadowOpacity;
                        slot.shadowBlur = emitter.shadowBlur;
                        slot.volumetricFogEnergy = emitter.volumetricFogEnergy;
                        slot.distanceFadeEnabled = emitter.distanceFadeEnabled;
                        slot.distanceFadeBegin = emitter.distanceFadeBegin;
                        slot.distanceFadeShadow = emitter.distanceFadeShadow;
                        slot.distanceFadeLength = emitter.distanceFadeLength;
                    }
                } else if (slot.isEditorInternal) {
                    slot.castsShadows = false;
                }
                visit(slot);
            }
        });
}

} // namespace

LightList LightSystem::ExtractActiveLights(Scene::IEntityManager& entityManager) const {
    LightList result;

    ForEachLightData(entityManager, [&](const LightData& light) {
        // A light contributing nothing (zero energy, a zero cull mask, an invalid component) is left
        // out of the list rather than occupying a slot: the shader would skip it anyway, and
        // holding a slot open costs a real light its place. Same rule as the ranked path below.
        if (light.intensity <= 0.0F || result.Full()) {
            return;
        }
        result.PushBack(light);
    });

    return result;
}

LightList LightSystem::ExtractActiveLightsForView(Scene::IEntityManager& entityManager,
                                                           const Math::Vector3& viewPosition) const {
    // Selection is a two-pass job: the first pass must see EVERY light to rank them, which the
    // single-pass fill above cannot do because it stops at kMaxLights. The validation rules are
    // reused exactly - a light rejected there must be rejected here, or the two overloads would
    // disagree about what a valid light is.
    struct RankedLight final {
        LightData light;
        float contribution;
    };
    std::vector<RankedLight> candidates;

    ForEachLightData(entityManager, [&](LightData light) {
        ApplyLightEmitter3DViewFade(light, viewPosition);
        if (light.intensity <= 0.0F) {
            return;
        }
        const float contribution = EstimateLightContribution(light, viewPosition);
        if (contribution <= 0.0F) {
            return;
        }
        candidates.push_back(RankedLight{light, contribution});
    });

    // Partial sort: only the top kMaxLights matter, and a scene with hundreds of lights should
    // not pay a full sort to discard almost all of them. When contributions tie (e.g. directional
    // lights), authored scene lights always outrank editor-internal fallback lights.
    const std::size_t keepCount = std::min(candidates.size(), kMaxLights);
    std::partial_sort(candidates.begin(), candidates.begin() + static_cast<std::ptrdiff_t>(keepCount),
                      candidates.end(),
                      [](const RankedLight& lhs, const RankedLight& rhs) {
                          if (lhs.contribution != rhs.contribution) {
                              return lhs.contribution > rhs.contribution;
                          }
                          return !lhs.light.isEditorInternal && rhs.light.isEditorInternal;
                      });

    LightList result;
    for (std::size_t index = 0U; index < keepCount; ++index) {
        // A light contributing nothing is left out of the list rather than occupying a slot: the
        // shader would skip it anyway, and holding a slot open costs a real light its place.
        if (candidates[index].contribution <= 0.0F) {
            break;
        }
        result.PushBack(candidates[index].light);
    }
    return result;
}

} // namespace UVE::Render
