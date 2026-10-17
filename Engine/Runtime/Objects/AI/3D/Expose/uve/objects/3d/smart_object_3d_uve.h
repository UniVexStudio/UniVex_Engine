// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Marks a 3D entity as an interactive AI object (`interactionTag`, `interactionOffset`,
/// `maxSimultaneousUsers`, `cooldownSeconds`) that AI agents can discover, claim, and release.
struct SmartObject3DComponentUVE final {
    std::string interactionTag{"Interact"};
    Math::Vector3UVE interactionOffset{0.0F, 0.0F, 1.0F};
    std::uint32_t maxSimultaneousUsers = 1U;
    float cooldownSeconds = 0.0F;
    bool enabled = true;

    // Runtime state, never persisted:
    std::vector<EntityUVE> activeUsers{};
    float remainingCooldownSeconds = 0.0F;

    [[nodiscard]] bool operator==(const SmartObject3DComponentUVE& other) const noexcept {
        return interactionTag == other.interactionTag &&
               interactionOffset == other.interactionOffset &&
               maxSimultaneousUsers == other.maxSimultaneousUsers &&
               cooldownSeconds == other.cooldownSeconds && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsSmartObject3DComponentValidUVE(
    const SmartObject3DComponentUVE& value) noexcept;

[[nodiscard]] SmartObject3DComponentUVE SanitizeSmartObject3DComponentUVE(
    const SmartObject3DComponentUVE& value);

struct SmartObject3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "SmartObject3D";
    SmartObject3DComponentUVE smartObject{};
};

[[nodiscard]] bool IsSmartObject3DObjectDefinitionValidUVE(
    const SmartObject3DObjectDefinitionUVE& value) noexcept;

void ApplySmartObject3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                           const SmartObject3DObjectDefinitionUVE& value);

class SmartObject3DUVE final {
public:
    SmartObject3DUVE() = delete;

    /// True when `smartObject` is enabled, off cooldown, and has a free slot (or is already
    /// claimed by `agent`).
    [[nodiscard]] static bool CanClaimUVE(const SmartObject3DComponentUVE& smartObject,
                                          EntityUVE agent) noexcept;

    /// Claims a slot for `agent` when available. Returns true on success.
    [[nodiscard]] static bool TryClaimUVE(SmartObject3DComponentUVE& smartObject, EntityUVE agent);

    /// Releases `agent`'s claim and starts `cooldownSeconds` when the last user leaves.
    static bool ReleaseUVE(SmartObject3DComponentUVE& smartObject, EntityUVE agent) noexcept;

    /// Resolves the world-space interaction stand point (`TransformWorldPointUVE(world, interactionOffset)`).
    [[nodiscard]] static Math::Vector3UVE ResolveWorldInteractionPointUVE(
        const SmartObject3DComponentUVE& smartObject,
        const WorldTransformComponentUVE& worldTransform) noexcept;
};

/// Tactical world-query component (`searchRadiusMetres`, `optimalDistanceMetres`, `requiredTag`,
/// `preferUnclaimedSmartObjects`) that scores candidate positions/entities in the scene and caches
/// the best candidate (`bestEntity`, `bestWorldPosition`, `bestScore`).
struct WorldQuery3DComponentUVE final {
    std::string requiredTag{};
    float searchRadiusMetres = 25.0F;
    float optimalDistanceMetres = 5.0F;
    bool preferUnclaimedSmartObjects = true;
    bool enabled = true;

    // Runtime state, never persisted:
    EntityUVE bestEntity = kInvalidEntityUVE;
    Math::Vector3UVE bestWorldPosition{0.0F, 0.0F, 0.0F};
    float bestScore = 0.0F;

    [[nodiscard]] bool operator==(const WorldQuery3DComponentUVE& other) const noexcept {
        return requiredTag == other.requiredTag &&
               searchRadiusMetres == other.searchRadiusMetres &&
               optimalDistanceMetres == other.optimalDistanceMetres &&
               preferUnclaimedSmartObjects == other.preferUnclaimedSmartObjects &&
               enabled == other.enabled;
    }
};

[[nodiscard]] bool IsWorldQuery3DComponentValidUVE(const WorldQuery3DComponentUVE& value) noexcept;

[[nodiscard]] WorldQuery3DComponentUVE SanitizeWorldQuery3DComponentUVE(
    const WorldQuery3DComponentUVE& value);

struct WorldQuery3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "WorldQuery3D";
    WorldQuery3DComponentUVE query{};
};

[[nodiscard]] bool IsWorldQuery3DObjectDefinitionValidUVE(
    const WorldQuery3DObjectDefinitionUVE& value) noexcept;

void ApplyWorldQuery3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const WorldQuery3DObjectDefinitionUVE& value);

/// Ticks all `SmartObject3DComponentUVE` cooldowns (pruning destroyed users) and evaluates all
/// enabled `WorldQuery3DComponentUVE` entities. Returns the number of queries that found a match.
std::size_t UpdateSmartObjectAndWorldQuery3DUVE(IEntityManagerUVE& entityManager,
                                                float deltaTimeSeconds);

} // namespace UVE::Scene
