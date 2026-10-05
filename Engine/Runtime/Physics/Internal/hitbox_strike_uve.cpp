// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/hitbox_strike_uve.h"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "uve/component/world_transform_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"

namespace UVE::Physics {
namespace {

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

    entityManager.ForEachUVE<Scene::Hurtbox3DComponentUVE>(
        [](const Scene::EntityUVE, Scene::Hurtbox3DComponentUVE& hurtbox) {
            Scene::Hurtbox3DUVE::ClearHitsUVE(hurtbox);
            if (!hurtbox.enabled) {
                Scene::Hurtbox3DUVE::ResetReceivedUVE(hurtbox);
            }
        });

    std::vector<HurtboxCandidateUVE> candidates;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::Hurtbox3DComponentUVE>(
        [&candidates](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
                      const Scene::Hurtbox3DComponentUVE& hurtbox) {
            if (!Scene::Hurtbox3DUVE::IsVulnerableUVE(hurtbox)) {
                return;
            }
            HurtboxCandidateUVE candidate;
            candidate.entity = entity;
            candidate.center = worldTransform.worldPosition;
            candidate.halfExtents = hurtbox.halfExtents;
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, candidate.rotation)) {
                candidate.rotation = {};
            }
            candidate.collisionLayer = hurtbox.collisionLayer;
            candidate.collisionMask = hurtbox.collisionMask;
            candidate.damageChannel = hurtbox.damageChannel;
            candidates.push_back(std::move(candidate));
        });
    report.hurtboxCount = candidates.size();

    std::unordered_map<Scene::EntityUVE, std::vector<Scene::Hurtbox3DHitUVE>> incoming;
    entityManager.ForEachUVE<Scene::Hitbox3DComponentUVE>(
        [&entityManager, &candidates, &report, &incoming](const Scene::EntityUVE entity,
                                                          Scene::Hitbox3DComponentUVE& hitbox) {
            ++report.hitboxCount;
            Scene::Hitbox3DUVE::ClearStrikesUVE(hitbox);
            if (!hitbox.enabled) {
                Scene::Hitbox3DUVE::ResetActivationUVE(hitbox);
                return;
            }
            if (!Scene::Hitbox3DUVE::IsArmedUVE(hitbox) ||
                !entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
                return;
            }

            const Scene::WorldTransformComponentUVE& worldTransform =
                entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
            Math::QuaternionUVE hitboxRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, hitboxRotation)) {
                hitboxRotation = {};
            }

            std::vector<Scene::Hitbox3DStrikeUVE> overlaps;
            for (const HurtboxCandidateUVE& candidate : candidates) {
                if (!Scene::Hitbox3DUVE::AcceptsTargetUVE(entity, hitbox, candidate.entity, candidate.collisionLayer,
                                                          candidate.collisionMask, candidate.damageChannel)) {
                    continue;
                }
                if (!entityManager.HasComponentUVE<Scene::Hurtbox3DComponentUVE>(candidate.entity)) {
                    continue;
                }
                const Scene::Hurtbox3DComponentUVE& hurtbox =
                    entityManager.GetComponentUVE<Scene::Hurtbox3DComponentUVE>(candidate.entity);
                if (!Scene::Hurtbox3DUVE::AcceptsAttackerUVE(candidate.entity, hurtbox, entity, hitbox.collisionLayer,
                                                             hitbox.collisionMask, hitbox.damageChannel)) {
                    continue;
                }
                const std::optional<Math::PenetrationUVE> penetration =
                    Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                        worldTransform.worldPosition, hitbox.halfExtents, hitboxRotation, candidate.center,
                        candidate.halfExtents, candidate.rotation);
                if (!penetration.has_value()) {
                    continue;
                }
                overlaps.push_back(
                    Scene::Hitbox3DStrikeUVE{candidate.entity, penetration->depth, penetration->axis});
                incoming[candidate.entity].push_back(
                    Scene::Hurtbox3DHitUVE{entity, penetration->depth, penetration->axis});
            }

            Scene::Hitbox3DUVE::CommitStrikesUVE(hitbox, overlaps.data(), overlaps.size());
            if (hitbox.strikesTruncated) {
                report.strikesTruncated = true;
            }
            for (const Scene::Hitbox3DStrikeUVE& overlap : overlaps) {
                if (report.strikes.size() >= kMaximumHitbox3DStrikeResultsUVE) {
                    report.strikesTruncated = true;
                    break;
                }
                report.strikes.push_back(Hitbox3DStrikePairUVE{entity, overlap.hurtboxEntity, overlap.penetrationDepth,
                                                               overlap.axis, hitbox.damageChannel});
            }
        });

    for (auto& [hurtboxEntity, hits] : incoming) {
        if (!entityManager.HasComponentUVE<Scene::Hurtbox3DComponentUVE>(hurtboxEntity)) {
            continue;
        }
        Scene::Hurtbox3DComponentUVE& hurtbox =
            entityManager.GetComponentUVE<Scene::Hurtbox3DComponentUVE>(hurtboxEntity);
        Scene::Hurtbox3DUVE::CommitHitsUVE(hurtbox, hits.data(), hits.size());
    }

    return report;
}

} // namespace UVE::Physics
