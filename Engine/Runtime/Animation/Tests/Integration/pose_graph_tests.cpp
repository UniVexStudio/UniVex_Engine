// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "pose_graph.h"

#include <limits>

#include <gtest/gtest.h>

namespace UVE::Core {
namespace {

AnimationClip MakeClip(const std::string& id, const float start, const float end) {
    AnimationClip clip;
    clip.clipId = id;
    clip.durationSeconds = 1.0;
    clip.samples = {
        PoseSample{0.0, TransformPose{{start, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
        PoseSample{1.0, TransformPose{{end, 0.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}},
    };
    return clip;
}

PoseGraph MakeBlendTree() {
    PoseGraph tree;
    tree.clips = {MakeClip("a", 0.0F, 10.0F), MakeClip("b", 10.0F, 20.0F)};
    tree.nodes = {
        PoseGraphNode{1U, PoseGraphNodeKind::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNode{2U, PoseGraphNodeKind::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNode{3U, PoseGraphNodeKind::Blend, "Blend", {}, {}, 1U, 2U, 0.25F, 1.0F, true},
        PoseGraphNode{4U, PoseGraphNodeKind::OutputPose, "Output", {}, {}, 3U, 0U, 0.5F, 1.0F, true},
    };
    return tree;
}

} // namespace

TEST(PoseGraphTest, ValidatePoseGraph_AcceptsClipBlendOutputGraph) {
    const PoseGraphValidationResult result = ValidatePoseGraph(MakeBlendTree());
    EXPECT_TRUE(result.IsValid());
    EXPECT_EQ(result.code, PoseGraphValidationCode::Valid);
}

TEST(PoseGraphTest, EvaluatePoseGraph_BlendsValidatedClipPosesDeterministically) {
    const PoseGraphEvaluationResult result = EvaluatePoseGraph(MakeBlendTree(), 0.5);

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.pose.position, (Math::Vector3{7.5F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedNodeCount, 4U);
}

TEST(PoseGraphTest, EvaluatePoseGraph_SelectsTransitionAndPropagatesTimeScale) {
    PoseGraph tree = MakeBlendTree();
    tree.nodes = {
        PoseGraphNode{1U, PoseGraphNodeKind::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNode{2U, PoseGraphNodeKind::ClipPlayer, "B", "b", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNode{3U, PoseGraphNodeKind::Transition, "Transition", {}, "useB", 1U, 2U, 0.5F, 1.0F, true},
        PoseGraphNode{4U, PoseGraphNodeKind::TimeScale, "Slow", {}, {}, 3U, 0U, 0.5F, 0.5F, true},
        PoseGraphNode{5U, PoseGraphNodeKind::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const PoseGraphEvaluationResult result = EvaluatePoseGraph(
        tree, 0.8, {PoseGraphParameter{"useB", 1.0F}});

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_EQ(result.pose.position, (Math::Vector3{14.0F, 0.0F, 0.0F}));
    EXPECT_EQ(result.evaluatedNodeCount, 4U);
}

TEST(PoseGraphTest, EvaluatePoseGraph_SharedNodeCacheIncludesLocalTime) {
    PoseGraph tree;
    tree.clips = {MakeClip("shared", 0.0F, 10.0F)};
    tree.nodes = {
        PoseGraphNode{1U, PoseGraphNodeKind::ClipPlayer, "Shared", "shared", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNode{2U, PoseGraphNodeKind::TimeScale, "Slow", {}, {}, 1U, 0U, 0.5F, 0.5F, true},
        PoseGraphNode{3U, PoseGraphNodeKind::TimeScale, "Full", {}, {}, 1U, 0U, 0.5F, 1.0F, true},
        PoseGraphNode{4U, PoseGraphNodeKind::Blend, "Blend", {}, {}, 2U, 3U, 0.5F, 1.0F, true},
        PoseGraphNode{5U, PoseGraphNodeKind::OutputPose, "Output", {}, {}, 4U, 0U, 0.5F, 1.0F, true},
    };

    const PoseGraphEvaluationResult result = EvaluatePoseGraph(tree, 0.8);

    ASSERT_TRUE(result.IsSuccess());
    EXPECT_FLOAT_EQ(result.pose.position.x, 6.0F);
    // The shared clip is evaluated once for each distinct local time.
    EXPECT_EQ(result.evaluatedNodeCount, 6U);
}

TEST(PoseGraphTest, EvaluatePoseGraph_RejectsNonFiniteScaledTimeBeforeRecursion) {
    PoseGraph tree;
    tree.clips = {MakeClip("a", 0.0F, 1.0F)};
    tree.nodes = {
        PoseGraphNode{1U, PoseGraphNodeKind::ClipPlayer, "A", "a", {}, 0U, 0U, 0.5F, 1.0F, true},
        PoseGraphNode{2U, PoseGraphNodeKind::TimeScale, "Scale", {}, {}, 1U, 0U, 0.5F,
                              std::numeric_limits<float>::max(), true},
        PoseGraphNode{3U, PoseGraphNodeKind::OutputPose, "Output", {}, {}, 2U, 0U, 0.5F, 1.0F, true},
    };

    const PoseGraphEvaluationResult result =
        EvaluatePoseGraph(tree, std::numeric_limits<double>::max());
    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.evaluatedNodeCount, 0U);
}

TEST(PoseGraphTest, ValidatePoseGraph_RejectsUnknownClipAndCycle) {
    PoseGraph unknownClip = MakeBlendTree();
    unknownClip.nodes[0].clipId = "missing";
    EXPECT_EQ(ValidatePoseGraph(unknownClip).code, PoseGraphValidationCode::UnknownClip);

    PoseGraph cycle = MakeBlendTree();
    cycle.nodes[2].inputA = 4U;
    EXPECT_EQ(ValidatePoseGraph(cycle).code, PoseGraphValidationCode::CycleDetected);
}

} // namespace UVE::Core
