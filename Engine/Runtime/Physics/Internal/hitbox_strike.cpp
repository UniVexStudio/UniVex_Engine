// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_strike.h"

#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "world_transform_component.h"
#include "quaternion.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "shape_narrow_phase.h"

namespace UVE::Physics {
namespace {

struct HurtboxCandidate final {
    Scene::Entity entity;
    Math::Vector3 center;
    Math::Vector3 halfExtents;
    Math::Quaternion rotation;
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel;
};

} // namespace

Hitbox3DSyncReport SyncHitboxes3D(Scene::IEntityManager& entityManager) {
    Hitbox3DSyncReport report;

    entityManager.ForEach<Scene::Hurtbox3DComponent>(
        [](const Scene::Entity, Scene::Hurtbox3DComponent& hurtbox) {
            Scene::Hurtbox3D::ClearHits(hurtbox);
            if (!hurtbox.enabled) {
                Scene::Hurtbox3D::ResetReceived(hurtbox);
            }
        });

    std::vector<HurtboxCandidate> candidates;
    entityManager.ForEach<Scene::WorldTransformComponent, Scene::Hurtbox3DComponent>(
        [&candidates](const Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform,
                      const Scene::Hurtbox3DComponent& hurtbox) {
            if (!Scene::Hurtbox3D::IsVulnerable(hurtbox)) {
                return;
            }
            HurtboxCandidate candidate;
            candidate.entity = entity;
            candidate.center = worldTransform.worldPosition;
            candidate.halfExtents = hurtbox.halfExtents;
            if (!Math::TryNormalize(worldTransform.worldRotation, candidate.rotation)) {
                candidate.rotation = {};
            }
            candidate.collisionLayer = hurtbox.collisionLayer;
            candidate.collisionMask = hurtbox.collisionMask;
            candidate.damageChannel = hurtbox.damageChannel;
            candidates.push_back(std::move(candidate));
        });
    report.hurtboxCount = candidates.size();

    std::unordered_map<Scene::Entity, std::vector<Scene::Hurtbox3DHit>> incoming;
    entityManager.ForEach<Scene::Hitbox3DComponent>(
        [&entityManager, &candidates, &report, &incoming](const Scene::Entity entity,
                                                          Scene::Hitbox3DComponent& hitbox) {
            ++report.hitboxCount;
            Scene::Hitbox3D::ClearStrikes(hitbox);
            if (!hitbox.enabled) {
                Scene::Hitbox3D::ResetActivation(hitbox);
                return;
            }
            if (!Scene::Hitbox3D::IsArmed(hitbox) ||
                !entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
                return;
            }

            const Scene::WorldTransformComponent& worldTransform =
                entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
            Math::Quaternion hitboxRotation{};
            if (!Math::TryNormalize(worldTransform.worldRotation, hitboxRotation)) {
                hitboxRotation = {};
            }

            std::vector<Scene::Hitbox3DStrike> overlaps;
            for (const HurtboxCandidate& candidate : candidates) {
                if (!Scene::Hitbox3D::AcceptsTarget(entity, hitbox, candidate.entity, candidate.collisionLayer,
                                                          candidate.collisionMask, candidate.damageChannel)) {
                    continue;
                }
                if (!entityManager.HasComponent<Scene::Hurtbox3DComponent>(candidate.entity)) {
                    continue;
                }
                const Scene::Hurtbox3DComponent& hurtbox =
                    entityManager.GetComponent<Scene::Hurtbox3DComponent>(candidate.entity);
                if (!Scene::Hurtbox3D::AcceptsAttacker(candidate.entity, hurtbox, entity, hitbox.collisionLayer,
                                                             hitbox.collisionMask, hitbox.damageChannel)) {
                    continue;
                }
                const std::optional<Math::Penetration> penetration =
                    Detail::ComputeOrientedBoxOrientedBoxPenetration(
                        worldTransform.worldPosition, hitbox.halfExtents, hitboxRotation, candidate.center,
                        candidate.halfExtents, candidate.rotation);
                if (!penetration.has_value()) {
                    continue;
                }
                overlaps.push_back(
                    Scene::Hitbox3DStrike{candidate.entity, penetration->depth, penetration->axis});
                incoming[candidate.entity].push_back(
                    Scene::Hurtbox3DHit{entity, penetration->depth, penetration->axis});
            }

            Scene::Hitbox3D::CommitStrikes(hitbox, overlaps.data(), overlaps.size());
            if (hitbox.strikesTruncated) {
                report.strikesTruncated = true;
            }
            for (const Scene::Hitbox3DStrike& overlap : overlaps) {
                if (report.strikes.size() >= kMaximumHitbox3DStrikeResults) {
                    report.strikesTruncated = true;
                    break;
                }
                report.strikes.push_back(Hitbox3DStrikePair{entity, overlap.hurtboxEntity, overlap.penetrationDepth,
                                                               overlap.axis, hitbox.damageChannel});
            }
        });

    for (auto& [hurtboxEntity, hits] : incoming) {
        if (!entityManager.HasComponent<Scene::Hurtbox3DComponent>(hurtboxEntity)) {
            continue;
        }
        Scene::Hurtbox3DComponent& hurtbox =
            entityManager.GetComponent<Scene::Hurtbox3DComponent>(hurtboxEntity);
        Scene::Hurtbox3D::CommitHits(hurtbox, hits.data(), hits.size());
    }

    return report;
}

} // namespace UVE::Physics
