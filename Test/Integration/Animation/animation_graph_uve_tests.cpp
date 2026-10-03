// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/animation/animation_graph_uve.h"

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

AnimationGraphUVE MakeBlendTreeUVE() {
    AnimationGraphUVE tree;
    tree.clips = {MakeClipUVE("a", 0.0F, 10.0F), MakeClipUVE("b", 10.0F, 20.0F)};
    tree.objects = {
        AnimationGraphObjectUVE{1U, AnimationGraphObjectKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{2U, AnimationGraphObjectKindUVE::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{3U, AnimationGraphObjectKindUVE::Blend, "Blend", {}, {}, 1U, 2U, 0.25F, 1.0F, true},
        AnimationGraphObjectUVE{4U, AnimationGraphObjectKindUVE::OutputPose, "Output", {}, {}, 3U, 0U, 0.5F, 1.0F, true},
    };
    return tree;
}

} // namespace

TEST(AnimationGraphUVETest, ValidateAnimationGraphUVE_AcceptsClipBlendOutputGraph) {
    const AnimationGraphValidationResultUVE result = ValidateAnimationGraphUVE(MakeBlendTreeUVE());
    EXPECT_TRUE(result.IsValidUVE());
    EXPECT_EQ(result.code, AnimationGraphValidationCodeUVE::Valid);
}

TEST(AnimationGraphUVETest, EvaluateAnimationGraphUVE_BlendsValidatedClipPosesDeterministically) {
    const AnimationGraphEvaluationResultUVE result = EvaluateAnimationGraphUVE(MakeBlendTreeUVE(), 0.5);

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_EQ(result.pose.position, (Math::Vector3UVE{7.5F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedObjectCount, 4U);
}

TEST(AnimationGraphUVETest, EvaluateAnimationGraphUVE_SelectsTransitionAndPropagatesTimeScale) {
    AnimationGraphUVE tree = MakeBlendTreeUVE();
    tree.objects = {
        AnimationGraphObjectUVE{1U, AnimationGraphObjectKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{2U, AnimationGraphObjectKindUVE::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{3U, AnimationGraphObjectKindUVE::Transition, "Transition", {}, "useB", 1U, 2U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{4U, AnimationGraphObjectKindUVE::TimeScale, "Slow", {}, {}, 3U, 0U, 0.5F, 0.5F, true},
        AnimationGraphObjectUVE{5U, AnimationGraphObjectKindUVE::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const AnimationGraphEvaluationResultUVE result = EvaluateAnimationGraphUVE(
        tree, 0.8, {AnimationGraphParameterUVE{"useB", 1.0F}});

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_EQ(result.pose.position, (Math::Vector3UVE{14.0F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedObjectCount, 4U);
}

TEST(AnimationGraphUVETest, EvaluateAnimationGraphUVE_SharedObjectCacheIncludesLocalTime) {
    AnimationGraphUVE tree;
    tree.clips = {MakeClipUVE("shared", 0.0F, 10.0F)};
    tree.objects = {
        AnimationGraphObjectUVE{1U, AnimationGraphObjectKindUVE::ClipPlayer, "Shared", "shared", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{2U, AnimationGraphObjectKindUVE::TimeScale, "Slow", {}, {}, 1U, 0U, 0.5F, 0.5F, true},
        AnimationGraphObjectUVE{3U, AnimationGraphObjectKindUVE::TimeScale, "Full", {}, {}, 1U, 0U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{4U, AnimationGraphObjectKindUVE::Blend, "Blend", {}, {}, 2U, 3U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{5U, AnimationGraphObjectKindUVE::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const AnimationGraphEvaluationResultUVE result = EvaluateAnimationGraphUVE(tree, 0.8);

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_FLOAT_EQ(result.pose.position.x, 6.0F);
    // The shared clip is evaluated once for each distinct local time.
    EXPECT_EQ(result.evaluatedObjectCount, 6U);
}

TEST(AnimationGraphUVETest, EvaluateAnimationGraphUVE_RejectsNonFiniteScaledTimeBeforeRecursion) {
    AnimationGraphUVE tree;
    tree.clips = {MakeClipUVE("a", 0.0F, 1.0F)};
    tree.objects = {
        AnimationGraphObjectUVE{1U, AnimationGraphObjectKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        AnimationGraphObjectUVE{2U, AnimationGraphObjectKindUVE::TimeScale, "Scale", {}, {}, 1U, 0U, 0.5F,
                              std::numeric_limits<float>::max(), true},
        AnimationGraphObjectUVE{3U, AnimationGraphObjectKindUVE::OutputPose, "Output", {}, {}, 2U, 0U, 0.5F, 1.0F, true},
    };

    const AnimationGraphEvaluationResultUVE result =
        EvaluateAnimationGraphUVE(tree, std::numeric_limits<double>::max());
    EXPECT_FALSE(result.IsSuccessUVE());
    EXPECT_EQ(result.evaluatedObjectCount, 0U);
}

TEST(AnimationGraphUVETest, ValidateAnimationGraphUVE_RejectsUnknownClipAndCycle) {
    AnimationGraphUVE unknownClip = MakeBlendTreeUVE();
    unknownClip.objects[0].clipId = "missing";
    EXPECT_EQ(ValidateAnimationGraphUVE(unknownClip).code, AnimationGraphValidationCodeUVE::UnknownClip);

    AnimationGraphUVE cycle = MakeBlendTreeUVE();
    cycle.objects[2].inputA = 4U;
    EXPECT_EQ(ValidateAnimationGraphUVE(cycle).code, AnimationGraphValidationCodeUVE::CycleDetected);
}

} // namespace UVE::Core
