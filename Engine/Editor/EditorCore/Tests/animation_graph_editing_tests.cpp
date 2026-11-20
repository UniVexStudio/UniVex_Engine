// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <optional>

#include <gtest/gtest.h>

#include "animation_graph_editing.h"

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;

[[nodiscard]] Scene::AnimationGraphComponentUVE TreeOfUVE(const std::vector<Scene::AnimationGraphNodeUVE>& objects) {
    Scene::AnimationGraphComponentUVE tree;
    tree.nodes = objects;
    return tree;
}

[[nodiscard]] const Scene::AnimationGraphNodeUVE& ObjectUVE(const std::vector<Scene::AnimationGraphNodeUVE>& objects,
                                                          const std::uint32_t id) {
    return *std::ranges::find(objects, id, &Scene::AnimationGraphNodeUVE::id);
}

TEST(AnimationGraphEditingUVETest, AddsObjectsWithTheSlotsTheirKindNeeds) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t blend = AddAnimationGraphNodeUVE(objects, Kind::Blend2, {10.0F, 20.0F});
    ASSERT_NE(blend, 0U);
    EXPECT_EQ(ObjectUVE(objects, blend).inputs.size(), 2U);
    EXPECT_EQ(ObjectUVE(objects, blend).position.x, 10.0F);
    const std::uint32_t space = AddAnimationGraphNodeUVE(objects, Kind::BlendSpace1D, {});
    EXPECT_TRUE(ObjectUVE(objects, space).inputs.empty()) << "a blend space has no inputs";
    EXPECT_TRUE(ObjectUVE(objects, space).blendPoints.empty()) << "its points are added in its editor";
    EXPECT_EQ(AddAnimationGraphNodeUVE(objects, Kind::Output, {}), 0U) << "one Output per graph";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty());
}

TEST(AnimationGraphEditingUVETest, ConnectsMovesAndRefusesCycles) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE(); // Output(1) <- Clip(2)
    const std::uint32_t blend = AddAnimationGraphNodeUVE(objects, Kind::Blend2, {});
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(objects, blend, 0U, 2U));
    EXPECT_EQ(ObjectUVE(objects, blend).inputs[0], 2U);
    EXPECT_EQ(ObjectUVE(objects, 1U).inputs[0], 0U) << "an object feeds one slot: the clip moved";

    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(objects, 1U, 0U, blend));
    const std::uint32_t scale = AddAnimationGraphNodeUVE(objects, Kind::TimeScale, {});
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(objects, blend, 1U, scale));
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(objects, scale, 0U, blend)) << "blend already feeds from scale";
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(objects, scale, 0U, scale)) << "not itself";
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(objects, blend, 0U, 1U)) << "the Output feeds nothing";
    EXPECT_FALSE(ConnectAnimationGraphNodesUVE(objects, blend, 5U, 2U)) << "no such slot";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty());

    EXPECT_TRUE(DisconnectAnimationGraphInputUVE(objects, blend, 1U));
    EXPECT_FALSE(DisconnectAnimationGraphInputUVE(objects, blend, 1U)) << "already empty";
}

TEST(AnimationGraphEditingUVETest, DeletesObjectsButNeverTheOutput) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    EXPECT_EQ(DeleteAnimationGraphNodesUVE(objects, {1U, 2U}), 1U);
    ASSERT_EQ(objects.size(), 1U);
    EXPECT_EQ(objects[0].kind, Kind::Output);
    EXPECT_EQ(objects[0].inputs[0], 0U) << "the slot that read the clip is empty";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty());
}

TEST(AnimationGraphEditingUVETest, DuplicatesKeepingWiresInsideTheCopy) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t blend = AddAnimationGraphNodeUVE(objects, Kind::Blend2, {});
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(objects, blend, 0U, 2U));
    const std::vector<std::uint32_t> copies = DuplicateAnimationGraphNodesUVE(objects, {blend, 2U, 1U}, {40.0F, 0.0F});
    ASSERT_EQ(copies.size(), 2U) << "not the Output";
    EXPECT_EQ(ObjectUVE(objects, copies[0]).inputs[0], copies[1]) << "the copied blend reads the copied clip";
    EXPECT_EQ(ObjectUVE(objects, copies[1]).position.x, 40.0F);
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty());
}

TEST(AnimationGraphEditingUVETest, RemovingAStateFixesItsTransitions) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t machine = AddAnimationGraphNodeUVE(objects, Kind::StateMachine, {});
    ASSERT_TRUE(AddAnimationGraphInputSlotUVE(objects, machine));
    ASSERT_TRUE(AddAnimationGraphInputSlotUVE(objects, machine));
    auto& object = *std::ranges::find(objects, machine, &Scene::AnimationGraphNodeUVE::id);
    object.entryState = 2U;
    const auto move = [](const std::uint32_t from, const std::uint32_t to) {
        Scene::AnimationGraphTransitionUVE transition;
        transition.fromState = from;
        transition.toState = to;
        return transition;
    };
    object.transitions = {move(0U, 1U), move(1U, 2U), move(Scene::kAnyAnimationStateUVE, 2U)};
    ASSERT_TRUE(RemoveAnimationGraphInputSlotUVE(objects, machine, 1U));
    const auto& after = ObjectUVE(objects, machine);
    EXPECT_EQ(after.inputs.size(), 2U);
    ASSERT_EQ(after.transitions.size(), 1U) << "the two touching state 1 went";
    EXPECT_EQ(after.transitions[0].toState, 1U) << "state 2 shifted down";
    EXPECT_EQ(after.entryState, 1U);
    EXPECT_FALSE(AddAnimationGraphInputSlotUVE(objects, 2U)) << "a Clip has no slots to add";
}

TEST(AnimationGraphEditingUVETest, TheNewKindsStartReadyToWire) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t space = AddAnimationGraphNodeUVE(objects, Kind::BlendSpace2D, {});
    EXPECT_TRUE(ObjectUVE(objects, space).inputs.empty());
    EXPECT_FALSE(AddAnimationGraphInputSlotUVE(objects, space)) << "points, not slots";
    EXPECT_LT(ObjectUVE(objects, space).areaMin.x, ObjectUVE(objects, space).areaMax.x);
    const std::uint32_t select = AddAnimationGraphNodeUVE(objects, Kind::Select, {});
    EXPECT_TRUE(AddAnimationGraphInputSlotUVE(objects, select));
    EXPECT_EQ(AnimationGraphSlotLabelUVE(Kind::Select, 2U), "Option 2");
    static_cast<void>(AddAnimationGraphNodeUVE(objects, Kind::LayeredBlend, {}));
    static_cast<void>(AddAnimationGraphNodeUVE(objects, Kind::TimeSeek, {}));
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty())
        << Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects));
}

TEST(AnimationGraphEditingUVETest, BlendSpacePointsHoldTheirOwnAnimation) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::size_t before = objects.size() + 1U;
    const std::uint32_t line = AddAnimationGraphNodeUVE(objects, Kind::BlendSpace1D, {});
    ASSERT_EQ(AddBlendSpacePointUVE(objects, line, {0.0F, 0.0F}, Asset::AssetGuidUVE{5U}), std::optional<std::size_t>{0U});
    ASSERT_EQ(AddBlendSpacePointUVE(objects, line, {1.0F, 0.0F}, Asset::AssetGuidUVE{6U}), std::optional<std::size_t>{1U});
    EXPECT_EQ(AddBlendSpacePointUVE(objects, line, {0.5F, 3.0F}, Asset::AssetGuidUVE{7U}), std::optional<std::size_t>{1U})
        << "a line keeps its points rising: 0.5 lands between, y ignored";
    EXPECT_EQ(objects.size(), before) << "no Clip objects made: the point holds its animation";
    const auto& points = ObjectUVE(objects, line).blendPoints;
    ASSERT_EQ(points.size(), 3U);
    EXPECT_EQ(points[1].clip, Asset::AssetGuidUVE{7U});
    EXPECT_EQ(points[1].position.y, 0.0F);
    EXPECT_FALSE(AddBlendSpacePointUVE(objects, line, {0.5F, 0.0F}, {}).has_value()) << "a point already there";

    EXPECT_FALSE(MoveBlendSpacePointUVE(objects, line, 1U, {1.5F, 0.0F})) << "past its right neighbour";
    EXPECT_TRUE(MoveBlendSpacePointUVE(objects, line, 1U, {0.75F, 0.0F}));
    ASSERT_TRUE(RemoveBlendSpacePointUVE(objects, line, 1U));
    EXPECT_EQ(ObjectUVE(objects, line).blendPoints.size(), 2U);
    EXPECT_FALSE(RemoveBlendSpacePointUVE(objects, line, 9U));

    const std::uint32_t plane = AddAnimationGraphNodeUVE(objects, Kind::BlendSpace2D, {});
    ASSERT_TRUE(AddBlendSpacePointUVE(objects, plane, {0.0F, 0.0F}, {}).has_value());
    ASSERT_EQ(AddBlendSpacePointUVE(objects, plane, {-1.0F, 0.5F}, {}), std::optional<std::size_t>{1U}) << "a plane appends";
    EXPECT_FALSE(MoveBlendSpacePointUVE(objects, plane, 1U, {0.0F, 0.0F})) << "onto another point";
    EXPECT_FALSE(AddBlendSpacePointUVE(objects, 1U, {2.0F, 0.0F}, {}).has_value()) << "the Output is not a blend space";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty())
        << Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects));
}

TEST(AnimationGraphEditingUVETest, StatesAreAddedWithAClipAndKeepTheirPlaces) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t machine = AddAnimationGraphNodeUVE(objects, Kind::StateMachine, {400.0F, 0.0F});
    const std::size_t before = objects.size();
    ASSERT_EQ(AddAnimationStateUVE(objects, machine, {10.0F, 20.0F}), std::optional<std::size_t>{0U})
        << "a new machine's empty first state is used";
    ASSERT_EQ(AddAnimationStateUVE(objects, machine, {220.0F, 20.0F}), std::optional<std::size_t>{1U});
    EXPECT_EQ(objects.size(), before + 2U) << "each state plays a new Clip";
    const auto& added = ObjectUVE(objects, machine);
    ASSERT_EQ(added.inputs.size(), 2U);
    EXPECT_EQ(ObjectUVE(objects, added.inputs[1]).kind, Kind::Clip);
    EXPECT_EQ(AnimationStateNameUVE(objects, added, 1U), "State 2");
    EXPECT_EQ(AnimationGraphScopeOfUVE(objects, added.inputs[1]), (AnimationGraphScopeUVE{machine, 1U})) << "its clip is in the state's graph";
    EXPECT_EQ(AnimationStatePositionUVE(added, 1U).x, 220.0F);
    EXPECT_EQ(AnimationStatePositionUVE(added, 7U).x, 200.0F) << "never placed: a grid spot";

    ASSERT_TRUE(SetAnimationStatePositionUVE(objects, machine, 1U, {-50.0F, 60.0F}));
    EXPECT_EQ(AnimationStatePositionUVE(ObjectUVE(objects, machine), 1U).y, 60.0F);
    ASSERT_TRUE(RemoveAnimationGraphInputSlotUVE(objects, machine, 0U));
    EXPECT_EQ(AnimationStatePositionUVE(ObjectUVE(objects, machine), 0U).x, -50.0F) << "the place moves with its state";
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty())
        << Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects));
}

TEST(AnimationGraphEditingUVETest, TransitionsAreAddedReorderedAndDescribed) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t machine = AddAnimationGraphNodeUVE(objects, Kind::StateMachine, {});
    static_cast<void>(AddAnimationStateUVE(objects, machine, {}));
    static_cast<void>(AddAnimationStateUVE(objects, machine, {200.0F, 0.0F}));
    EXPECT_FALSE(AddAnimationTransitionUVE(objects, machine, 1U, 1U).has_value()) << "not a state to itself";
    EXPECT_FALSE(AddAnimationTransitionUVE(objects, machine, 0U, 5U).has_value());
    ASSERT_EQ(AddAnimationTransitionUVE(objects, machine, 0U, 1U), std::optional<std::size_t>{0U});
    ASSERT_EQ(AddAnimationTransitionUVE(objects, machine, Scene::kAnyAnimationStateUVE, 0U), std::optional<std::size_t>{1U});
    ASSERT_TRUE(MoveAnimationTransitionUVE(objects, machine, 1U, 0U));
    EXPECT_EQ(ObjectUVE(objects, machine).transitions[0].fromState, Scene::kAnyAnimationStateUVE) << "now tried first";
    EXPECT_FALSE(MoveAnimationTransitionUVE(objects, machine, 0U, 4U));
    ASSERT_TRUE(RemoveAnimationTransitionUVE(objects, machine, 0U));
    EXPECT_EQ(ObjectUVE(objects, machine).transitions.size(), 1U);

    Scene::AnimationGraphTransitionUVE transition;
    EXPECT_EQ(DescribeAnimationTransitionUVE(transition), "always");
    transition.conditions = {{Scene::AnimationConditionUVE::ParameterGreater, "speed", 0.5F},
                             {Scene::AnimationConditionUVE::ParameterFalse, "crouched", 0.0F}};
    transition.exitPhase = 0.75F;
    EXPECT_EQ(DescribeAnimationTransitionUVE(transition), "speed > 0.5 and not crouched, after 75%");
    transition.enabled = false;
    EXPECT_EQ(DescribeAnimationTransitionUVE(transition), "speed > 0.5 and not crouched, after 75% (off)");
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty());
}

TEST(AnimationGraphEditingUVETest, EachStateHasAGraphOfItsOwn) {
    auto objects = Scene::AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    const std::uint32_t machine = AddAnimationGraphNodeUVE(objects, Kind::StateMachine, {});
    static_cast<void>(AddAnimationStateUVE(objects, machine, {}));
    static_cast<void>(AddAnimationStateUVE(objects, machine, {200.0F, 0.0F}));
    const std::uint32_t idle = ObjectUVE(objects, machine).inputs[0];
    const std::uint32_t run = ObjectUVE(objects, machine).inputs[1];
    EXPECT_EQ(AnimationStateNameUVE(objects, ObjectUVE(objects, machine), 0U), "State 1");
    EXPECT_EQ(AnimationGraphScopeOfUVE(objects, machine), AnimationGraphScopeUVE{}) << "the machine is in the top tree";

    // A node wired into a state's clip is in that state; one made there but not wired stays there.
    const std::uint32_t speed = AddAnimationGraphNodeUVE(objects, Kind::TimeScale, {});
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(objects, machine, 1U, speed));
    ASSERT_TRUE(ConnectAnimationGraphNodesUVE(objects, speed, 0U, run));
    EXPECT_EQ(AnimationGraphScopeOfUVE(objects, run), (AnimationGraphScopeUVE{machine, 1U})) << "through the wires";
    const std::uint32_t loose = AddAnimationGraphNodeUVE(objects, Kind::Blend2, {});
    for (Scene::AnimationGraphNodeUVE& node : objects) {
        if (node.id == loose) {
            node.owner = machine;
            node.ownerState = 1U;
        }
    }
    EXPECT_EQ(AnimationGraphScopeOfUVE(objects, loose), (AnimationGraphScopeUVE{machine, 1U})) << "where it was put";
    const std::vector<std::uint32_t> top = AnimationGraphScopeNodesUVE(objects, AnimationGraphScopeUVE{});
    EXPECT_EQ(std::ranges::count(top, idle) + std::ranges::count(top, run) + std::ranges::count(top, loose), 0)
        << "the top tree shows none of the states' nodes";
    EXPECT_EQ(AnimationGraphNodesInsideUVE(objects, machine).size(), 4U) << "both clips, the time scale and the loose blend";

    // Duplicating the machine copies its states' graphs, each copy in the copy's states.
    const std::size_t before = objects.size();
    const std::vector<std::uint32_t> copied = DuplicateAnimationGraphNodesUVE(objects, {machine}, {50.0F, 0.0F});
    ASSERT_EQ(copied.size(), 1U);
    EXPECT_EQ(objects.size(), before + 5U);
    EXPECT_EQ(AnimationGraphNodesInsideUVE(objects, copied[0]).size(), 4U);
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty())
        << Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects));

    // Removing a state takes its graph with it; the later states' nodes move down a slot.
    ASSERT_TRUE(RemoveAnimationGraphInputSlotUVE(objects, machine, 0U));
    EXPECT_EQ(std::ranges::count_if(objects, [idle](const Scene::AnimationGraphNodeUVE& n) { return n.id == idle; }), 0);
    EXPECT_EQ(AnimationGraphScopeOfUVE(objects, loose), (AnimationGraphScopeUVE{machine, 0U}));
    EXPECT_EQ(AnimationStateNameUVE(objects, ObjectUVE(objects, machine), 0U), "State 2") << "its name moved with it";
    // Deleting the machine takes every state's graph.
    ASSERT_EQ(DeleteAnimationGraphNodesUVE(objects, {machine}), 4U);
    EXPECT_TRUE(Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects)).empty())
        << Scene::DescribeAnimationGraphProblemUVE(TreeOfUVE(objects));
}

} // namespace
TEST(AnimationGraphEditingUVETest, ANodeGetsAParameterNamedAfterItAndARenameFollowsEverywhere) {
    Scene::AnimationGraphComponentUVE graph;
    graph.parameters.clear();
    const std::uint32_t space = AddAnimationGraphNodeUVE(graph.nodes, Kind::BlendSpace2D, Math::Vector2UVE{});
    const auto node = [&graph](const std::uint32_t id) {
        return std::ranges::find(graph.nodes, id, &Scene::AnimationGraphNodeUVE::id);
    };
    node(space)->name = "Walk";
    BindNewNodeParametersUVE(graph, space);
    ASSERT_EQ(graph.parameters.size(), 1U);
    EXPECT_EQ(graph.parameters[0].name, "Walk");
    EXPECT_EQ(graph.parameters[0].type, Scene::AnimationParameterTypeUVE::Vector3) << "a 2D space: one Vector3";
    EXPECT_EQ(node(space)->parameter, "Walk");
    BindNewNodeParametersUVE(graph, space);
    EXPECT_EQ(graph.parameters.size(), 1U) << "bound already: nothing more";

    const std::uint32_t walk2 = AddAnimationGraphNodeUVE(graph.nodes, Kind::BlendSpace2D, Math::Vector2UVE{});
    node(walk2)->name = "Walk";
    BindNewNodeParametersUVE(graph, walk2);
    EXPECT_EQ(node(walk2)->parameter, "Walk2") << "unique";

    const std::uint32_t slot = AddAnimationGraphNodeUVE(graph.nodes, Kind::Slot, Math::Vector2UVE{});
    node(slot)->parameter = "Walk";   // a slot's name, not a parameter
    BindNewNodeParametersUVE(graph, slot);
    EXPECT_EQ(graph.parameters.size(), 2U) << "a Slot takes no parameter";

    EXPECT_TRUE(RenameGraphParameterUVE(graph, "Walk", "Run"));
    EXPECT_EQ(node(space)->parameter, "Run");
    EXPECT_EQ(node(slot)->parameter, "Walk") << "the slot keeps its own name";
    EXPECT_FALSE(RenameGraphParameterUVE(graph, "Run", "Walk2")) << "taken";
    EXPECT_EQ(ParameterNameForNodeUVE("Blend Space 2D"), "BlendSpace2D");
}

} // namespace UVE::Editor
