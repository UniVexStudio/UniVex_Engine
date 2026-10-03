// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/animation/animation_tree_uve.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Core {
namespace {

AnimationClipUVE MakeClipUVE(const std::string& id, const float start, const float end) {
    AnimationClipUVE clip;
    clip.clipId = id;
    clip.durationSeconds = 1.0;
    clip.samples = {
        PoseSampleUVE{0.0, TransformPoseUVE{{start, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
        PoseSampleUVE{1.0, TransformPoseUVE{{end, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    return clip;
}

AnimationTreeUVE MakeBlendTreeUVE() {
    AnimationTreeUVE tree;
    tree.clips = {MakeClipUVE("a", 0.0F, 10.0F), MakeClipUVE("b", 10.0F, 20.0F)};
    tree.objects = {
        AnimationTreeObjectUVE{1U, AnimationTreeObjectKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{2U, AnimationTreeObjectKindUVE::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{3U, AnimationTreeObjectKindUVE::Blend, "Blend", {}, {}, 1U, 2U, 0.25F, 1.0F, true},
        AnimationTreeObjectUVE{4U, AnimationTreeObjectKindUVE::OutputPose, "Output", {}, {}, 3U, 0U, 0.5F, 1.0F, true},
    };
    return tree;
}

} // namespace

TEST(AnimationTreeUVETest, ValidateAnimationTreeUVE_AcceptsClipBlendOutputGraph) {
    const AnimationTreeValidationResultUVE result = ValidateAnimationTreeUVE(MakeBlendTreeUVE());
    EXPECT_TRUE(result.IsValidUVE());
    EXPECT_EQ(result.code, AnimationTreeValidationCodeUVE::Valid);
}

TEST(AnimationTreeUVETest, EvaluateAnimationTreeUVE_BlendsValidatedClipPosesDeterministically) {
    const AnimationTreeEvaluationResultUVE result = EvaluateAnimationTreeUVE(MakeBlendTreeUVE(), 0.5);

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_EQ(result.pose.position, (Math::Vector3UVE{7.5F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedObjectCount, 4U);
}

TEST(AnimationTreeUVETest, EvaluateAnimationTreeUVE_SelectsTransitionAndPropagatesTimeScale) {
    AnimationTreeUVE tree = MakeBlendTreeUVE();
    tree.objects = {
        AnimationTreeObjectUVE{1U, AnimationTreeObjectKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{2U, AnimationTreeObjectKindUVE::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{3U, AnimationTreeObjectKindUVE::Transition, "Transition", {}, "useB", 1U, 2U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{4U, AnimationTreeObjectKindUVE::TimeScale, "Slow", {}, {}, 3U, 0U, 0.5F, 0.5F, true},
        AnimationTreeObjectUVE{5U, AnimationTreeObjectKindUVE::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const AnimationTreeEvaluationResultUVE result = EvaluateAnimationTreeUVE(
        tree, 0.8, {AnimationTreeParameterUVE{"useB", 1.0F}});

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_EQ(result.pose.position, (Math::Vector3UVE{14.0F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedObjectCount, 4U);
}

TEST(AnimationTreeUVETest, EvaluateAnimationTreeUVE_SharedObjectCacheIncludesLocalTime) {
    AnimationTreeUVE tree;
    tree.clips = {MakeClipUVE("shared", 0.0F, 10.0F)};
    tree.objects = {
        AnimationTreeObjectUVE{1U, AnimationTreeObjectKindUVE::ClipPlayer, "Shared", "shared", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{2U, AnimationTreeObjectKindUVE::TimeScale, "Slow", {}, {}, 1U, 0U, 0.5F, 0.5F, true},
        AnimationTreeObjectUVE{3U, AnimationTreeObjectKindUVE::TimeScale, "Full", {}, {}, 1U, 0U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{4U, AnimationTreeObjectKindUVE::Blend, "Blend", {}, {}, 2U, 3U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{5U, AnimationTreeObjectKindUVE::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const AnimationTreeEvaluationResultUVE result = EvaluateAnimationTreeUVE(tree, 0.8);

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_FLOAT_EQ(result.pose.position.x, 6.0F);
    // The shared clip is evaluated once for each distinct local time.
    EXPECT_EQ(result.evaluatedObjectCount, 6U);
}

TEST(AnimationTreeUVETest, EvaluateAnimationTreeUVE_RejectsNonFiniteScaledTimeBeforeRecursion) {
    AnimationTreeUVE tree;
    tree.clips = {MakeClipUVE("a", 0.0F, 1.0F)};
    tree.objects = {
        AnimationTreeObjectUVE{1U, AnimationTreeObjectKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationTreeObjectUVE{2U, AnimationTreeObjectKindUVE::TimeScale, "Scale", {}, {}, 1U, 0U, 0.5F,
                              std::numeric_limits<float>::max(), true},
        AnimationTreeObjectUVE{3U, AnimationTreeObjectKindUVE::OutputPose, "Output", {}, {}, 2U, 0U, 0.5F, 1.0F, true},
    };

    const AnimationTreeEvaluationResultUVE result =
        EvaluateAnimationTreeUVE(tree, std::numeric_limits<double>::max());
    EXPECT_FALSE(result.IsSuccessUVE());
    EXPECT_EQ(result.evaluatedObjectCount, 0U);
}

TEST(AnimationTreeUVETest, ValidateAnimationTreeUVE_RejectsUnknownClipAndCycle) {
    AnimationTreeUVE unknownClip = MakeBlendTreeUVE();
    unknownClip.objects[0].clipId = "missing";
    EXPECT_EQ(ValidateAnimationTreeUVE(unknownClip).code, AnimationTreeValidationCodeUVE::UnknownClip);

    AnimationTreeUVE cycle = MakeBlendTreeUVE();
    cycle.objects[2].inputA = 4U;
    EXPECT_EQ(ValidateAnimationTreeUVE(cycle).code, AnimationTreeValidationCodeUVE::CycleDetected);
}

} // namespace UVE::Core
