// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>

#include <string_view>

#include "character_controller_component.h"
#include "collider_component.h"
#include "entity.h"
#include "pawn.h"
#include "transform_component.h"
#include "vector2.h"
#include "vector3.h"
#include "character_3d.h"
#include "health.h"

namespace UVE::Scene {

class IEntityManager;

/// Marks a Character3D as the possessed player: input, spawn, look, and interact go here. While
/// possessOnPlay is set, the engine keeps the body self-possessed whenever its possession links
/// are free - never stealing it back from a director mid-takeover.
struct PlayerComponent final {
    bool possessOnPlay = true;
    bool lookEnabled = true;
    float lookSensitivity = 0.12F;
    float lookStickSpeedDegrees = 180.0F;
    float minPitchDegrees = -80.0F;
    float maxPitchDegrees = 80.0F;
    float pitchDegrees = 0.0F;
};

[[nodiscard]] bool IsPlayer3DObjectComponentValid(const PlayerComponent& value) noexcept;

struct Player3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Player3D";

    /// No collider of its own, for the same reason a Character3D has none: the body's shape is a
    /// Collider3D the author adds under it, where it can be seen, selected and given a shape. A
    /// built-in capsule made that Collider3D a second shape in the same place, and the controller
    /// pushed the body out of the one nobody had asked for.
    std::optional<ColliderComponent> collider = std::nullopt;
    CharacterControllerComponent controller{};
    PlayerComponent player{};
    HealthComponent health{};
    // Only the kind is authored: possession links and pawn input are runtime state, so the
    // definition carries no Pawn/Controller components to overwrite them with.
    ControllerKind controllerKind = ControllerKind::Player;
};

[[nodiscard]] bool IsPlayer3DObjectDefinitionValid(const Player3DObjectDefinition& value) noexcept;

void ApplyPlayer3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                      const Player3DObjectDefinition& value);

/// The earliest player-marked body currently driven through mutual possession by a
/// Player-kind controller - a self-possessed player, or a pawn a Player-kind director holds.
/// AI-held and unpossessed bodies never resolve, whatever their flags say.
[[nodiscard]] Entity ResolvePossessedPlayer(IEntityManager& entityManager);

/// Opportunistic self-possession fill for player-marked bodies: every entity with a valid
/// PlayerComponent whose possessOnPlay is set, carrying a Player-kind controller that drives
/// nothing and a pawn driven by nothing, is self-possessed. Never steals, so a director's
/// takeover survives until released - and the fill hands the body back the frame after. The
/// engine calls this before resolving or routing, every frame; safe to call whenever.
void MaintainPlayerPossession(IEntityManager& entityManager);

[[nodiscard]] Entity ResolvePlayCharacter(IEntityManager& entityManager);

/// Whether Play should spawn the project's default player: only when the level holds no character at
/// all. A character the author placed is the player, so Play never adds a second one beside it.
[[nodiscard]] bool ShouldSpawnDefaultPlayer(IEntityManager& entityManager);

[[nodiscard]] Entity FindPlayerLookTarget(IEntityManager& entityManager, Entity player);

[[nodiscard]] Entity FindPlayerCamera(IEntityManager& entityManager, Entity player);

void MakePlayerCameraCurrent(IEntityManager& entityManager, Entity player);

[[nodiscard]] Math::Vector3 FaceMoveFromLook(const Math::Quaternion& yawRotation,
                                                   const Math::Vector3& move) noexcept;

void ApplyPlayerLook(PlayerComponent& player, TransformComponent& body,
                        TransformComponent* lookTarget, Math::Vector2 pointerDelta,
                        Math::Vector2 stick, float deltaTimeSeconds) noexcept;

[[nodiscard]] Entity FindFocusedInteractionArea(IEntityManager& entityManager);

} // namespace UVE::Scene
