// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "entity.h"
#include "vector3.h"

namespace UVE::Scene {

enum class AreaSpaceOverrideMode : std::uint8_t {
    Disabled = 0,
    Combine = 1,
    CombineReplace = 2,
    Replace = 3,
    ReplaceCombine = 4,
};

inline constexpr std::size_t kMaximumArea3DOverlaps = 32U;
inline constexpr float kDefaultAreaGravityMagnitude = 9.81F;

struct AreaComponent final {
    Math::Vector3 halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool monitoring = true;
    bool monitorable = true;

    AreaSpaceOverrideMode gravityOverride = AreaSpaceOverrideMode::Disabled;
    Math::Vector3 gravityDirection{0.0F, -1.0F, 0.0F};
    float gravityMagnitude = kDefaultAreaGravityMagnitude;
    bool gravityPoint = false;
    Math::Vector3 gravityPointOffset{};
    float gravityPointUnitDistance = 0.0F;
    AreaSpaceOverrideMode linearDampOverride = AreaSpaceOverrideMode::Disabled;
    float linearDamp = 0.1F;
    AreaSpaceOverrideMode angularDampOverride = AreaSpaceOverrideMode::Disabled;
    float angularDamp = 0.1F;
    std::int32_t priority = 0;

    std::array<Entity, kMaximumArea3DOverlaps> overlappingBodies{};
    std::uint8_t overlappingBodyCount = 0U;
    bool overlappingBodiesTruncated = false;
    std::array<Entity, kMaximumArea3DOverlaps> overlappingAreas{};
    std::uint8_t overlappingAreaCount = 0U;
    bool overlappingAreasTruncated = false;
};

[[nodiscard]] bool IsAreaSpaceOverrideModeValid(AreaSpaceOverrideMode mode) noexcept;

[[nodiscard]] inline bool HasAreaSpaceOverride(const AreaComponent& area) noexcept {
    return area.gravityOverride != AreaSpaceOverrideMode::Disabled ||
           area.linearDampOverride != AreaSpaceOverrideMode::Disabled ||
           area.angularDampOverride != AreaSpaceOverrideMode::Disabled;
}

[[nodiscard]] bool IsAreaComponentValid(const AreaComponent& area) noexcept;

/// Sanitizes `area` so `halfExtents`, collision layers, space-override modes, gravity, and damping fields are valid.
[[nodiscard]] AreaComponent SanitizeAreaComponent(const AreaComponent& area) noexcept;

/// True when `entity` is currently in `area.overlappingBodies` (`[0, overlappingBodyCount)`).
[[nodiscard]] bool ContainsAreaOverlappingBody(const AreaComponent& area,
                                                  Entity entity) noexcept;

/// True when `entity` is currently in `area.overlappingAreas` (`[0, overlappingAreaCount)`).
[[nodiscard]] bool ContainsAreaOverlappingArea(const AreaComponent& area,
                                                  Entity entity) noexcept;

/// Evaluates the local gravity acceleration vector at `bodyWorldPosition` relative to `areaWorldPosition`.
[[nodiscard]] Math::Vector3 EvaluateAreaGravityVector(
    const AreaComponent& area, const Math::Vector3& areaWorldPosition,
    const Math::Vector3& bodyWorldPosition) noexcept;

/// Clears runtime overlap buffers (`overlappingBodyCount`, `overlappingAreaCount`, and truncation flags).
void ClearAreaRuntimeOverlaps(AreaComponent& area) noexcept;

} // namespace UVE::Scene
