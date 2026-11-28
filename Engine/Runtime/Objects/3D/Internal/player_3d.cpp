// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "player_3d.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "camera_component.h"
#include "hierarchy_component.h"
#include "name_component.h"
#include "i_entity_manager.h"
#include "quaternion.h"
#include "scalar.h"
#include "camera_3d.h"
#include "interaction_area_3d.h"
#include "spring_arm_3d.h"

namespace UVE::Scene {
namespace {

constexpr float kMaximumPitchLimitDegrees = 89.0F;

[[nodiscard]] bool IsEarlierEntity(const Entity lhs, const Entity rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

[[nodiscard]] bool IsFinitePositiveOrZero(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsPlayer3DObjectComponentValid(const PlayerComponent& value) noexcept {
    if (!IsFinitePositiveOrZero(value.lookSensitivity) ||
        !IsFinitePositiveOrZero(value.lookStickSpeedDegrees) || !std::isfinite(value.minPitchDegrees) ||
        !std::isfinite(value.maxPitchDegrees) || !std::isfinite(value.pitchDegrees)) {
        return false;
    }
    if (value.minPitchDegrees > value.maxPitchDegrees) {
        return false;
    }
    if (value.minPitchDegrees < -kMaximumPitchLimitDegrees ||
        value.maxPitchDegrees > kMaximumPitchLimitDegrees) {
        return false;
    }
    return true;
}

bool IsPlayer3DObjectDefinitionValid(const Player3DObjectDefinition& value) noexcept {
    ControllerComponent authoredController;
    authoredController.kind = value.controllerKind;
    return (!value.collider.has_value() || IsColliderComponentValid(*value.collider)) &&
           IsCharacterControllerComponentValid(value.controller) &&
           IsPlayer3DObjectComponentValid(value.player) && IsHealthComponentValid(value.health) &&
           IsControllerComponentValid(authoredController);
}

void ApplyPlayer3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                      const Player3DObjectDefinition& value) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    Character3DObjectDefinition character{};
    character.collider = value.collider;
    character.controller = value.controller;
    character.controllerKind = value.controllerKind;
    ApplyCharacter3DObjectDefinition(entityManager, entity, character);
    if (entityManager.HasComponent<NameComponent>(entity) &&
        entityManager.GetComponent<NameComponent>(entity).name ==
            Character3DObjectDefinition::defaultName) {
        entityManager.GetComponent<NameComponent>(entity).name =
            std::string{Player3DObjectDefinition::defaultName};
    }
    if (!entityManager.HasComponent<PlayerComponent>(entity)) {
        entityManager.AddComponent<PlayerComponent>(entity, value.player);
    }
    if (!entityManager.HasComponent<HealthComponent>(entity)) {
        entityManager.AddComponent<HealthComponent>(entity, value.health);
    }
    if (!entityManager.HasComponent<PawnComponent>(entity)) {
        entityManager.AddComponent<PawnComponent>(entity, PawnComponent{});
    }
    if (!entityManager.HasComponent<ControllerComponent>(entity)) {
        ControllerComponent possession;
        possession.kind = value.controllerKind;
        entityManager.AddComponent<ControllerComponent>(entity, possession);
    }
}

void MaintainPlayerPossession(IEntityManager& entityManager) {
    std::vector<Entity> flagged;
    entityManager.ForEach<PlayerComponent>(
        [&entityManager, &flagged](const Entity entity, const PlayerComponent& player) {
            if (!player.possessOnPlay || !IsPlayer3DObjectComponentValid(player) ||
                !entityManager.HasComponent<PawnComponent>(entity) ||
                !entityManager.HasComponent<ControllerComponent>(entity)) {
                return;
            }
            const ControllerComponent& controller =
                entityManager.GetComponent<ControllerComponent>(entity);
            const PawnComponent& pawn = entityManager.GetComponent<PawnComponent>(entity);
            if (controller.kind != ControllerKind::Player ||
                controller.pawn != kInvalidEntity || pawn.controller != kInvalidEntity) {
                return;
            }
            flagged.push_back(entity);
        });
    for (const Entity entity : flagged) {
        static_cast<void>(PossessController(entityManager, entity, entity));
    }
}

Entity ResolvePossessedPlayer(IEntityManager& entityManager) {
    Entity best = kInvalidEntity;
    entityManager.ForEach<PlayerComponent>(
        [&entityManager, &best](const Entity entity, PlayerComponent& player) {
            if (!player.possessOnPlay || !IsPlayer3DObjectComponentValid(player) ||
                !entityManager.HasComponent<CharacterControllerComponent>(entity) ||
                !entityManager.HasComponent<TransformComponent>(entity) ||
                !entityManager.HasComponent<HierarchyComponent>(entity)) {
                return;
            }
            const Entity driver = FindPawnController(entityManager, entity);
            if (driver == kInvalidEntity ||
                entityManager.GetComponent<ControllerComponent>(driver).kind !=
                    ControllerKind::Player) {
                return;
            }
            if (best == kInvalidEntity || IsEarlierEntity(entity, best)) {
                best = entity;
            }
        });
    return best;
}

Entity ResolvePlayCharacter(IEntityManager& entityManager) {
    const Entity possessed = ResolvePossessedPlayer(entityManager);
    if (possessed != kInvalidEntity) {
        return possessed;
    }
    // A body already meant for a player (Player-kind controller, or none at all) is preferred over
    // an AI-kind one, so a level with a hero and a crowd of NPCs plays the hero. Only when every
    // character is AI-kind does the first of them stand in, so a lone Character3D still playtests.
    Entity preferred = kInvalidEntity;
    Entity anyBody = kInvalidEntity;
    entityManager.ForEach<CharacterControllerComponent>(
        [&entityManager, &preferred, &anyBody](const Entity entity, CharacterControllerComponent&) {
            if (!entityManager.HasComponent<TransformComponent>(entity) ||
                !entityManager.HasComponent<HierarchyComponent>(entity)) {
                return;
            }
            const bool aiDriven = entityManager.HasComponent<ControllerComponent>(entity) &&
                                  entityManager.GetComponent<ControllerComponent>(entity).kind == ControllerKind::AI;
            if (!aiDriven && (preferred == kInvalidEntity || IsEarlierEntity(entity, preferred))) {
                preferred = entity;
            }
            if (anyBody == kInvalidEntity || IsEarlierEntity(entity, anyBody)) {
                anyBody = entity;
            }
        });
    return preferred != kInvalidEntity ? preferred : anyBody;
}

bool ShouldSpawnDefaultPlayer(IEntityManager& entityManager) {
    return ResolvePlayCharacter(entityManager) == kInvalidEntity;
}

Entity FindPlayerCamera(IEntityManager& entityManager, const Entity player) {
    if (!entityManager.IsAlive(player)) {
        return kInvalidEntity;
    }
    if (entityManager.HasComponent<CameraComponent>(player)) {
        return player;
    }
    Entity best = kInvalidEntity;
    entityManager.ForEach<CameraComponent>([&entityManager, player, &best](const Entity entity,
                                                                                CameraComponent&) {
        if (entity == player) {
            return;
        }
        Entity cursor = entity;
        bool underPlayer = false;
        while (entityManager.IsAlive(cursor)) {
            if (!entityManager.HasComponent<HierarchyComponent>(cursor)) {
                break;
            }
            const Entity parent = entityManager.GetComponent<HierarchyComponent>(cursor).parent;
            if (parent == player) {
                underPlayer = true;
                break;
            }
            if (parent == cursor || parent == kInvalidEntity) {
                break;
            }
            cursor = parent;
        }
        if (!underPlayer) {
            return;
        }
        if (best == kInvalidEntity || IsEarlierEntity(entity, best)) {
            best = entity;
        }
    });
    return best;
}

void MakePlayerCameraCurrent(IEntityManager& entityManager, const Entity player) {
    const Entity camera = FindPlayerCamera(entityManager, player);
    if (camera != kInvalidEntity) {
        MakeCameraCurrent(entityManager, camera);
    }
}

Entity FindPlayerLookTarget(IEntityManager& entityManager, const Entity player) {
    if (!entityManager.IsAlive(player)) {
        return kInvalidEntity;
    }
    Entity springArm = kInvalidEntity;
    Entity camera = kInvalidEntity;
    std::int64_t springOrder = std::numeric_limits<std::int64_t>::max();
    std::int64_t cameraOrder = std::numeric_limits<std::int64_t>::max();
    entityManager.ForEach<HierarchyComponent>(
        [&entityManager, player, &springArm, &camera, &springOrder, &cameraOrder](
            const Entity entity, HierarchyComponent& hierarchy) {
            if (hierarchy.parent != player) {
                return;
            }
            if (entityManager.HasComponent<SpringArm3DComponent>(entity) &&
                hierarchy.siblingOrder < springOrder) {
                springArm = entity;
                springOrder = hierarchy.siblingOrder;
            }
            if (entityManager.HasComponent<CameraComponent>(entity) &&
                hierarchy.siblingOrder < cameraOrder) {
                camera = entity;
                cameraOrder = hierarchy.siblingOrder;
            }
        });
    if (springArm != kInvalidEntity) {
        return springArm;
    }
    return camera;
}

Math::Vector3 FaceMoveFromLook(const Math::Quaternion& yawRotation,
                                     const Math::Vector3& move) noexcept {
    if (!Math::IsFinite(yawRotation) || !std::isfinite(move.x) || !std::isfinite(move.z)) {
        return {};
    }
    const Math::Vector3 world = Math::RotateVector(yawRotation, Math::Vector3{move.x, 0.0F, move.z});
    return Math::Vector3{world.x, 0.0F, world.z};
}

void ApplyPlayerLook(PlayerComponent& player, TransformComponent& body,
                        TransformComponent* lookTarget, const Math::Vector2 pointerDelta,
                        const Math::Vector2 stick, const float deltaTimeSeconds) noexcept {
    if (!player.lookEnabled || !IsPlayer3DObjectComponentValid(player)) {
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
        Math::Quaternion yaw{};
        if (Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F},
                                      Math::DegToRad(yawDeltaDegrees), yaw)) {
            body.localRotation = Math::Multiply(yaw, body.localRotation);
            Math::Quaternion normalized{};
            if (Math::TryNormalize(body.localRotation, normalized)) {
                body.localRotation = normalized;
            }
            body.rotationEditMode = RotationEditMode::Quaternion;
        }
    }

    if (lookTarget == nullptr) {
        return;
    }
    Math::Quaternion pitch{};
    if (Math::TryMakeAxisAngle(Math::Vector3{1.0F, 0.0F, 0.0F},
                                  Math::DegToRad(player.pitchDegrees), pitch)) {
        lookTarget->localRotation = pitch;
        lookTarget->rotationEditMode = RotationEditMode::Quaternion;
    }
}

Entity FindFocusedInteractionArea(IEntityManager& entityManager) {
    Entity focused = kInvalidEntity;
    entityManager.ForEach<InteractionArea3DComponent>(
        [&focused](const Entity entity, InteractionArea3DComponent& area) {
            if (focused == kInvalidEntity && area.focusedByPrimaryInteractor) {
                focused = entity;
            }
        });
    return focused;
}

} // namespace UVE::Scene
