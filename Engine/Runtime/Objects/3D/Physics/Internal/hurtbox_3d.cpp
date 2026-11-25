// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hurtbox_3d.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "hitbox_3d.h"

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

void RememberReceived(Hurtbox3DComponent& hurtbox, const Entity hitbox) noexcept {
    if (ContainsEntity(hurtbox.receivedEntities.data(), hurtbox.receivedCount, hitbox)) {
        return;
    }
    if (hurtbox.receivedCount >= kMaximumHurtbox3DReceived) {
        hurtbox.receivedTruncated = true;
        return;
    }
    hurtbox.receivedEntities[hurtbox.receivedCount] = hitbox;
    ++hurtbox.receivedCount;
}

[[nodiscard]] bool IsDeeperHit(const Hurtbox3DHit& left, const Hurtbox3DHit& right) noexcept {
    if (left.penetrationDepth != right.penetrationDepth) {
        return left.penetrationDepth > right.penetrationDepth;
    }
    if (left.hitboxEntity.index != right.hitboxEntity.index) {
        return left.hitboxEntity.index < right.hitboxEntity.index;
    }
    return left.hitboxEntity.generation < right.hitboxEntity.generation;
}

} // namespace

bool IsHurtbox3DObjectComponentValid(const Hurtbox3DComponent& value) noexcept {
    const Hitbox3DComponent equivalent{value.halfExtents, value.collisionLayer, value.collisionMask,
                                         value.damageChannel, value.enabled};
    return IsHitbox3DObjectComponentValid(equivalent);
}

bool Hurtbox3D::IsVulnerable(const Hurtbox3DComponent& hurtbox) noexcept {
    return hurtbox.enabled && IsHurtbox3DObjectComponentValid(hurtbox);
}

bool Hurtbox3D::AcceptsAttacker(const Entity hurtboxEntity, const Hurtbox3DComponent& hurtbox,
                                      const Entity hitboxEntity, const std::uint32_t hitboxLayer,
                                      const std::uint32_t hitboxMask,
                                      const std::string_view hitboxChannel) noexcept {
    if (hitboxEntity == kInvalidEntity || hitboxEntity == hurtboxEntity) {
        return false;
    }
    if (hurtbox.ignoreEntity != kInvalidEntity && hitboxEntity == hurtbox.ignoreEntity) {
        return false;
    }
    if ((hitboxLayer & hurtbox.collisionMask) == 0U || (hurtbox.collisionLayer & hitboxMask) == 0U) {
        return false;
    }
    return hitboxChannel == hurtbox.damageChannel;
}

void Hurtbox3D::ClearHits(Hurtbox3DComponent& hurtbox) noexcept {
    hurtbox.hitCount = 0U;
    hurtbox.hitsTruncated = false;
}

void Hurtbox3D::ResetReceived(Hurtbox3DComponent& hurtbox) noexcept {
    hurtbox.receivedCount = 0U;
    hurtbox.receivedTruncated = false;
}

const Hurtbox3DHit* Hurtbox3D::GetHit(const Hurtbox3DComponent& hurtbox,
                                               const std::size_t index) noexcept {
    if (index >= hurtbox.hitCount) {
        return nullptr;
    }
    return &hurtbox.hits[index];
}

bool Hurtbox3D::HasHitFrom(const Hurtbox3DComponent& hurtbox, const Entity hitbox) noexcept {
    for (std::uint8_t index = 0U; index < hurtbox.hitCount; ++index) {
        if (hurtbox.hits[index].hitboxEntity == hitbox) {
            return true;
        }
    }
    return false;
}

bool Hurtbox3D::HasReceived(const Hurtbox3DComponent& hurtbox, const Entity hitbox) noexcept {
    return ContainsEntity(hurtbox.receivedEntities.data(), hurtbox.receivedCount, hitbox);
}

void Hurtbox3D::CommitHits(Hurtbox3DComponent& hurtbox, const Hurtbox3DHit* incoming,
                                 const std::size_t incomingCount) {
    ClearHits(hurtbox);
    if (incoming == nullptr || incomingCount == 0U) {
        return;
    }

    std::vector<Hurtbox3DHit> unique;
    unique.reserve(incomingCount);
    for (std::size_t index = 0U; index < incomingCount; ++index) {
        const Hurtbox3DHit& hit = incoming[index];
        if (hit.hitboxEntity == kInvalidEntity || !std::isfinite(hit.penetrationDepth) ||
            hit.penetrationDepth <= 0.0F) {
            continue;
        }
        bool merged = false;
        for (Hurtbox3DHit& existing : unique) {
            if (existing.hitboxEntity != hit.hitboxEntity) {
                continue;
            }
            if (hit.penetrationDepth > existing.penetrationDepth) {
                existing = hit;
            }
            merged = true;
            break;
        }
        if (!merged) {
            unique.push_back(hit);
        }
    }

    std::sort(unique.begin(), unique.end(), IsDeeperHit);

    hurtbox.hitsTruncated = unique.size() > kMaximumHurtbox3DHits;
    const std::size_t committed = std::min(unique.size(), kMaximumHurtbox3DHits);
    for (std::size_t index = 0U; index < committed; ++index) {
        Hurtbox3DHit hit = unique[index];
        hit.received = !HasReceived(hurtbox, hit.hitboxEntity);
        if (hit.received) {
            RememberReceived(hurtbox, hit.hitboxEntity);
        }
        hurtbox.hits[index] = hit;
    }
    hurtbox.hitCount = static_cast<std::uint8_t>(committed);
}

} // namespace UVE::Scene
