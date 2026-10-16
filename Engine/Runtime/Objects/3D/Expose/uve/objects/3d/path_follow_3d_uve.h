// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Moves and optionally orients an entity along a target `Path3DComponentUVE` at `progressMetres`
/// (advancing automatically by `speedMetresPerSecond * deltaTimeSeconds` each frame when
/// `enabled == true`). `hOffset` and `vOffset` shift the follower perpendicular to the path along
/// the sampled `right` and `up` axes.
struct PathFollow3DComponentUVE final {
    EntityUVE pathEntity = kInvalidEntityUVE;
    float progressMetres = 0.0F;
    float speedMetresPerSecond = 0.0F;
    float hOffset = 0.0F;
    float vOffset = 0.0F;
    bool rotateToTangent = true;
    bool loop = true;
    bool enabled = true;
    /// Runtime state, never persisted: normalized progress in `[0, 1]` along the path.
    float progressRatio = 0.0F;

    [[nodiscard]] bool operator==(const PathFollow3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsPathFollow3DComponentValidUVE(const PathFollow3DComponentUVE& value) noexcept;

[[nodiscard]] PathFollow3DComponentUVE SanitizePathFollow3DComponentUVE(
    const PathFollow3DComponentUVE& value) noexcept;

/// Advances `value.progressMetres` and updates `value.progressRatio` against `pathTotalLength`.
void StepPathFollow3DProgressUVE(PathFollow3DComponentUVE& value, float pathTotalLength,
                                 float deltaTimeSeconds) noexcept;

struct PathFollow3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "PathFollow3D";
    PathFollow3DComponentUVE follower{};
};

[[nodiscard]] bool IsPathFollow3DObjectDefinitionValidUVE(
    const PathFollow3DObjectDefinitionUVE& value) noexcept;

void ApplyPathFollow3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const PathFollow3DObjectDefinitionUVE& value);

/// Steps every enabled `PathFollow3DComponentUVE` in deterministic entity order and writes its
/// sampled pose to its `TransformComponentUVE`. Returns the number of followers updated.
std::size_t UpdatePathFollow3DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
