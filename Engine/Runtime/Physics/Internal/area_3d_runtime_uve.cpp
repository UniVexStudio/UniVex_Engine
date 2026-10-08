// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/area_3d_runtime_uve.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include "uve/component/area_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"
#include "uve/objects/3d/area_3d_uve.h"

namespace UVE::Physics {
namespace {

[[nodiscard]] bool EntityLessUVE(const Scene::EntityUVE& lhs, const Scene::EntityUVE& rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

struct BodyMembershipUVE final {
    Scene::EntityUVE body{};
    Math::Vector3UVE bodyPosition{};
    float bodyLinearDamp = 0.0F;
    Scene::Area3DFieldUVE field{};
};

[[nodiscard]] bool MembershipLessUVE(const BodyMembershipUVE& lhs, const BodyMembershipUVE& rhs) noexcept {
    if (lhs.body.index != rhs.body.index || lhs.body.generation != rhs.body.generation) {
        return EntityLessUVE(lhs.body, rhs.body);
    }
    if (lhs.field.priority != rhs.field.priority) {
        return lhs.field.priority > rhs.field.priority;
    }
    return EntityLessUVE(lhs.field.areaEntity, rhs.field.areaEntity);
}

} // namespace

bool HasAnyArea3DSpaceOverrideUVE(Scene::IEntityManagerUVE& entityManager) {
    bool any = false;
    entityManager.ForEachUVE<Scene::AreaComponentUVE>(
        [&any](const Scene::EntityUVE, const Scene::AreaComponentUVE& area) {
            if (!any && Scene::IsAreaComponentValidUVE(area) && Scene::HasAreaSpaceOverrideUVE(area)) {
                any = true;
            }
        });
    return any;
}

Area3DOccupancySyncResultUVE ApplyArea3DOccupancyUVE(Scene::IEntityManagerUVE& entityManager,
                                                     const AreaOverlapQueryResultUVE& snapshot) {
    Area3DOccupancySyncResultUVE result;
    entityManager.ForEachUVE<Scene::AreaComponentUVE>(
        [&entityManager, &snapshot, &result](const Scene::EntityUVE entity,
                                             Scene::AreaComponentUVE& area) {
            ++result.areaCount;
            std::vector<Scene::Area3DOverlapUVE> overlaps;
            if (Scene::IsAreaComponentValidUVE(area) && area.monitoring) {
                overlaps.reserve(8U);
                for (const AreaOverlapPairUVE& pair : snapshot.overlaps) {
                    if (pair.area != entity || pair.other == entity) {
                        continue;
                    }
                    const bool otherIsArea =
                        entityManager.HasComponentUVE<Scene::AreaComponentUVE>(pair.other);
                    overlaps.push_back(
                        Scene::Area3DOverlapUVE{pair.other, pair.penetrationDepth, otherIsArea});
                }
                ++result.refreshedAreaCount;
            }
            Scene::Area3DUVE::RefreshOccupancyUVE(area, overlaps);
            result.listedBodyCount += static_cast<std::size_t>(area.overlappingBodyCount);
            result.listedAreaCount += static_cast<std::size_t>(area.overlappingAreaCount);
            if (area.overlappingBodiesTruncated || area.overlappingAreasTruncated) {
                ++result.truncatedAreaCount;
            }
        });
    return result;
}

Area3DOccupancySyncResultUVE SyncArea3DOccupancyUVE(Scene::IEntityManagerUVE& entityManager) {
    return ApplyArea3DOccupancyUVE(entityManager, AreaOverlapSystemUVE::QueryUVE(entityManager));
}

std::vector<Area3DBodySpaceUVE> CollectArea3DBodySpacesUVE(Scene::IEntityManagerUVE& entityManager,
                                                           const Math::Vector3UVE& worldGravity) {
    std::vector<Area3DBodySpaceUVE> spaces;
    if (!HasAnyArea3DSpaceOverrideUVE(entityManager)) {
        return spaces;
    }

    const AreaOverlapQueryResultUVE snapshot = AreaOverlapSystemUVE::QueryUVE(
        entityManager, kMaximumAreaOverlapResultsUVE, AreaOverlapParticipationUVE::AllValidAreas);

    std::vector<BodyMembershipUVE> memberships;
    memberships.reserve(snapshot.overlaps.size());
    for (const AreaOverlapPairUVE& pair : snapshot.overlaps) {
        if (pair.area == pair.other) {
            continue;
        }
        if (!entityManager.HasComponentUVE<Scene::AreaComponentUVE>(pair.area) ||
            !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(pair.area) ||
            !entityManager.HasComponentUVE<Scene::Rigid3DComponentUVE>(pair.other) ||
            !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(pair.other)) {
            continue;
        }
        const Scene::AreaComponentUVE& area =
            entityManager.GetComponentUVE<Scene::AreaComponentUVE>(pair.area);
        if (!Scene::IsAreaComponentValidUVE(area) || !Scene::HasAreaSpaceOverrideUVE(area)) {
            continue;
        }
        const Scene::Rigid3DComponentUVE& rigidBody =
            entityManager.GetComponentUVE<Scene::Rigid3DComponentUVE>(pair.other);
        if (!Scene::IsRigid3DComponentValidUVE(rigidBody) || rigidBody.isKinematic ||
            !Scene::IsPhysicsObjectSimulatedUVE(entityManager, pair.other)) {
            continue;
        }
        const Scene::WorldTransformComponentUVE& areaWorld =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(pair.area);
        const Scene::WorldTransformComponentUVE& bodyWorld =
            entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(pair.other);
        BodyMembershipUVE membership;
        membership.body = pair.other;
        membership.bodyPosition = bodyWorld.worldPosition;
        membership.bodyLinearDamp = rigidBody.drag;
        membership.field = Scene::Area3DUVE::MakeFieldUVE(pair.area, area, areaWorld.worldPosition);
        memberships.push_back(membership);
    }

    std::sort(memberships.begin(), memberships.end(), MembershipLessUVE);
    spaces.reserve(memberships.size());
    std::size_t index = 0U;
    while (index < memberships.size()) {
        const Scene::EntityUVE body = memberships[index].body;
        const Math::Vector3UVE bodyPosition = memberships[index].bodyPosition;
        const float bodyLinearDamp = memberships[index].bodyLinearDamp;
        std::vector<Scene::Area3DFieldUVE> fields;
        while (index < memberships.size() && memberships[index].body == body) {
            bool duplicate = false;
            for (const Scene::Area3DFieldUVE& existing : fields) {
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
        const Scene::Area3DSpaceResultUVE resolved = Scene::Area3DUVE::ResolveSpaceUVE(
            worldGravity, bodyLinearDamp, 0.0F, bodyPosition, fields);
        Area3DBodySpaceUVE space;
        space.body = body;
        space.gravity = resolved.gravity;
        space.linearDamp = resolved.linearDamp;
        space.angularDamp = resolved.angularDamp;
        spaces.push_back(space);
    }
    return spaces;
}

const Area3DBodySpaceUVE* FindArea3DBodySpaceUVE(const std::span<const Area3DBodySpaceUVE> spaces,
                                                 const Scene::EntityUVE body) noexcept {
    const auto iterator = std::lower_bound(
        spaces.begin(), spaces.end(), body,
        [](const Area3DBodySpaceUVE& space, const Scene::EntityUVE& value) {
            return EntityLessUVE(space.body, value);
        });
    if (iterator == spaces.end() || iterator->body != body) {
        return nullptr;
    }
    return &*iterator;
}

} // namespace UVE::Physics
