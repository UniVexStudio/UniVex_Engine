// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "audio_listener_3d.h"

#include "disabled_component.h"
#include "editor_internal_entity_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsEarlierEntityId(const Entity lhs, const Entity rhs) noexcept {
    if (lhs.index != rhs.index) {
        return lhs.index < rhs.index;
    }
    return lhs.generation < rhs.generation;
}

[[nodiscard]] bool IsEligibleListenerEntity(const IEntityManager& entityManager,
                                               const Entity entity) noexcept {
    return entityManager.IsAlive(entity) &&
           !entityManager.HasComponent<EditorInternalEntityComponent>(entity) &&
           !entityManager.HasComponent<DisabledComponent>(entity) &&
           entityManager.HasComponent<AudioListener3DComponent>(entity);
}

} // namespace

bool IsAudioListener3DComponentValid(const AudioListener3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.positionOffset);
}

AudioListener3DComponent SanitizeAudioListener3DComponent(
    const AudioListener3DComponent& value) noexcept {
    AudioListener3DComponent out = value;
    if (!IsFinite3DObjectVector(out.positionOffset)) {
        out.positionOffset = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    return out;
}

bool IsAudioListener3DObjectDefinitionValid(
    const AudioListener3DObjectDefinition& value) noexcept {
    return IsAudioListener3DComponentValid(value.listener);
}

void ApplyAudioListener3DObjectDefinition(IEntityManager& entityManager,
                                             const Entity entity,
                                             const AudioListener3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, AudioListener3DObjectDefinition::defaultName);
    const AudioListener3DComponent sanitized =
        SanitizeAudioListener3DComponent(value.listener);
    entityManager.AddComponent<AudioListener3DComponent>(entity, sanitized);
    if (sanitized.current) {
        AudioListener3D::MakeCurrent(entityManager, entity);
    }
}

void AudioListener3D::MakeCurrent(IEntityManager& entityManager,
                                        const Entity listenerEntity) {
    if (!entityManager.IsAlive(listenerEntity) ||
        !entityManager.HasComponent<AudioListener3DComponent>(listenerEntity)) {
        return;
    }
    entityManager.ForEach<AudioListener3DComponent>(
        [&](const Entity entity, AudioListener3DComponent& listener) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity)) {
                return;
            }
            listener.current = (entity == listenerEntity);
        });
}

ResolvedAudioListener3DPose AudioListener3D::ResolvePoseFromWorldTransform(
    const Entity entity, const AudioListener3DComponent& listener,
    const WorldTransformComponent& worldTransform) noexcept {
    const AudioListener3DComponent safe = SanitizeAudioListener3DComponent(listener);
    ResolvedAudioListener3DPose resolved{};
    resolved.entity = entity;
    if (!IsWorldTransformComponentValid(worldTransform)) {
        return resolved;
    }
    if (safe.ignoreRotation) {
        resolved.position = worldTransform.worldPosition + safe.positionOffset;
        resolved.forward = Math::Vector3{0.0F, 0.0F, -1.0F};
        resolved.up = Math::Vector3{0.0F, 1.0F, 0.0F};
        return resolved;
    }
    resolved.position =
        worldTransform.worldPosition +
        Math::RotateVector(worldTransform.worldRotation, safe.positionOffset);
    resolved.forward = ResolveWorldForward(worldTransform);
    resolved.up = ResolveWorldUp(worldTransform);
    return resolved;
}

bool AudioListener3D::TryResolveActiveListenerPose(
    IEntityManager& entityManager, const Entity fallbackCameraEntity,
    ResolvedAudioListener3DPose& outPose) {
    Entity chosenListener = kInvalidEntity;
    entityManager.ForEach<AudioListener3DComponent>(
        [&](const Entity entity, const AudioListener3DComponent& listener) {
            if (!listener.current || !IsAudioListener3DComponentValid(listener) ||
                !IsEligibleListenerEntity(entityManager, entity) ||
                !entityManager.HasComponent<WorldTransformComponent>(entity)) {
                return;
            }
            const WorldTransformComponent& worldTransform =
                entityManager.GetComponent<WorldTransformComponent>(entity);
            if (!IsWorldTransformComponentValid(worldTransform)) {
                return;
            }
            if (chosenListener == kInvalidEntity ||
                IsEarlierEntityId(entity, chosenListener)) {
                chosenListener = entity;
            }
        });

    if (chosenListener != kInvalidEntity) {
        outPose = ResolvePoseFromWorldTransform(
            chosenListener,
            entityManager.GetComponent<AudioListener3DComponent>(chosenListener),
            entityManager.GetComponent<WorldTransformComponent>(chosenListener));
        return true;
    }

    if (fallbackCameraEntity != kInvalidEntity &&
        entityManager.IsAlive(fallbackCameraEntity) &&
        entityManager.HasComponent<WorldTransformComponent>(fallbackCameraEntity)) {
        const WorldTransformComponent& cameraWorld =
            entityManager.GetComponent<WorldTransformComponent>(fallbackCameraEntity);
        if (IsWorldTransformComponentValid(cameraWorld)) {
            outPose = ResolvePoseFromWorldTransform(
                fallbackCameraEntity, AudioListener3DComponent{}, cameraWorld);
            return true;
        }
    }
    return false;
}

} // namespace UVE::Scene
