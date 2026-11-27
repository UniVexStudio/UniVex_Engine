// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_clip.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Core {
namespace {

AnimationClip MakeClip() {
    AnimationClip clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {
        PoseSample{0.0, TransformPose{{0.0F, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
        PoseSample{1.0, TransformPose{{10.0F, 2.0F, 0.0F}, {}, {2.0F, 2.0F, 2.0F}}},
    };
    clip.events = {AnimationEvent{0.2, "step_left"}, AnimationEvent{0.8, "step_right"}};
    return clip;
}

} // namespace

TEST(AnimationClipTest, ValidateAnimationClip_AcceptsBoundedSortedClip) {
    const AnimationClip clip = MakeClip();
    const AnimationClipValidationResult result = ValidateAnimationClip(clip);

    EXPECT_TRUE(result.IsValid());
    EXPECT_EQ(result.code, AnimationClipValidationCode::Valid);
}

TEST(AnimationClipTest, TrySampleAnimationClip_InterpolatesAndLoopsDeterministically) {
    const AnimationClip clip = MakeClip();
    TransformPose midpoint;
    TransformPose looped;

    ASSERT_TRUE(TrySampleAnimationClip(clip, 0.5, false, midpoint));
    EXPECT_EQ(midpoint.position, (Math::Vector3{5.0F, 1.0F, 0.0F}));
    EXPECT_EQ(midpoint.scale, (Math::Vector3{1.5F, 1.5F, 1.5F}));
    ASSERT_TRUE(TrySampleAnimationClip(clip, 1.25, true, looped));
    EXPECT_EQ(looped.position, (Math::Vector3{2.5F, 0.5F, 0.0F}));
}

TEST(AnimationClipTest, TrySampleAnimationClip_OppositeHemisphereQuaternionsTakeShortestPath) {
    AnimationClip clip;
    clip.clipId = "quat_shortest";
    clip.durationSeconds = 1.0;
    const Math::Quaternion q0{0.0F, 0.0F, 0.0F, 1.0F};
    // -q0 represents the exact same orientation in the opposite quaternion hemisphere.
    const Math::Quaternion q1{0.0F, 0.0F, 0.0F, -1.0F};
    clip.samples = {
        PoseSample{0.0, TransformPose{{0.0F, 0.0F, 0.0F}, q0, {1.0F, 1.0F, 1.0F}}},
        PoseSample{1.0, TransformPose{{0.0F, 0.0F, 0.0F}, q1, {1.0F, 1.0F, 1.0F}}},
    };
    TransformPose midpoint;
    ASSERT_TRUE(TrySampleAnimationClip(clip, 0.5, false, midpoint));
    // Midpoint must remain the identity orientation (|w| == 1), never degenerate zero or a 180-deg flip.
    EXPECT_NEAR(std::abs(midpoint.rotation.w), 1.0F, 1e-5F);
    EXPECT_NEAR(midpoint.rotation.x, 0.0F, 1e-5F);
    EXPECT_NEAR(midpoint.rotation.y, 0.0F, 1e-5F);
    EXPECT_NEAR(midpoint.rotation.z, 0.0F, 1e-5F);
}

TEST(AnimationClipTest, CollectAnimationEvents_ReturnsEventsAcrossLoopBoundaryInOrder) {
    const AnimationClip clip = MakeClip();
    const std::vector<AnimationEvent> events = CollectAnimationEvents(clip, 0.7, 1.2, true);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].eventId, "step_right");
    EXPECT_EQ(events[1].eventId, "step_left");
}

TEST(AnimationClipTest, CollectAnimationEvents_RejectsUnboundedFiniteLoopRange) {
    const AnimationClip clip = MakeClip();
    const std::vector<AnimationEvent> events =
        CollectAnimationEvents(clip, 0.0, std::numeric_limits<double>::max(), true);
    EXPECT_TRUE(events.empty());
}

TEST(AnimationClipTest, ValidateAnimationClip_RejectsUnsortedOrInvalidData) {
    AnimationClip clip = MakeClip();
    clip.samples[1].timeSeconds = -0.1;
    EXPECT_EQ(ValidateAnimationClip(clip).code, AnimationClipValidationCode::InvalidSampleTime);

    clip = MakeClip();
    clip.events[0].eventId.clear();
    EXPECT_EQ(ValidateAnimationClip(clip).code, AnimationClipValidationCode::InvalidEventIdentifier);

    clip = MakeClip();
    clip.samples[1].pose.rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_EQ(ValidateAnimationClip(clip).code, AnimationClipValidationCode::InvalidPose);
}

} // namespace UVE::Core
