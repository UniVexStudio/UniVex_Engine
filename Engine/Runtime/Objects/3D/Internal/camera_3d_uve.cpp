// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/camera_3d_uve.h"

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsCamera3DObjectDefinitionValidUVE(const Camera3DObjectDefinitionUVE& value) noexcept {
    return IsCameraComponentValidUVE(value.camera);
}

bool IsDocumentCameraEntityUVE(const IEntityManagerUVE& entityManager, const EntityUVE entity) {
    return entityManager.IsAliveUVE(entity) &&
           entityManager.HasComponentUVE<CameraComponentUVE>(entity) &&
           entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity) &&
           !entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity);
}

void MakeCameraCurrentUVE(IEntityManagerUVE& entityManager, const EntityUVE entity) {
    if (!IsDocumentCameraEntityUVE(entityManager, entity)) {
        return;
    }
    entityManager.ForEachUVE<CameraComponentUVE>(
        [&entityManager, entity](const EntityUVE other, CameraComponentUVE& camera) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(other)) {
                camera.current = false;
                return;
            }
            camera.current = other == entity;
        });
}

std::optional<EntityUVE> FindCurrentCameraEntityUVE(IEntityManagerUVE& entityManager) {
    std::optional<EntityUVE> current;
    std::optional<EntityUVE> first;
    entityManager.ForEachUVE<WorldTransformComponentUVE, CameraComponentUVE>(
        [&entityManager, &current, &first](const EntityUVE entity, const WorldTransformComponentUVE&,
                                           const CameraComponentUVE& camera) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity)) {
                return;
            }
            if (!first.has_value()) {
                first = entity;
            }
            if (!current.has_value() && camera.current) {
                current = entity;
            }
        });
    return current.has_value() ? current : first;
}

void ApplyCamera3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Camera3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, Camera3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<CameraComponentUVE>(entity, value.camera);
    bool otherCurrent = false;
    entityManager.ForEachUVE<CameraComponentUVE>(
        [&entityManager, entity, &otherCurrent](const EntityUVE other, const CameraComponentUVE& camera) {
            if (other != entity && camera.current && IsDocumentCameraEntityUVE(entityManager, other)) {
                otherCurrent = true;
            }
        });
    if (value.camera.current || !otherCurrent) {
        MakeCameraCurrentUVE(entityManager, entity);
    }
}

} // namespace UVE::Scene
