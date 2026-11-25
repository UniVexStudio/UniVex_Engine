// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_3d.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool ContainsEntity(const Entity* entities, const std::uint8_t count,
                                     const Entity entity) noexcept {
    for (std::uint8_t index = 0U; index < count; ++index) {
        if (entities[index] == entity) {
            return true;
        }
    }
    return false;
}

void RememberStruck(Hitbox3DComponent& hitbox, const Entity hurtbox) noexcept {
    if (ContainsEntity(hitbox.struckEntities.data(), hitbox.struckCount, hurtbox)) {
        return;
    }
    if (hitbox.struckCount >= kMaximumHitbox3DStruck) {
        hitbox.struckTruncated = true;
        return;
    }
    hitbox.struckEntities[hitbox.struckCount] = hurtbox;
    ++hitbox.struckCount;
}

[[nodiscard]] bool IsDeeperStrike(const Hitbox3DStrike& left, const Hitbox3DStrike& right) noexcept {
    if (left.penetrationDepth != right.penetrationDepth) {
        return left.penetrationDepth > right.penetrationDepth;
    }
    if (left.hurtboxEntity.index != right.hurtboxEntity.index) {
        return left.hurtboxEntity.index < right.hurtboxEntity.index;
    }
    return left.hurtboxEntity.generation < right.hurtboxEntity.generation;
}

} // namespace

bool IsHitbox3DObjectComponentValid(const Hitbox3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           IsBounded3DObjectString(value.damageChannel, false);
}

bool Hitbox3D::IsArmed(const Hitbox3DComponent& hitbox) noexcept {
    return hitbox.enabled && IsHitbox3DObjectComponentValid(hitbox);
}

bool Hitbox3D::AcceptsTarget(const Entity hitboxEntity, const Hitbox3DComponent& hitbox,
                                   const Entity hurtboxEntity, const std::uint32_t hurtboxLayer,
                                   const std::uint32_t hurtboxMask,
                                   const std::string_view hurtboxChannel) noexcept {
    if (hurtboxEntity == kInvalidEntity || hurtboxEntity == hitboxEntity) {
        return false;
    }
    if (hitbox.ignoreEntity != kInvalidEntity && hurtboxEntity == hitbox.ignoreEntity) {
        return false;
    }
    if ((hurtboxLayer & hitbox.collisionMask) == 0U || (hitbox.collisionLayer & hurtboxMask) == 0U) {
        return false;
    }
    return hurtboxChannel == hitbox.damageChannel;
}

void Hitbox3D::ClearStrikes(Hitbox3DComponent& hitbox) noexcept {
    hitbox.strikeCount = 0U;
    hitbox.strikesTruncated = false;
}

void Hitbox3D::ResetActivation(Hitbox3DComponent& hitbox) noexcept {
    hitbox.struckCount = 0U;
    hitbox.struckTruncated = false;
}

const Hitbox3DStrike* Hitbox3D::GetStrike(const Hitbox3DComponent& hitbox,
                                                   const std::size_t index) noexcept {
    if (index >= hitbox.strikeCount) {
        return nullptr;
    }
    return &hitbox.strikes[index];
}

bool Hitbox3D::HasStrikeAgainst(const Hitbox3DComponent& hitbox, const Entity hurtbox) noexcept {
    for (std::uint8_t index = 0U; index < hitbox.strikeCount; ++index) {
        if (hitbox.strikes[index].hurtboxEntity == hurtbox) {
            return true;
        }
    }
    return false;
}

bool Hitbox3D::HasStruck(const Hitbox3DComponent& hitbox, const Entity hurtbox) noexcept {
    return ContainsEntity(hitbox.struckEntities.data(), hitbox.struckCount, hurtbox);
}

void Hitbox3D::CommitStrikes(Hitbox3DComponent& hitbox, const Hitbox3DStrike* overlaps,
                                   const std::size_t overlapCount) {
    ClearStrikes(hitbox);
    if (overlaps == nullptr || overlapCount == 0U) {
        return;
    }

    std::vector<Hitbox3DStrike> unique;
    unique.reserve(overlapCount);
    for (std::size_t index = 0U; index < overlapCount; ++index) {
        const Hitbox3DStrike& overlap = overlaps[index];
        if (overlap.hurtboxEntity == kInvalidEntity || !std::isfinite(overlap.penetrationDepth) ||
            overlap.penetrationDepth <= 0.0F) {
            continue;
        }
        bool merged = false;
        for (Hitbox3DStrike& existing : unique) {
            if (existing.hurtboxEntity != overlap.hurtboxEntity) {
                continue;
            }
            if (overlap.penetrationDepth > existing.penetrationDepth) {
                existing = overlap;
            }
            merged = true;
            break;
        }
        if (!merged) {
            unique.push_back(overlap);
        }
    }

    std::sort(unique.begin(), unique.end(), IsDeeperStrike);

    hitbox.strikesTruncated = unique.size() > kMaximumHitbox3DStrikes;
    const std::size_t committed = std::min(unique.size(), kMaximumHitbox3DStrikes);
    for (std::size_t index = 0U; index < committed; ++index) {
        Hitbox3DStrike strike = unique[index];
        strike.landed = !HasStruck(hitbox, strike.hurtboxEntity);
        if (strike.landed) {
            RememberStruck(hitbox, strike.hurtboxEntity);
        }
        hitbox.strikes[index] = strike;
    }
    hitbox.strikeCount = static_cast<std::uint8_t>(committed);
}

} // namespace UVE::Scene
