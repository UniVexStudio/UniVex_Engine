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
#include "uve/objects/3d/interaction_area_3d_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"

namespace UVE::Physics {

namespace {

/// One eligible interactor, snapshotted once per frame: world pose, the collider's shape-aware
/// local half extents, and the layer/mask pair the symmetric acceptance needs. A pure value, so
/// the per-area pass never touches the ECS while it iterates a different component set.
struct InteractorSnapshotUVE final {
    Scene::EntityUVE entity;
    Math::Vector3UVE center;
    Math::Vector3UVE halfExtents;
    Math::QuaternionUVE rotation{};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
};

/// AreaOverlapSystemUVE's acceptance rule, spelled once: both directions must agree, so an
/// interactor that rejects an area is as excluded as an area that rejects the interactor.
[[nodiscard]] bool AcceptsEachOtherUVE(const InteractorSnapshotUVE& interactor,
                                       const Scene::InteractionArea3DComponentUVE& area) noexcept {
    return (interactor.collisionLayer & area.collisionMask) != 0U &&
           (area.collisionLayer & interactor.collisionMask) != 0U;
}

} // namespace

InteractionAreaScanResultUVE SyncInteractionAreasUVE(Scene::IEntityManagerUVE& entityManager) {
    InteractionAreaScanResultUVE result;

    // Pass 1 (read-only): snapshot every interactor, so the mutation pass evaluates every area
    // against a stable set without holding ECS iteration open across a second ForEachUVE. The set
    // is deliberately unbounded - capping it would silently pretend interactors beyond the cap do
    // not exist; only each area's stored list is bounded, and it reports its own overflow.
    std::vector<InteractorSnapshotUVE> interactors;
    std::vector<Scene::EntityUVE> interactorEntities;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE,
                             Scene::CharacterControllerComponentUVE, Scene::ColliderComponentUVE>(
        [&interactors, &interactorEntities](
            const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::CharacterControllerComponentUVE&, const Scene::ColliderComponentUVE& collider) {
            if (!Scene::IsColliderComponentValidUVE(collider)) {
                return; // an invalid collider (e.g. a zero layer) can never interact
            }
            InteractorSnapshotUVE snapshot;
            snapshot.entity = entity;
            snapshot.center = worldTransform.worldPosition;
            snapshot.halfExtents = Scene::GetColliderLocalHalfExtentsUVE(collider);
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, snapshot.rotation)) {
                snapshot.rotation = {}; // degenerate rotation falls back to identity
            }
            snapshot.collisionLayer = collider.collisionLayer;
            snapshot.collisionMask = collider.collisionMask;
            interactorEntities.push_back(entity);
            interactors.push_back(std::move(snapshot));
        });
    result.interactorCount = interactors.size();

    result.primaryInteractor = Scene::ResolvePrimaryInteractorUVE(interactorEntities)
                                   .value_or(Scene::kInvalidEntityUVE);
    const Scene::EntityUVE primaryEntity = result.primaryInteractor;

    // Pass 2: refresh every area's runtime state against that snapshot, gathering the primary
    // interactor's focus candidates on the way. Every gate fails closed: anything that stops an
    // area participating this frame clears its runtime state in full, never leaving a stale
    // interactor list or focus flag behind.
    std::vector<Scene::InteractionFocusCandidateUVE> focusCandidates;
    entityManager.ForEachUVE<Scene::InteractionArea3DComponentUVE>(
        [&entityManager, &interactors, primaryEntity, &focusCandidates, &result](
            const Scene::EntityUVE entity, Scene::InteractionArea3DComponentUVE& area) {
            ++result.areaCount;
            area.interactorCount = 0U;
            area.interactorsTruncated = false;
            area.focusedByPrimaryInteractor = false;
            if (!area.enabled || !Scene::IsInteractionArea3DObjectComponentValidUVE(area) ||
                !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                return;
            }
            ++result.refreshedAreaCount;

            const Scene::WorldTransformComponentUVE& worldTransform =
                entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            Math::QuaternionUVE areaRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, areaRotation)) {
                areaRotation = {}; // degenerate rotation falls back to identity
            }
            const std::size_t candidateCap = Scene::ResolveInteractionAreaCandidateCapUVE(
                area.maximumCandidates, Scene::kMaximumInteractionAreaCandidatesUVE);

            for (const InteractorSnapshotUVE& interactor : interactors) {
                if (interactor.entity == entity) {
                    continue; // an area never lists the interactor living on its own entity
                }
                if (!AcceptsEachOtherUVE(interactor, area)) {
                    continue;
                }
                const std::optional<Math::PenetrationUVE> penetration =
                    Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                        worldTransform.worldPosition, area.halfExtents, areaRotation,
                        interactor.center, interactor.halfExtents, interactor.rotation);
                if (!penetration.has_value()) {
                    continue; // no overlap (touching boundaries are not overlaps either)
                }
                if (interactor.entity == primaryEntity) {
                    // Rank by squared center distance - nearest center is the focus candidate; no
                    // sqrt needed for a comparison.
                    focusCandidates.push_back(Scene::InteractionFocusCandidateUVE{
                        entity, Math::LengthSquaredUVE(worldTransform.worldPosition -
                                                       interactor.center)});
                }
                if (area.interactorCount >= candidateCap) {
                    area.interactorsTruncated = true;
                    ++result.truncatedAreaCount;
                    break;
                }
                area.interactors[area.interactorCount] = interactor.entity;
                ++area.interactorCount;
            }
        });

    // Pass 3: exactly one area gets the primary interactor's focus - nearest center wins, ties
    // deterministically by (index, generation); a scene with no eligible interactor focuses nothing
    // (ResolvePrimaryInteractorUVE already failed closed above) and stale focus flags were all
    // cleared in pass 2.
    const std::optional<Scene::EntityUVE> focusedArea =
        Scene::ResolveInteractionFocusUVE(focusCandidates);
    if (focusedArea.has_value() &&
        entityManager.HasComponentUVE<Scene::InteractionArea3DComponentUVE>(*focusedArea)) {
        entityManager.GetComponentUVE<Scene::InteractionArea3DComponentUVE>(*focusedArea)
            .focusedByPrimaryInteractor = true;
        result.focusedArea = *focusedArea;
    }
    return result;
}

} // namespace UVE::Physics
