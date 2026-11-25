// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "area_3d_runtime.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "area_component.h"
#include "rigid_3d_component.h"
#include "world_transform_component.h"
#include "abstract_physics_objects_3d.h"
#include "area_3d.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] bool EntityLess(const Scene::Entity& lhs, const Scene::Entity& rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

struct BodyMembership final {
    Scene::Entity body{};
    Math::Vector3 bodyPosition{};
    float bodyLinearDamp = 0.0F;
    Scene::Area3DField field{};
};

[[nodiscard]] bool MembershipLess(const BodyMembership& lhs, const BodyMembership& rhs) noexcept {
    if (lhs.body.index != rhs.body.index || lhs.body.generation != rhs.body.generation) {
        return EntityLess(lhs.body, rhs.body);
    }
    if (lhs.field.priority != rhs.field.priority) {
        return lhs.field.priority > rhs.field.priority;
    }
    return EntityLess(lhs.field.areaEntity, rhs.field.areaEntity);
}

} // namespace

bool HasAnyArea3DSpaceOverride(Scene::IEntityManager& entityManager) {
    bool any = false;
    entityManager.ForEach<Scene::AreaComponent>(
        [&any](const Scene::Entity, const Scene::AreaComponent& area) {
            if (!any && Scene::IsAreaComponentValid(area) && Scene::HasAreaSpaceOverride(area)) {
                any = true;
            }
        });
    return any;
}

Area3DOccupancySyncResult ApplyArea3DOccupancy(Scene::IEntityManager& entityManager,
                                                     const AreaOverlapQueryResult& snapshot) {
    Area3DOccupancySyncResult result;
    entityManager.ForEach<Scene::AreaComponent>(
        [&entityManager, &snapshot, &result](const Scene::Entity entity,
                                             Scene::AreaComponent& area) {
            ++result.areaCount;
            std::vector<Scene::Area3DOverlap> overlaps;
            if (Scene::IsAreaComponentValid(area) && area.monitoring) {
                overlaps.reserve(8U);
                for (const AreaOverlapPair& pair : snapshot.overlaps) {
                    if (pair.area != entity || pair.other == entity) {
                        continue;
                    }
                    const bool otherIsArea =
                        entityManager.HasComponent<Scene::AreaComponent>(pair.other);
                    overlaps.push_back(
                        Scene::Area3DOverlap{pair.other, pair.penetrationDepth, otherIsArea});
                }
                ++result.refreshedAreaCount;
            }
            Scene::Area3D::RefreshOccupancy(area, overlaps);
            result.listedBodyCount += static_cast<std::size_t>(area.overlappingBodyCount);
            result.listedAreaCount += static_cast<std::size_t>(area.overlappingAreaCount);
            if (area.overlappingBodiesTruncated || area.overlappingAreasTruncated) {
                ++result.truncatedAreaCount;
            }
        });
    return result;
}

Area3DOccupancySyncResult SyncArea3DOccupancy(Scene::IEntityManager& entityManager) {
    return ApplyArea3DOccupancy(entityManager, AreaOverlapSystem::Query(entityManager));
}

std::vector<Area3DBodySpace> CollectArea3DBodySpaces(Scene::IEntityManager& entityManager,
                                                           const Math::Vector3& worldGravity) {
    std::vector<Area3DBodySpace> spaces;
    if (!HasAnyArea3DSpaceOverride(entityManager)) {
        return spaces;
    }

    const AreaOverlapQueryResult snapshot = AreaOverlapSystem::Query(
        entityManager, kMaximumAreaOverlapResults, AreaOverlapParticipation::AllValidAreas);

    std::vector<BodyMembership> memberships;
    memberships.reserve(snapshot.overlaps.size());
    for (const AreaOverlapPair& pair : snapshot.overlaps) {
        if (pair.area == pair.other) {
            continue;
        }
        if (!entityManager.HasComponent<Scene::AreaComponent>(pair.area) ||
            !entityManager.HasComponent<Scene::WorldTransformComponent>(pair.area) ||
            !entityManager.HasComponent<Scene::Rigid3DComponent>(pair.other) ||
            !entityManager.HasComponent<Scene::WorldTransformComponent>(pair.other)) {
            continue;
        }
        const Scene::AreaComponent& area =
            entityManager.GetComponent<Scene::AreaComponent>(pair.area);
        if (!Scene::IsAreaComponentValid(area) || !Scene::HasAreaSpaceOverride(area)) {
            continue;
        }
        const Scene::Rigid3DComponent& rigidBody =
            entityManager.GetComponent<Scene::Rigid3DComponent>(pair.other);
        if (!Scene::IsRigid3DComponentValid(rigidBody) || rigidBody.isKinematic ||
            !Scene::IsPhysicsObjectSimulated(entityManager, pair.other)) {
            continue;
        }
        const Scene::WorldTransformComponent& areaWorld =
            entityManager.GetComponent<Scene::WorldTransformComponent>(pair.area);
        const Scene::WorldTransformComponent& bodyWorld =
            entityManager.GetComponent<Scene::WorldTransformComponent>(pair.other);
        BodyMembership membership;
        membership.body = pair.other;
        membership.bodyPosition = bodyWorld.worldPosition;
        membership.bodyLinearDamp = rigidBody.drag;
        membership.field = Scene::Area3D::MakeField(pair.area, area, areaWorld.worldPosition);
        memberships.push_back(membership);
    }

    std::sort(memberships.begin(), memberships.end(), MembershipLess);
    spaces.reserve(memberships.size());
    std::size_t index = 0U;
    while (index < memberships.size()) {
        const Scene::Entity body = memberships[index].body;
        const Math::Vector3 bodyPosition = memberships[index].bodyPosition;
        const float bodyLinearDamp = memberships[index].bodyLinearDamp;
        std::vector<Scene::Area3DField> fields;
        while (index < memberships.size() && memberships[index].body == body) {
            bool duplicate = false;
            for (const Scene::Area3DField& existing : fields) {
                if (existing.areaEntity == memberships[index].field.areaEntity) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                fields.push_back(memberships[index].field);
            }
            ++index;
        }
        const Scene::Area3DSpaceResult resolved = Scene::Area3D::ResolveSpace(
            worldGravity, bodyLinearDamp, 0.0F, bodyPosition, fields);
        Area3DBodySpace space;
        space.body = body;
        space.gravity = resolved.gravity;
        space.linearDamp = resolved.linearDamp;
        space.angularDamp = resolved.angularDamp;
        spaces.push_back(space);
    }
    return spaces;
}

const Area3DBodySpace* FindArea3DBodySpace(const std::span<const Area3DBodySpace> spaces,
                                                 const Scene::Entity body) noexcept {
    const auto iterator = std::lower_bound(
        spaces.begin(), spaces.end(), body,
        [](const Area3DBodySpace& space, const Scene::Entity& value) {
            return EntityLess(space.body, value);
        });
    if (iterator == spaces.end() || iterator->body != body) {
        return nullptr;
    }
    return &*iterator;
}

} // namespace UVE::Physics
