// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/animation_tree_uve.h"

namespace UVE::Scene {
namespace {

/// A one-second clip sliding along X from 0 to `distance`.
[[nodiscard]] Asset::AnimationClipAssetUVE MakeSlideClipUVE(const float distance = 10.0F, const double duration = 1.0) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "slide";
    clip.durationSeconds = duration;
    Asset::AnimationAssetSampleUVE start;
    start.timeSeconds = 0.0;
    Asset::AnimationAssetSampleUVE end;
    end.timeSeconds = duration;
    end.pose.position = Math::Vector3UVE{distance, 0.0F, 0.0F};
    end.pose.scale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    clip.samples = {start, end};
    return clip;
}

[[nodiscard]] AnimationPlayerComponentUVE StartedUVE(AnimationPlayerComponentUVE player,
                                                     const TransformComponentUVE& target,
                                                     const Asset::AnimationClipAssetUVE& clip) {
    PlayAnimationPlayerUVE(player, target, clip.durationSeconds);
    return player;
}

TEST(AnimationPlayerUVETest, LoopWrapsBackToTheStart) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationPlayerComponentUVE player = StartedUVE(AnimationPlayerComponentUVE{}, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 5.0F, 1e-4F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.75F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_NEAR(target.localPosition.x, 2.5F, 1e-4F);
    EXPECT_TRUE(player.isPlaying);
}

TEST(AnimationPlayerUVETest, PingPongTurnsRoundAtEachEnd) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationPlayerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::PingPong;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 1.25F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.75F, 1e-4F);
    EXPECT_EQ(player.direction, -1.0F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 1.0F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_EQ(player.direction, 1.0F);
}

TEST(AnimationPlayerUVETest, OnceStopsAtTheEndAndCanReturnToStart) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{-3.0F, 1.0F, 0.0F};
    AnimationPlayerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::Once;
    AnimationPlayerComponentUVE hold = StartedUVE(settings, target, clip);
    TransformComponentUVE held = target;
    ASSERT_TRUE(StepAnimationPlayerUVE(hold, clip, 2.0F, held));
    EXPECT_FALSE(hold.isPlaying);
    EXPECT_TRUE(hold.finished);
    EXPECT_NEAR(held.localPosition.x, 10.0F, 1e-4F);
    EXPECT_FALSE(StepAnimationPlayerUVE(hold, clip, 0.1F, held)); // stopped: nothing more written

    settings.onFinish = AnimationFinishActionUVE::ReturnToStart;
    AnimationPlayerComponentUVE back = StartedUVE(settings, target, clip);
    TransformComponentUVE returned = target;
    ASSERT_TRUE(StepAnimationPlayerUVE(back, clip, 2.0F, returned));
    EXPECT_EQ(returned.localPosition, target.localPosition);
}

TEST(AnimationPlayerUVETest, NegativeSpeedPlaysBackwardsFromTheEnd) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationPlayerComponentUVE settings;
    settings.speed = -1.0F;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    EXPECT_NEAR(player.currentTimeSeconds, 1.0F, 1e-6F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.25F, target));
    EXPECT_NEAR(target.localPosition.x, 7.5F, 1e-4F);
}

TEST(AnimationPlayerUVETest, BlendInEasesFromWhereTheTargetWas) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE(10.0F, 10.0);
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{0.0F, 4.0F, 0.0F};
    AnimationPlayerComponentUVE settings;
    settings.blendInSeconds = 1.0F;
    settings.startOffsetSeconds = 5.0F; // the clip is at x=5, y=0 there
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    // Halfway through the blend: halfway between the start (0,4) and the clip (5.5,0).
    EXPECT_NEAR(target.localPosition.x, 2.75F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 2.0F, 1e-3F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 1.0F, target));
    EXPECT_NEAR(target.localPosition.x, 6.5F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 0.0F, 1e-3F);
}

TEST(AnimationPlayerUVETest, RelativeLaysTheMotionOnTopOfTheStartPose) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{100.0F, 0.0F, 7.0F};
    AnimationPlayerComponentUVE settings;
    settings.relative = true;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 105.0F, 1e-3F);
    EXPECT_NEAR(target.localPosition.z, 7.0F, 1e-3F);
}

TEST(AnimationPlayerUVETest, ChannelMasksLeaveTheOtherChannelsAlone) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{-1.0F, -1.0F, -1.0F};
    AnimationPlayerComponentUVE settings;
    settings.animatePosition = false;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    EXPECT_EQ(target.localPosition, (Math::Vector3UVE{-1.0F, -1.0F, -1.0F}));
    EXPECT_NEAR(target.localScale.x, 1.5F, 1e-4F);
}

TEST(AnimationPlayerUVETest, AnEmptyClipStopsWithoutWriting) {
    Asset::AnimationClipAssetUVE empty;
    TransformComponentUVE target;
    AnimationPlayerComponentUVE player = StartedUVE(AnimationPlayerComponentUVE{}, target, empty);
    EXPECT_FALSE(StepAnimationPlayerUVE(player, empty, 0.1F, target));
    EXPECT_FALSE(player.isPlaying);
}

TEST(AnimationPlayerUVETest, ValidationRejectsBadSettings) {
    AnimationPlayerComponentUVE player;
    EXPECT_TRUE(IsAnimationPlayerComponentValidUVE(player));
    player.blendInSeconds = -1.0F;
    EXPECT_FALSE(IsAnimationPlayerComponentValidUVE(player));
    player = AnimationPlayerComponentUVE{};
    player.loopMode = static_cast<AnimationLoopModeUVE>(9);
    EXPECT_FALSE(IsAnimationPlayerComponentValidUVE(player));
}

// ---- AnimationTree graph ------------------------------------------------------------------------

/// Clips by guid, and a tree builder that keeps the tests to what each one is about.
class GraphFixtureUVE {
public:
    Asset::AssetGuidUVE AddClipUVE(const float distance, const double duration = 1.0) {
        const Asset::AssetGuidUVE guid{m_clips.size() + 100U};
        m_clips.emplace(guid.value, MakeSlideClipUVE(distance, duration));
        return guid;
    }

    [[nodiscard]] AnimationClipResolverUVE ResolverUVE() const {
        return [this](const Asset::AssetGuidUVE guid) -> const Asset::AnimationClipAssetUVE* {
            const auto found = m_clips.find(guid.value);
            return found == m_clips.end() ? nullptr : &found->second;
        };
    }

    [[nodiscard]] static AnimationGraphNodeUVE NodeUVE(const std::uint32_t id, const AnimationGraphNodeKindUVE kind,
                                                      std::vector<std::uint32_t> inputs = {}) {
        AnimationGraphNodeUVE node;
        node.id = id;
        node.kind = kind;
        node.inputs = std::move(inputs);
        return node;
    }

    [[nodiscard]] static AnimationGraphNodeUVE ClipNodeUVE(const std::uint32_t id, const Asset::AssetGuidUVE clip,
                                                          const bool loop = true, std::string name = {}) {
        AnimationGraphNodeUVE node = NodeUVE(id, AnimationGraphNodeKindUVE::Clip);
        node.clip = clip;
        node.loop = loop;
        node.name = std::move(name);
        return node;
    }

    [[nodiscard]] float StepUVE(AnimationTreeComponentUVE& tree, const float seconds) {
        EXPECT_TRUE(IsAnimationTreeComponentValidUVE(tree)) << DescribeAnimationGraphProblemUVE(tree);
        EXPECT_TRUE(StepAnimationTreeUVE(tree, ResolverUVE(), seconds, m_target));
        return m_target.localPosition.x;
    }

private:
    std::unordered_map<std::uint64_t, Asset::AnimationClipAssetUVE> m_clips;
    TransformComponentUVE m_target;
};

using Kind = AnimationGraphNodeKindUVE;

TEST(AnimationTreeUVETest, TheDefaultGraphPlaysItsClip) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    ASSERT_TRUE(IsAnimationTreeComponentValidUVE(tree));
    tree.nodes[1].clip = fixture.AddClipUVE(10.0F);
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F);
}

TEST(AnimationTreeUVETest, BlendMixesByItsParameter) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"blend", AnimationParameterTypeUVE::Float, 0.5F}};
    AnimationGraphNodeUVE blend = GraphFixtureUVE::NodeUVE(2U, Kind::Blend2, {3U, 4U});
    blend.parameter = "blend";
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), blend,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(20.0F))};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 7.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationTreeParameterUVE(tree, "blend", 1.0F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 15.0F, 1e-4F);
    EXPECT_FALSE(SetAnimationTreeParameterUVE(tree, "missing", 1.0F));
}

TEST(AnimationTreeUVETest, BlendSpaceMixesTheTwoPointsAroundItsValue) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 1.5F}};
    AnimationGraphNodeUVE space = GraphFixtureUVE::NodeUVE(2U, Kind::BlendSpace1D, {3U, 4U, 5U});
    space.parameter = "speed";
    space.points = {0.0F, 1.0F, 2.0F};
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), space,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(20.0F)),
                  GraphFixtureUVE::ClipNodeUVE(5U, fixture.AddClipUVE(30.0F))};
    // Halfway between walk (20 -> 10 at t=0.5) and run (30 -> 15): 12.5.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 12.5F, 1e-4F);
}

TEST(AnimationTreeUVETest, StateMachineMovesOnConditionsAndConsumesTriggers) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.0F},
                       AnimationParameterUVE{"stop", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphNodeUVE machine = GraphFixtureUVE::NodeUVE(2U, Kind::StateMachine, {3U, 4U});
    AnimationTransitionUVE go;
    go.fromState = 0U;
    go.toState = 1U;
    go.condition = AnimationConditionUVE::ParameterGreater;
    go.parameter = "speed";
    go.threshold = 0.5F;
    go.fadeSeconds = 0.0F;
    AnimationTransitionUVE stop;
    stop.fromState = kAnyAnimationStateUVE;
    stop.toState = 0U;
    stop.condition = AnimationConditionUVE::Triggered;
    stop.parameter = "stop";
    stop.fadeSeconds = 0.0F;
    machine.transitions = {go, stop};
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), machine,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(1.0F), true, "Idle"),
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(10.0F), true, "Run")};

    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 0.5F, 1e-4F);
    EXPECT_EQ(tree.activeStates, "Idle");
    ASSERT_TRUE(SetAnimationTreeParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F)); // the move happens on this step
    EXPECT_EQ(tree.activeStates, "Run");
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F); // Run started from its beginning

    ASSERT_TRUE(SetAnimationTreeParameterUVE(tree, "stop", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Idle");
    EXPECT_EQ(tree.parameters[1].value, 0.0F); // the trigger was used up
}

TEST(AnimationTreeUVETest, StateMachineCrossfadesAndCanWaitForTheEnd) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    AnimationGraphNodeUVE machine = GraphFixtureUVE::NodeUVE(2U, Kind::StateMachine, {3U, 4U});
    AnimationTransitionUVE atEnd;
    atEnd.fromState = 0U;
    atEnd.toState = 1U;
    atEnd.condition = AnimationConditionUVE::AtEnd;
    atEnd.fadeSeconds = 1.0F;
    machine.transitions = {atEnd};
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), machine,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(10.0F), false, "Jump"),
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(20.0F), true, "Land")};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F);
    EXPECT_EQ(tree.activeStates, "Jump");
    EXPECT_NEAR(fixture.StepUVE(tree, 0.6F), 10.0F, 1e-4F); // reached the end: the move is made
    EXPECT_EQ(tree.activeStates, "Land");
    // Half a second into a one-second fade: halfway from Jump's held end (10) to Land at 0.5 (10).
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 10.0F, 1e-4F);
    // Three quarters through the fade: Jump's 10 mixed 75% toward Land's 15.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 13.75F, 1e-3F);
}

TEST(AnimationTreeUVETest, OneShotPlaysOverTheBaseAndHandsBack) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"attack", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphNodeUVE shot = GraphFixtureUVE::NodeUVE(2U, Kind::OneShot, {3U, 4U});
    shot.parameter = "attack";
    shot.fadeSeconds = 0.0F;
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), shot,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(100.0F), false)};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 2.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationTreeParameterUVE(tree, "attack", 1.0F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 50.0F, 1e-4F);
    EXPECT_EQ(tree.parameters[0].value, 0.0F);
    static_cast<void>(fixture.StepUVE(tree, 0.6F)); // the shot ends here
    EXPECT_NEAR(fixture.StepUVE(tree, 0.1F), 4.5F, 1e-3F); // base alone again (1.45 s wrapped)
}

TEST(AnimationTreeUVETest, AdditiveLayersAndTimeScaleRescales) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    AnimationGraphNodeUVE additive = GraphFixtureUVE::NodeUVE(3U, Kind::Additive, {4U, 5U});
    additive.value = 0.5F;
    AnimationGraphNodeUVE scale = GraphFixtureUVE::NodeUVE(2U, Kind::TimeScale, {3U});
    scale.speed = 2.0F;
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), scale, additive,
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipNodeUVE(5U, fixture.AddClipUVE(2.0F))};
    // 0.25 s at double speed is 0.5 s in: base 5 plus half of the layer's 1.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 5.5F, 1e-4F);
}

TEST(AnimationTreeUVETest, ValidationCatchesMalformedGraphsButAllowsEmptySlots) {
    AnimationTreeComponentUVE tree;
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {0U})};
    EXPECT_TRUE(IsAnimationTreeComponentValidUVE(tree)); // being built: nothing wired yet

    tree.nodes.push_back(GraphFixtureUVE::NodeUVE(2U, Kind::Output, {0U}));
    EXPECT_FALSE(IsAnimationTreeComponentValidUVE(tree)); // two outputs

    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), GraphFixtureUVE::NodeUVE(2U, Kind::Blend2, {3U, 3U}),
                  GraphFixtureUVE::NodeUVE(3U, Kind::Clip)};
    EXPECT_FALSE(IsAnimationTreeComponentValidUVE(tree)); // one node feeding two inputs

    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), GraphFixtureUVE::NodeUVE(2U, Kind::TimeScale, {3U}),
                  GraphFixtureUVE::NodeUVE(3U, Kind::TimeScale, {2U})};
    EXPECT_FALSE(IsAnimationTreeComponentValidUVE(tree)); // a loop

    AnimationGraphNodeUVE space = GraphFixtureUVE::NodeUVE(2U, Kind::BlendSpace1D, {0U, 0U});
    space.points = {1.0F, 0.0F};
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), space};
    EXPECT_FALSE(IsAnimationTreeComponentValidUVE(tree)); // points going backwards

    tree = AnimationTreeComponentUVE{};
    tree.parameters = {AnimationParameterUVE{"a", AnimationParameterTypeUVE::Float, 0.0F},
                       AnimationParameterUVE{"a", AnimationParameterTypeUVE::Bool, 0.0F}};
    EXPECT_FALSE(IsAnimationTreeComponentValidUVE(tree)); // two parameters with one name
}

} // namespace
} // namespace UVE::Scene
