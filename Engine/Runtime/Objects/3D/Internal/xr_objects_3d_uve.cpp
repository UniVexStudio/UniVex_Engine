// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/xr_objects_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/camera_component_uve.h"
#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

void ApplyXROrigin3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const XROrigin3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, XROrigin3DObjectDefinitionUVE::defaultName);
    XROrigin3DComponentUVE out = value.origin;
    if (!std::isfinite(out.worldScale) || out.worldScale <= 0.0F) {
        out.worldScale = 1.0F;
    }
    entityManager.AddComponentUVE<XROrigin3DComponentUVE>(entity, out);
}

void ApplyXRCamera3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const XRCamera3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, XRCamera3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<CameraComponentUVE>(entity, CameraComponentUVE{});
    XRTracker3DComponentUVE out = value.tracker;
    out.trackerRole = XRTrackedHandUVE::Head;
    entityManager.AddComponentUVE<XRTracker3DComponentUVE>(entity, out);
}

void ApplyXRController3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const XRController3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              XRController3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<XRTracker3DComponentUVE>(entity, value.tracker);
}

void ApplyXRFaceModifier3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                              const EntityUVE entity,
                                              const XRFaceModifier3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              XRFaceModifier3DObjectDefinitionUVE::defaultName);
    XRFaceModifier3DComponentUVE out = value.faceModifier;
    for (float& w : out.blendWeights) {
        w = std::isfinite(w) ? std::clamp(w, 0.0F, 1.0F) : 0.0F;
    }
    entityManager.AddComponentUVE<XRFaceModifier3DComponentUVE>(entity, std::move(out));
}

std::size_t UpdateXRObjects3DUVE(IEntityManagerUVE& entityManager) {
    float worldScale = 1.0F;
    entityManager.ForEachUVE<XROrigin3DComponentUVE>(
        [&](const EntityUVE entity, const XROrigin3DComponentUVE& orig) {
            if (!entityManager.HasComponentUVE<DisabledComponentUVE>(entity) && orig.currentOrigin &&
                std::isfinite(orig.worldScale) && orig.worldScale > 0.0F) {
                worldScale = orig.worldScale;
            }
        });

    std::size_t syncedCount = 0U;
    entityManager.ForEachUVE<XRTracker3DComponentUVE, TransformComponentUVE>(
        [&](const EntityUVE entity, const XRTracker3DComponentUVE& tracker,
            TransformComponentUVE& local) {
            if (!tracker.trackingActive ||
                entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsFinite3DObjectVectorUVE(tracker.trackedLocalPosition) ||
                !IsFinite3DObjectQuaternionUVE(tracker.trackedLocalRotation)) {
                return;
            }
            local.localPosition = tracker.trackedLocalPosition * worldScale;
            local.localRotation = tracker.trackedLocalRotation;
            if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
            }
            ++syncedCount;
        });

    entityManager.ForEachUVE<XRFaceModifier3DComponentUVE>(
        [&](const EntityUVE entity, XRFaceModifier3DComponentUVE& face) {
            if (!face.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity)) {
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
