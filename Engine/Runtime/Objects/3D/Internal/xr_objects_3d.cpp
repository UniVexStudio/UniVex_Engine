// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "xr_objects_3d.h"

#include <algorithm>
#include <cmath>

#include "camera_component.h"
#include "disabled_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

void ApplyXROrigin3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                        const XROrigin3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, XROrigin3DObjectDefinition::defaultName);
    XROrigin3DComponent out = value.origin;
    if (!std::isfinite(out.worldScale) || out.worldScale <= 0.0F) {
        out.worldScale = 1.0F;
    }
    entityManager.AddComponent<XROrigin3DComponent>(entity, out);
}

void ApplyXRCamera3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                        const XRCamera3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, XRCamera3DObjectDefinition::defaultName);
    entityManager.AddComponent<CameraComponent>(entity, CameraComponent{});
    XRTracker3DComponent out = value.tracker;
    out.trackerRole = XRTrackedHand::Head;
    entityManager.AddComponent<XRTracker3DComponent>(entity, out);
}

void ApplyXRController3DObjectDefinition(IEntityManager& entityManager,
                                            const Entity entity,
                                            const XRController3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              XRController3DObjectDefinition::defaultName);
    entityManager.AddComponent<XRTracker3DComponent>(entity, value.tracker);
}

void ApplyXRFaceModifier3DObjectDefinition(IEntityManager& entityManager,
                                              const Entity entity,
                                              const XRFaceModifier3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              XRFaceModifier3DObjectDefinition::defaultName);
    XRFaceModifier3DComponent out = value.faceModifier;
    for (float& w : out.blendWeights) {
        w = std::isfinite(w) ? std::clamp(w, 0.0F, 1.0F) : 0.0F;
    }
    entityManager.AddComponent<XRFaceModifier3DComponent>(entity, std::move(out));
}

std::size_t UpdateXRObjects3D(IEntityManager& entityManager) {
    float worldScale = 1.0F;
    entityManager.ForEach<XROrigin3DComponent>(
        [&](const Entity entity, const XROrigin3DComponent& orig) {
            if (!entityManager.HasComponent<DisabledComponent>(entity) && orig.currentOrigin &&
                std::isfinite(orig.worldScale) && orig.worldScale > 0.0F) {
                worldScale = orig.worldScale;
            }
        });

    std::size_t syncedCount = 0U;
    entityManager.ForEach<XRTracker3DComponent, TransformComponent>(
        [&](const Entity entity, const XRTracker3DComponent& tracker,
            TransformComponent& local) {
            if (!tracker.trackingActive ||
                entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsFinite3DObjectVector(tracker.trackedLocalPosition) ||
                !IsFinite3DObjectQuaternion(tracker.trackedLocalRotation)) {
                return;
            }
            local.localPosition = tracker.trackedLocalPosition * worldScale;
            local.localRotation = tracker.trackedLocalRotation;
            if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
                entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
            }
            ++syncedCount;
        });

    entityManager.ForEach<XRFaceModifier3DComponent>(
        [&](const Entity entity, XRFaceModifier3DComponent& face) {
            if (!face.enabled || entityManager.HasComponent<DisabledComponent>(entity)) {
                return;
            }
            for (float& w : face.blendWeights) {
                w = std::isfinite(w) ? std::clamp(w, 0.0F, 1.0F) : 0.0F;
            }
            ++syncedCount;
        });
    return syncedCount;
}

} // namespace UVE::Scene
