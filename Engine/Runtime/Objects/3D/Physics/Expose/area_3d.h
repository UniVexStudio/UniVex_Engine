// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "area_component.h"
#include "entity.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

struct Area3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Area3D";
    AreaComponent area{};
};

[[nodiscard]] bool IsArea3DObjectDefinitionValid(const Area3DObjectDefinition& value) noexcept;

void ApplyArea3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                  const Area3DObjectDefinition& value);

struct Area3DOverlap final {
    Entity other{};
    float penetrationDepth = 0.0F;
    bool otherIsArea = false;
};

struct Area3DField final {
    Entity areaEntity{};
    std::int32_t priority = 0;
    AreaSpaceOverrideMode gravityOverride = AreaSpaceOverrideMode::Disabled;
    Math::Vector3 gravityDirection{0.0F, -1.0F, 0.0F};
    float gravityMagnitude = kDefaultAreaGravityMagnitude;
    bool gravityPoint = false;
    Math::Vector3 gravityPointWorldCenter{};
    float gravityPointUnitDistance = 0.0F;
    AreaSpaceOverrideMode linearDampOverride = AreaSpaceOverrideMode::Disabled;
    float linearDamp = 0.1F;
    AreaSpaceOverrideMode angularDampOverride = AreaSpaceOverrideMode::Disabled;
    float angularDamp = 0.1F;
};

struct Area3DSpaceResult final {
    Math::Vector3 gravity{};
    float linearDamp = 0.0F;
    float angularDamp = 0.0F;
    bool gravityFromArea = false;
    bool linearDampFromArea = false;
    bool angularDampFromArea = false;

    [[nodiscard]] bool operator==(const Area3DSpaceResult&) const noexcept = default;
};

class Area3D final {
public:
    static void ClearOccupancy(AreaComponent& area) noexcept;
    static void RefreshOccupancy(AreaComponent& area,
                                    std::span<const Area3DOverlap> overlaps) noexcept;

    [[nodiscard]] static bool HasOverlappingBody(const AreaComponent& area,
                                                    Entity entity) noexcept;
    [[nodiscard]] static bool HasOverlappingArea(const AreaComponent& area,
                                                    Entity entity) noexcept;
    [[nodiscard]] static const Entity* GetOverlappingBody(const AreaComponent& area,
                                                                std::size_t index) noexcept;
    [[nodiscard]] static const Entity* GetOverlappingArea(const AreaComponent& area,
                                                                std::size_t index) noexcept;

    [[nodiscard]] static Area3DField MakeField(Entity areaEntity,
                                                     const AreaComponent& area,
                                                     const Math::Vector3& areaWorldCenter) noexcept;
    [[nodiscard]] static Math::Vector3 EvaluateGravityAt(const Area3DField& field,
                                                               const Math::Vector3& bodyPosition) noexcept;
    static void SortFieldsByPriority(std::vector<Area3DField>& fields);
    [[nodiscard]] static Area3DSpaceResult ResolveSpace(
        const Math::Vector3& worldGravity, float bodyLinearDamp, float bodyAngularDamp,
        const Math::Vector3& bodyPosition, std::span<const Area3DField> overlappingFields);
};

} // namespace UVE::Scene
