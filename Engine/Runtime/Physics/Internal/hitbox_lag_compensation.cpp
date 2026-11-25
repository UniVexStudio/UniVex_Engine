// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_lag_compensation.h"

#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include "world_transform_component.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "shape_narrow_phase.h"

namespace UVE::Physics {
namespace {

// A zero direction has no line to cast along; anything shorter is degenerate input, not a ray.
inline constexpr float kMinimumRayLength = 1e-6F;

[[nodiscard]] const LagPose* FindPose(
    const std::vector<std::pair<Scene::Entity, LagPose>>& snapshot,
    const Scene::Entity entity) noexcept {
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
[[nodiscard]] LagPose BlendPose(const LagPose& floor, const LagPose& ceiling,
                                      const float alpha) {
    LagPose blended;
    blended.position = floor.position + (ceiling.position - floor.position) * alpha;
    blended.scale = floor.scale + (ceiling.scale - floor.scale) * alpha;
    if (!Math::TrySlerp(floor.rotation, ceiling.rotation, alpha, blended.rotation)) {
        blended.rotation = floor.rotation;
    }
    return blended;
}

// Rewound half-extents: authored size times the absolute recorded scale (a mirror occupies
// the same box). Zero, negative-authored, or non-finite products are unhittable - a
// zero-scale body renders nothing, and garbage resolves empty, fail closed.
[[nodiscard]] bool TryRewoundHalfExtents(const Math::Vector3& authored,
                                            const Math::Vector3& scale,
                                            Math::Vector3& out) noexcept {
    const float x = authored.x * std::fabs(scale.x);
    const float y = authored.y * std::fabs(scale.y);
    const float z = authored.z * std::fabs(scale.z);
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || x <= 0.0F || y <= 0.0F ||
        z <= 0.0F) {
        return false;
    }
    out = Math::Vector3{x, y, z};
    return true;
}

} // namespace

bool HitboxLagCompensator::Record(const LagTick tick,
                                        Scene::IEntityManager& entityManager) {
    if (m_hasHistory && tick <= m_lastRecorded) {
        ++m_stats.recordsRejected;
        return false;
    }
    Snapshot snapshot;
    entityManager.ForEach<Scene::WorldTransformComponent>(
        [&entityManager, &snapshot](const Scene::Entity entity,
                                    const Scene::WorldTransformComponent& world) {
            if (!entityManager.HasComponent<Scene::Hitbox3DComponent>(entity) &&
                !entityManager.HasComponent<Scene::Hurtbox3DComponent>(entity)) {
                return;
            }
            snapshot.emplace_back(entity,
                                LagPose{world.worldPosition, world.worldRotation,
                                           world.worldScale});
        });
    m_history.emplace(tick, std::move(snapshot));
    m_lastRecorded = tick;
    m_hasHistory = true;
    ++m_stats.snapshotsRecorded;
    while (m_history.size() > kLagHistoryMaximumTicks) {
        m_history.erase(m_history.begin());
        ++m_stats.snapshotsEvicted;
    }
    return true;
}

const HitboxLagCompensator::Snapshot* HitboxLagCompensator::FindSnapshot(
    const LagTick targetTick, LagTick& resolvedTick, bool& clamped) const {
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

const HitboxLagCompensator::Snapshot* HitboxLagCompensator::ResolveBracket(
    const LagTick tick, const float fraction, LagTick& resolvedTick, bool& clamped,
    bool& interpolated, Snapshot& scratch) const {
    interpolated = false;
    const Snapshot* const floor = FindSnapshot(tick, resolvedTick, clamped);
    if (floor == nullptr || fraction == 0.0F || resolvedTick != tick) {
        return floor;
    }
    if (tick == std::numeric_limits<LagTick>::max()) {
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
        const LagPose* const next = FindPose(ceiling->second, entity);
        scratch.emplace_back(entity,
                             next == nullptr ? pose : BlendPose(pose, *next, fraction));
    }
    interpolated = true;
    return &scratch;
}

LagShotReport HitboxLagCompensator::ResolveShotFrom(
    const Scene::Entity shooter, const Snapshot* snapshot, const LagTick resolvedTick,
    const bool clamped, Scene::IEntityManager& entityManager) {
    LagShotReport report;
    // A shot from nothing - or from the destroyed - hits nothing. The invalid check comes
    // first so the manager never sees an id it cannot even ask about.
    if (shooter == Scene::kInvalidEntity || !entityManager.IsAlive(shooter)) {
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
    if (!entityManager.HasComponent<Scene::Hitbox3DComponent>(shooter)) {
        return report;
    }
    const Scene::Hitbox3DComponent& hitbox =
        entityManager.GetComponent<Scene::Hitbox3DComponent>(shooter);
    if (!hitbox.enabled || !Scene::Hitbox3D::IsArmed(hitbox)) {
        return report;
    }
    const LagPose* const shooterPose = FindPose(*snapshot, shooter);
    if (shooterPose == nullptr) {
        return report;
    }
    // The shooter's own rewound size: a zero-scale attacker swings nothing.
    Math::Vector3 shooterHalf{};
    if (!TryRewoundHalfExtents(hitbox.halfExtents, shooterPose->scale, shooterHalf)) {
        return report;
    }
    Math::Quaternion shooterRotation{};
    if (!Math::TryNormalize(shooterPose->rotation, shooterRotation)) {
        shooterRotation = {};
    }
    for (const auto& [entity, pose] : *snapshot) {
        // Snapshots outlive their entities: the dead keep their poses but lose their hurtboxes.
        if (!entityManager.IsAlive(entity) ||
            !entityManager.HasComponent<Scene::Hurtbox3DComponent>(entity)) {
            continue;
        }
        const Scene::Hurtbox3DComponent& hurtbox =
            entityManager.GetComponent<Scene::Hurtbox3DComponent>(entity);
        if (!Scene::Hurtbox3D::IsVulnerable(hurtbox)) {
            continue;
        }
        ++report.candidatesConsidered;
        // The live scan's own gates, in its own order: hitbox accepts, hurtbox accepts, then
        // the rewound-size gate, then the exact 15-axis test. Self-pairs die in the acceptance
        // checks, exactly as live.
        if (!Scene::Hitbox3D::AcceptsTarget(shooter, hitbox, entity, hurtbox.collisionLayer,
                                                  hurtbox.collisionMask, hurtbox.damageChannel)) {
            continue;
        }
        if (!Scene::Hurtbox3D::AcceptsAttacker(entity, hurtbox, shooter, hitbox.collisionLayer,
                                                     hitbox.collisionMask, hitbox.damageChannel)) {
            continue;
        }
        Math::Vector3 targetHalf{};
        if (!TryRewoundHalfExtents(hurtbox.halfExtents, pose.scale, targetHalf)) {
            continue;
        }
        Math::Quaternion targetRotation{};
        if (!Math::TryNormalize(pose.rotation, targetRotation)) {
            targetRotation = {};
        }
        const std::optional<Math::Penetration> penetration =
            Detail::ComputeOrientedBoxOrientedBoxPenetration(
                shooterPose->position, shooterHalf, shooterRotation, pose.position, targetHalf,
                targetRotation);
        if (!penetration.has_value()) {
            continue;
        }
        report.pairs.push_back(Hitbox3DStrikePair{shooter, entity, penetration->depth,
                                                     penetration->axis, hitbox.damageChannel});
    }
    m_stats.pairsFound += report.pairs.size();
    return report;
}

LagShotReport HitboxLagCompensator::ResolveShot(
    const Scene::Entity shooter, const LagTick targetTick,
    Scene::IEntityManager& entityManager) {
    ++m_stats.shotsResolved;
    LagTick resolvedTick = 0U;
    bool clamped = false;
    const Snapshot* const snapshot = FindSnapshot(targetTick, resolvedTick, clamped);
    return ResolveShotFrom(shooter, snapshot, resolvedTick, clamped, entityManager);
}

LagShotReport HitboxLagCompensator::ResolveShotAt(
    const Scene::Entity shooter, const LagTick tick, const float tickFraction,
    Scene::IEntityManager& entityManager) {
    ++m_stats.shotsResolved;
    if (!std::isfinite(tickFraction) || tickFraction < 0.0F || tickFraction >= 1.0F) {
        return LagShotReport{};
    }
    Snapshot scratch;
    LagTick resolvedTick = 0U;
    bool clamped = false;
    bool interpolated = false;
    const Snapshot* const snapshot =
        ResolveBracket(tick, tickFraction, resolvedTick, clamped, interpolated, scratch);
    LagShotReport report =
        ResolveShotFrom(shooter, snapshot, resolvedTick, clamped, entityManager);
    report.interpolated = interpolated;
    if (interpolated) {
        ++m_stats.resolvesInterpolated;
    }
    return report;
}

LagRayReport HitboxLagCompensator::ResolveRayFrom(
    const Scene::Entity shooter, const Math::Ray& ray, const float maxDistance,
    const Snapshot* snapshot, const LagTick resolvedTick, const bool clamped,
    Scene::IEntityManager& entityManager) {
    LagRayReport report;
    if (shooter == Scene::kInvalidEntity || !entityManager.IsAlive(shooter)) {
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
    if (!entityManager.HasComponent<Scene::Hitbox3DComponent>(shooter)) {
        return report;
    }
    const Scene::Hitbox3DComponent& hitbox =
        entityManager.GetComponent<Scene::Hitbox3DComponent>(shooter);
    bool found = false;
    float nearest = maxDistance;
    for (const auto& [entity, pose] : *snapshot) {
        if (!entityManager.IsAlive(entity) ||
            !entityManager.HasComponent<Scene::Hurtbox3DComponent>(entity)) {
            continue;
        }
        const Scene::Hurtbox3DComponent& hurtbox =
            entityManager.GetComponent<Scene::Hurtbox3DComponent>(entity);
        if (!Scene::Hurtbox3D::IsVulnerable(hurtbox)) {
            continue;
        }
        if (!Scene::Hitbox3D::AcceptsTarget(shooter, hitbox, entity, hurtbox.collisionLayer,
                                                  hurtbox.collisionMask, hurtbox.damageChannel)) {
            continue;
        }
        if (!Scene::Hurtbox3D::AcceptsAttacker(entity, hurtbox, shooter, hitbox.collisionLayer,
                                                     hitbox.collisionMask, hitbox.damageChannel)) {
            continue;
        }
        Math::Vector3 targetHalf{};
        if (!TryRewoundHalfExtents(hurtbox.halfExtents, pose.scale, targetHalf)) {
            continue;
        }
        Math::Quaternion targetRotation{};
        if (!Math::TryNormalize(pose.rotation, targetRotation)) {
            targetRotation = {};
        }
        const std::optional<Math::RayHit> hit = Detail::IntersectRayOrientedBox(
            ray, Math::OrientedBox{pose.position, targetHalf, targetRotation}, maxDistance);
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

LagRayReport HitboxLagCompensator::ResolveRay(
    const Scene::Entity shooter, const Math::Ray& ray, const float maxDistance,
    const LagTick targetTick, Scene::IEntityManager& entityManager) {
    ++m_stats.raysResolved;
    LagRayReport report;
    if (!std::isfinite(maxDistance) || maxDistance <= 0.0F) {
        return report;
    }
    const float length = Math::Length(ray.direction);
    if (!std::isfinite(length) || length < kMinimumRayLength) {
        return report;
    }
    Math::Ray unit = ray;
    unit.direction = ray.direction * (1.0F / length);
    LagTick resolvedTick = 0U;
    bool clamped = false;
    const Snapshot* const snapshot = FindSnapshot(targetTick, resolvedTick, clamped);
    return ResolveRayFrom(shooter, unit, maxDistance, snapshot, resolvedTick, clamped,
                             entityManager);
}

LagRayReport HitboxLagCompensator::ResolveRayAt(
    const Scene::Entity shooter, const Math::Ray& ray, const float maxDistance,
    const LagTick tick, const float tickFraction, Scene::IEntityManager& entityManager) {
    ++m_stats.raysResolved;
    LagRayReport report;
    if (!std::isfinite(maxDistance) || maxDistance <= 0.0F) {
        return report;
    }
    const float length = Math::Length(ray.direction);
    if (!std::isfinite(length) || length < kMinimumRayLength) {
        return report;
    }
    if (!std::isfinite(tickFraction) || tickFraction < 0.0F || tickFraction >= 1.0F) {
        return LagRayReport{};
    }
    Math::Ray unit = ray;
    unit.direction = ray.direction * (1.0F / length);
    Snapshot scratch;
    LagTick resolvedTick = 0U;
    bool clamped = false;
    bool interpolated = false;
    const Snapshot* const snapshot =
        ResolveBracket(tick, tickFraction, resolvedTick, clamped, interpolated, scratch);
    report = ResolveRayFrom(shooter, unit, maxDistance, snapshot, resolvedTick, clamped,
                               entityManager);
    report.interpolated = interpolated;
    if (interpolated) {
        ++m_stats.resolvesInterpolated;
    }
    return report;
}

} // namespace UVE::Physics
