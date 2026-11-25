// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "entity.h"
#include "world_transform_component.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

inline constexpr std::size_t kMaximumPath3DPoints = 512U;

/// Authored 3D piecewise-linear curve (`points` in local object space, optionally closed into a
/// loop) consumed by `PathFollow3DComponent` and gameplay queries.
struct Path3DComponent final {
    std::vector<Math::Vector3> points{};
    bool closed = false;
    Math::Vector3 upVector{0.0F, 1.0F, 0.0F};

    [[nodiscard]] bool operator==(const Path3DComponent&) const = default;
};

[[nodiscard]] bool IsPath3DComponentValid(const Path3DComponent& value) noexcept;

[[nodiscard]] Path3DComponent SanitizePath3DComponent(const Path3DComponent& value);

struct Path3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Path3D";
    Path3DComponent path{};
};

[[nodiscard]] bool IsPath3DObjectDefinitionValid(const Path3DObjectDefinition& value) noexcept;

void ApplyPath3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                    const Path3DObjectDefinition& value);

struct Path3DSampledPose final {
    Math::Vector3 position{0.0F, 0.0F, 0.0F};
    Math::Vector3 tangent{0.0F, 0.0F, -1.0F};
    Math::Vector3 right{1.0F, 0.0F, 0.0F};
    Math::Vector3 up{0.0F, 1.0F, 0.0F};
    Math::Quaternion rotation{};
};

class Path3D final {
public:
    Path3D() = delete;

    /// Total arc length in local space across all consecutive segments (plus closing segment when
    /// `path.closed` is true and at least 3 points exist).
    [[nodiscard]] static float ComputeTotalLength(const Path3DComponent& path) noexcept;

    /// Samples the local-space position and unit tangent at arc-length `distanceMetres` clamped or
    /// wrapped to `[0, totalLength]`.
    [[nodiscard]] static Path3DSampledPose SampleLocalPoseAtDistance(
        const Path3DComponent& path, float distanceMetres, bool loop = false) noexcept;

    /// Samples the world-space position, basis vectors, and orientation at arc-length
    /// `distanceMetres` transformed by `pathWorldTransform`.
    [[nodiscard]] static Path3DSampledPose SampleWorldPoseAtDistance(
        const Path3DComponent& path, const WorldTransformComponent& pathWorldTransform,
        float distanceMetres, bool loop = false) noexcept;
};

} // namespace UVE::Scene
