// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/character_3d_uve.h"
#include "uve/objects/3d/health_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Marks a Character3D as the possessed player: input, spawn, look, and interact go here.
struct PlayerComponentUVE final {
    bool possessOnPlay = true;
    bool lookEnabled = true;
    float lookSensitivity = 0.12F;
    float lookStickSpeedDegrees = 180.0F;
    float minPitchDegrees = -80.0F;
    float maxPitchDegrees = 80.0F;
    float pitchDegrees = 0.0F;
};

[[nodiscard]] bool IsPlayer3DObjectComponentValidUVE(const PlayerComponentUVE& value) noexcept;

struct Player3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Player3D";

    ColliderComponentUVE collider = Character3DObjectDefinitionUVE::MakeDefaultColliderUVE();
    CharacterControllerComponentUVE controller{};
    PlayerComponentUVE player{};
    HealthComponentUVE health{};
};

[[nodiscard]] bool IsPlayer3DObjectDefinitionValidUVE(const Player3DObjectDefinitionUVE& value) noexcept;

void ApplyPlayer3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Player3DObjectDefinitionUVE& value);

[[nodiscard]] EntityUVE ResolvePossessedPlayerUVE(IEntityManagerUVE& entityManager);

[[nodiscard]] EntityUVE ResolvePlayCharacterUVE(IEntityManagerUVE& entityManager);

[[nodiscard]] EntityUVE FindPlayerLookTargetUVE(IEntityManagerUVE& entityManager, EntityUVE player);

[[nodiscard]] EntityUVE FindPlayerCameraUVE(IEntityManagerUVE& entityManager, EntityUVE player);

void MakePlayerCameraCurrentUVE(IEntityManagerUVE& entityManager, EntityUVE player);

[[nodiscard]] Math::Vector3UVE FaceMoveFromLookUVE(const Math::QuaternionUVE& yawRotation,
                                                   const Math::Vector3UVE& move) noexcept;

void ApplyPlayerLookUVE(PlayerComponentUVE& player, TransformComponentUVE& body,
                        TransformComponentUVE* lookTarget, Math::Vector2UVE pointerDelta,
                        Math::Vector2UVE stick, float deltaTimeSeconds) noexcept;

[[nodiscard]] EntityUVE FindFocusedInteractionAreaUVE(IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
