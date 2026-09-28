// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

/// Which clock advances an animation.
enum class AnimationProcessCallbackUVE : std::uint8_t {
    /// Every rendered frame: smoothest for anything the camera looks at.
    Frame = 0,
    /// Every physics step, in step with the physics bodies it moves.
    Physics,
};

/// AnimationMixer: the abstract base AnimationPlayer and AnimationTree share - what they move, which
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

    [[nodiscard]] bool operator==(const AnimationMixerComponentUVE&) const = default;
};

/// A finite, non-negative speed scale and a known clock.
[[nodiscard]] bool IsAnimationMixerComponentValidUVE(const AnimationMixerComponentUVE& component) noexcept;

} // namespace UVE::Scene
