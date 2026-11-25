// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "interaction_area.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "character_controller_component.h"
#include "collider_component.h"
#include "world_transform_component.h"
#include "quaternion.h"
#include "vector3.h"
#include "collider_3d.h"
#include "interaction_area_3d.h"
#include "player_3d.h"
#include "shape_narrow_phase.h"

namespace UVE::Physics {
namespace {

struct InteractorSnapshot final {
    Scene::Entity entity;
    Math::Vector3 center;
    Math::Vector3 halfExtents;
    Math::Quaternion rotation{};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
};

} // namespace

InteractionAreaScanResult SyncInteractionAreas(Scene::IEntityManager& entityManager) {
    InteractionAreaScanResult result;

    std::vector<InteractorSnapshot> interactors;
    std::vector<Scene::Entity> interactorEntities;
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::CharacterControllerComponent,
                             Scene::ColliderComponent>(
        [&interactors, &interactorEntities](const Scene::Entity entity,
                                            const Scene::WorldTransformComponent& worldTransform,
                                            const Scene::CharacterControllerComponent&,
                                            const Scene::ColliderComponent& collider) {
            if (!Scene::Collider3D::IsParticipating(collider)) {
                return;
            }
            InteractorSnapshot snapshot;
            snapshot.entity = entity;
            snapshot.center = worldTransform.worldPosition;
            snapshot.halfExtents = Scene::GetColliderLocalHalfExtents(
                Scene::ScaleCollider(collider, worldTransform.worldScale));
            if (!Math::TryNormalize(worldTransform.worldRotation, snapshot.rotation)) {
                snapshot.rotation = {};
            }
            snapshot.collisionLayer = collider.collisionLayer;
            snapshot.collisionMask = collider.collisionMask;
            interactorEntities.push_back(entity);
            interactors.push_back(std::move(snapshot));
        });
    result.interactorCount = interactors.size();
    Scene::Entity possessed = Scene::kInvalidEntity;
    for (const InteractorSnapshot& snapshot : interactors) {
        if (!entityManager.HasComponent<Scene::PlayerComponent>(snapshot.entity)) {
            continue;
        }
        const Scene::PlayerComponent& player =
            entityManager.GetComponent<Scene::PlayerComponent>(snapshot.entity);
        if (!player.possessOnPlay) {
            continue;
        }
        if (possessed == Scene::kInvalidEntity ||
            snapshot.entity.index < possessed.index ||
            (snapshot.entity.index == possessed.index && snapshot.entity.generation < possessed.generation)) {
            possessed = snapshot.entity;
        }
    }
    result.primaryInteractor =
        possessed != Scene::kInvalidEntity
            ? possessed
            : Scene::InteractionArea3D::ResolvePrimaryInteractor(interactorEntities)
                  .value_or(Scene::kInvalidEntity);
    const Scene::Entity primaryEntity = result.primaryInteractor;

    std::vector<Scene::InteractionFocusCandidate> focusCandidates;
    entityManager.ForEach<Scene::InteractionArea3DComponent>(
        [&entityManager, &interactors, primaryEntity, &focusCandidates, &result](
            const Scene::Entity entity, Scene::InteractionArea3DComponent& area) {
            ++result.areaCount;
            Scene::InteractionArea3D::ClearInteractors(area);
            if (!Scene::InteractionArea3D::IsArmed(area) ||
                !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
                return;
            }
            ++result.refreshedAreaCount;

            const Scene::WorldTransformComponent& worldTransform =
                entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
            Math::Quaternion areaRotation{};
            if (!Math::TryNormalize(worldTransform.worldRotation, areaRotation)) {
                areaRotation = {};
            }

            std::vector<Scene::Entity> overlapping;
            for (const InteractorSnapshot& interactor : interactors) {
                if (!Scene::InteractionArea3D::AcceptsInteractor(entity, area, interactor.entity,
                                                                      interactor.collisionLayer,
                                                                      interactor.collisionMask)) {
                    continue;
                }
                const std::optional<Math::Penetration> penetration =
                    Detail::ComputeOrientedBoxOrientedBoxPenetration(
                        worldTransform.worldPosition, area.halfExtents, areaRotation, interactor.center,
                        interactor.halfExtents, interactor.rotation);
                if (!penetration.has_value()) {
                    continue;
                }
                if (interactor.entity == primaryEntity) {
                    focusCandidates.push_back(Scene::InteractionFocusCandidate{
                        entity, Math::LengthSquared(worldTransform.worldPosition - interactor.center)});
                }
                overlapping.push_back(interactor.entity);
            }

            Scene::InteractionArea3D::CommitInteractors(area, overlapping.data(), overlapping.size());
            if (area.interactorsTruncated) {
                ++result.truncatedAreaCount;
            }
        });

    const std::optional<Scene::Entity> focusedArea = Scene::InteractionArea3D::ResolveFocus(focusCandidates);
    if (focusedArea.has_value() &&
        entityManager.HasComponent<Scene::InteractionArea3DComponent>(*focusedArea)) {
        entityManager.GetComponent<Scene::InteractionArea3DComponent>(*focusedArea)
            .focusedByPrimaryInteractor = true;
        result.focusedArea = *focusedArea;
    }
    return result;
}

} // namespace UVE::Physics
