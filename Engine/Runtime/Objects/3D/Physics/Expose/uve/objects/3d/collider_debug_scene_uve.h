// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "uve/component/entity_uve.h"
#include "uve/physics/collider_debug_geometry_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Which colliders a debug view draws, and how finely.
///
/// Every field has a default that means "show everything", because the common case is an author
/// turning the view on to look at their level, not an engineer narrowing a bug report.
struct ColliderDebugSceneSettingsUVE final {
    /// Only colliders with at least one of these layer bits are drawn; `0xFFFFFFFF` means every
    /// layer. This is the same one-sided test the layer-mask field already uses, so "show me the
    /// trigger volumes" is a bit, not a special case.
    std::uint32_t layerMask = 0xFFFFFFFFU;
    /// Cost bound on how many colliders one call will walk before it stops. A level with ten
    /// thousand props should not turn a debug toggle into a frame-time cliff.
    std::size_t maximumColliders = 512U;
    /// Draw a collider the author has switched off. Off by default, because a disabled collider is
    /// not part of the simulation and drawing it would misreport what is being tested.
    bool includeDisabled = false;
    /// Leave this collider out of the bulk pass. A viewport that wants to draw one collider
    /// differently - the selected one, in its own colour and width - builds it on the side and
    /// names it here, so the shape is never drawn twice into the same pixels.
    EntityUVE excludedEntity = kInvalidEntityUVE;
    Physics::ColliderDebugStyleUVE style{};

    [[nodiscard]] bool operator==(const ColliderDebugSceneSettingsUVE&) const noexcept = default;
};

struct ColliderDebugSceneStatsUVE final {
    /// Colliders that passed every filter and contributed geometry.
    std::size_t colliderCount = 0U;
    /// Lines actually written.
    std::size_t lineCount = 0U;
    /// True when either bound stopped the walk: the entity cap was reached, or the buffer filled.
    /// A truncated buffer always holds whole colliders - a shape is written only when its entire
    /// tessellation fits - so a partial frame is a smaller picture, never a broken one.
    bool truncated = false;
    /// True when the collider cap was what stopped it, so a caller can tell "the level is bigger
    /// than the debug budget" from "the buffer I allocated is too small".
    bool reachedColliderLimit = false;
};

/// Collects the world-space wireframes of every collider the settings accept.
///
/// Answers the same participation question the collision system does, through the same helpers -
/// `Collider3DUVE::IsParticipatingUVE` for the collider's own enabled state and
/// `IsPhysicsObjectInWorldUVE` for whether its Process mode has taken it out of the world - so a
/// shape that is drawn is a shape that can actually be hit. A debug view that disagreed with the
/// simulation about which shapes exist would be worse than none.
///
/// Reads `WorldTransformComponentUVE` for the pose; a collider without one has no world position
/// and is skipped. Deterministic: one `ForEachUVE` pass in storage order, no unordered container
/// anywhere, and the same world always produces the same lines in the same order.
[[nodiscard]] ColliderDebugSceneStatsUVE BuildColliderDebugSceneUVE(
    IEntityManagerUVE& entityManager, const ColliderDebugSceneSettingsUVE& settings,
    std::span<Physics::ColliderDebugLineUVE> outLines);

} // namespace UVE::Scene
