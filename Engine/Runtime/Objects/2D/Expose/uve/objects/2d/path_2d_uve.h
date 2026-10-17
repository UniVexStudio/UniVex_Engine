// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::size_t kMaximumPath2DPointsUVE = 512U;

struct Path2DComponentUVE final {
    std::vector<Math::Vector2UVE> points{};
    bool closed = false;

    [[nodiscard]] bool operator==(const Path2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsPath2DComponentValidUVE(const Path2DComponentUVE& value) noexcept;

[[nodiscard]] Path2DComponentUVE SanitizePath2DComponentUVE(const Path2DComponentUVE& value);

struct Path2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "Path2D";
    static constexpr std::string_view defaultName = "Path2D";
    Path2DComponentUVE path{};

    [[nodiscard]] bool operator==(const Path2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsPath2DObjectDefinitionValidUVE(const Path2DObjectDefinitionUVE& value) noexcept;

void ApplyPath2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                    const Path2DObjectDefinitionUVE& value);

struct Path2DSampledPoseUVE final {
    Math::Vector2UVE position{0.0F, 0.0F};
    Math::Vector2UVE tangent{1.0F, 0.0F};
    Math::Vector2UVE normal{0.0F, 1.0F};
    float rotationRadians = 0.0F;
};

class Path2DUVE final {
public:
    Path2DUVE() = delete;

    [[nodiscard]] static float ComputeTotalLengthUVE(const Path2DComponentUVE& path) noexcept;

    [[nodiscard]] static Path2DSampledPoseUVE SamplePoseAtDistanceUVE(
        const Path2DComponentUVE& path, float distancePixels, bool loop = false) noexcept;
};

struct PathFollow2DComponentUVE final {
    EntityUVE pathEntity = kInvalidEntityUVE;
    float progressPixels = 0.0F;
    float speedPixelsPerSecond = 0.0F;
    float hOffset = 0.0F;
    float vOffset = 0.0F;
    bool rotateToTangent = true;
    bool loop = true;
    bool enabled = true;
    /// Runtime state, never persisted:
    float progressRatio = 0.0F;

    [[nodiscard]] bool operator==(const PathFollow2DComponentUVE&) const = default;
};

[[nodiscard]] bool IsPathFollow2DComponentValidUVE(const PathFollow2DComponentUVE& value) noexcept;

[[nodiscard]] PathFollow2DComponentUVE SanitizePathFollow2DComponentUVE(
    const PathFollow2DComponentUVE& value) noexcept;

struct PathFollow2DObjectDefinitionUVE final {
    static constexpr std::string_view typeName = "PathFollow2D";
    static constexpr std::string_view defaultName = "PathFollow2D";
    PathFollow2DComponentUVE follower{};

    [[nodiscard]] bool operator==(const PathFollow2DObjectDefinitionUVE&) const = default;
};

[[nodiscard]] bool IsPathFollow2DObjectDefinitionValidUVE(
    const PathFollow2DObjectDefinitionUVE& value) noexcept;

void ApplyPathFollow2DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const PathFollow2DObjectDefinitionUVE& value);

std::size_t UpdatePathFollow2DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
