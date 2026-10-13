// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// A camera that positions itself from another entity every frame. The follow runs in the engine
/// update after every mover has stepped and before SceneGraphUVE::UpdateUVE() propagates, so the
/// camera's world transform is current for the same frame's render - but the target position it
/// reads is last frame's propagation (the graph propagates once, at the end), so a followed
/// target leads its camera by one frame. At 60 Hz that is 16 ms of lag: imperceptible for a
/// follow camera, and the same staleness the spring-arm raycasts already accept.
///
/// Only root cameras follow: a camera parented under a rig (a spring arm, a vehicle, a bone) is
/// driven by that rig instead, and the follow leaves it untouched. A camera whose target is
/// invalid, dead, or missing its world transform is likewise left untouched, holding its last
/// pose - the follow never clears a target on its own.
///
/// `followPossessedPawn` opts into the possession hooks: while set, every Player-kind possession
/// retargets this camera to the newly possessed pawn, and the matching unpossession parks it
/// (target back to invalid). AI-kind possession never retargets - an AI stealing a pawn must not
/// yank the player's camera - and last-possessed wins when several Player controllers possess in
/// one frame, so multi-controller scenes should target their cameras by hand.
///
/// Serialization resolves `target` through file-local ids: a target outside the saved set
/// loads back as invalid, parking the camera.
struct CameraFollowComponentUVE final {
    EntityUVE target = kInvalidEntityUVE;
    Math::Vector3UVE offset{};
    bool followPossessedPawn = false;

    [[nodiscard]] bool operator==(const CameraFollowComponentUVE&) const = default;
};

/// A finite offset. The target is dynamic, not structural: any entity id validates.
[[nodiscard]] bool IsCameraFollowComponentValidUVE(const CameraFollowComponentUVE& value) noexcept;

} // namespace UVE::Scene
