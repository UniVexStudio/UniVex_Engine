// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/hitbox_lag_compensation_uve.h"

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "uve/component/world_transform_component_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"
#include "uve/physics/detail/shape_narrow_phase_uve.h"

namespace UVE::Physics {
namespace {

// A zero direction has no line to cast along; anything shorter is degenerate input, not a ray.
inline constexpr float kMinimumRayLengthUVE = 1e-6F;

[[nodiscard]] const LagPoseUVE* FindPoseUVE(
    const std::vector<std::pair<Scene::EntityUVE, LagPoseUVE>>& snapshot,
    const Scene::EntityUVE entity) noexcept {
    for (const auto& [posed, pose] : snapshot) {
        if (posed == entity) {
            return &pose;
        }
    }
    return nullptr;
}

// Blends two poses at alpha: positions and scales lerp, rotations slerp. A failed slerp
// (degenerate input quaternions) keeps the floor pose - the same answer fraction 0 would
// give, and the resolve's TryNormalize pass treats it like any recorded pose.
[[nodiscard]] LagPoseUVE BlendPoseUVE(const LagPoseUVE& floor, const LagPoseUVE& ceiling,
                                      const float alpha) {
    LagPoseUVE blended;
    blended.position = floor.position + (ceiling.position - floor.position) * alpha;
    blended.scale = floor.scale + (ceiling.scale - floor.scale) * alpha;
    if (!Math::TrySlerpUVE(floor.rotation, ceiling.rotation, alpha, blended.rotation)) {
        blended.rotation = floor.rotation;
    }
    return blended;
}

// Rewound half-extents: authored size times the absolute recorded scale (a mirror occupies
// the same box). Zero, negative-authored, or non-finite products are unhittable - a
// zero-scale body renders nothing, and garbage resolves empty, fail closed.
[[nodiscard]] bool TryRewoundHalfExtentsUVE(const Math::Vector3UVE& authored,
                                            const Math::Vector3UVE& scale,
                                            Math::Vector3UVE& out) noexcept {
    const float x = authored.x * std::fabs(scale.x);
    const float y = authored.y * std::fabs(scale.y);
    const float z = authored.z * std::fabs(scale.z);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || x <= 0.0F || y <= 0.0F ||
        z <= 0.0F) {
        return false;
    }
    out = Math::Vector3UVE{x, y, z};
    return true;
}

} // namespace

bool HitboxLagCompensatorUVE::RecordUVE(const LagTickUVE tick,
                                        Scene::IEntityManagerUVE& entityManager) {
    if (m_hasHistory && tick <= m_lastRecorded) {
        ++m_stats.recordsRejected;
        return false;
    }
    SnapshotUVE snapshot;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE>(
        [&entityManager, &snapshot](const Scene::EntityUVE entity,
                                    const Scene::WorldTransformComponentUVE& world) {
            if (!entityManager.HasComponentUVE<Scene::Hitbox3DComponentUVE>(entity) &&
                !entityManager.HasComponentUVE<Scene::Hurtbox3DComponentUVE>(entity)) {
                return;
            }
            snapshot.emplace_back(entity,
                                LagPoseUVE{world.worldPosition, world.worldRotation,
                                           world.worldScale});
        });
    m_history.emplace(tick, std::move(snapshot));
    m_lastRecorded = tick;
    m_hasHistory = true;
    ++m_stats.snapshotsRecorded;
    while (m_history.size() > kLagHistoryMaximumTicksUVE) {
        m_history.erase(m_history.begin());
        ++m_stats.snapshotsEvicted;
    }
    return true;
}

const HitboxLagCompensatorUVE::SnapshotUVE* HitboxLagCompensatorUVE::FindSnapshotUVE(
    const LagTickUVE targetTick, LagTickUVE& resolvedTick, bool& clamped) const {
    if (m_history.empty()) {
        resolvedTick = 0U;
        clamped = true;
        return nullptr;
    }
    auto newer = m_history.upper_bound(targetTick);
    if (newer == m_history.begin()) {
        // Older than everything kept: the oldest survivor is the closest the window gets.
        resolvedTick = newer->first;
        clamped = true;
        return &newer->second;
    }
    --newer;
    resolvedTick = newer->first;
    clamped = resolvedTick != targetTick;
    return &newer->second;
}

const HitboxLagCompensatorUVE::SnapshotUVE* HitboxLagCompensatorUVE::ResolveBracketUVE(
    const LagTickUVE tick, const float fraction, LagTickUVE& resolvedTick, bool& clamped,
    bool& interpolated, SnapshotUVE& scratch) const {
    interpolated = false;
    const SnapshotUVE* const floor = FindSnapshotUVE(tick, resolvedTick, clamped);
    if (floor == nullptr || fraction == 0.0F || resolvedTick != tick) {
        return floor;
    }
    if (tick == std::numeric_limits<LagTickUVE>::max()) {
        return floor;
    }
    const auto ceiling = m_history.find(tick + 1U);
    if (ceiling == m_history.end()) {
        return floor;
    }
    // Interpolation covers what both snapshots saw: shared entities blend, floor-only ones
    // keep the floor pose (the resolve's live gates filter the dead anyway), and ceiling-only
    // ones are excluded - a spawn has no past to blend from.
    scratch.clear();
    scratch.reserve(floor->size());
    for (const auto& [entity, pose] : *floor) {
        const LagPoseUVE* const next = FindPoseUVE(ceiling->second, entity);
        scratch.emplace_back(entity,
                             next == nullptr ? pose : BlendPoseUVE(pose, *next, fraction));
    }
    interpolated = true;
    return &scratch;
}

LagShotReportUVE HitboxLagCompensatorUVE::ResolveShotFromUVE(
    const Scene::EntityUVE shooter, const SnapshotUVE* snapshot, const LagTickUVE resolvedTick,
    const bool clamped, Scene::IEntityManagerUVE& entityManager) {
    LagShotReportUVE report;
    // A shot from nothing - or from the destroyed - hits nothing. The invalid check comes
    // first so the manager never sees an id it cannot even ask about.
    if (shooter == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(shooter)) {
        return report;
    }
    report.resolvedTick = resolvedTick;
    report.clamped = clamped;
    if (report.clamped) {
        ++m_stats.resolvesClamped;
    }
    if (snapshot == nullptr) {
        return report;
    }
    // Current state, rewound pose: the attack is live now, the question is who it touched then.
    if (!entityManager.HasComponentUVE<Scene::Hitbox3DComponentUVE>(shooter)) {
        return report;
    }
    const Scene::Hitbox3DComponentUVE& hitbox =
        entityManager.GetComponentUVE<Scene::Hitbox3DComponentUVE>(shooter);
    if (!hitbox.enabled || !Scene::Hitbox3DUVE::IsArmedUVE(hitbox)) {
        return report;
    }
    const LagPoseUVE* const shooterPose = FindPoseUVE(*snapshot, shooter);
    if (shooterPose == nullptr) {
        return report;
    }
    // The shooter's own rewound size: a zero-scale attacker swings nothing.
    Math::Vector3UVE shooterHalf{};
    if (!TryRewoundHalfExtentsUVE(hitbox.halfExtents, shooterPose->scale, shooterHalf)) {
        return report;
    }
    Math::QuaternionUVE shooterRotation{};
    if (!Math::TryNormalizeUVE(shooterPose->rotation, shooterRotation)) {
        shooterRotation = {};
    }
    for (const auto& [entity, pose] : *snapshot) {
        // Snapshots outlive their entities: the dead keep their poses but lose their hurtboxes.
        if (!entityManager.IsAliveUVE(entity) ||
            !entityManager.HasComponentUVE<Scene::Hurtbox3DComponentUVE>(entity)) {
            continue;
        }
        const Scene::Hurtbox3DComponentUVE& hurtbox =
            entityManager.GetComponentUVE<Scene::Hurtbox3DComponentUVE>(entity);
        if (!Scene::Hurtbox3DUVE::IsVulnerableUVE(hurtbox)) {
            continue;
        }
        ++report.candidatesConsidered;
        // The live scan's own gates, in its own order: hitbox accepts, hurtbox accepts, then
        // the rewound-size gate, then the exact 15-axis test. Self-pairs die in the acceptance
        // checks, exactly as live.
        if (!Scene::Hitbox3DUVE::AcceptsTargetUVE(shooter, hitbox, entity, hurtbox.collisionLayer,
                                                  hurtbox.collisionMask, hurtbox.damageChannel)) {
            continue;
        }
        if (!Scene::Hurtbox3DUVE::AcceptsAttackerUVE(entity, hurtbox, shooter, hitbox.collisionLayer,
                                                     hitbox.collisionMask, hitbox.damageChannel)) {
            continue;
        }
        Math::Vector3UVE targetHalf{};
        if (!TryRewoundHalfExtentsUVE(hurtbox.halfExtents, pose.scale, targetHalf)) {
            continue;
        }
        Math::QuaternionUVE targetRotation{};
        if (!Math::TryNormalizeUVE(pose.rotation, targetRotation)) {
            targetRotation = {};
        }
        const std::optional<Math::PenetrationUVE> penetration =
            Detail::ComputeOrientedBoxOrientedBoxPenetrationUVE(
                shooterPose->position, shooterHalf, shooterRotation, pose.position, targetHalf,
                targetRotation);
        if (!penetration.has_value()) {
            continue;
        }
        report.pairs.push_back(Hitbox3DStrikePairUVE{shooter, entity, penetration->depth,
                                                     penetration->axis, hitbox.damageChannel});
    }
    m_stats.pairsFound += report.pairs.size();
    return report;
}

LagShotReportUVE HitboxLagCompensatorUVE::ResolveShotUVE(
    const Scene::EntityUVE shooter, const LagTickUVE targetTick,
    Scene::IEntityManagerUVE& entityManager) {
    ++m_stats.shotsResolved;
    LagTickUVE resolvedTick = 0U;
    bool clamped = false;
    const SnapshotUVE* const snapshot = FindSnapshotUVE(targetTick, resolvedTick, clamped);
    return ResolveShotFromUVE(shooter, snapshot, resolvedTick, clamped, entityManager);
}

LagShotReportUVE HitboxLagCompensatorUVE::ResolveShotAtUVE(
    const Scene::EntityUVE shooter, const LagTickUVE tick, const float tickFraction,
    Scene::IEntityManagerUVE& entityManager) {
    ++m_stats.shotsResolved;
    if (!std::isfinite(tickFraction) || tickFraction < 0.0F || tickFraction >= 1.0F) {
        return LagShotReportUVE{};
    }
    SnapshotUVE scratch;
    LagTickUVE resolvedTick = 0U;
    bool clamped = false;
    bool interpolated = false;
    const SnapshotUVE* const snapshot =
        ResolveBracketUVE(tick, tickFraction, resolvedTick, clamped, interpolated, scratch);
    LagShotReportUVE report =
        ResolveShotFromUVE(shooter, snapshot, resolvedTick, clamped, entityManager);
    report.interpolated = interpolated;
    if (interpolated) {
        ++m_stats.resolvesInterpolated;
    }
    return report;
}

LagRayReportUVE HitboxLagCompensatorUVE::ResolveRayFromUVE(
    const Scene::EntityUVE shooter, const Math::RayUVE& ray, const float maxDistance,
    const SnapshotUVE* snapshot, const LagTickUVE resolvedTick, const bool clamped,
    Scene::IEntityManagerUVE& entityManager) {
    LagRayReportUVE report;
    if (shooter == Scene::kInvalidEntityUVE || !entityManager.IsAliveUVE(shooter)) {
        return report;
    }
    report.resolvedTick = resolvedTick;
    report.clamped = clamped;
    if (report.clamped) {
        ++m_stats.resolvesClamped;
    }
    if (snapshot == nullptr) {
        return report;
    }
    // Identity only, no arming gate: the ray is not the hitbox's attack, it just borrows the
    // shooter's layers, mask, and channel for the same symmetric acceptance.
    if (!entityManager.HasComponentUVE<Scene::Hitbox3DComponentUVE>(shooter)) {
        return report;
    }
    const Scene::Hitbox3DComponentUVE& hitbox =
        entityManager.GetComponentUVE<Scene::Hitbox3DComponentUVE>(shooter);
    bool found = false;
    float nearest = maxDistance;
    for (const auto& [entity, pose] : *snapshot) {
        if (!entityManager.IsAliveUVE(entity) ||
            !entityManager.HasComponentUVE<Scene::Hurtbox3DComponentUVE>(entity)) {
            continue;
        }
        const Scene::Hurtbox3DComponentUVE& hurtbox =
            entityManager.GetComponentUVE<Scene::Hurtbox3DComponentUVE>(entity);
        if (!Scene::Hurtbox3DUVE::IsVulnerableUVE(hurtbox)) {
            continue;
        }
        if (!Scene::Hitbox3DUVE::AcceptsTargetUVE(shooter, hitbox, entity, hurtbox.collisionLayer,
                                                  hurtbox.collisionMask, hurtbox.damageChannel)) {
            continue;
        }
        if (!Scene::Hurtbox3DUVE::AcceptsAttackerUVE(entity, hurtbox, shooter, hitbox.collisionLayer,
                                                     hitbox.collisionMask, hitbox.damageChannel)) {
            continue;
        }
        Math::Vector3UVE targetHalf{};
        if (!TryRewoundHalfExtentsUVE(hurtbox.halfExtents, pose.scale, targetHalf)) {
            continue;
        }
        Math::QuaternionUVE targetRotation{};
        if (!Math::TryNormalizeUVE(pose.rotation, targetRotation)) {
            targetRotation = {};
        }
        const std::optional<Math::RayHitUVE> hit = Detail::IntersectRayOrientedBoxUVE(
            ray, Math::OrientedBoxUVE{pose.position, targetHalf, targetRotation}, maxDistance);
        // Strictly nearer wins, so ties keep snapshot order - deterministic per frame.
        if (hit.has_value() && (!found || hit->distance < nearest)) {
            found = true;
            nearest = hit->distance;
            report.entity = entity;
            report.distance = hit->distance;
        }
    }
    if (found) {
        report.point = ray.origin + ray.direction * nearest;
    }
    return report;
}

LagRayReportUVE HitboxLagCompensatorUVE::ResolveRayUVE(
    const Scene::EntityUVE shooter, const Math::RayUVE& ray, const float maxDistance,
    const LagTickUVE targetTick, Scene::IEntityManagerUVE& entityManager) {
    ++m_stats.raysResolved;
    LagRayReportUVE report;
    if (!std::isfinite(maxDistance) || maxDistance <= 0.0F) {
        return report;
    }
    const float length = Math::LengthUVE(ray.direction);
    if (!std::isfinite(length) || length < kMinimumRayLengthUVE) {
        return report;
    }
    Math::RayUVE unit = ray;
    unit.direction = ray.direction * (1.0F / length);
    LagTickUVE resolvedTick = 0U;
    bool clamped = false;
    const SnapshotUVE* const snapshot = FindSnapshotUVE(targetTick, resolvedTick, clamped);
    return ResolveRayFromUVE(shooter, unit, maxDistance, snapshot, resolvedTick, clamped,
                             entityManager);
}

LagRayReportUVE HitboxLagCompensatorUVE::ResolveRayAtUVE(
    const Scene::EntityUVE shooter, const Math::RayUVE& ray, const float maxDistance,
    const LagTickUVE tick, const float tickFraction, Scene::IEntityManagerUVE& entityManager) {
    ++m_stats.raysResolved;
    LagRayReportUVE report;
    if (!std::isfinite(maxDistance) || maxDistance <= 0.0F) {
        return report;
    }
    const float length = Math::LengthUVE(ray.direction);
    if (!std::isfinite(length) || length < kMinimumRayLengthUVE) {
        return report;
    }
    if (!std::isfinite(tickFraction) || tickFraction < 0.0F || tickFraction >= 1.0F) {
        return LagRayReportUVE{};
    }
    Math::RayUVE unit = ray;
    unit.direction = ray.direction * (1.0F / length);
    SnapshotUVE scratch;
    LagTickUVE resolvedTick = 0U;
    bool clamped = false;
    bool interpolated = false;
    const SnapshotUVE* const snapshot =
        ResolveBracketUVE(tick, tickFraction, resolvedTick, clamped, interpolated, scratch);
    report = ResolveRayFromUVE(shooter, unit, maxDistance, snapshot, resolvedTick, clamped,
                               entityManager);
    report.interpolated = interpolated;
    if (interpolated) {
        ++m_stats.resolvesInterpolated;
    }
    return report;
}

} // namespace UVE::Physics
