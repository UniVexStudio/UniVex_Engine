// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <optional>

#include <gtest/gtest.h>

#include "uve/editor/animation_graph_editing_uve.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;

[[nodiscard]] Scene::AnimationTreeComponentUVE TreeOfUVE(const std::vector<Scene::AnimationGraphNodeUVE>& nodes) {
    Scene::AnimationTreeComponentUVE tree;
    tree.nodes = nodes;
    return tree;
}

[[nodiscard]] const Scene::AnimationGraphNodeUVE& NodeUVE(const std::vector<Scene::AnimationGraphNodeUVE>& nodes,
                                                          const std::uint32_t id) {
    return *std::ranges::find(nodes, id, &Scene::AnimationGraphNodeUVE::id);
}

TEST(AnimationGraphEditingUVETest, AddsNodesWithTheSlotsTheirKindNeeds) {
    auto nodes = Scene::AnimationTreeComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t blend = AddAnimationGraphNodeUVE(nodes, Kind::Blend2, {10.0F, 20.0F});
    ASSERT_NE(blend, 0U);
    EXPECT_EQ(NodeUVE(nodes, blend).inputs.size(), 2U);
    EXPECT_EQ(NodeUVE(nodes, blend).position.x, 10.0F);
    const std::uint32_t space = AddAnimationGraphNodeUVE(nodes, Kind::BlendSpace1D, {});
    EXPECT_TRUE(NodeUVE(nodes, space).inputs.empty()) << "a blend space has no inputs";
    EXPECT_TRUE(NodeUVE(nodes, space).blendPoints.empty()) << "its points are added in its editor";
    EXPECT_EQ(AddAnimationGraphNodeUVE(nodes, Kind::Output, {}), 0U) << "one Output per graph";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes)).empty());
}

TEST(AnimationGraphEditingUVETest, ConnectsMovesAndRefusesCycles) {
    auto nodes = Scene::AnimationTreeComponentUVE::MakeDefaultAnimationGraphUVE(); // Output(1) <- Clip(2)
    const std::uint32_t blend = AddAnimationGraphNodeUVE(nodes, Kind::Blend2, {});
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(nodes, blend, 0U, 2U));
    EXPECT_EQ(NodeUVE(nodes, blend).inputs[0], 2U);
    EXPECT_EQ(NodeUVE(nodes, 1U).inputs[0], 0U) << "a node feeds one slot: the clip moved";

    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(nodes, 1U, 0U, blend));
    const std::uint32_t scale = AddAnimationGraphNodeUVE(nodes, Kind::TimeScale, {});
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(nodes, blend, 1U, scale));
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(nodes, scale, 0U, blend)) << "blend already feeds from scale";
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(nodes, scale, 0U, scale)) << "not itself";
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(nodes, blend, 0U, 1U)) << "the Output feeds nothing";
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(nodes, blend, 5U, 2U)) << "no such slot";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes)).empty());

    EXPECT_TRUE(DisconnectAnimationGraphInputUVE(nodes, blend, 1U));
    EXPECT_FALSE(DisconnectAnimationGraphInputUVE(nodes, blend, 1U)) << "already empty";
}

TEST(AnimationGraphEditingUVETest, DeletesNodesButNeverTheOutput) {
    auto nodes = Scene::AnimationTreeComponentUVE::MakeDefaultAnimationGraphUVE();
    EXPECT_EQ(DeleteAnimationGraphNodesUVE(nodes, {1U, 2U}), 1U);
    ASSERT_EQ(nodes.size(), 1U);
    EXPECT_EQ(nodes[0].kind, Kind::Output);
    EXPECT_EQ(nodes[0].inputs[0], 0U) << "the slot that read the clip is empty";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes)).empty());
}

TEST(AnimationGraphEditingUVETest, DuplicatesKeepingWiresInsideTheCopy) {
    auto nodes = Scene::AnimationTreeComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t blend = AddAnimationGraphNodeUVE(nodes, Kind::Blend2, {});
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(nodes, blend, 0U, 2U));
    const std::vector<std::uint32_t> copies = DuplicateAnimationGraphNodesUVE(nodes, {blend, 2U, 1U}, {40.0F, 0.0F});
    ASSERT_EQ(copies.size(), 2U) << "not the Output";
    EXPECT_EQ(NodeUVE(nodes, copies[0]).inputs[0], copies[1]) << "the copied blend reads the copied clip";
    EXPECT_EQ(NodeUVE(nodes, copies[1]).position.x, 40.0F);
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes)).empty());
}

TEST(AnimationGraphEditingUVETest, RemovingAStateFixesItsTransitions) {
    auto nodes = Scene::AnimationTreeComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t machine = AddAnimationGraphNodeUVE(nodes, Kind::StateMachine, {});
    ASSERT_TRUE(AddAnimationGraphInputSlotUVE(nodes, machine));
    ASSERT_TRUE(AddAnimationGraphInputSlotUVE(nodes, machine));
    auto& node = *std::ranges::find(nodes, machine, &Scene::AnimationGraphNodeUVE::id);
    node.entryState = 2U;
    const auto move = [](const std::uint32_t from, const std::uint32_t to) {
        Scene::AnimationTransitionUVE transition;
        transition.fromState = from;
        transition.toState = to;
        return transition;
    };
    node.transitions = {move(0U, 1U), move(1U, 2U), move(Scene::kAnyAnimationStateUVE, 2U)};
    ASSERT_TRUE(RemoveAnimationGraphInputSlotUVE(nodes, machine, 1U));
    const auto& after = NodeUVE(nodes, machine);
    EXPECT_EQ(after.inputs.size(), 2U);
    ASSERT_EQ(after.transitions.size(), 1U) << "the two touching state 1 went";
    EXPECT_EQ(after.transitions[0].toState, 1U) << "state 2 shifted down";
    EXPECT_EQ(after.entryState, 1U);
    EXPECT_FALSE(AddAnimationGraphInputSlotUVE(nodes, 2U)) << "a Clip has no slots to add";
}

TEST(AnimationGraphEditingUVETest, TheNewKindsStartReadyToWire) {
    auto nodes = Scene::AnimationTreeComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t space = AddAnimationGraphNodeUVE(nodes, Kind::BlendSpace2D, {});
    EXPECT_TRUE(NodeUVE(nodes, space).inputs.empty());
    EXPECT_FALSE(AddAnimationGraphInputSlotUVE(nodes, space)) << "points, not slots";
    EXPECT_LT(NodeUVE(nodes, space).areaMin.x, NodeUVE(nodes, space).areaMax.x);
    const std::uint32_t select = AddAnimationGraphNodeUVE(nodes, Kind::Select, {});
    EXPECT_TRUE(AddAnimationGraphInputSlotUVE(nodes, select));
    EXPECT_EQ(AnimationGraphSlotLabelUVE(Kind::Select, 2U), "Option 2");
    static_cast<void>(AddAnimationGraphNodeUVE(nodes, Kind::LayeredBlend, {}));
    static_cast<void>(AddAnimationGraphNodeUVE(nodes, Kind::TimeSeek, {}));
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes)).empty())
        << Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes));
}

TEST(AnimationGraphEditingUVETest, BlendSpacePointsHoldTheirOwnAnimation) {
    auto nodes = Scene::AnimationTreeComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::size_t before = nodes.size() + 1U;
    const std::uint32_t line = AddAnimationGraphNodeUVE(nodes, Kind::BlendSpace1D, {});
    ASSERT_EQ(AddBlendSpacePointUVE(nodes, line, {0.0F, 0.0F}, Asset::AssetGuidUVE{5U}), std::optional<std::size_t>{0U});
    ASSERT_EQ(AddBlendSpacePointUVE(nodes, line, {1.0F, 0.0F}, Asset::AssetGuidUVE{6U}), std::optional<std::size_t>{1U});
    EXPECT_EQ(AddBlendSpacePointUVE(nodes, line, {0.5F, 3.0F}, Asset::AssetGuidUVE{7U}), std::optional<std::size_t>{1U})
        << "a line keeps its points rising: 0.5 lands between, y ignored";
    EXPECT_EQ(nodes.size(), before) << "no Clip nodes made: the point holds its animation";
    const auto& points = NodeUVE(nodes, line).blendPoints;
    ASSERT_EQ(points.size(), 3U);
    EXPECT_EQ(points[1].clip, Asset::AssetGuidUVE{7U});
    EXPECT_EQ(points[1].position.y, 0.0F);
    EXPECT_FALSE(AddBlendSpacePointUVE(nodes, line, {0.5F, 0.0F}, {}).has_value()) << "a point already there";

    EXPECT_FALSE(MoveBlendSpacePointUVE(nodes, line, 1U, {1.5F, 0.0F})) << "past its right neighbour";
    EXPECT_TRUE(MoveBlendSpacePointUVE(nodes, line, 1U, {0.75F, 0.0F}));
    ASSERT_TRUE(RemoveBlendSpacePointUVE(nodes, line, 1U));
    EXPECT_EQ(NodeUVE(nodes, line).blendPoints.size(), 2U);
    EXPECT_FALSE(RemoveBlendSpacePointUVE(nodes, line, 9U));

    const std::uint32_t plane = AddAnimationGraphNodeUVE(nodes, Kind::BlendSpace2D, {});
    ASSERT_TRUE(AddBlendSpacePointUVE(nodes, plane, {0.0F, 0.0F}, {}).has_value());
    ASSERT_EQ(AddBlendSpacePointUVE(nodes, plane, {-1.0F, 0.5F}, {}), std::optional<std::size_t>{1U}) << "a plane appends";
    EXPECT_FALSE(MoveBlendSpacePointUVE(nodes, plane, 1U, {0.0F, 0.0F})) << "onto another point";
    EXPECT_FALSE(AddBlendSpacePointUVE(nodes, 1U, {2.0F, 0.0F}, {}).has_value()) << "the Output is not a blend space";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes)).empty())
        << Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(nodes));
}

} // namespace
} // namespace UVE::Editor
