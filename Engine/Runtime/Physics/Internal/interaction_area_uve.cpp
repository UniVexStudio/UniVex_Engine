// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/interaction_area_uve.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "uve/component/character_controller_component_uve.h"
#include "uve/component/collider_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/collider_3d_uve.h"
#include "uve/objects/3d/interaction_area_3d_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"

namespace UVE::Physics {
namespace {

struct InteractorSnapshotUVE final {
    Scene::EntityUVE entity;
    Math::Vector3UVE center;
    Math::Vector3UVE halfExtents;
    Math::QuaternionUVE rotation{};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
};

} // namespace

InteractionAreaScanResultUVE SyncInteractionAreasUVE(Scene::IEntityManagerUVE& entityManager) {
    InteractionAreaScanResultUVE result;

    std::vector<InteractorSnapshotUVE> interactors;
    std::vector<Scene::EntityUVE> interactorEntities;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::CharacterControllerComponentUVE,
                             Scene::ColliderComponentUVE>(
        [&interactors, &interactorEntities](const Scene::EntityUVE entity,
                                            const Scene::WorldTransformComponentUVE& worldTransform,
                                            const Scene::CharacterControllerComponentUVE&,
                                            const Scene::ColliderComponentUVE& collider) {
            if (!Scene::Collider3DUVE::IsParticipatingUVE(collider)) {
                return;
            }
            InteractorSnapshotUVE snapshot;
            snapshot.entity = entity;
            snapshot.center = worldTransform.worldPosition;
            snapshot.halfExtents = Scene::GetColliderLocalHalfExtentsUVE(collider);
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, snapshot.rotation)) {
                snapshot.rotation = {};
            }
            snapshot.collisionLayer = collider.collisionLayer;
            snapshot.collisionMask = collider.collisionMask;
            interactorEntities.push_back(entity);
            interactors.push_back(std::move(snapshot));
        });
    result.interactorCount = interactors.size();
    result.primaryInteractor =
        Scene::InteractionArea3DUVE::ResolvePrimaryInteractorUVE(interactorEntities).value_or(Scene::kInvalidEntityUVE);
    const Scene::EntityUVE primaryEntity = result.primaryInteractor;

    std::vector<Scene::InteractionFocusCandidateUVE> focusCandidates;
    entityManager.ForEachUVE<Scene::InteractionArea3DComponentUVE>(
        [&entityManager, &interactors, primaryEntity, &focusCandidates, &result](
            const Scene::EntityUVE entity, Scene::InteractionArea3DComponentUVE& area) {
            ++result.areaCount;
            Scene::InteractionArea3DUVE::ClearInteractorsUVE(area);
            if (!Scene::InteractionArea3DUVE::IsArmedUVE(area) ||
                !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                return;
            }
            ++result.refreshedAreaCount;

            const Scene::WorldTransformComponentUVE& worldTransform =
                entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            Math::QuaternionUVE areaRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, areaRotation)) {
                areaRotation = {};
            }

            std::vector<Scene::EntityUVE> overlapping;
            for (const InteractorSnapshotUVE& interactor : interactors) {
                if (!Scene::InteractionArea3DUVE::AcceptsInteractorUVE(entity, area, interactor.entity,
                                                                      interactor.collisionLayer,
                                                                      interactor.collisionMask)) {
                    continue;
                }
                const std::optional<Math::PenetrationUVE> penetration =
                    Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                        worldTransform.worldPosition, area.halfExtents, areaRotation, interactor.center,
                        interactor.halfExtents, interactor.rotation);
                if (!penetration.has_value()) {
                    continue;
                }
                if (interactor.entity == primaryEntity) {
                    focusCandidates.push_back(Scene::InteractionFocusCandidateUVE{
                        entity, Math::LengthSquaredUVE(worldTransform.worldPosition - interactor.center)});
                }
                overlapping.push_back(interactor.entity);
            }

            Scene::InteractionArea3DUVE::CommitInteractorsUVE(area, overlapping.data(), overlapping.size());
            if (area.interactorsTruncated) {
                ++result.truncatedAreaCount;
            }
        });

    const std::optional<Scene::EntityUVE> focusedArea = Scene::InteractionArea3DUVE::ResolveFocusUVE(focusCandidates);
    if (focusedArea.has_value() &&
        entityManager.HasComponentUVE<Scene::InteractionArea3DComponentUVE>(*focusedArea)) {
        entityManager.GetComponentUVE<Scene::InteractionArea3DComponentUVE>(*focusedArea)
            .focusedByPrimaryInteractor = true;
        result.focusedArea = *focusedArea;
    }
    return result;
}

} // namespace UVE::Physics
