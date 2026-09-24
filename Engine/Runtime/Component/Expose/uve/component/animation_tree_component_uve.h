// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

/// AnimationTree's own state: blends two clips on a target node by one number, so a single slider
/// takes a character from walk to run, or a door from closed to open, with no script. Like
/// AnimationPlayer it is a pure Node, and it moves `target`, or its parent when no target is set.
///
/// Authored settings first; the runtime state written each step comes last and is never saved.
struct AnimationTreeComponentUVE final {
    /// Evaluates the tree every frame while the scene runs.
    bool active = true;
    /// The node the blend moves. Invalid means the tree's parent.
    EntityUVE target = kInvalidEntityUVE;
    /// The clip at Blend 0.
    Asset::AssetGuidUVE clipA{};
    /// The clip at Blend 1.
    Asset::AssetGuidUVE clipB{};
    /// 0 plays only Clip A, 1 only Clip B, anything between mixes them.
    float blend = 0.0F;
    /// How fast Blend follows a new value, in units per second. 0 jumps at once.
    float blendSmoothing = 0.0F;
    /// Plays both clips at the same phase, stretching the shorter one, so feet stay in step when
    /// blending walk into run. Off, each clip runs on its own clock.
    bool syncPhase = true;
    /// Playback rate of both clips.
    float speed = 1.0F;
    bool animatePosition = true;
    bool animateRotation = true;
    bool animateScale = true;

    // ---- Runtime state, written by the tree; never saved ----------------------------------------
    /// The blend actually applied, chasing `blend` at `blendSmoothing`.
    float currentBlend = 0.0F;
    /// Phase 0..1 of the shared clock when syncing; otherwise Clip A's seconds.
    float timeA = 0.0F;
    /// Clip B's seconds when not syncing.
    float timeB = 0.0F;
    /// False until the first evaluation, so `currentBlend` starts at `blend` instead of easing in.
    bool started = false;

    [[nodiscard]] bool operator==(const AnimationTreeComponentUVE&) const = default;
};

/// Blend in 0..1, smoothing and speed finite (smoothing non-negative), runtime values finite.
[[nodiscard]] bool IsAnimationTreeComponentValidUVE(const AnimationTreeComponentUVE& component) noexcept;

} // namespace UVE::Scene
