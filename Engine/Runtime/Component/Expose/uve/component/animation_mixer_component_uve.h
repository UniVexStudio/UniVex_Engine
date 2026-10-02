// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

/// Which clock advances an animation.
enum class AnimationProcessCallbackUVE : std::uint8_t {
    /// Every rendered frame: smoothest for anything the camera looks at.
    Frame = 0,
    /// Every physics step, in step with the physics bodies it moves.
    Physics,
};

/// What a skeletal clip's travel (its root bone moving across the ground) does.
enum class AnimationRootMotionModeUVE : std::uint8_t {
    /// The root bone moves as authored: a run cycle runs away from its node.
    Off = 0,
    /// The root bone's ground travel is taken out of the pose; nothing moves the node.
    InPlace,
    /// Taken out of the pose and added to the target instead, so the node goes where the feet go.
    /// A Character3D target gets it as velocity, so collisions still stop it.
    ApplyToTarget,
};

/// How a clip that starts takes over from the pose that was there (over the player's Blend In).
enum class AnimationTransitionModeUVE : std::uint8_t {
    /// The new clip plays at full weight at once; the difference from the old pose is carried
    /// along and fades out smoothly (zero speed at both ends). No cost of evaluating two clips,
    /// no sliding feet halfway through, and it keeps the new motion's timing from the first frame.
    Inertialize = 0,
    /// The old pose and the new clip are mixed, the mix moving to the new clip over the blend.
    Crossfade,
};

/// AnimationMixer: the abstract base AnimationSequencer and AnimationGraph share - what they move, which
/// channels, on which clock and how fast. Not a node of its own; the section both show between
/// their own and the Node section.
struct AnimationMixerComponentUVE final {
    /// Off, nothing is evaluated or written: the target is left alone.
    bool active = true;
    /// The node that is moved. Invalid means the mixer's parent.
    EntityUVE target = kInvalidEntityUVE;
    /// Multiplies every clock under this mixer: 0.5 is slow motion, 0 freezes.
    float speedScale = 1.0F;
    AnimationProcessCallbackUVE processCallback = AnimationProcessCallbackUVE::Frame;
    /// Per-channel masks: rotation alone can be animated while physics or a script owns position.
    bool animatePosition = true;
    bool animateRotation = true;
    bool animateScale = true;
    /// How a newly started clip takes over, over the player's Blend In.
    AnimationTransitionModeUVE transition = AnimationTransitionModeUVE::Inertialize;
    /// Root motion, for skeletal clips.
    AnimationRootMotionModeUVE rootMotion = AnimationRootMotionModeUVE::Off;
    /// The bone whose ground travel is root motion. Empty picks it: the first bone, parents first,
    /// whose track travels across the ground (usually the root or the hips).
    std::string rootMotionBone;

    [[nodiscard]] bool operator==(const AnimationMixerComponentUVE&) const = default;
};

/// A finite, non-negative speed scale and a known clock.
[[nodiscard]] bool IsAnimationMixerComponentValidUVE(const AnimationMixerComponentUVE& component) noexcept;

} // namespace UVE::Scene
