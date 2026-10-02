// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/nodes/3d/node_3d_common_uve.h"

namespace UVE::Scene {

enum class Occluder3DNodeModeUVE : std::uint8_t {
    ConservativeBox = 0,
};

/// An Occluder3D: an authored axis-aligned box (halfExtents around the node's world position)
/// that hides whatever stands strictly BEHIND it from the current viewer - the classic
/// conservative occlusion cull, so a concrete wall stops the engine from drawing the courtyard
/// behind it. No Godot built-in answers this (Godot ships portal/room culling addons only), and
/// Unreal solves it offline; here the evaluation is per-frame exact for point-granularity
/// targets, with NO state kept between frames: the verdict is re-derived against the live camera
/// every visibility build, so a stale verdict that hides content after a camera teleport cannot
/// exist by construction.
///
/// Honest granularity: the occlusion question is answered for the MESH ORIGIN (center-point),
/// exactly the key the LodGroup3D, WorldPartition3D and VisibilityRegion3D gates already use -
/// one rule the whole candidate walk keeps. A huge mesh whose bounds peek past the wall belongs
/// in the future bounds-aware mode; ConservativeBox culls centers only and NEVER invents bounds.
/// `ConservativeBox` is the only accepted mode today (the sphere/bounds modes land with their own
/// resolvers, not by widening this one).
struct Occluder3DNodeComponentUVE final {
    Math::Vector3UVE halfExtents{2.0F, 2.0F, 2.0F};
    Occluder3DNodeModeUVE mode = Occluder3DNodeModeUVE::ConservativeBox;
    bool enabled = true;
};

[[nodiscard]] bool IsOccluder3DNodeComponentValidUVE(const Occluder3DNodeComponentUVE& value) noexcept;

// The whole semantics of one occluder against one candidate point, pure and frame-exact. Rules,
// each measured in the node tests:
//   * invalid config, a non-finite pose, or the viewer standing INSIDE the box -> NOT hidden
//     (you cannot be occluded by the cover you stand in; anything unusable fails open)
//   * a candidate strictly inside the box -> NOT hidden (a wall never hides what it contains)
//   * a candidate is hidden only when the open segment viewer->point strictly PASSES THROUGH
//     the box interior and finishes on the other side; a grazing touch of the surface, or the
//     box sitting exactly at the candidate, answers NOT hidden (the no-false-culls edge: at any
//     honest ambiguity, the mesh draws)
//   * world-axis-aligned box, the same rule WorldPartition3D and VisibilityRegion3D document -
//     a rotated occluder node does not rotate the cover.
//
// Composition (several occluders hiding in front of one mesh) is a plain OR and lives in the
// renderer's candidate walk, where the camera pose is sourced from the visibility set.
[[nodiscard]] bool ResolveOccluder3DFullyHiddenUVE(const Occluder3DNodeComponentUVE& config,
                                                   const Math::Vector3UVE& occluderWorldPosition,
                                                   const Math::Vector3UVE& viewerWorldPosition,
                                                   const Math::Vector3UVE& pointWorld) noexcept;

} // namespace UVE::Scene
