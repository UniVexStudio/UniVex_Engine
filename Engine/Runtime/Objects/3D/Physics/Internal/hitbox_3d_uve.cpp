// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/hitbox_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool ContainsEntityUVE(const EntityUVE* entities, const std::uint8_t count,
                                     const EntityUVE entity) noexcept {
    for (std::uint8_t index = 0U; index < count; ++index) {
        if (entities[index] == entity) {
            return true;
        }
    }
    return false;
}

void RememberStruckUVE(Hitbox3DComponentUVE& hitbox, const EntityUVE hurtbox) noexcept {
    if (ContainsEntityUVE(hitbox.struckEntities.data(), hitbox.struckCount, hurtbox)) {
        return;
    }
    if (hitbox.struckCount >= kMaximumHitbox3DStruckUVE) {
        hitbox.struckTruncated = true;
        return;
    }
    hitbox.struckEntities[hitbox.struckCount] = hurtbox;
    ++hitbox.struckCount;
}

[[nodiscard]] bool IsDeeperStrikeUVE(const Hitbox3DStrikeUVE& left, const Hitbox3DStrikeUVE& right) noexcept {
    if (left.penetrationDepth != right.penetrationDepth) {
        return left.penetrationDepth > right.penetrationDepth;
    }
    if (left.hurtboxEntity.index != right.hurtboxEntity.index) {
        return left.hurtboxEntity.index < right.hurtboxEntity.index;
    }
    return left.hurtboxEntity.generation < right.hurtboxEntity.generation;
}

} // namespace

bool IsHitbox3DObjectComponentValidUVE(const Hitbox3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.halfExtents) && value.halfExtents.x > 0.0F &&
           value.halfExtents.y > 0.0F && value.halfExtents.z > 0.0F && value.collisionLayer != 0U &&
           IsBounded3DObjectStringUVE(value.damageChannel, false);
}

bool Hitbox3DUVE::IsArmedUVE(const Hitbox3DComponentUVE& hitbox) noexcept {
    return hitbox.enabled && IsHitbox3DObjectComponentValidUVE(hitbox);
}

bool Hitbox3DUVE::AcceptsTargetUVE(const EntityUVE hitboxEntity, const Hitbox3DComponentUVE& hitbox,
                                   const EntityUVE hurtboxEntity, const std::uint32_t hurtboxLayer,
                                   const std::uint32_t hurtboxMask,
                                   const std::string_view hurtboxChannel) noexcept {
    if (hurtboxEntity == kInvalidEntityUVE || hurtboxEntity == hitboxEntity) {
        return false;
    }
    if (hitbox.ignoreEntity != kInvalidEntityUVE && hurtboxEntity == hitbox.ignoreEntity) {
        return false;
    }
    if ((hurtboxLayer & hitbox.collisionMask) == 0U || (hitbox.collisionLayer & hurtboxMask) == 0U) {
        return false;
    }
    return hurtboxChannel == hitbox.damageChannel;
}

void Hitbox3DUVE::ClearStrikesUVE(Hitbox3DComponentUVE& hitbox) noexcept {
    hitbox.strikeCount = 0U;
    hitbox.strikesTruncated = false;
}

void Hitbox3DUVE::ResetActivationUVE(Hitbox3DComponentUVE& hitbox) noexcept {
    hitbox.struckCount = 0U;
    hitbox.struckTruncated = false;
}

const Hitbox3DStrikeUVE* Hitbox3DUVE::GetStrikeUVE(const Hitbox3DComponentUVE& hitbox,
                                                   const std::size_t index) noexcept {
    if (index >= hitbox.strikeCount) {
        return nullptr;
    }
    return &hitbox.strikes[index];
}

bool Hitbox3DUVE::HasStrikeAgainstUVE(const Hitbox3DComponentUVE& hitbox, const EntityUVE hurtbox) noexcept {
    for (std::uint8_t index = 0U; index < hitbox.strikeCount; ++index) {
        if (hitbox.strikes[index].hurtboxEntity == hurtbox) {
            return true;
        }
    }
    return false;
}

bool Hitbox3DUVE::HasStruckUVE(const Hitbox3DComponentUVE& hitbox, const EntityUVE hurtbox) noexcept {
    return ContainsEntityUVE(hitbox.struckEntities.data(), hitbox.struckCount, hurtbox);
}

void Hitbox3DUVE::CommitStrikesUVE(Hitbox3DComponentUVE& hitbox, const Hitbox3DStrikeUVE* overlaps,
                                   const std::size_t overlapCount) {
    ClearStrikesUVE(hitbox);
    if (overlaps == nullptr || overlapCount == 0U) {
        return;
    }

    std::vector<Hitbox3DStrikeUVE> unique;
    unique.reserve(overlapCount);
    for (std::size_t index = 0U; index < overlapCount; ++index) {
        const Hitbox3DStrikeUVE& overlap = overlaps[index];
        if (overlap.hurtboxEntity == kInvalidEntityUVE || !std::isfinite(overlap.penetrationDepth) ||
            overlap.penetrationDepth <= 0.0F) {
            continue;
        }
        bool merged = false;
        for (Hitbox3DStrikeUVE& existing : unique) {
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

    std::sort(unique.begin(), unique.end(), IsDeeperStrikeUVE);

    hitbox.strikesTruncated = unique.size() > kMaximumHitbox3DStrikesUVE;
    const std::size_t committed = std::min(unique.size(), kMaximumHitbox3DStrikesUVE);
    for (std::size_t index = 0U; index < committed; ++index) {
        Hitbox3DStrikeUVE strike = unique[index];
        strike.landed = !HasStruckUVE(hitbox, strike.hurtboxEntity);
        if (strike.landed) {
            RememberStruckUVE(hitbox, strike.hurtboxEntity);
        }
        hitbox.strikes[index] = strike;
    }
    hitbox.strikeCount = static_cast<std::uint8_t>(committed);
}

} // namespace UVE::Scene
