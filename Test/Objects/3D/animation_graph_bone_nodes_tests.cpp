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

using Kind = AnimationGraphNodeKindUVE;

/// A straight three-bone arm up the Y axis: Shoulder at the origin, Elbow 1 above, Hand 1 above that.
[[nodiscard]] Skeleton3DComponentUVE ArmUVE() {
    Skeleton3DComponentUVE skeleton;
    SkeletonBoneUVE shoulder;
    shoulder.name = "Shoulder";
    SkeletonBoneUVE elbow;
    elbow.name = "Elbow";
    elbow.parentIndex = 0;
    elbow.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    SkeletonBoneUVE hand;
    hand.name = "Hand";
    hand.parentIndex = 1;
    hand.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    skeleton.bones = {shoulder, elbow, hand};
    return skeleton;
}

/// A one-second clip holding the arm's rest pose.
[[nodiscard]] Asset::AnimationClipAssetUVE RestClipUVE(const Skeleton3DComponentUVE& skeleton) {
    Asset::AnimationClipAssetUVE clip;
    clip.durationSeconds = 1.0;
    for (const SkeletonBoneUVE& bone : skeleton.bones) {
        Asset::AnimationAssetSampleUVE a;
        a.pose.position = bone.localPosition;
        Asset::AnimationAssetSampleUVE b = a;
        b.timeSeconds = 1.0;
        clip.bones.push_back(Asset::AnimationAssetBoneTrackUVE{bone.name, {a, b}});
    }
    return clip;
}

[[nodiscard]] AnimationGraphNodeUVE NodeUVE(const std::uint32_t id, const Kind kind, std::vector<std::uint32_t> inputs) {
    AnimationGraphNodeUVE node;
    node.id = id;
    node.kind = kind;
    node.name = "Node" + std::to_string(id);
    node.inputs = std::move(inputs);
    return node;
}

/// Output <- modifier (id 2) <- Clip (id 3).
[[nodiscard]] AnimationGraphComponentUVE ChainUVE(const AnimationGraphNodeUVE& modifier) {
    AnimationGraphComponentUVE graph;
    AnimationGraphNodeUVE clip = NodeUVE(3U, Kind::Clip, {});
    clip.clip = Asset::AssetGuidUVE{7U};
    graph.nodes = {NodeUVE(1U, Kind::Output, {2U}), modifier, clip};
    return graph;
}

/// Where each bone is in the skeleton's space, from the pose the graph wrote.
[[nodiscard]] std::vector<Math::Vector3UVE> GlobalPositionsUVE(const Skeleton3DComponentUVE& skeleton) {
    std::vector<Math::Vector3UVE> positions(skeleton.pose.size());
    std::vector<Math::QuaternionUVE> rotations(skeleton.pose.size());
    for (std::size_t i = 0U; i < skeleton.pose.size(); ++i) {
        const std::int32_t parent = skeleton.bones[i].parentIndex;
        if (parent >= 0) {
            const auto p = static_cast<std::size_t>(parent);
            positions[i] = positions[p] + Math::RotateVectorUVE(rotations[p], skeleton.pose[i].position);
            rotations[i] = Math::MultiplyUVE(rotations[p], skeleton.pose[i].rotation);
        } else {
            positions[i] = skeleton.pose[i].position;
            rotations[i] = skeleton.pose[i].rotation;
        }
    }
    return positions;
}

class AnimationGraphBoneNodesUVETest : public ::testing::Test {
protected:
    Skeleton3DComponentUVE skeleton = ArmUVE();
    Asset::AnimationClipAssetUVE clip = RestClipUVE(skeleton);
    AnimationClipResolverUVE clipFor = [this](const Asset::AssetGuidUVE guid) -> const Asset::AnimationClipAssetUVE* {
        return guid.value == 7U ? &clip : nullptr;
    };
    bool StepUVE(AnimationGraphComponentUVE& graph) {
        return StepSkeletalAnimationGraphUVE(graph, clipFor, 0.1F, skeleton, AnimationDriverComponentUVE{});
    }
};

} // namespace

TEST_F(AnimationGraphBoneNodesUVETest, TwoBoneIKPutsTheHandOnAReachableTarget) {
    AnimationGraphNodeUVE ik = NodeUVE(2U, Kind::TwoBoneIK, {3U});
    ik.bones = {"Shoulder", "Elbow", "Hand"};
    ik.target = Math::Vector3UVE{1.0F, 1.0F, 0.0F};
    ik.pole = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    ik.value = 1.0F;
    AnimationGraphComponentUVE graph = ChainUVE(ik);
    ASSERT_EQ(DescribeAnimationGraphProblemUVE(graph), "");
    ASSERT_TRUE(StepUVE(graph));
    const std::vector<Math::Vector3UVE> at = GlobalPositionsUVE(skeleton);
    EXPECT_NEAR(at[2].x, 1.0F, 1e-3F);
    EXPECT_NEAR(at[2].y, 1.0F, 1e-3F);
    EXPECT_NEAR(at[2].z, 0.0F, 1e-3F);
    EXPECT_NEAR(Math::LengthUVE(at[1] - at[0]), 1.0F, 1e-3F) << "the bones keep their lengths";

    // From a Vector3 parameter, at half alpha: the hand only goes part of the way.
    graph.nodes[1].targetSource = AnimationGraphTargetSourceUVE::Parameter;
    graph.nodes[1].parameter = "Reach";
    AnimationParameterUVE reach;
    reach.name = "Reach";
    reach.type = AnimationParameterTypeUVE::Vector3;
    graph.parameters = {reach};
    graph.nodeStates.clear();
    ASSERT_TRUE(SetAnimationGraphVectorParameterUVE(graph, "Reach", Math::Vector3UVE{-1.0F, 1.0F, 0.0F}));
    EXPECT_FALSE(SetAnimationGraphVectorParameterUVE(graph, "Missing", Math::Vector3UVE{}));
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(GlobalPositionsUVE(skeleton)[2].x, -1.0F, 1e-3F);
    graph.nodes[1].value = 0.0F;
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(GlobalPositionsUVE(skeleton)[2].y, 2.0F, 1e-3F) << "no alpha: the pose as it was";
}

TEST_F(AnimationGraphBoneNodesUVETest, ModifyBoneAddsOrReplaces) {
    AnimationGraphNodeUVE modify = NodeUVE(2U, Kind::ModifyBone, {3U});
    modify.bones = {"Hand"};
    modify.target = Math::Vector3UVE{0.5F, 0.0F, 0.0F};
    modify.value = 1.0F;
    AnimationGraphComponentUVE graph = ChainUVE(modify);
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(skeleton.pose[2].position.x, 0.5F, 1e-5F);
    EXPECT_NEAR(skeleton.pose[2].position.y, 1.0F, 1e-5F) << "added to the pose";
    graph.nodes[1].modifyMode = AnimationBoneModifyModeUVE::Replace;
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(skeleton.pose[2].position.y, 0.0F, 1e-5F) << "instead of the pose";
    graph.nodes[1].value = 0.5F;
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(skeleton.pose[2].position.y, 0.5F, 1e-5F) << "half way";
}

TEST_F(AnimationGraphBoneNodesUVETest, LookAtTurnsTheBoneToTheTargetWithinItsClamp) {
    AnimationGraphNodeUVE look = NodeUVE(2U, Kind::LookAt, {3U});
    look.bones = {"Shoulder"};
    look.pole = Math::Vector3UVE{0.0F, 1.0F, 0.0F};   // the arm points along its Y
    look.target = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    look.value = 1.0F;
    AnimationGraphComponentUVE graph = ChainUVE(look);
    ASSERT_TRUE(StepUVE(graph));
    std::vector<Math::Vector3UVE> at = GlobalPositionsUVE(skeleton);
    EXPECT_NEAR(at[2].x, 2.0F, 1e-3F) << "the whole arm now points at +X";
    EXPECT_NEAR(at[2].y, 0.0F, 1e-3F);
    graph.nodes[1].clampDegrees = 45.0F;
    ASSERT_TRUE(StepUVE(graph));
    at = GlobalPositionsUVE(skeleton);
    EXPECT_NEAR(at[2].x, at[2].y, 1e-3F) << "stopped at 45 degrees";
}

TEST_F(AnimationGraphBoneNodesUVETest, BlendByBoolSwitchesWithItsFade) {
    Asset::AnimationClipAssetUVE raised = clip;
    for (auto& track : raised.bones) {
        for (auto& sample : track.samples) {
            sample.pose.position.z = 3.0F;
        }
    }
    clipFor = [this, &raised](const Asset::AssetGuidUVE guid) -> const Asset::AnimationClipAssetUVE* {
        return guid.value == 7U ? &clip : guid.value == 8U ? &raised : nullptr;
    };
    AnimationGraphComponentUVE graph;
    AnimationGraphNodeUVE byBool = NodeUVE(2U, Kind::BlendByBool, {3U, 4U});
    byBool.parameter = "Armed";
    byBool.fadeSeconds = 0.2F;
    byBool.fadeBackSeconds = 0.0F;
    AnimationGraphNodeUVE whenTrue = NodeUVE(3U, Kind::Clip, {});
    whenTrue.clip = Asset::AssetGuidUVE{8U};
    AnimationGraphNodeUVE whenFalse = NodeUVE(4U, Kind::Clip, {});
    whenFalse.clip = Asset::AssetGuidUVE{7U};
    graph.nodes = {NodeUVE(1U, Kind::Output, {2U}), byBool, whenTrue, whenFalse};
    AnimationParameterUVE armed;
    armed.name = "Armed";
    armed.type = AnimationParameterTypeUVE::Bool;
    graph.parameters = {armed};
    ASSERT_EQ(DescribeAnimationGraphProblemUVE(graph), "");
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-5F) << "false: the second input";
    ASSERT_TRUE(SetAnimationGraphParameterUVE(graph, "Armed", 1.0F));
    ASSERT_TRUE(StepUVE(graph));   // 0.1 s of a 0.2 s fade
    EXPECT_NEAR(skeleton.pose[0].position.z, 1.5F, 1e-4F) << "half way toward true";
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(skeleton.pose[0].position.z, 3.0F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameterUVE(graph, "Armed", 0.0F));
    ASSERT_TRUE(StepUVE(graph));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "no fade back: at once";
}

TEST(AnimationGraphCharacterParametersUVETest, TheCharacterFeedsSpeedDirectionAndAir) {
    AnimationGraphComponentUVE graph;
    const auto parameter = [](const char* name, const AnimationParameterSourceUVE source) {
        AnimationParameterUVE p;
        p.name = name;
        p.source = source;
        return p;
    };
    graph.parameters = {parameter("Speed", AnimationParameterSourceUVE::CharacterSpeed),
                        parameter("Direction", AnimationParameterSourceUVE::CharacterDirection),
                        parameter("InAir", AnimationParameterSourceUVE::CharacterInAir),
                        parameter("Grounded", AnimationParameterSourceUVE::CharacterGrounded),
                        parameter("Mine", AnimationParameterSourceUVE::Script)};
    graph.parameters[4].value = 7.0F;
    const Math::Vector3UVE up{0.0F, 1.0F, 0.0F};
    EXPECT_EQ(WriteCharacterAnimationParametersUVE(graph, Math::Vector3UVE{0.0F, -9.0F, -3.0F}, up, Math::QuaternionUVE{}, true), 4U);
    EXPECT_NEAR(graph.parameters[0].value, 3.0F, 1e-5F) << "ground speed: the fall does not count";
    EXPECT_NEAR(graph.parameters[1].value, 0.0F, 1e-4F) << "straight ahead (-Z)";
    EXPECT_EQ(graph.parameters[2].value, 0.0F);
    EXPECT_EQ(graph.parameters[3].value, 1.0F);
    EXPECT_EQ(graph.parameters[4].value, 7.0F) << "a script's parameter is left alone";
    static_cast<void>(WriteCharacterAnimationParametersUVE(graph, Math::Vector3UVE{2.0F, 0.0F, 0.0F}, up, Math::QuaternionUVE{}, false));
    EXPECT_NEAR(graph.parameters[1].value, 90.0F, 1e-3F) << "to the right";
    EXPECT_EQ(graph.parameters[2].value, 1.0F);
    EXPECT_EQ(graph.parameters[3].value, 0.0F);
}

} // namespace UVE::Scene
