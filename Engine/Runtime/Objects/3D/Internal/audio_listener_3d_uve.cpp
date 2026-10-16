// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/audio_listener_3d_uve.h"

#include "uve/component/disabled_component_uve.h"
#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsEarlierEntityIdUVE(const EntityUVE lhs, const EntityUVE rhs) noexcept {
    if (lhs.index != rhs.index) {
        return lhs.index < rhs.index;
    }
    return lhs.generation < rhs.generation;
}

[[nodiscard]] bool IsEligibleListenerEntityUVE(const IEntityManagerUVE& entityManager,
                                               const EntityUVE entity) noexcept {
    return entityManager.IsAliveUVE(entity) &&
           !entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) &&
           !entityManager.HasComponentUVE<DisabledComponentUVE>(entity) &&
           entityManager.HasComponentUVE<AudioListener3DComponentUVE>(entity);
}

} // namespace

bool IsAudioListener3DComponentValidUVE(const AudioListener3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.positionOffset);
}

AudioListener3DComponentUVE SanitizeAudioListener3DComponentUVE(
    const AudioListener3DComponentUVE& value) noexcept {
    AudioListener3DComponentUVE out = value;
    if (!IsFinite3DObjectVectorUVE(out.positionOffset)) {
        out.positionOffset = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    return out;
}

bool IsAudioListener3DObjectDefinitionValidUVE(
    const AudioListener3DObjectDefinitionUVE& value) noexcept {
    return IsAudioListener3DComponentValidUVE(value.listener);
}

void ApplyAudioListener3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                             const EntityUVE entity,
                                             const AudioListener3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, AudioListener3DObjectDefinitionUVE::defaultName);
    const AudioListener3DComponentUVE sanitized =
        SanitizeAudioListener3DComponentUVE(value.listener);
    entityManager.AddComponentUVE<AudioListener3DComponentUVE>(entity, sanitized);
    if (sanitized.current) {
        AudioListener3DUVE::MakeCurrentUVE(entityManager, entity);
    }
}

void AudioListener3DUVE::MakeCurrentUVE(IEntityManagerUVE& entityManager,
                                        const EntityUVE listenerEntity) {
    if (!entityManager.IsAliveUVE(listenerEntity) ||
        !entityManager.HasComponentUVE<AudioListener3DComponentUVE>(listenerEntity)) {
        return;
    }
    entityManager.ForEachUVE<AudioListener3DComponentUVE>(
        [&](const EntityUVE entity, AudioListener3DComponentUVE& listener) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity)) {
                return;
            }
            listener.current = (entity == listenerEntity);
        });
}

ResolvedAudioListener3DPoseUVE AudioListener3DUVE::ResolvePoseFromWorldTransformUVE(
    const EntityUVE entity, const AudioListener3DComponentUVE& listener,
    const WorldTransformComponentUVE& worldTransform) noexcept {
    const AudioListener3DComponentUVE safe = SanitizeAudioListener3DComponentUVE(listener);
    ResolvedAudioListener3DPoseUVE resolved{};
    resolved.entity = entity;
    if (!IsWorldTransformComponentValidUVE(worldTransform)) {
        return resolved;
    }
    if (safe.ignoreRotation) {
        resolved.position = worldTransform.worldPosition + safe.positionOffset;
        resolved.forward = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
        resolved.up = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        return resolved;
    }
    resolved.position =
        worldTransform.worldPosition +
        Math::RotateVectorUVE(worldTransform.worldRotation, safe.positionOffset);
    resolved.forward = ResolveWorldForwardUVE(worldTransform);
    resolved.up = ResolveWorldUpUVE(worldTransform);
    return resolved;
}

bool AudioListener3DUVE::TryResolveActiveListenerPoseUVE(
    IEntityManagerUVE& entityManager, const EntityUVE fallbackCameraEntity,
    ResolvedAudioListener3DPoseUVE& outPose) {
    EntityUVE chosenListener = kInvalidEntityUVE;
    entityManager.ForEachUVE<AudioListener3DComponentUVE>(
        [&](const EntityUVE entity, const AudioListener3DComponentUVE& listener) {
            if (!listener.current || !IsAudioListener3DComponentValidUVE(listener) ||
                !IsEligibleListenerEntityUVE(entityManager, entity) ||
                !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                return;
            }
            const WorldTransformComponentUVE& worldTransform =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
            if (!IsWorldTransformComponentValidUVE(worldTransform)) {
                return;
            }
            if (chosenListener == kInvalidEntityUVE ||
                IsEarlierEntityIdUVE(entity, chosenListener)) {
                chosenListener = entity;
            }
        });

    if (chosenListener != kInvalidEntityUVE) {
        outPose = ResolvePoseFromWorldTransformUVE(
            chosenListener,
            entityManager.GetComponentUVE<AudioListener3DComponentUVE>(chosenListener),
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(chosenListener));
        return true;
    }

    if (fallbackCameraEntity != kInvalidEntityUVE &&
        entityManager.IsAliveUVE(fallbackCameraEntity) &&
        entityManager.HasComponentUVE<WorldTransformComponentUVE>(fallbackCameraEntity)) {
        const WorldTransformComponentUVE& cameraWorld =
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(fallbackCameraEntity);
        if (IsWorldTransformComponentValidUVE(cameraWorld)) {
            outPose = ResolvePoseFromWorldTransformUVE(
                fallbackCameraEntity, AudioListener3DComponentUVE{}, cameraWorld);
            return true;
        }
    }
    return false;
}

} // namespace UVE::Scene
