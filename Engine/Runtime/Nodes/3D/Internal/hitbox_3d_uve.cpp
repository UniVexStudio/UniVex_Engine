// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/hitbox_3d_uve.h"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/hurtbox_3d_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"

namespace UVE::Scene {

bool IsHitbox3DNodeComponentValidUVE(const Hitbox3DNodeComponentUVE& value) noexcept {
    return IsFinite3DNodeVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           IsBounded3DNodeStringUVE(value.damageChannel, false);
}

namespace {

/// Snapshot of one hurtbox's world-space strike volume, taken once per frame so the mutation pass
/// can evaluate every hitbox against a stable candidate set without holding ECS iteration open
/// across a second ForEachUVE.
struct HurtboxCandidateUVE final {
    EntityUVE entity;
    Math::Vector3UVE center;
    Math::Vector3UVE halfExtents;
    Math::QuaternionUVE rotation;
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel;
};

} // namespace

void SyncHitbox3DStrikesUVE(IEntityManagerUVE& entityManager) {
    // Pass 1 (read-only): snapshot every enabled, valid hurtbox that has a world transform. The
    // candidate set is deliberately unbounded - capping it would mean silently pretending
    // hurtboxes beyond the cap do not exist; only the per-hitbox strike LIST is bounded, and it
    // reports its own overflow.
    std::vector<HurtboxCandidateUVE> candidates;
    entityManager.ForEachUVE<WorldTransformComponentUVE, Hurtbox3DNodeComponentUVE>(
        [&candidates](const EntityUVE entity, const WorldTransformComponentUVE& worldTransform,
                      const Hurtbox3DNodeComponentUVE& hurtbox) {
            if (!hurtbox.enabled || !IsHurtbox3DNodeComponentValidUVE(hurtbox)) {
                return;
            }
            HurtboxCandidateUVE candidate;
            candidate.entity = entity;
            candidate.center = worldTransform.worldPosition;
            candidate.halfExtents = hurtbox.halfExtents;
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, candidate.rotation)) {
                candidate.rotation = {}; // degenerate rotation falls back to identity
            }
            candidate.collisionLayer = hurtbox.collisionLayer;
            candidate.collisionMask = hurtbox.collisionMask;
            candidate.damageChannel = hurtbox.damageChannel;
            candidates.push_back(std::move(candidate));
        });

    // Pass 2: refresh every hitbox's runtime strike state against that snapshot.
    entityManager.ForEachUVE<Hitbox3DNodeComponentUVE>(
        [&entityManager, &candidates](const EntityUVE entity, Hitbox3DNodeComponentUVE& hitbox) {
            hitbox.strikeCount = 0U;
            hitbox.strikesTruncated = false;
            if (!hitbox.enabled || !IsHitbox3DNodeComponentValidUVE(hitbox) ||
                !entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                return;
            }

            const WorldTransformComponentUVE& worldTransform =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity);
            Math::QuaternionUVE hitboxRotation{};
            if (!Math::TryNormalizeUVE(worldTransform.worldRotation, hitboxRotation)) {
                hitboxRotation = {}; // degenerate rotation falls back to identity
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
                    Physics::Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                        worldTransform.worldPosition, hitbox.halfExtents, hitboxRotation,
                        candidate.center, candidate.halfExtents, candidate.rotation);
                if (!penetration.has_value()) {
                    continue; // no overlap (touching boundaries are not strikes either)
                }
                if (hitbox.strikeCount >= kMaximumHitbox3DStrikesUVE) {
                    hitbox.strikesTruncated = true;
                    break;
                }
                hitbox.strikes[hitbox.strikeCount] =
                    Hitbox3DStrikeUVE{candidate.entity, penetration->depth};
                ++hitbox.strikeCount;
            }
        });
}

} // namespace UVE::Scene
