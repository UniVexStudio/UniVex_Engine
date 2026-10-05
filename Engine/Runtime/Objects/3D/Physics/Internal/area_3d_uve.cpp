// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/area_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool EntityLessUVE(const EntityUVE& lhs, const EntityUVE& rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

template <std::size_t kCapacity>
void AppendUniqueOccupantUVE(std::array<EntityUVE, kCapacity>& occupants, std::uint8_t& count,
                             bool& truncated, const EntityUVE entity) noexcept {
    if (entity == kInvalidEntityUVE) {
        return;
    }
    for (std::uint8_t index = 0U; index < count; ++index) {
        if (occupants[index] == entity) {
            return;
        }
    }
    if (static_cast<std::size_t>(count) >= kCapacity) {
        truncated = true;
        return;
    }
    occupants[count] = entity;
    ++count;
}

[[nodiscard]] bool ContainsOccupantUVE(const EntityUVE* occupants, const std::uint8_t count,
                                       const EntityUVE entity) noexcept {
    for (std::uint8_t index = 0U; index < count; ++index) {
        if (occupants[index] == entity) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] Math::Vector3UVE DirectionOrZeroUVE(const Math::Vector3UVE& value) noexcept {
    if (!Math::IsFiniteUVE(value)) {
        return {};
    }
    const float lengthSquared = Math::LengthSquaredUVE(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return {};
    }
    return Math::NormalizeUVE(value);
}

struct SpaceMixActionUVE final {
    bool skip = true;
    bool add = false;
    bool done = false;
};

[[nodiscard]] SpaceMixActionUVE DecodeSpaceOverrideUVE(const AreaSpaceOverrideModeUVE mode) noexcept {
    switch (mode) {
    case AreaSpaceOverrideModeUVE::Disabled:
        return SpaceMixActionUVE{true, false, false};
    case AreaSpaceOverrideModeUVE::Combine:
        return SpaceMixActionUVE{false, true, false};
    case AreaSpaceOverrideModeUVE::CombineReplace:
        return SpaceMixActionUVE{false, true, true};
    case AreaSpaceOverrideModeUVE::Replace:
        return SpaceMixActionUVE{false, false, true};
    case AreaSpaceOverrideModeUVE::ReplaceCombine:
        return SpaceMixActionUVE{false, false, false};
    }
    return SpaceMixActionUVE{true, false, false};
}

} // namespace

bool IsArea3DObjectDefinitionValidUVE(const Area3DObjectDefinitionUVE& value) noexcept {
    return IsAreaComponentValidUVE(value.area);
}

void ApplyArea3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                    const Area3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, Area3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<AreaComponentUVE>(entity, value.area);
}

void Area3DUVE::ClearOccupancyUVE(AreaComponentUVE& area) noexcept {
    area.overlappingBodyCount = 0U;
    area.overlappingBodiesTruncated = false;
    area.overlappingAreaCount = 0U;
    area.overlappingAreasTruncated = false;
}

void Area3DUVE::RefreshOccupancyUVE(AreaComponentUVE& area,
                                    const std::span<const Area3DOverlapUVE> overlaps) noexcept {
    ClearOccupancyUVE(area);
    if (!area.monitoring) {
        return;
    }
    for (const Area3DOverlapUVE& overlap : overlaps) {
        if (overlap.otherIsArea) {
            AppendUniqueOccupantUVE(area.overlappingAreas, area.overlappingAreaCount,
                                    area.overlappingAreasTruncated, overlap.other);
        } else {
            AppendUniqueOccupantUVE(area.overlappingBodies, area.overlappingBodyCount,
                                    area.overlappingBodiesTruncated, overlap.other);
        }
    }
}

bool Area3DUVE::HasOverlappingBodyUVE(const AreaComponentUVE& area, const EntityUVE entity) noexcept {
    return ContainsOccupantUVE(area.overlappingBodies.data(), area.overlappingBodyCount, entity);
}

bool Area3DUVE::HasOverlappingAreaUVE(const AreaComponentUVE& area, const EntityUVE entity) noexcept {
    return ContainsOccupantUVE(area.overlappingAreas.data(), area.overlappingAreaCount, entity);
}

const EntityUVE* Area3DUVE::GetOverlappingBodyUVE(const AreaComponentUVE& area,
                                                  const std::size_t index) noexcept {
    return index < static_cast<std::size_t>(area.overlappingBodyCount) ? &area.overlappingBodies[index]
                                                                       : nullptr;
}

const EntityUVE* Area3DUVE::GetOverlappingAreaUVE(const AreaComponentUVE& area,
                                                  const std::size_t index) noexcept {
    return index < static_cast<std::size_t>(area.overlappingAreaCount) ? &area.overlappingAreas[index]
                                                                       : nullptr;
}

Area3DFieldUVE Area3DUVE::MakeFieldUVE(const EntityUVE areaEntity, const AreaComponentUVE& area,
                                       const Math::Vector3UVE& areaWorldCenter) noexcept {
    Area3DFieldUVE field;
    field.areaEntity = areaEntity;
    field.priority = area.priority;
    field.gravityOverride = area.gravityOverride;
    field.gravityDirection = area.gravityDirection;
    field.gravityMagnitude = area.gravityMagnitude;
    field.gravityPoint = area.gravityPoint;
    field.gravityPointWorldCenter = areaWorldCenter + area.gravityPointOffset;
    field.gravityPointUnitDistance = area.gravityPointUnitDistance;
    field.linearDampOverride = area.linearDampOverride;
    field.linearDamp = area.linearDamp;
    field.angularDampOverride = area.angularDampOverride;
    field.angularDamp = area.angularDamp;
    return field;
}

Math::Vector3UVE Area3DUVE::EvaluateGravityAtUVE(const Area3DFieldUVE& field,
                                                 const Math::Vector3UVE& bodyPosition) noexcept {
    if (!std::isfinite(field.gravityMagnitude) || !Math::IsFiniteUVE(bodyPosition)) {
        return {};
    }
    if (!field.gravityPoint) {
        const Math::Vector3UVE direction = DirectionOrZeroUVE(field.gravityDirection);
        if (direction.x == 0.0F && direction.y == 0.0F && direction.z == 0.0F) {
            return {};
        }
        return direction * field.gravityMagnitude;
    }
    if (!Math::IsFiniteUVE(field.gravityPointWorldCenter) ||
        !std::isfinite(field.gravityPointUnitDistance) || field.gravityPointUnitDistance < 0.0F) {
        return {};
    }
    const Math::Vector3UVE offset = field.gravityPointWorldCenter - bodyPosition;
    if (!Math::IsFiniteUVE(offset)) {
        return {};
    }
    const float lengthSquared = Math::LengthSquaredUVE(offset);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return {};
    }
    const Math::Vector3UVE direction = Math::NormalizeUVE(offset);
    if (field.gravityPointUnitDistance <= 0.0F) {
        return direction * field.gravityMagnitude;
    }
    const double distance = std::sqrt(static_cast<double>(lengthSquared));
    if (!std::isfinite(distance) || distance <= 0.0) {
        return {};
    }
    const double unitOverDistance =
        static_cast<double>(field.gravityPointUnitDistance) / distance;
    const double strength =
        static_cast<double>(field.gravityMagnitude) * unitOverDistance * unitOverDistance;
    if (!std::isfinite(strength)) {
        return {};
    }
    return direction * static_cast<float>(strength);
}

void Area3DUVE::SortFieldsByPriorityUVE(std::vector<Area3DFieldUVE>& fields) {
    std::sort(fields.begin(), fields.end(), [](const Area3DFieldUVE& lhs, const Area3DFieldUVE& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return EntityLessUVE(lhs.areaEntity, rhs.areaEntity);
    });
}

Area3DSpaceResultUVE Area3DUVE::ResolveSpaceUVE(const Math::Vector3UVE& worldGravity,
                                                const float bodyLinearDamp,
                                                const float bodyAngularDamp,
                                                const Math::Vector3UVE& bodyPosition,
                                                const std::span<const Area3DFieldUVE> overlappingFields) {
    Area3DSpaceResultUVE result;
    result.gravity = worldGravity;
    result.linearDamp = bodyLinearDamp;
    result.angularDamp = bodyAngularDamp;
    if (!Math::IsFiniteUVE(worldGravity) || !std::isfinite(bodyLinearDamp) ||
        !std::isfinite(bodyAngularDamp) || !Math::IsFiniteUVE(bodyPosition)) {
        result.gravity = {};
        result.linearDamp = 0.0F;
        result.angularDamp = 0.0F;
        return result;
    }

    std::vector<Area3DFieldUVE> fields(overlappingFields.begin(), overlappingFields.end());
    SortFieldsByPriorityUVE(fields);

    bool gravityDone = false;
    bool linearDampDone = false;
    bool angularDampDone = false;
    for (const Area3DFieldUVE& field : fields) {
        if (!gravityDone) {
            const SpaceMixActionUVE action = DecodeSpaceOverrideUVE(field.gravityOverride);
            if (!action.skip) {
                const Math::Vector3UVE areaGravity = EvaluateGravityAtUVE(field, bodyPosition);
                result.gravity = action.add ? (result.gravity + areaGravity) : areaGravity;
                result.gravityFromArea = true;
                gravityDone = action.done;
            }
        }
        if (!linearDampDone) {
            const SpaceMixActionUVE action = DecodeSpaceOverrideUVE(field.linearDampOverride);
            if (!action.skip) {
                result.linearDamp = action.add ? (result.linearDamp + field.linearDamp) : field.linearDamp;
                result.linearDampFromArea = true;
                linearDampDone = action.done;
            }
        }
        if (!angularDampDone) {
            const SpaceMixActionUVE action = DecodeSpaceOverrideUVE(field.angularDampOverride);
            if (!action.skip) {
                result.angularDamp =
                    action.add ? (result.angularDamp + field.angularDamp) : field.angularDamp;
                result.angularDampFromArea = true;
                angularDampDone = action.done;
            }
        }
        if (gravityDone && linearDampDone && angularDampDone) {
            break;
        }
    }
    if (!Math::IsFiniteUVE(result.gravity) || !std::isfinite(result.linearDamp) ||
        !std::isfinite(result.angularDamp)) {
        result.gravity = worldGravity;
        result.linearDamp = bodyLinearDamp;
        result.angularDamp = bodyAngularDamp;
        result.gravityFromArea = false;
        result.linearDampFromArea = false;
        result.angularDampFromArea = false;
    }
    if (result.linearDamp < 0.0F) {
        result.linearDamp = 0.0F;
    }
    if (result.angularDamp < 0.0F) {
        result.angularDamp = 0.0F;
    }
    return result;
}

} // namespace UVE::Scene
