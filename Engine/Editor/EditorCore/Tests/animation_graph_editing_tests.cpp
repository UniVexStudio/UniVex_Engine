// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <optional>

#include <gtest/gtest.h>

#include "animation_graph_editing.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKind;

[[nodiscard]] Scene::AnimationGraphComponent TreeOf(const std::vector<Scene::AnimationGraphNode>& objects) {
    Scene::AnimationGraphComponent tree;
    tree.nodes = objects;
    return tree;
}

[[nodiscard]] const Scene::AnimationGraphNode& Object(const std::vector<Scene::AnimationGraphNode>& objects,
                                                          const std::uint32_t id) {
    return *std::ranges::find(objects, id, &Scene::AnimationGraphNode::id);
}

TEST(AnimationGraphEditingTest, AddsObjectsWithTheSlotsTheirKindNeeds) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::uint32_t blend = AddAnimationGraphNode(objects, Kind::Blend2, {10.0F, 20.0F});
    ASSERT_NE(blend, 0U);
    EXPECT_EQ(Object(objects, blend).inputs.size(), 2U);
    EXPECT_EQ(Object(objects, blend).position.x, 10.0F);
    const std::uint32_t space = AddAnimationGraphNode(objects, Kind::BlendSpace1D, {});
    EXPECT_TRUE(Object(objects, space).inputs.empty()) << "a blend space has no inputs";
    EXPECT_TRUE(Object(objects, space).blendPoints.empty()) << "its points are added in its editor";
    EXPECT_EQ(AddAnimationGraphNode(objects, Kind::Output, {}), 0U) << "one Output per graph";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty());
}

TEST(AnimationGraphEditingTest, ConnectsMovesAndRefusesCycles) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph(); // Output(1) <- Clip(2)
    const std::uint32_t blend = AddAnimationGraphNode(objects, Kind::Blend2, {});
    ASSERT_TRUE(ConnectAnimationGraphNodes(objects, blend, 0U, 2U));
    EXPECT_EQ(Object(objects, blend).inputs[0], 2U);
    EXPECT_EQ(Object(objects, 1U).inputs[0], 0U) << "an object feeds one slot: the clip moved";

    ASSERT_TRUE(ConnectAnimationGraphNodes(objects, 1U, 0U, blend));
    const std::uint32_t scale = AddAnimationGraphNode(objects, Kind::TimeScale, {});
    ASSERT_TRUE(ConnectAnimationGraphNodes(objects, blend, 1U, scale));
    EXPECT_FALSE(ConnectAnimationGraphNodes(objects, scale, 0U, blend)) << "blend already feeds from scale";
    EXPECT_FALSE(ConnectAnimationGraphNodes(objects, scale, 0U, scale)) << "not itself";
    EXPECT_FALSE(ConnectAnimationGraphNodes(objects, blend, 0U, 1U)) << "the Output feeds nothing";
    EXPECT_FALSE(ConnectAnimationGraphNodes(objects, blend, 5U, 2U)) << "no such slot";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty());

    EXPECT_TRUE(DisconnectAnimationGraphInput(objects, blend, 1U));
    EXPECT_FALSE(DisconnectAnimationGraphInput(objects, blend, 1U)) << "already empty";
}

TEST(AnimationGraphEditingTest, DeletesObjectsButNeverTheOutput) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    EXPECT_EQ(DeleteAnimationGraphNodes(objects, {1U, 2U}), 1U);
    ASSERT_EQ(objects.size(), 1U);
    EXPECT_EQ(objects[0].kind, Kind::Output);
    EXPECT_EQ(objects[0].inputs[0], 0U) << "the slot that read the clip is empty";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty());
}

TEST(AnimationGraphEditingTest, DuplicatesKeepingWiresInsideTheCopy) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::uint32_t blend = AddAnimationGraphNode(objects, Kind::Blend2, {});
    ASSERT_TRUE(ConnectAnimationGraphNodes(objects, blend, 0U, 2U));
    const std::vector<std::uint32_t> copies = DuplicateAnimationGraphNodes(objects, {blend, 2U, 1U}, {40.0F, 0.0F});
    ASSERT_EQ(copies.size(), 2U) << "not the Output";
    EXPECT_EQ(Object(objects, copies[0]).inputs[0], copies[1]) << "the copied blend reads the copied clip";
    EXPECT_EQ(Object(objects, copies[1]).position.x, 40.0F);
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty());
}

TEST(AnimationGraphEditingTest, RemovingAStateFixesItsTransitions) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::uint32_t machine = AddAnimationGraphNode(objects, Kind::StateMachine, {});
    ASSERT_TRUE(AddAnimationGraphInputSlot(objects, machine));
    ASSERT_TRUE(AddAnimationGraphInputSlot(objects, machine));
    auto& object = *std::ranges::find(objects, machine, &Scene::AnimationGraphNode::id);
    object.entryState = 2U;
    const auto move = [](const std::uint32_t from, const std::uint32_t to) {
        Scene::AnimationGraphTransition transition;
        transition.fromState = from;
        transition.toState = to;
        return transition;
    };
    object.transitions = {move(0U, 1U), move(1U, 2U), move(Scene::kAnyAnimationState, 2U)};
    ASSERT_TRUE(RemoveAnimationGraphInputSlot(objects, machine, 1U));
    const auto& after = Object(objects, machine);
    EXPECT_EQ(after.inputs.size(), 2U);
    ASSERT_EQ(after.transitions.size(), 1U) << "the two touching state 1 went";
    EXPECT_EQ(after.transitions[0].toState, 1U) << "state 2 shifted down";
    EXPECT_EQ(after.entryState, 1U);
    EXPECT_FALSE(AddAnimationGraphInputSlot(objects, 2U)) << "a Clip has no slots to add";
}

TEST(AnimationGraphEditingTest, TheNewKindsStartReadyToWire) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::uint32_t space = AddAnimationGraphNode(objects, Kind::BlendSpace2D, {});
    EXPECT_TRUE(Object(objects, space).inputs.empty());
    EXPECT_FALSE(AddAnimationGraphInputSlot(objects, space)) << "points, not slots";
    EXPECT_LT(Object(objects, space).areaMin.x, Object(objects, space).areaMax.x);
    const std::uint32_t select = AddAnimationGraphNode(objects, Kind::Select, {});
    EXPECT_TRUE(AddAnimationGraphInputSlot(objects, select));
    EXPECT_EQ(AnimationGraphSlotLabel(Kind::Select, 2U), "Option 2");
    static_cast<void>(AddAnimationGraphNode(objects, Kind::LayeredBlend, {}));
    static_cast<void>(AddAnimationGraphNode(objects, Kind::TimeSeek, {}));
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty())
        << Scene::DescribeAnimationGraphProblem(TreeOf(objects));
}

TEST(AnimationGraphEditingTest, BlendSpacePointsHoldTheirOwnAnimation) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::size_t before = objects.size() + 1U;
    const std::uint32_t line = AddAnimationGraphNode(objects, Kind::BlendSpace1D, {});
    ASSERT_EQ(AddBlendSpacePoint(objects, line, {0.0F, 0.0F}, Asset::AssetGuid{5U}), std::optional<std::size_t>{0U});
    ASSERT_EQ(AddBlendSpacePoint(objects, line, {1.0F, 0.0F}, Asset::AssetGuid{6U}), std::optional<std::size_t>{1U});
    EXPECT_EQ(AddBlendSpacePoint(objects, line, {0.5F, 3.0F}, Asset::AssetGuid{7U}), std::optional<std::size_t>{1U})
        << "a line keeps its points rising: 0.5 lands between, y ignored";
    EXPECT_EQ(objects.size(), before) << "no Clip objects made: the point holds its animation";
    const auto& points = Object(objects, line).blendPoints;
    ASSERT_EQ(points.size(), 3U);
    EXPECT_EQ(points[1].clip, Asset::AssetGuid{7U});
    EXPECT_EQ(points[1].position.y, 0.0F);
    EXPECT_FALSE(AddBlendSpacePoint(objects, line, {0.5F, 0.0F}, {}).has_value()) << "a point already there";

    EXPECT_FALSE(MoveBlendSpacePoint(objects, line, 1U, {1.5F, 0.0F})) << "past its right neighbour";
    EXPECT_TRUE(MoveBlendSpacePoint(objects, line, 1U, {0.75F, 0.0F}));
    ASSERT_TRUE(RemoveBlendSpacePoint(objects, line, 1U));
    EXPECT_EQ(Object(objects, line).blendPoints.size(), 2U);
    EXPECT_FALSE(RemoveBlendSpacePoint(objects, line, 9U));

    const std::uint32_t plane = AddAnimationGraphNode(objects, Kind::BlendSpace2D, {});
    ASSERT_TRUE(AddBlendSpacePoint(objects, plane, {0.0F, 0.0F}, {}).has_value());
    ASSERT_EQ(AddBlendSpacePoint(objects, plane, {-1.0F, 0.5F}, {}), std::optional<std::size_t>{1U}) << "a plane appends";
    EXPECT_FALSE(MoveBlendSpacePoint(objects, plane, 1U, {0.0F, 0.0F})) << "onto another point";
    EXPECT_FALSE(AddBlendSpacePoint(objects, 1U, {2.0F, 0.0F}, {}).has_value()) << "the Output is not a blend space";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty())
        << Scene::DescribeAnimationGraphProblem(TreeOf(objects));
}

TEST(AnimationGraphEditingTest, StatesAreAddedWithAClipAndKeepTheirPlaces) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::uint32_t machine = AddAnimationGraphNode(objects, Kind::StateMachine, {400.0F, 0.0F});
    const std::size_t before = objects.size();
    ASSERT_EQ(AddAnimationState(objects, machine, {10.0F, 20.0F}), std::optional<std::size_t>{0U})
        << "a new machine's empty first state is used";
    ASSERT_EQ(AddAnimationState(objects, machine, {220.0F, 20.0F}), std::optional<std::size_t>{1U});
    EXPECT_EQ(objects.size(), before + 2U) << "each state plays a new Clip";
    const auto& added = Object(objects, machine);
    ASSERT_EQ(added.inputs.size(), 2U);
    EXPECT_EQ(Object(objects, added.inputs[1]).kind, Kind::Clip);
    EXPECT_EQ(AnimationStateName(objects, added, 1U), "State 2");
    EXPECT_EQ(AnimationGraphScopeOf(objects, added.inputs[1]), (AnimationGraphScope{machine, 1U})) << "its clip is in the state's graph";
    EXPECT_EQ(AnimationStatePosition(added, 1U).x, 220.0F);
    EXPECT_EQ(AnimationStatePosition(added, 7U).x, 200.0F) << "never placed: a grid spot";

    ASSERT_TRUE(SetAnimationStatePosition(objects, machine, 1U, {-50.0F, 60.0F}));
    EXPECT_EQ(AnimationStatePosition(Object(objects, machine), 1U).y, 60.0F);
    ASSERT_TRUE(RemoveAnimationGraphInputSlot(objects, machine, 0U));
    EXPECT_EQ(AnimationStatePosition(Object(objects, machine), 0U).x, -50.0F) << "the place moves with its state";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty())
        << Scene::DescribeAnimationGraphProblem(TreeOf(objects));
}

TEST(AnimationGraphEditingTest, TransitionsAreAddedReorderedAndDescribed) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::uint32_t machine = AddAnimationGraphNode(objects, Kind::StateMachine, {});
    static_cast<void>(AddAnimationState(objects, machine, {}));
    static_cast<void>(AddAnimationState(objects, machine, {200.0F, 0.0F}));
    EXPECT_FALSE(AddAnimationTransition(objects, machine, 1U, 1U).has_value()) << "not a state to itself";
    EXPECT_FALSE(AddAnimationTransition(objects, machine, 0U, 5U).has_value());
    ASSERT_EQ(AddAnimationTransition(objects, machine, 0U, 1U), std::optional<std::size_t>{0U});
    ASSERT_EQ(AddAnimationTransition(objects, machine, Scene::kAnyAnimationState, 0U), std::optional<std::size_t>{1U});
    ASSERT_TRUE(MoveAnimationTransition(objects, machine, 1U, 0U));
    EXPECT_EQ(Object(objects, machine).transitions[0].fromState, Scene::kAnyAnimationState) << "now tried first";
    EXPECT_FALSE(MoveAnimationTransition(objects, machine, 0U, 4U));
    ASSERT_TRUE(RemoveAnimationTransition(objects, machine, 0U));
    EXPECT_EQ(Object(objects, machine).transitions.size(), 1U);

    Scene::AnimationGraphTransition transition;
    EXPECT_EQ(DescribeAnimationTransition(transition), "always");
    transition.conditions = {{Scene::AnimationCondition::ParameterGreater, "speed", 0.5F},
                             {Scene::AnimationCondition::ParameterFalse, "crouched", 0.0F}};
    transition.exitPhase = 0.75F;
    EXPECT_EQ(DescribeAnimationTransition(transition), "speed > 0.5 and not crouched, after 75%");
    transition.enabled = false;
    EXPECT_EQ(DescribeAnimationTransition(transition), "speed > 0.5 and not crouched, after 75% (off)");
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty());
}

TEST(AnimationGraphEditingTest, EachStateHasAGraphOfItsOwn) {
    auto objects = Scene::AnimationGraphComponent::MakeDefaultAnimationGraph();
    const std::uint32_t machine = AddAnimationGraphNode(objects, Kind::StateMachine, {});
    static_cast<void>(AddAnimationState(objects, machine, {}));
    static_cast<void>(AddAnimationState(objects, machine, {200.0F, 0.0F}));
    const std::uint32_t idle = Object(objects, machine).inputs[0];
    const std::uint32_t run = Object(objects, machine).inputs[1];
    EXPECT_EQ(AnimationStateName(objects, Object(objects, machine), 0U), "State 1");
    EXPECT_EQ(AnimationGraphScopeOf(objects, machine), AnimationGraphScope{}) << "the machine is in the top tree";

    // A node wired into a state's clip is in that state; one made there but not wired stays there.
    const std::uint32_t speed = AddAnimationGraphNode(objects, Kind::TimeScale, {});
    ASSERT_TRUE(ConnectAnimationGraphNodes(objects, machine, 1U, speed));
    ASSERT_TRUE(ConnectAnimationGraphNodes(objects, speed, 0U, run));
    EXPECT_EQ(AnimationGraphScopeOf(objects, run), (AnimationGraphScope{machine, 1U})) << "through the wires";
    const std::uint32_t loose = AddAnimationGraphNode(objects, Kind::Blend2, {});
    for (Scene::AnimationGraphNode& node : objects) {
        if (node.id == loose) {
            node.owner = machine;
            node.ownerState = 1U;
        }
    }
    EXPECT_EQ(AnimationGraphScopeOf(objects, loose), (AnimationGraphScope{machine, 1U})) << "where it was put";
    const std::vector<std::uint32_t> top = AnimationGraphScopeNodes(objects, AnimationGraphScope{});
    EXPECT_EQ(std::ranges::count(top, idle) + std::ranges::count(top, run) + std::ranges::count(top, loose), 0)
        << "the top tree shows none of the states' nodes";
    EXPECT_EQ(AnimationGraphNodesInside(objects, machine).size(), 4U) << "both clips, the time scale and the loose blend";

    // Duplicating the machine copies its states' graphs, each copy in the copy's states.
    const std::size_t before = objects.size();
    const std::vector<std::uint32_t> copied = DuplicateAnimationGraphNodes(objects, {machine}, {50.0F, 0.0F});
    ASSERT_EQ(copied.size(), 1U);
    EXPECT_EQ(objects.size(), before + 5U);
    EXPECT_EQ(AnimationGraphNodesInside(objects, copied[0]).size(), 4U);
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty())
        << Scene::DescribeAnimationGraphProblem(TreeOf(objects));

    // Removing a state takes its graph with it; the later states' nodes move down a slot.
    ASSERT_TRUE(RemoveAnimationGraphInputSlot(objects, machine, 0U));
    EXPECT_EQ(std::ranges::count_if(objects, [idle](const Scene::AnimationGraphNode& n) { return n.id == idle; }), 0);
    EXPECT_EQ(AnimationGraphScopeOf(objects, loose), (AnimationGraphScope{machine, 0U}));
    EXPECT_EQ(AnimationStateName(objects, Object(objects, machine), 0U), "State 2") << "its name moved with it";
    // Deleting the machine takes every state's graph.
    ASSERT_EQ(DeleteAnimationGraphNodes(objects, {machine}), 4U);
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblem(TreeOf(objects)).empty())
        << Scene::DescribeAnimationGraphProblem(TreeOf(objects));
}

} // namespace
TEST(AnimationGraphEditingTest, ANodeGetsAParameterNamedAfterItAndARenameFollowsEverywhere) {
    Scene::AnimationGraphComponent graph;
    graph.parameters.clear();
    const std::uint32_t space = AddAnimationGraphNode(graph.nodes, Kind::BlendSpace2D, Math::Vector2{});
    const auto node = [&graph](const std::uint32_t id) {
        return std::ranges::find(graph.nodes, id, &Scene::AnimationGraphNode::id);
    };
    node(space)->name = "Walk";
    BindNewNodeParameters(graph, space);
    ASSERT_EQ(graph.parameters.size(), 1U);
    EXPECT_EQ(graph.parameters[0].name, "Walk");
    EXPECT_EQ(graph.parameters[0].type, Scene::AnimationParameterType::Vector3) << "a 2D space: one Vector3";
    EXPECT_EQ(node(space)->parameter, "Walk");
    BindNewNodeParameters(graph, space);
    EXPECT_EQ(graph.parameters.size(), 1U) << "bound already: nothing more";

    const std::uint32_t walk2 = AddAnimationGraphNode(graph.nodes, Kind::BlendSpace2D, Math::Vector2{});
    node(walk2)->name = "Walk";
    BindNewNodeParameters(graph, walk2);
    EXPECT_EQ(node(walk2)->parameter, "Walk2") << "unique";

    const std::uint32_t slot = AddAnimationGraphNode(graph.nodes, Kind::Slot, Math::Vector2{});
    node(slot)->parameter = "Walk";   // a slot's name, not a parameter
    BindNewNodeParameters(graph, slot);
    EXPECT_EQ(graph.parameters.size(), 2U) << "a Slot takes no parameter";

    EXPECT_TRUE(RenameGraphParameter(graph, "Walk", "Run"));
    EXPECT_EQ(node(space)->parameter, "Run");
    EXPECT_EQ(node(slot)->parameter, "Walk") << "the slot keeps its own name";
    EXPECT_FALSE(RenameGraphParameter(graph, "Run", "Walk2")) << "taken";
    EXPECT_EQ(ParameterNameForNode("Blend Space 2D"), "BlendSpace2D");
}

} // namespace UVE::Editor
