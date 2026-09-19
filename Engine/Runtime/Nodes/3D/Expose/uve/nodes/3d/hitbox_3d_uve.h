// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "uve/component/entity_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Upper bound on the per-hitbox strike list refreshed every frame by SyncHitbox3DStrikesUVE().
/// A frame with more overlapping hurtboxes than this keeps the first ones in deterministic
/// entity order and raises strikesTruncated instead of silently dropping the overflow fact.
inline constexpr std::size_t kMaximumHitbox3DStrikesUVE = 16U;

/// One runtime strike record: which hurtbox this hitbox overlapped this frame, and how deeply.
/// The depth is the exact oriented-box penetration along the minimum translation axis, in world
/// units, pointing from the hitbox toward the hurtbox.
struct Hitbox3DStrikeUVE final {
    EntityUVE hurtboxEntity{};
    float penetrationDepth = 0.0F;

    [[nodiscard]] bool operator==(const Hitbox3DStrikeUVE&) const noexcept = default;
};

struct Hitbox3DNodeComponentUVE final {
    Math::Vector3UVE halfExtents{0.5F, 0.5F, 0.5F};
    std::uint32_t collisionLayer = 1U;
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    std::string damageChannel = "default";
    bool enabled = true;
    // Runtime-only result state below, refreshed every frame by SyncHitbox3DStrikesUVE() - never
    // serialized (mirrors RayCast3DNodeComponentUVE's own authored-config/runtime-state split).
    // Deliberately NOT done here: applying what a hit means (damage, knockback, i-frames,
    // events). That is gameplay code this engine does not own yet - real, separate follow-up,
    // not silently faked.
    std::array<Hitbox3DStrikeUVE, kMaximumHitbox3DStrikesUVE> strikes{};
    std::uint8_t strikeCount = 0U;
    bool strikesTruncated = false;
};

[[nodiscard]] bool IsHitbox3DNodeComponentValidUVE(const Hitbox3DNodeComponentUVE& value) noexcept;

/// Refreshes every Hitbox3D node's runtime strike state against every Hurtbox3D node in the
/// entity manager, in place, every frame. The full pairing contract:
///
/// - Only enabled, valid hitboxes and hurtboxes participate; everything else fails closed
///   (a disabled or invalid hitbox ends the frame with zero strikes, never stale ones).
/// - Both volumes are exact oriented boxes: the entity's world position/rotation with the
///   authored halfExtents. World scale is intentionally not applied - the same convention
///   ColliderComponentUVE and AreaComponentUVE world shapes already use. Degenerate (non-
///   normalizable) rotations fall back to identity.
/// - A strike requires symmetric layer/mask acceptance: (hurtbox.collisionLayer &
///   hitbox.collisionMask) and (hitbox.collisionLayer & hurtbox.collisionMask) must both be
///   non-zero, matching AreaOverlapSystemUVE's own acceptance rule.
/// - A strike requires the two damage channels to be equal (both default to "default").
/// - A hitbox never strikes a hurtbox on its own entity (self-exclusion).
/// - Overlap itself is the exact 15-axis oriented-box-vs-oriented-box test from
///   Physics::Detail - touching boundaries are not strikes.
///
/// This seam only writes the runtime strike state; it publishes no events and owns no other
/// entity state. The per-hitbox result list is bounded by kMaximumHitbox3DStrikesUVE in
/// deterministic entity iteration order, with strikesTruncated reporting overflow.
void SyncHitbox3DStrikesUVE(IEntityManagerUVE& entityManager);

} // namespace UVE::Scene
