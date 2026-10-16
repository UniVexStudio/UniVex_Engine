// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumPath3DPointsUVE = 512U;

/// Authored 3D piecewise-linear curve (`points` in local object space, optionally closed into a
/// loop) consumed by `PathFollow3DComponentUVE` and gameplay queries.
struct Path3DComponentUVE final {
    std::vector<Math::Vector3UVE> points{};
    bool closed = false;
    Math::Vector3UVE upVector{0.0F, 1.0F, 0.0F};

    [[nodiscard]] bool operator==(const Path3DComponentUVE&) const = default;
};

[[nodiscard]] bool IsPath3DComponentValidUVE(const Path3DComponentUVE& value) noexcept;

[[nodiscard]] Path3DComponentUVE SanitizePath3DComponentUVE(const Path3DComponentUVE& value);

struct Path3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Path3D";
    Path3DComponentUVE path{};
};

[[nodiscard]] bool IsPath3DObjectDefinitionValidUVE(const Path3DObjectDefinitionUVE& value) noexcept;

void ApplyPath3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Path3DObjectDefinitionUVE& value);

struct Path3DSampledPoseUVE final {
    Math::Vector3UVE position{0.0F, 0.0F, 0.0F};
    Math::Vector3UVE tangent{0.0F, 0.0F, -1.0F};
    Math::Vector3UVE right{1.0F, 0.0F, 0.0F};
    Math::Vector3UVE up{0.0F, 1.0F, 0.0F};
    Math::QuaternionUVE rotation{};
};

class Path3DUVE final {
public:
    Path3DUVE() = delete;

    /// Total arc length in local space across all consecutive segments (plus closing segment when
    /// `path.closed` is true and at least 3 points exist).
    [[nodiscard]] static float ComputeTotalLengthUVE(const Path3DComponentUVE& path) noexcept;

    /// Samples the local-space position and unit tangent at arc-length `distanceMetres` clamped or
    /// wrapped to `[0, totalLength]`.
    [[nodiscard]] static Path3DSampledPoseUVE SampleLocalPoseAtDistanceUVE(
        const Path3DComponentUVE& path, float distanceMetres, bool loop = false) noexcept;

    /// Samples the world-space position, basis vectors, and orientation at arc-length
    /// `distanceMetres` transformed by `pathWorldTransform`.
    [[nodiscard]] static Path3DSampledPoseUVE SampleWorldPoseAtDistanceUVE(
        const Path3DComponentUVE& path, const WorldTransformComponentUVE& pathWorldTransform,
        float distanceMetres, bool loop = false) noexcept;
};

} // namespace UVE::Scene
