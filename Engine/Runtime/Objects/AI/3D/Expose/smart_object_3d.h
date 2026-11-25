// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "world_transform_component.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Marks a 3D entity as an interactive AI object (`interactionTag`, `interactionOffset`,
/// `maxSimultaneousUsers`, `cooldownSeconds`) that AI agents can discover, claim, and release.
struct SmartObject3DComponent final {
    std::string interactionTag{"Interact"};
    Math::Vector3 interactionOffset{0.0F, 0.0F, 1.0F};
    std::uint32_t maxSimultaneousUsers = 1U;
    float cooldownSeconds = 0.0F;
    bool enabled = true;

    // Runtime state, never persisted:
    std::vector<Entity> activeUsers{};
    float remainingCooldownSeconds = 0.0F;

    [[nodiscard]] bool operator==(const SmartObject3DComponent& other) const noexcept {
        return interactionTag == other.interactionTag &&
               interactionOffset == other.interactionOffset &&
               maxSimultaneousUsers == other.maxSimultaneousUsers &&
               cooldownSeconds == other.cooldownSeconds && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsSmartObject3DComponentValid(
    const SmartObject3DComponent& value) noexcept;

[[nodiscard]] SmartObject3DComponent SanitizeSmartObject3DComponent(
    const SmartObject3DComponent& value);

struct SmartObject3DObjectDefinition final {
    static constexpr std::string_view defaultName = "SmartObject3D";
    SmartObject3DComponent smartObject{};
};

[[nodiscard]] bool IsSmartObject3DObjectDefinitionValid(
    const SmartObject3DObjectDefinition& value) noexcept;

void ApplySmartObject3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                           const SmartObject3DObjectDefinition& value);

class SmartObject3D final {
public:
    SmartObject3D() = delete;

    /// True when `smartObject` is enabled, off cooldown, and has a free slot (or is already
    /// claimed by `agent`).
    [[nodiscard]] static bool CanClaim(const SmartObject3DComponent& smartObject,
                                          Entity agent) noexcept;

    /// Claims a slot for `agent` when available. Returns true on success.
    [[nodiscard]] static bool TryClaim(SmartObject3DComponent& smartObject, Entity agent);

    /// Releases `agent`'s claim and starts `cooldownSeconds` when the last user leaves.
    static bool Release(SmartObject3DComponent& smartObject, Entity agent) noexcept;

    /// Resolves the world-space interaction stand point (`TransformWorldPoint(world, interactionOffset)`).
    [[nodiscard]] static Math::Vector3 ResolveWorldInteractionPoint(
        const SmartObject3DComponent& smartObject,
        const WorldTransformComponent& worldTransform) noexcept;
};

/// Tactical world-query component (`searchRadiusMetres`, `optimalDistanceMetres`, `requiredTag`,
/// `preferUnclaimedSmartObjects`) that scores candidate positions/entities in the scene and caches
/// the best candidate (`bestEntity`, `bestWorldPosition`, `bestScore`).
struct WorldQuery3DComponent final {
    std::string requiredTag{};
    float searchRadiusMetres = 25.0F;
    float optimalDistanceMetres = 5.0F;
    bool preferUnclaimedSmartObjects = true;
    bool enabled = true;

    // Runtime state, never persisted:
    Entity bestEntity = kInvalidEntity;
    Math::Vector3 bestWorldPosition{0.0F, 0.0F, 0.0F};
    float bestScore = 0.0F;

    [[nodiscard]] bool operator==(const WorldQuery3DComponent& other) const noexcept {
        return requiredTag == other.requiredTag &&
               searchRadiusMetres == other.searchRadiusMetres &&
               optimalDistanceMetres == other.optimalDistanceMetres &&
               preferUnclaimedSmartObjects == other.preferUnclaimedSmartObjects &&
               enabled == other.enabled;
    }
};

[[nodiscard]] bool IsWorldQuery3DComponentValid(const WorldQuery3DComponent& value) noexcept;

[[nodiscard]] WorldQuery3DComponent SanitizeWorldQuery3DComponent(
    const WorldQuery3DComponent& value);

struct WorldQuery3DObjectDefinition final {
    static constexpr std::string_view defaultName = "WorldQuery3D";
    WorldQuery3DComponent query{};
};

[[nodiscard]] bool IsWorldQuery3DObjectDefinitionValid(
    const WorldQuery3DObjectDefinition& value) noexcept;

void ApplyWorldQuery3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                          const WorldQuery3DObjectDefinition& value);

/// Ticks all `SmartObject3DComponent` cooldowns (pruning destroyed users) and evaluates all
/// enabled `WorldQuery3DComponent` entities. Returns the number of queries that found a match.
std::size_t UpdateSmartObjectAndWorldQuery3D(IEntityManager& entityManager,
                                                float deltaTimeSeconds);

} // namespace UVE::Scene
