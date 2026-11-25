// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <cmath>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "animation_graph.h"
#include "animation_graph_component.h"
#include "skeleton_3d.h"

namespace UVE::Scene {
namespace {

using Kind = AnimationGraphNodeKind;

/// A straight three-bone arm up the Y axis: Shoulder at the origin, Elbow 1 above, Hand 1 above that.
[[nodiscard]] Skeleton3DComponent Arm() {
    Skeleton3DComponent skeleton;
    SkeletonBone shoulder;
    shoulder.name = "Shoulder";
    SkeletonBone elbow;
    elbow.name = "Elbow";
    elbow.parentIndex = 0;
    elbow.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    SkeletonBone hand;
    hand.name = "Hand";
    hand.parentIndex = 1;
    hand.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    skeleton.bones = {shoulder, elbow, hand};
    return skeleton;
}

/// A one-second clip holding the arm's rest pose.
[[nodiscard]] Asset::AnimationClipAsset RestClip(const Skeleton3DComponent& skeleton) {
    Asset::AnimationClipAsset clip;
    clip.durationSeconds = 1.0;
    for (const SkeletonBone& bone : skeleton.bones) {
        Asset::AnimationAssetSample a;
        a.pose.position = bone.localPosition;
        Asset::AnimationAssetSample b = a;
        b.timeSeconds = 1.0;
        clip.bones.push_back(Asset::AnimationAssetBoneTrack{bone.name, {a, b}});
    }
    return clip;
}

[[nodiscard]] AnimationGraphNode Node(const std::uint32_t id, const Kind kind, std::vector<std::uint32_t> inputs) {
    AnimationGraphNode node;
    node.id = id;
    node.kind = kind;
    node.name = "Node" + std::to_string(id);
    node.inputs = std::move(inputs);
    return node;
}

/// Output <- modifier (id 2) <- Clip (id 3).
[[nodiscard]] AnimationGraphComponent Chain(const AnimationGraphNode& modifier) {
    AnimationGraphComponent graph;
    AnimationGraphNode clip = Node(3U, Kind::Clip, {});
    clip.clip = Asset::AssetGuid{7U};
    graph.nodes = {Node(1U, Kind::Output, {2U}), modifier, clip};
    return graph;
}

/// Where each bone is in the skeleton's space, from the pose the graph wrote.
[[nodiscard]] std::vector<Math::Vector3> GlobalPositions(const Skeleton3DComponent& skeleton) {
    std::vector<Math::Vector3> positions(skeleton.pose.size());
    std::vector<Math::Quaternion> rotations(skeleton.pose.size());
    for (std::size_t i = 0U; i < skeleton.pose.size(); ++i) {
        const std::int32_t parent = skeleton.bones[i].parentIndex;
        if (parent >= 0) {
            const auto p = static_cast<std::size_t>(parent);
            positions[i] = positions[p] + Math::RotateVector(rotations[p], skeleton.pose[i].position);
            rotations[i] = Math::Multiply(rotations[p], skeleton.pose[i].rotation);
        } else {
            positions[i] = skeleton.pose[i].position;
            rotations[i] = skeleton.pose[i].rotation;
        }
    }
    return positions;
}

class AnimationGraphBoneNodesTest : public ::testing::Test {
protected:
    Skeleton3DComponent skeleton = Arm();
    Asset::AnimationClipAsset clip = RestClip(skeleton);
    AnimationClipResolver clipFor = [this](const Asset::AssetGuid guid) -> const Asset::AnimationClipAsset* {
        return guid.value == 7U ? &clip : nullptr;
    };
    bool Step(AnimationGraphComponent& graph) {
        return StepSkeletalAnimationGraph(graph, clipFor, 0.1F, skeleton, AnimationDriverComponent{});
    }
};

} // namespace

TEST_F(AnimationGraphBoneNodesTest, TwoBoneIKPutsTheHandOnAReachableTarget) {
    AnimationGraphNode ik = Node(2U, Kind::TwoBoneIK, {3U});
    ik.bones = {"Shoulder", "Elbow", "Hand"};
    ik.target = Math::Vector3{1.0F, 1.0F, 0.0F};
    ik.pole = Math::Vector3{0.0F, 0.0F, 1.0F};
    ik.value = 1.0F;
    AnimationGraphComponent graph = Chain(ik);
    ASSERT_EQ(DescribeAnimationGraphProblem(graph), "");
    ASSERT_TRUE(Step(graph));
    const std::vector<Math::Vector3> at = GlobalPositions(skeleton);
    EXPECT_NEAR(at[2].x, 1.0F, 1e-3F);
    EXPECT_NEAR(at[2].y, 1.0F, 1e-3F);
    EXPECT_NEAR(at[2].z, 0.0F, 1e-3F);
    EXPECT_NEAR(Math::Length(at[1] - at[0]), 1.0F, 1e-3F) << "the bones keep their lengths";

    // From a Vector3 parameter, at half alpha: the hand only goes part of the way.
    graph.nodes[1].targetSource = AnimationGraphTargetSource::Parameter;
    graph.nodes[1].parameter = "Reach";
    AnimationParameter reach;
    reach.name = "Reach";
    reach.type = AnimationParameterType::Vector3;
    graph.parameters = {reach};
    graph.nodeStates.clear();
    ASSERT_TRUE(SetAnimationGraphVectorParameter(graph, "Reach", Math::Vector3{-1.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(SetAnimationGraphVectorParameter(graph, "Missing", Math::Vector3{}));
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(GlobalPositions(skeleton)[2].x, -1.0F, 1e-3F);
    graph.nodes[1].value = 0.0F;
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(GlobalPositions(skeleton)[2].y, 2.0F, 1e-3F) << "no alpha: the pose as it was";
}

TEST_F(AnimationGraphBoneNodesTest, ModifyBoneAddsOrReplaces) {
    AnimationGraphNode modify = Node(2U, Kind::ModifyBone, {3U});
    modify.bones = {"Hand"};
    modify.target = Math::Vector3{0.5F, 0.0F, 0.0F};
    modify.value = 1.0F;
    AnimationGraphComponent graph = Chain(modify);
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(skeleton.pose[2].position.x, 0.5F, 1e-5F);
    EXPECT_NEAR(skeleton.pose[2].position.y, 1.0F, 1e-5F) << "added to the pose";
    graph.nodes[1].modifyMode = AnimationBoneModifyMode::Replace;
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(skeleton.pose[2].position.y, 0.0F, 1e-5F) << "instead of the pose";
    graph.nodes[1].value = 0.5F;
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(skeleton.pose[2].position.y, 0.5F, 1e-5F) << "half way";
}

TEST_F(AnimationGraphBoneNodesTest, LookAtTurnsTheBoneToTheTargetWithinItsClamp) {
    AnimationGraphNode look = Node(2U, Kind::LookAt, {3U});
    look.bones = {"Shoulder"};
    look.pole = Math::Vector3{0.0F, 1.0F, 0.0F};   // the arm points along its Y
    look.target = Math::Vector3{5.0F, 0.0F, 0.0F};
    look.value = 1.0F;
    AnimationGraphComponent graph = Chain(look);
    ASSERT_TRUE(Step(graph));
    std::vector<Math::Vector3> at = GlobalPositions(skeleton);
    EXPECT_NEAR(at[2].x, 2.0F, 1e-3F) << "the whole arm now points at +X";
    EXPECT_NEAR(at[2].y, 0.0F, 1e-3F);
    graph.nodes[1].clampDegrees = 45.0F;
    ASSERT_TRUE(Step(graph));
    at = GlobalPositions(skeleton);
    EXPECT_NEAR(at[2].x, at[2].y, 1e-3F) << "stopped at 45 degrees";
}

TEST_F(AnimationGraphBoneNodesTest, BlendByBoolSwitchesWithItsFade) {
    Asset::AnimationClipAsset raised = clip;
    for (auto& track : raised.bones) {
        for (auto& sample : track.samples) {
            sample.pose.position.z = 3.0F;
        }
    }
    clipFor = [this, &raised](const Asset::AssetGuid guid) -> const Asset::AnimationClipAsset* {
        return guid.value == 7U ? &clip : guid.value == 8U ? &raised : nullptr;
    };
    AnimationGraphComponent graph;
    AnimationGraphNode byBool = Node(2U, Kind::BlendByBool, {3U, 4U});
    byBool.parameter = "Armed";
    byBool.fadeSeconds = 0.2F;
    byBool.fadeBackSeconds = 0.0F;
    AnimationGraphNode whenTrue = Node(3U, Kind::Clip, {});
    whenTrue.clip = Asset::AssetGuid{8U};
    AnimationGraphNode whenFalse = Node(4U, Kind::Clip, {});
    whenFalse.clip = Asset::AssetGuid{7U};
    graph.nodes = {Node(1U, Kind::Output, {2U}), byBool, whenTrue, whenFalse};
    AnimationParameter armed;
    armed.name = "Armed";
    armed.type = AnimationParameterType::Bool;
    graph.parameters = {armed};
    ASSERT_EQ(DescribeAnimationGraphProblem(graph), "");
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-5F) << "false: the second input";
    ASSERT_TRUE(SetAnimationGraphParameter(graph, "Armed", 1.0F));
    ASSERT_TRUE(Step(graph));   // 0.1 s of a 0.2 s fade
    EXPECT_NEAR(skeleton.pose[0].position.z, 1.5F, 1e-4F) << "half way toward true";
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(skeleton.pose[0].position.z, 3.0F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameter(graph, "Armed", 0.0F));
    ASSERT_TRUE(Step(graph));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "no fade back: at once";
}

TEST(AnimationGraphCharacterParametersTest, TheCharacterFeedsSpeedDirectionAndAir) {
    AnimationGraphComponent graph;
    const auto parameter = [](const char* name, const AnimationParameterSource source) {
        AnimationParameter p;
        p.name = name;
        p.source = source;
        return p;
    };
    graph.parameters = {parameter("Speed", AnimationParameterSource::CharacterSpeed),
                        parameter("Direction", AnimationParameterSource::CharacterDirection),
                        parameter("InAir", AnimationParameterSource::CharacterInAir),
                        parameter("Grounded", AnimationParameterSource::CharacterGrounded),
                        parameter("Mine", AnimationParameterSource::Script)};
    graph.parameters[4].value = 7.0F;
    const Math::Vector3 up{0.0F, 1.0F, 0.0F};
    EXPECT_EQ(WriteCharacterAnimationParameters(graph, Math::Vector3{0.0F, -9.0F, -3.0F}, up, Math::Quaternion{}, true), 4U);
    EXPECT_NEAR(graph.parameters[0].value, 3.0F, 1e-5F) << "ground speed: the fall does not count";
    EXPECT_NEAR(graph.parameters[1].value, 0.0F, 1e-4F) << "straight ahead (-Z)";
    EXPECT_EQ(graph.parameters[2].value, 0.0F);
    EXPECT_EQ(graph.parameters[3].value, 1.0F);
    EXPECT_EQ(graph.parameters[4].value, 7.0F) << "a script's parameter is left alone";
    static_cast<void>(WriteCharacterAnimationParameters(graph, Math::Vector3{2.0F, 0.0F, 0.0F}, up, Math::Quaternion{}, false));
    EXPECT_NEAR(graph.parameters[1].value, 90.0F, 1e-3F) << "to the right";
    EXPECT_EQ(graph.parameters[2].value, 1.0F);
    EXPECT_EQ(graph.parameters[3].value, 0.0F);
}

} // namespace UVE::Scene
