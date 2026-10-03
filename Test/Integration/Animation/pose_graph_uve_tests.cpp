// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/animation/pose_graph_uve.h"

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

PoseGraphUVE MakeBlendTreeUVE() {
    PoseGraphUVE tree;
    tree.clips = {MakeClipUVE("a", 0.0F, 10.0F), MakeClipUVE("b", 10.0F, 20.0F)};
    tree.nodes = {
        PoseGraphNodeUVE{1U, PoseGraphNodeKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{2U, PoseGraphNodeKindUVE::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{3U, PoseGraphNodeKindUVE::Blend, "Blend", {}, {}, 1U, 2U, 0.25F, 1.0F, true},
        PoseGraphNodeUVE{4U, PoseGraphNodeKindUVE::OutputPose, "Output", {}, {}, 3U, 0U, 0.5F, 1.0F, true},
    };
    return tree;
}

} // namespace

TEST(PoseGraphUVETest, ValidatePoseGraphUVE_AcceptsClipBlendOutputGraph) {
    const PoseGraphValidationResultUVE result = ValidatePoseGraphUVE(MakeBlendTreeUVE());
    EXPECT_TRUE(result.IsValidUVE());
    EXPECT_EQ(result.code, PoseGraphValidationCodeUVE::Valid);
}

TEST(PoseGraphUVETest, EvaluatePoseGraphUVE_BlendsValidatedClipPosesDeterministically) {
    const PoseGraphEvaluationResultUVE result = EvaluatePoseGraphUVE(MakeBlendTreeUVE(), 0.5);

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_EQ(result.pose.position, (Math::Vector3UVE{7.5F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedObjectCount, 4U);
}

TEST(PoseGraphUVETest, EvaluatePoseGraphUVE_SelectsTransitionAndPropagatesTimeScale) {
    PoseGraphUVE tree = MakeBlendTreeUVE();
    tree.nodes = {
        PoseGraphNodeUVE{1U, PoseGraphNodeKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{2U, PoseGraphNodeKindUVE::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{3U, PoseGraphNodeKindUVE::Transition, "Transition", {}, "useB", 1U, 2U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{4U, PoseGraphNodeKindUVE::TimeScale, "Slow", {}, {}, 3U, 0U, 0.5F, 0.5F, true},
        PoseGraphNodeUVE{5U, PoseGraphNodeKindUVE::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const PoseGraphEvaluationResultUVE result = EvaluatePoseGraphUVE(
        tree, 0.8, {PoseGraphParameterUVE{"useB", 1.0F}});

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_EQ(result.pose.position, (Math::Vector3UVE{14.0F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedObjectCount, 4U);
}

TEST(PoseGraphUVETest, EvaluatePoseGraphUVE_SharedObjectCacheIncludesLocalTime) {
    PoseGraphUVE tree;
    tree.clips = {MakeClipUVE("shared", 0.0F, 10.0F)};
    tree.nodes = {
        PoseGraphNodeUVE{1U, PoseGraphNodeKindUVE::ClipPlayer, "Shared", "shared", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{2U, PoseGraphNodeKindUVE::TimeScale, "Slow", {}, {}, 1U, 0U, 0.5F, 0.5F, true},
        PoseGraphNodeUVE{3U, PoseGraphNodeKindUVE::TimeScale, "Full", {}, {}, 1U, 0U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{4U, PoseGraphNodeKindUVE::Blend, "Blend", {}, {}, 2U, 3U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{5U, PoseGraphNodeKindUVE::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const PoseGraphEvaluationResultUVE result = EvaluatePoseGraphUVE(tree, 0.8);

    ASSERT_TRUE(result.IsSuccessUVE());
    EXPECT_FLOAT_EQ(result.pose.position.x, 6.0F);
    // The shared clip is evaluated once for each distinct local time.
    EXPECT_EQ(result.evaluatedObjectCount, 6U);
}

TEST(PoseGraphUVETest, EvaluatePoseGraphUVE_RejectsNonFiniteScaledTimeBeforeRecursion) {
    PoseGraphUVE tree;
    tree.clips = {MakeClipUVE("a", 0.0F, 1.0F)};
    tree.nodes = {
        PoseGraphNodeUVE{1U, PoseGraphNodeKindUVE::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNodeUVE{2U, PoseGraphNodeKindUVE::TimeScale, "Scale", {}, {}, 1U, 0U, 0.5F,
                              std::numeric_limits<float>::max(), true},
        PoseGraphNodeUVE{3U, PoseGraphNodeKindUVE::OutputPose, "Output", {}, {}, 2U, 0U, 0.5F, 1.0F, true},
    };

    const PoseGraphEvaluationResultUVE result =
        EvaluatePoseGraphUVE(tree, std::numeric_limits<double>::max());
    EXPECT_FALSE(result.IsSuccessUVE());
    EXPECT_EQ(result.evaluatedObjectCount, 0U);
}

TEST(PoseGraphUVETest, ValidatePoseGraphUVE_RejectsUnknownClipAndCycle) {
    PoseGraphUVE unknownClip = MakeBlendTreeUVE();
    unknownClip.nodes[0].clipId = "missing";
    EXPECT_EQ(ValidatePoseGraphUVE(unknownClip).code, PoseGraphValidationCodeUVE::UnknownClip);

    PoseGraphUVE cycle = MakeBlendTreeUVE();
    cycle.nodes[2].inputA = 4U;
    EXPECT_EQ(ValidatePoseGraphUVE(cycle).code, PoseGraphValidationCodeUVE::CycleDetected);
}

} // namespace UVE::Core
