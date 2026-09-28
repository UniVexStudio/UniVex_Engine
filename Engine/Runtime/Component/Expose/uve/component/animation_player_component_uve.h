// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/animation_mixer_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// What a clip does when it reaches its end.
enum class AnimationLoopModeUVE : std::uint8_t {
    /// Plays once, then stops.
    Once = 0,
    /// Starts again from the other end.
    Loop,
    /// Turns round and plays back the other way, forever.
    PingPong,
};

/// Where a finished one-shot clip leaves its target.
enum class AnimationFinishActionUVE : std::uint8_t {
    /// Keeps the clip's last pose.
    HoldLastPose = 0,
    /// Puts the target back where it was before the clip started.
    ReturnToStart,
};

/// AnimationPlayer's own state: plays a `.uvanim` clip on a target node's transform. What it moves,
/// which channels and on which clock live in its AnimationMixer base (AnimationMixerComponentUVE).
///
/// Authored settings first; the runtime state the player writes back each step comes last and is
/// shown in the Inspector only while playing, never saved.
struct AnimationPlayerComponentUVE final {
    /// The clip to play. Invalid means nothing to play.
    Asset::AssetGuidUVE clip{};
    /// Starts playing as soon as the scene runs.
    bool autoplay = true;
    /// Playback rate: 1 is normal, 2 twice as fast, negative plays backwards. 0 holds the pose.
    float speed = 1.0F;
    AnimationLoopModeUVE loopMode = AnimationLoopModeUVE::Loop;
    AnimationFinishActionUVE onFinish = AnimationFinishActionUVE::HoldLastPose;
    /// Where in the clip playback starts, in seconds.
    float startOffsetSeconds = 0.0F;
    /// Eases from wherever the target is into the clip over this many seconds, instead of snapping.
    float blendInSeconds = 0.0F;
    /// Plays the clip on top of where the target already is, so one clip (a bob, a sway, a door
    /// swing) works on any node wherever it was placed. Off, the clip's poses are absolute.
    bool relative = false;

    // ---- Runtime state, written by the player; never saved --------------------------------------
    bool isPlaying = false;
    /// Seconds into the clip.
    float currentTimeSeconds = 0.0F;
    /// +1 forwards, -1 on a ping-pong's way back.
    float direction = 1.0F;
    /// Seconds since playback started, for the blend-in.
    float blendElapsedSeconds = 0.0F;
    /// True once a Once clip has reached its end. Cleared by playing again.
    bool finished = false;
    /// True once `startPose` holds the target's pose from the moment playback started.
    bool hasStartPose = false;
    Math::Vector3UVE startPosition{};
    Math::QuaternionUVE startRotation{};
    Math::Vector3UVE startScale{1.0F, 1.0F, 1.0F};
    /// Root motion of the last step: the root bone's ground travel, in the skeleton's space.
    Math::Vector3UVE rootMotionDelta{};
    /// The clip's events the playhead passed in the last step, in the order it passed them.
    std::vector<std::string> firedEvents;

    /// Authored settings only: runtime state is ignored, so a playing player equals its saved self.
    [[nodiscard]] bool HasSameSettingsUVE(const AnimationPlayerComponentUVE& other) const noexcept {
        return clip == other.clip && autoplay == other.autoplay &&
               speed == other.speed && loopMode == other.loopMode && onFinish == other.onFinish &&
               startOffsetSeconds == other.startOffsetSeconds && blendInSeconds == other.blendInSeconds &&
               relative == other.relative;
    }

    [[nodiscard]] bool operator==(const AnimationPlayerComponentUVE&) const = default;
};

/// Every setting finite and in range, and every runtime value finite.
[[nodiscard]] bool IsAnimationPlayerComponentValidUVE(const AnimationPlayerComponentUVE& component) noexcept;

} // namespace UVE::Scene
