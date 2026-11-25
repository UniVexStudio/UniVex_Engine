// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "area_3d.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "i_entity_manager.h"
#include "vector3.h"
#include "object_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool EntityLess(const Entity& lhs, const Entity& rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

template <std::size_t kCapacity>
void AppendUniqueOccupant(std::array<Entity, kCapacity>& occupants, std::uint8_t& count,
                             bool& truncated, const Entity entity) noexcept {
    if (entity == kInvalidEntity) {
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

[[nodiscard]] Math::Vector3 DirectionOrZero(const Math::Vector3& value) noexcept {
    if (!Math::IsFinite(value)) {
        return {};
    }
    const float lengthSquared = Math::LengthSquared(value);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return {};
    }
    return Math::Normalize(value);
}

struct SpaceMixAction final {
    bool skip = true;
    bool add = false;
    bool done = false;
};

[[nodiscard]] SpaceMixAction DecodeSpaceOverride(const AreaSpaceOverrideMode mode) noexcept {
    switch (mode) {
    case AreaSpaceOverrideMode::Disabled:
        return SpaceMixAction{true, false, false};
    case AreaSpaceOverrideMode::Combine:
        return SpaceMixAction{false, true, false};
    case AreaSpaceOverrideMode::CombineReplace:
        return SpaceMixAction{false, true, true};
    case AreaSpaceOverrideMode::Replace:
        return SpaceMixAction{false, false, true};
    case AreaSpaceOverrideMode::ReplaceCombine:
        return SpaceMixAction{false, false, false};
    }
    return SpaceMixAction{true, false, false};
}

} // namespace

bool IsArea3DObjectDefinitionValid(const Area3DObjectDefinition& value) noexcept {
    return IsAreaComponentValid(value.area);
}

void ApplyArea3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                    const Area3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, Area3DObjectDefinition::defaultName);
    entityManager.AddComponent<AreaComponent>(entity, SanitizeAreaComponent(value.area));
}

void Area3D::ClearOccupancy(AreaComponent& area) noexcept {
    ClearAreaRuntimeOverlaps(area);
}

void Area3D::RefreshOccupancy(AreaComponent& area,
                                    const std::span<const Area3DOverlap> overlaps) noexcept {
    ClearOccupancy(area);
    if (!area.monitoring) {
        return;
    }
    for (const Area3DOverlap& overlap : overlaps) {
        if (overlap.otherIsArea) {
            AppendUniqueOccupant(area.overlappingAreas, area.overlappingAreaCount,
                                    area.overlappingAreasTruncated, overlap.other);
        } else {
            AppendUniqueOccupant(area.overlappingBodies, area.overlappingBodyCount,
                                    area.overlappingBodiesTruncated, overlap.other);
        }
    }
}

bool Area3D::HasOverlappingBody(const AreaComponent& area, const Entity entity) noexcept {
    return ContainsAreaOverlappingBody(area, entity);
}

bool Area3D::HasOverlappingArea(const AreaComponent& area, const Entity entity) noexcept {
    return ContainsAreaOverlappingArea(area, entity);
}

const Entity* Area3D::GetOverlappingBody(const AreaComponent& area,
                                                  const std::size_t index) noexcept {
    return index < static_cast<std::size_t>(area.overlappingBodyCount) ? &area.overlappingBodies[index]
                                                                       : nullptr;
}

const Entity* Area3D::GetOverlappingArea(const AreaComponent& area,
                                                  const std::size_t index) noexcept {
    return index < static_cast<std::size_t>(area.overlappingAreaCount) ? &area.overlappingAreas[index]
                                                                       : nullptr;
}

Area3DField Area3D::MakeField(const Entity areaEntity, const AreaComponent& area,
                                       const Math::Vector3& areaWorldCenter) noexcept {
    Area3DField field;
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

Math::Vector3 Area3D::EvaluateGravityAt(const Area3DField& field,
                                                 const Math::Vector3& bodyPosition) noexcept {
    if (!std::isfinite(field.gravityMagnitude) || !Math::IsFinite(bodyPosition)) {
        return {};
    }
    if (!field.gravityPoint) {
        const Math::Vector3 direction = DirectionOrZero(field.gravityDirection);
        if (direction.x == 0.0F && direction.y == 0.0F && direction.z == 0.0F) {
            return {};
        }
        return direction * field.gravityMagnitude;
    }
    if (!Math::IsFinite(field.gravityPointWorldCenter) ||
        !std::isfinite(field.gravityPointUnitDistance) || field.gravityPointUnitDistance < 0.0F) {
        return {};
    }
    const Math::Vector3 offset = field.gravityPointWorldCenter - bodyPosition;
    if (!Math::IsFinite(offset)) {
        return {};
    }
    const float lengthSquared = Math::LengthSquared(offset);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return {};
    }
    const Math::Vector3 direction = Math::Normalize(offset);
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

void Area3D::SortFieldsByPriority(std::vector<Area3DField>& fields) {
    std::sort(fields.begin(), fields.end(), [](const Area3DField& lhs, const Area3DField& rhs) {
        if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
        }
        return EntityLess(lhs.areaEntity, rhs.areaEntity);
    });
}

Area3DSpaceResult Area3D::ResolveSpace(const Math::Vector3& worldGravity,
                                                const float bodyLinearDamp,
                                                const float bodyAngularDamp,
                                                const Math::Vector3& bodyPosition,
                                                const std::span<const Area3DField> overlappingFields) {
    Area3DSpaceResult result;
    result.gravity = worldGravity;
    result.linearDamp = bodyLinearDamp;
    result.angularDamp = bodyAngularDamp;
    if (!Math::IsFinite(worldGravity) || !std::isfinite(bodyLinearDamp) ||
        !std::isfinite(bodyAngularDamp) || !Math::IsFinite(bodyPosition)) {
        result.gravity = {};
        result.linearDamp = 0.0F;
        result.angularDamp = 0.0F;
        return result;
    }

    std::vector<Area3DField> fields(overlappingFields.begin(), overlappingFields.end());
    SortFieldsByPriority(fields);

    bool gravityDone = false;
    bool linearDampDone = false;
    bool angularDampDone = false;
    for (const Area3DField& field : fields) {
        if (!gravityDone) {
            const SpaceMixAction action = DecodeSpaceOverride(field.gravityOverride);
            if (!action.skip) {
                const Math::Vector3 areaGravity = EvaluateGravityAt(field, bodyPosition);
                result.gravity = action.add ? (result.gravity + areaGravity) : areaGravity;
                result.gravityFromArea = true;
                gravityDone = action.done;
            }
        }
        if (!linearDampDone) {
            const SpaceMixAction action = DecodeSpaceOverride(field.linearDampOverride);
            if (!action.skip) {
                result.linearDamp = action.add ? (result.linearDamp + field.linearDamp) : field.linearDamp;
                result.linearDampFromArea = true;
                linearDampDone = action.done;
            }
        }
        if (!angularDampDone) {
            const SpaceMixAction action = DecodeSpaceOverride(field.angularDampOverride);
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
    if (!Math::IsFinite(result.gravity) || !std::isfinite(result.linearDamp) ||
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
