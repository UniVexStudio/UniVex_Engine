// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/hurtbox_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "uve/objects/3d/hitbox_3d_uve.h"

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

void RememberReceivedUVE(Hurtbox3DComponentUVE& hurtbox, const EntityUVE hitbox) noexcept {
    if (ContainsEntityUVE(hurtbox.receivedEntities.data(), hurtbox.receivedCount, hitbox)) {
        return;
    }
    if (hurtbox.receivedCount >= kMaximumHurtbox3DReceivedUVE) {
        hurtbox.receivedTruncated = true;
        return;
    }
    hurtbox.receivedEntities[hurtbox.receivedCount] = hitbox;
    ++hurtbox.receivedCount;
}

[[nodiscard]] bool IsDeeperHitUVE(const Hurtbox3DHitUVE& left, const Hurtbox3DHitUVE& right) noexcept {
    if (left.penetrationDepth != right.penetrationDepth) {
        return left.penetrationDepth > right.penetrationDepth;
    }
    if (left.hitboxEntity.index != right.hitboxEntity.index) {
        return left.hitboxEntity.index < right.hitboxEntity.index;
    }
    return left.hitboxEntity.generation < right.hitboxEntity.generation;
}

} // namespace

bool IsHurtbox3DObjectComponentValidUVE(const Hurtbox3DComponentUVE& value) noexcept {
    const Hitbox3DComponentUVE equivalent{value.halfExtents, value.collisionLayer, value.collisionMask,
                                         value.damageChannel, value.enabled};
    return IsHitbox3DObjectComponentValidUVE(equivalent);
}

bool Hurtbox3DUVE::IsVulnerableUVE(const Hurtbox3DComponentUVE& hurtbox) noexcept {
    return hurtbox.enabled && IsHurtbox3DObjectComponentValidUVE(hurtbox);
}

bool Hurtbox3DUVE::AcceptsAttackerUVE(const EntityUVE hurtboxEntity, const Hurtbox3DComponentUVE& hurtbox,
                                      const EntityUVE hitboxEntity, const std::uint32_t hitboxLayer,
                                      const std::uint32_t hitboxMask,
                                      const std::string_view hitboxChannel) noexcept {
    if (hitboxEntity == kInvalidEntityUVE || hitboxEntity == hurtboxEntity) {
        return false;
    }
    if (hurtbox.ignoreEntity != kInvalidEntityUVE && hitboxEntity == hurtbox.ignoreEntity) {
        return false;
    }
    if ((hitboxLayer & hurtbox.collisionMask) == 0U || (hurtbox.collisionLayer & hitboxMask) == 0U) {
        return false;
    }
    return hitboxChannel == hurtbox.damageChannel;
}

void Hurtbox3DUVE::ClearHitsUVE(Hurtbox3DComponentUVE& hurtbox) noexcept {
    hurtbox.hitCount = 0U;
    hurtbox.hitsTruncated = false;
}

void Hurtbox3DUVE::ResetReceivedUVE(Hurtbox3DComponentUVE& hurtbox) noexcept {
    hurtbox.receivedCount = 0U;
    hurtbox.receivedTruncated = false;
}

const Hurtbox3DHitUVE* Hurtbox3DUVE::GetHitUVE(const Hurtbox3DComponentUVE& hurtbox,
                                               const std::size_t index) noexcept {
    if (index >= hurtbox.hitCount) {
        return nullptr;
    }
    return &hurtbox.hits[index];
}

bool Hurtbox3DUVE::HasHitFromUVE(const Hurtbox3DComponentUVE& hurtbox, const EntityUVE hitbox) noexcept {
    for (std::uint8_t index = 0U; index < hurtbox.hitCount; ++index) {
        if (hurtbox.hits[index].hitboxEntity == hitbox) {
            return true;
        }
    }
    return false;
}

bool Hurtbox3DUVE::HasReceivedUVE(const Hurtbox3DComponentUVE& hurtbox, const EntityUVE hitbox) noexcept {
    return ContainsEntityUVE(hurtbox.receivedEntities.data(), hurtbox.receivedCount, hitbox);
}

void Hurtbox3DUVE::CommitHitsUVE(Hurtbox3DComponentUVE& hurtbox, const Hurtbox3DHitUVE* incoming,
                                 const std::size_t incomingCount) {
    ClearHitsUVE(hurtbox);
    if (incoming == nullptr || incomingCount == 0U) {
        return;
    }

    std::vector<Hurtbox3DHitUVE> unique;
    unique.reserve(incomingCount);
    for (std::size_t index = 0U; index < incomingCount; ++index) {
        const Hurtbox3DHitUVE& hit = incoming[index];
        if (hit.hitboxEntity == kInvalidEntityUVE || !std::isfinite(hit.penetrationDepth) ||
            hit.penetrationDepth <= 0.0F) {
            continue;
        }
        bool merged = false;
        for (Hurtbox3DHitUVE& existing : unique) {
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

    std::sort(unique.begin(), unique.end(), IsDeeperHitUVE);

    hurtbox.hitsTruncated = unique.size() > kMaximumHurtbox3DHitsUVE;
    const std::size_t committed = std::min(unique.size(), kMaximumHurtbox3DHitsUVE);
    for (std::size_t index = 0U; index < committed; ++index) {
        Hurtbox3DHitUVE hit = unique[index];
        hit.received = !HasReceivedUVE(hurtbox, hit.hitboxEntity);
        if (hit.received) {
            RememberReceivedUVE(hurtbox, hit.hitboxEntity);
        }
        hurtbox.hits[index] = hit;
    }
    hurtbox.hitCount = static_cast<std::uint8_t>(committed);
}

} // namespace UVE::Scene
