// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

enum class AreaSpaceOverrideModeUVE : std::uint8_t {
    Disabled = 0,
    Combine = 1,
    CombineReplace = 2,
    Replace = 3,
    ReplaceCombine = 4,
};

inline constexpr std::size_t kMaximumArea3DOverlapsUVE = 32U;
inline constexpr float kDefaultAreaGravityMagnitudeUVE = 9.81F;

struct AreaComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    bool monitoring = true;
    bool monitorable = true;

    AreaSpaceOverrideModeUVE gravityOverride = AreaSpaceOverrideModeUVE::Disabled;
    Math::Vector3UVE gravityDirection{0.0F, -1.0F, 0.0F};
    float gravityMagnitude = kDefaultAreaGravityMagnitudeUVE;
    bool gravityPoint = false;
    Math::Vector3UVE gravityPointOffset{};
    float gravityPointUnitDistance = 0.0F;
    AreaSpaceOverrideModeUVE linearDampOverride = AreaSpaceOverrideModeUVE::Disabled;
    float linearDamp = 0.1F;
    AreaSpaceOverrideModeUVE angularDampOverride = AreaSpaceOverrideModeUVE::Disabled;
    float angularDamp = 0.1F;
    std::int32_t priority = 0;

    std::array<EntityUVE, kMaximumArea3DOverlapsUVE> overlappingBodies{};
    std::uint8_t overlappingBodyCount = 0U;
    bool overlappingBodiesTruncated = false;
    std::array<EntityUVE, kMaximumArea3DOverlapsUVE> overlappingAreas{};
    std::uint8_t overlappingAreaCount = 0U;
    bool overlappingAreasTruncated = false;
};

[[nodiscard]] bool IsAreaSpaceOverrideModeValidUVE(AreaSpaceOverrideModeUVE mode) noexcept;

[[nodiscard]] inline bool HasAreaSpaceOverrideUVE(const AreaComponentUVE& area) noexcept {
    return area.gravityOverride != AreaSpaceOverrideModeUVE::Disabled ||
           area.linearDampOverride != AreaSpaceOverrideModeUVE::Disabled ||
           area.angularDampOverride != AreaSpaceOverrideModeUVE::Disabled;
}

[[nodiscard]] bool IsAreaComponentValidUVE(const AreaComponentUVE& area) noexcept;

} // namespace UVE::Scene
