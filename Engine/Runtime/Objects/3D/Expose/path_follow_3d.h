// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "entity.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

/// Moves and optionally orients an entity along a target `Path3DComponent` at `progressMetres`
/// (advancing automatically by `speedMetresPerSecond * deltaTimeSeconds` each frame when
/// `enabled == true`). `hOffset` and `vOffset` shift the follower perpendicular to the path along
/// the sampled `right` and `up` axes.
struct PathFollow3DComponent final {
    Entity pathEntity = kInvalidEntity;
    float progressMetres = 0.0F;
    float speedMetresPerSecond = 0.0F;
    float hOffset = 0.0F;
    float vOffset = 0.0F;
    bool rotateToTangent = true;
    bool loop = true;
    bool enabled = true;
    /// Runtime state, never persisted: normalized progress in `[0, 1]` along the path.
    float progressRatio = 0.0F;

    [[nodiscard]] bool operator==(const PathFollow3DComponent&) const = default;
};

[[nodiscard]] bool IsPathFollow3DComponentValid(const PathFollow3DComponent& value) noexcept;

[[nodiscard]] PathFollow3DComponent SanitizePathFollow3DComponent(
    const PathFollow3DComponent& value) noexcept;

/// Advances `value.progressMetres` and updates `value.progressRatio` against `pathTotalLength`.
void StepPathFollow3DProgress(PathFollow3DComponent& value, float pathTotalLength,
                                 float deltaTimeSeconds) noexcept;

struct PathFollow3DObjectDefinition final {
    static constexpr std::string_view defaultName = "PathFollow3D";
    PathFollow3DComponent follower{};
};

[[nodiscard]] bool IsPathFollow3DObjectDefinitionValid(
    const PathFollow3DObjectDefinition& value) noexcept;

void ApplyPathFollow3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                          const PathFollow3DObjectDefinition& value);

/// Steps every enabled `PathFollow3DComponent` in deterministic entity order and writes its
/// sampled pose to its `TransformComponent`. Returns the number of followers updated.
std::size_t UpdatePathFollow3DObjects(IEntityManager& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
