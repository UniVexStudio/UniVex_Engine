// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/player_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

#include "uve/component/camera_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/name_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/interaction_area_3d_uve.h"
#include "uve/objects/3d/spring_arm_3d_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kPiUVE = 3.14159265358979323846F;
constexpr float kRadiansPerDegreeUVE = kPiUVE / 180.0F;
constexpr float kMaximumPitchLimitDegreesUVE = 89.0F;

[[nodiscard]] bool IsEarlierEntityUVE(const EntityUVE lhs, const EntityUVE rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

[[nodiscard]] bool IsFinitePositiveOrZeroUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsPlayer3DObjectComponentValidUVE(const PlayerComponentUVE& value) noexcept {
    if (!IsFinitePositiveOrZeroUVE(value.lookSensitivity) ||
        !IsFinitePositiveOrZeroUVE(value.lookStickSpeedDegrees) || !std::isfinite(value.minPitchDegrees) ||
        !std::isfinite(value.maxPitchDegrees) || !std::isfinite(value.pitchDegrees)) {
        return false;
    }
    if (value.minPitchDegrees > value.maxPitchDegrees) {
        return false;
    }
    if (value.minPitchDegrees < -kMaximumPitchLimitDegreesUVE ||
        value.maxPitchDegrees > kMaximumPitchLimitDegreesUVE) {
        return false;
    }
    return true;
}

bool IsPlayer3DObjectDefinitionValidUVE(const Player3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider) &&
           IsCharacterControllerComponentValidUVE(value.controller) &&
           IsPlayer3DObjectComponentValidUVE(value.player);
}

void ApplyPlayer3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Player3DObjectDefinitionUVE& value) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    Character3DObjectDefinitionUVE character{};
    character.collider = value.collider;
    character.controller = value.controller;
    ApplyCharacter3DObjectDefinitionUVE(entityManager, entity, character);
    if (entityManager.HasComponentUVE<NameComponentUVE>(entity) &&
        entityManager.GetComponentUVE<NameComponentUVE>(entity).name ==
            Character3DObjectDefinitionUVE::defaultName) {
        entityManager.GetComponentUVE<NameComponentUVE>(entity).name =
            std::string{Player3DObjectDefinitionUVE::defaultName};
    }
    if (!entityManager.HasComponentUVE<PlayerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<PlayerComponentUVE>(entity, value.player);
    }
}

EntityUVE ResolvePossessedPlayerUVE(IEntityManagerUVE& entityManager) {
    EntityUVE best = kInvalidEntityUVE;
    entityManager.ForEachUVE<PlayerComponentUVE>(
        [&entityManager, &best](const EntityUVE entity, PlayerComponentUVE& player) {
            if (!player.possessOnPlay || !IsPlayer3DObjectComponentValidUVE(player) ||
                !entityManager.HasComponentUVE<CharacterControllerComponentUVE>(entity) ||
                !entityManager.HasComponentUVE<TransformComponentUVE>(entity) ||
                !entityManager.HasComponentUVE<HierarchyComponentUVE>(entity)) {
                return;
            }
            if (best == kInvalidEntityUVE || IsEarlierEntityUVE(entity, best)) {
                best = entity;
            }
        });
    return best;
}

EntityUVE FindPlayerLookTargetUVE(IEntityManagerUVE& entityManager, const EntityUVE player) {
    if (!entityManager.IsAliveUVE(player)) {
        return kInvalidEntityUVE;
    }
    EntityUVE springArm = kInvalidEntityUVE;
    EntityUVE camera = kInvalidEntityUVE;
    std::int64_t springOrder = std::numeric_limits<std::int64_t>::max();
    std::int64_t cameraOrder = std::numeric_limits<std::int64_t>::max();
    entityManager.ForEachUVE<HierarchyComponentUVE>(
        [&entityManager, player, &springArm, &camera, &springOrder, &cameraOrder](
            const EntityUVE entity, HierarchyComponentUVE& hierarchy) {
            if (hierarchy.parent != player) {
                return;
            }
            if (entityManager.HasComponentUVE<SpringArm3DComponentUVE>(entity) &&
                hierarchy.siblingOrder < springOrder) {
                springArm = entity;
                springOrder = hierarchy.siblingOrder;
            }
            if (entityManager.HasComponentUVE<CameraComponentUVE>(entity) &&
                hierarchy.siblingOrder < cameraOrder) {
                camera = entity;
                cameraOrder = hierarchy.siblingOrder;
            }
        });
    if (springArm != kInvalidEntityUVE) {
        return springArm;
    }
    return camera;
}

Math::Vector3UVE FaceMoveFromLookUVE(const Math::QuaternionUVE& yawRotation,
                                     const Math::Vector3UVE& move) noexcept {
    if (!Math::IsFiniteUVE(yawRotation) || !std::isfinite(move.x) || !std::isfinite(move.z)) {
        return {};
    }
    const Math::Vector3UVE world = Math::RotateVectorUVE(yawRotation, Math::Vector3UVE{move.x, 0.0F, move.z});
    return Math::Vector3UVE{world.x, 0.0F, world.z};
}

void ApplyPlayerLookUVE(PlayerComponentUVE& player, TransformComponentUVE& body,
                        TransformComponentUVE* lookTarget, const Math::Vector2UVE pointerDelta,
                        const Math::Vector2UVE stick, const float deltaTimeSeconds) noexcept {
    if (!player.lookEnabled || !IsPlayer3DObjectComponentValidUVE(player)) {
        return;
    }
    const float pointerX = std::isfinite(pointerDelta.x) ? pointerDelta.x : 0.0F;
    const float pointerY = std::isfinite(pointerDelta.y) ? pointerDelta.y : 0.0F;
    const float stickX = std::isfinite(stick.x) ? std::clamp(stick.x, -1.0F, 1.0F) : 0.0F;
    const float stickY = std::isfinite(stick.y) ? std::clamp(stick.y, -1.0F, 1.0F) : 0.0F;
    const float dt = std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F ? deltaTimeSeconds : 0.0F;

    const float yawDeltaDegrees =
        -pointerX * player.lookSensitivity + stickX * player.lookStickSpeedDegrees * dt;
    player.pitchDegrees = std::clamp(
        player.pitchDegrees - pointerY * player.lookSensitivity + stickY * player.lookStickSpeedDegrees * dt,
        player.minPitchDegrees, player.maxPitchDegrees);

    if (std::fabs(yawDeltaDegrees) > 0.0F) {
        Math::QuaternionUVE yaw{};
        if (Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                      yawDeltaDegrees * kRadiansPerDegreeUVE, yaw)) {
            body.localRotation = Math::MultiplyUVE(yaw, body.localRotation);
            Math::QuaternionUVE normalized{};
            if (Math::TryNormalizeUVE(body.localRotation, normalized)) {
                body.localRotation = normalized;
            }
            body.rotationEditMode = RotationEditModeUVE::Quaternion;
        }
    }

    if (lookTarget == nullptr) {
        return;
    }
    Math::QuaternionUVE pitch{};
    if (Math::TryMakeAxisAngleUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                  player.pitchDegrees * kRadiansPerDegreeUVE, pitch)) {
        lookTarget->localRotation = pitch;
        lookTarget->rotationEditMode = RotationEditModeUVE::Quaternion;
    }
}

EntityUVE FindFocusedInteractionAreaUVE(IEntityManagerUVE& entityManager) {
    EntityUVE focused = kInvalidEntityUVE;
    entityManager.ForEachUVE<InteractionArea3DComponentUVE>(
        [&focused](const EntityUVE entity, InteractionArea3DComponentUVE& area) {
            if (focused == kInvalidEntityUVE && area.focusedByPrimaryInteractor) {
                focused = entity;
            }
        });
    return focused;
}

} // namespace UVE::Scene
