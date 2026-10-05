// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "uve/component/area_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct Area3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Area3D";
    AreaComponentUVE area{};
};

[[nodiscard]] bool IsArea3DObjectDefinitionValidUVE(const Area3DObjectDefinitionUVE& value) noexcept;

void ApplyArea3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const Area3DObjectDefinitionUVE& value);

struct Area3DOverlapUVE final {
    EntityUVE other{};
    float penetrationDepth = 0.0F;
    bool otherIsArea = false;
};

struct Area3DFieldUVE final {
    EntityUVE areaEntity{};
    std::int32_t priority = 0;
    AreaSpaceOverrideModeUVE gravityOverride = AreaSpaceOverrideModeUVE::Disabled;
    Math::Vector3UVE gravityDirection{0.0F, -1.0F, 0.0F};
    float gravityMagnitude = kDefaultAreaGravityMagnitudeUVE;
    bool gravityPoint = false;
    Math::Vector3UVE gravityPointWorldCenter{};
    float gravityPointUnitDistance = 0.0F;
    AreaSpaceOverrideModeUVE linearDampOverride = AreaSpaceOverrideModeUVE::Disabled;
    float linearDamp = 0.1F;
    AreaSpaceOverrideModeUVE angularDampOverride = AreaSpaceOverrideModeUVE::Disabled;
    float angularDamp = 0.1F;
};

struct Area3DSpaceResultUVE final {
    Math::Vector3UVE gravity{};
    float linearDamp = 0.0F;
    float angularDamp = 0.0F;
    bool gravityFromArea = false;
    bool linearDampFromArea = false;
    bool angularDampFromArea = false;

    [[nodiscard]] bool operator==(const Area3DSpaceResultUVE&) const noexcept = default;
};

class Area3DUVE final {
public:
    static void ClearOccupancyUVE(AreaComponentUVE& area) noexcept;
    static void RefreshOccupancyUVE(AreaComponentUVE& area,
                                    std::span<const Area3DOverlapUVE> overlaps) noexcept;

    [[nodiscard]] static bool HasOverlappingBodyUVE(const AreaComponentUVE& area,
                                                    EntityUVE entity) noexcept;
    [[nodiscard]] static bool HasOverlappingAreaUVE(const AreaComponentUVE& area,
                                                    EntityUVE entity) noexcept;
    [[nodiscard]] static const EntityUVE* GetOverlappingBodyUVE(const AreaComponentUVE& area,
                                                                std::size_t index) noexcept;
    [[nodiscard]] static const EntityUVE* GetOverlappingAreaUVE(const AreaComponentUVE& area,
                                                                std::size_t index) noexcept;

    [[nodiscard]] static Area3DFieldUVE MakeFieldUVE(EntityUVE areaEntity,
                                                     const AreaComponentUVE& area,
                                                     const Math::Vector3UVE& areaWorldCenter) noexcept;
    [[nodiscard]] static Math::Vector3UVE EvaluateGravityAtUVE(const Area3DFieldUVE& field,
                                                               const Math::Vector3UVE& bodyPosition) noexcept;
    static void SortFieldsByPriorityUVE(std::vector<Area3DFieldUVE>& fields);
    [[nodiscard]] static Area3DSpaceResultUVE ResolveSpaceUVE(
        const Math::Vector3UVE& worldGravity, float bodyLinearDamp, float bodyAngularDamp,
        const Math::Vector3UVE& bodyPosition, std::span<const Area3DFieldUVE> overlappingFields);
};

} // namespace UVE::Scene
