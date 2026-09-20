// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "uve/component/entity_uve.h"
#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

/// Upper bound on the per-hitbox strike list refreshed every frame by
/// EngineCoreUVE::SyncHitbox3DNodesUVE(). A frame with more overlapping hurtboxes than this
/// keeps the first ones in deterministic entity order and raises strikesTruncated instead of
/// silently dropping the overflow fact.
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
    // Runtime-only result state below, refreshed every frame by EngineCoreUVE's
    // SyncHitbox3DNodesUVE() (which, like RayCast3D/Projectile3D, keeps the node's per-frame
    // behavior in the engine core tick - node modules stay pure authoring data) - never
    // serialized, mirroring RayCast3DNodeComponentUVE's authored-config/runtime-state split.
    // Deliberately NOT done anywhere yet: applying what a hit means (damage, knockback,
    // i-frames, events). That is gameplay code this engine does not own yet - real, separate
    // follow-up, not silently faked.
    std::array<Hitbox3DStrikeUVE, kMaximumHitbox3DStrikesUVE> strikes{};
    std::uint8_t strikeCount = 0U;
    bool strikesTruncated = false;
};

[[nodiscard]] bool IsHitbox3DNodeComponentValidUVE(const Hitbox3DNodeComponentUVE& value) noexcept;

} // namespace UVE::Scene
