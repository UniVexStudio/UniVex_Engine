// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/hitbox_strike_uve.h"

#include <optional>
#include <utility>

#include "uve/component/world_transform_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"

namespace UVE::Physics {
namespace {

/// One accepted hurtbox, copied out of the pass-1 iteration so pass 2 never holds it open.
struct HurtboxCandidateUVE final {
    Scene::EntityUVE entity;
    Math::Vector3UVE center;
    Math::Vector3UVE halfExtents;
    Math::QuaternionUVE rotation;
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel;
};

} // namespace

Hitbox3DSyncReportUVE SyncHitboxes3DUVE(Scene::IEntityManagerUVE& entityManager) {
    Hitbox3DSyncReportUVE report;

    // ---- Pass 1 (read-only): the candidate set -----------------------------------------------
    std::vector<HurtboxCandidateUVE> candidates;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::Hurtbox3DComponentUVE>(
        [&candidates](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
                      const Scene::Hurtbox3DComponentUVE& hurtbox) {
            if (!hurtbox.enabled || !Scene::IsHurtbox3DObjectComponentValidUVE(hurtbox)) {
                return;
            }
            HurtboxCandidateUVE candidate;
            candidate.entity = entity;
            candidate.center = worldTransform.worldPosition;
            candidate.halfExtents = hurtbox.halfExtents;
            // A degenerate rotation is not a reason to drop a hurtbox from the world: the pose is
            // read as the identity, exactly how the collider cache and the hitbox side read it.
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, candidate.rotation)) {
                candidate.rotation = {};
            }
            candidate.collisionLayer = hurtbox.collisionLayer;
            candidate.collisionMask = hurtbox.collisionMask;
            candidate.damageChannel = hurtbox.damageChannel;
            candidates.push_back(std::move(candidate));
        });
    report.hurtboxCount = candidates.size();

    // ---- Pass 2: refresh every hitbox's strike state against that snapshot --------------------
    entityManager.ForEachUVE<Scene::Hitbox3DComponentUVE>(
        [&entityManager, &candidates, &report](const Scene::EntityUVE entity, Scene::Hitbox3DComponentUVE& hitbox) {
            ++report.hitboxCount;
            // State is cleared first, so a hitbox that is switched off, malformed or unposed ends
            // the tick with no strikes rather than with last tick's.
            hitbox.strikeCount = 0U;
            hitbox.strikesTruncated = false;
            if (!hitbox.enabled || !Scene::IsHitbox3DObjectComponentValidUVE(hitbox) ||
                !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                return;
            }

            const Scene::WorldTransformComponentUVE& worldTransform =
                entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            Math::QuaternionUVE hitboxRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, hitboxRotation)) {
                hitboxRotation = {};
            }

            for (const HurtboxCandidateUVE& candidate : candidates) {
                if (candidate.entity == entity) {
                    continue; // a hitbox never strikes a hurtbox on its own entity
                }
                if ((candidate.collisionLayer & hitbox.collisionMask) == 0U ||
                    (hitbox.collisionLayer & candidate.collisionMask) == 0U) {
                    continue; // symmetric layer/mask acceptance, AreaOverlapSystemUVE-style
                }
                if (candidate.damageChannel != hitbox.damageChannel) {
                    continue; // a strike requires matching damage channels
                }
                const std::optional<Math::PenetrationUVE> penetration =
                    Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                        worldTransform.worldPosition, hitbox.halfExtents, hitboxRotation,
                        candidate.center, candidate.halfExtents, candidate.rotation);
                if (!penetration.has_value()) {
                    continue; // no overlap (touching boundaries are not strikes either)
                }
                if (hitbox.strikeCount >= Scene::kMaximumHitbox3DStrikesUVE) {
                    // The list is full. The overflow is a fact about this tick and it is reported,
                    // never hidden: a consumer that diffs snapshots must not read the missing pairs
                    // as strikes that ended.
                    hitbox.strikesTruncated = true;
                    report.strikesTruncated = true;
                    break;
                }

                hitbox.strikes[hitbox.strikeCount] =
                    Scene::Hitbox3DStrikeUVE{candidate.entity, penetration->depth, penetration->axis};
                ++hitbox.strikeCount;

                if (report.strikes.size() >= kMaximumHitbox3DStrikeResultsUVE) {
                    report.strikesTruncated = true;
                    continue; // the component list still records it; the report is what is bounded
                }
                report.strikes.push_back(Hitbox3DStrikePairUVE{entity, candidate.entity, penetration->depth,
                                                               penetration->axis, hitbox.damageChannel});
            }
        });

    return report;
}

} // namespace UVE::Physics
