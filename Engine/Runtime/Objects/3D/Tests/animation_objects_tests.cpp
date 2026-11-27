// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "animation_sequencer_component.h"
#include "animation_graph_component.h"
#include "transform_component.h"
#include "animation_sequencer.h"
#include "animation_graph.h"
#include "skeleton_3d.h"

namespace UVE::Scene {
namespace {

/// A one-second clip sliding along X from 0 to `distance`.
[[nodiscard]] Asset::AnimationClipAsset MakeSlideClip(const float distance = 10.0F, const double duration = 1.0) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "slide";
    clip.durationSeconds = duration;
    Asset::AnimationAssetSample start;
    start.timeSeconds = 0.0;
    Asset::AnimationAssetSample end;
    end.timeSeconds = duration;
    end.pose.position = Math::Vector3{distance, 0.0F, 0.0F};
    end.pose.scale = Math::Vector3{2.0F, 2.0F, 2.0F};
    clip.samples = {start, end};
    return clip;
}

[[nodiscard]] AnimationSequencerComponent Started(AnimationSequencerComponent player,
                                                     const TransformComponent& target,
                                                     const Asset::AnimationClipAsset& clip) {
    PlayAnimationSequencer(player, target, clip.durationSeconds);
    return player;
}

TEST(AnimationSequencerTest, LoopWrapsBackToTheStart) {
    const Asset::AnimationClipAsset clip = MakeSlideClip();
    TransformComponent target;
    AnimationSequencerComponent player = Started(AnimationSequencerComponent{}, target, clip);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 5.0F, 1e-4F);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 0.75F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_NEAR(target.localPosition.x, 2.5F, 1e-4F);
    EXPECT_TRUE(player.isPlaying);
}

TEST(AnimationSequencerTest, PingPongTurnsRoundAtEachEnd) {
    const Asset::AnimationClipAsset clip = MakeSlideClip();
    TransformComponent target;
    AnimationSequencerComponent settings;
    settings.loopMode = AnimationLoopMode::PingPong;
    AnimationSequencerComponent player = Started(settings, target, clip);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 1.25F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.75F, 1e-4F);
    EXPECT_EQ(player.direction, -1.0F);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 1.0F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_EQ(player.direction, 1.0F);
}

TEST(AnimationSequencerTest, OnceStopsAtTheEndAndCanReturnToStart) {
    const Asset::AnimationClipAsset clip = MakeSlideClip();
    TransformComponent target;
    target.localPosition = Math::Vector3{-3.0F, 1.0F, 0.0F};
    AnimationSequencerComponent settings;
    settings.loopMode = AnimationLoopMode::Once;
    AnimationSequencerComponent hold = Started(settings, target, clip);
    TransformComponent held = target;
    ASSERT_TRUE(StepAnimationSequencer(hold, clip, 2.0F, held));
    EXPECT_FALSE(hold.isPlaying);
    EXPECT_TRUE(hold.finished);
    EXPECT_NEAR(held.localPosition.x, 10.0F, 1e-4F);
    EXPECT_FALSE(StepAnimationSequencer(hold, clip, 0.1F, held)); // stopped: nothing more written

    settings.onFinish = AnimationFinishAction::ReturnToStart;
    AnimationSequencerComponent back = Started(settings, target, clip);
    TransformComponent returned = target;
    ASSERT_TRUE(StepAnimationSequencer(back, clip, 2.0F, returned));
    EXPECT_EQ(returned.localPosition, target.localPosition);
}

TEST(AnimationSequencerTest, NegativeSpeedPlaysBackwardsFromTheEnd) {
    const Asset::AnimationClipAsset clip = MakeSlideClip();
    TransformComponent target;
    AnimationSequencerComponent settings;
    settings.speed = -1.0F;
    AnimationSequencerComponent player = Started(settings, target, clip);
    EXPECT_NEAR(player.currentTimeSeconds, 1.0F, 1e-6F);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 0.25F, target));
    EXPECT_NEAR(target.localPosition.x, 7.5F, 1e-4F);
}

TEST(AnimationSequencerTest, BlendInEasesFromWhereTheTargetWas) {
    const Asset::AnimationClipAsset clip = MakeSlideClip(10.0F, 10.0);
    TransformComponent target;
    target.localPosition = Math::Vector3{0.0F, 4.0F, 0.0F};
    AnimationSequencerComponent settings;
    settings.blendInSeconds = 1.0F;
    settings.startOffsetSeconds = 5.0F; // the clip is at x=5, y=0 there
    AnimationSequencerComponent player = Started(settings, target, clip);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 0.5F, target));
    // Halfway through the blend: halfway between the start (0,4) and the clip (5.5,0).
    EXPECT_NEAR(target.localPosition.x, 2.75F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 2.0F, 1e-3F);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 1.0F, target));
    EXPECT_NEAR(target.localPosition.x, 6.5F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 0.0F, 1e-3F);
}

TEST(AnimationSequencerTest, RelativeLaysTheMotionOnTopOfTheStartPose) {
    const Asset::AnimationClipAsset clip = MakeSlideClip();
    TransformComponent target;
    target.localPosition = Math::Vector3{100.0F, 0.0F, 7.0F};
    AnimationSequencerComponent settings;
    settings.relative = true;
    AnimationSequencerComponent player = Started(settings, target, clip);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 105.0F, 1e-3F);
    EXPECT_NEAR(target.localPosition.z, 7.0F, 1e-3F);
}

TEST(AnimationSequencerTest, ChannelMasksLeaveTheOtherChannelsAlone) {
    const Asset::AnimationClipAsset clip = MakeSlideClip();
    TransformComponent target;
    target.localPosition = Math::Vector3{-1.0F, -1.0F, -1.0F};
    AnimationDriverComponent mixer;
    mixer.animatePosition = false;
    AnimationSequencerComponent player = Started(AnimationSequencerComponent{}, target, clip);
    ASSERT_TRUE(StepAnimationSequencer(player, clip, 0.5F, target, mixer));
    EXPECT_EQ(target.localPosition, (Math::Vector3{-1.0F, -1.0F, -1.0F}));
    EXPECT_NEAR(target.localScale.x, 1.5F, 1e-4F);
}

TEST(AnimationSequencerTest, AnEmptyClipStopsWithoutWriting) {
    Asset::AnimationClipAsset empty;
    TransformComponent target;
    AnimationSequencerComponent player = Started(AnimationSequencerComponent{}, target, empty);
    EXPECT_FALSE(StepAnimationSequencer(player, empty, 0.1F, target));
    EXPECT_FALSE(player.isPlaying);
}

TEST(AnimationSequencerTest, AClipOfOnlyOtherObjectsKeepsPlayingAndLeavesTheTargetAlone) {
    Asset::AnimationClipAsset clip = MakeSlideClip();
    Asset::AnimationAssetBoneTrack wheel;
    wheel.bone = "Wheel";
    wheel.samples = clip.samples;
    clip.objects = {wheel};
    clip.samples.clear();
    TransformComponent target;
    target.localPosition = Math::Vector3{3.0F, 0.0F, 0.0F};
    AnimationSequencerComponent player = Started(AnimationSequencerComponent{}, target, clip);
    EXPECT_TRUE(StepAnimationSequencer(player, clip, 0.5F, target));
    EXPECT_TRUE(player.isPlaying);
    EXPECT_NEAR(player.currentTimeSeconds, 0.5F, 1e-4F);
    EXPECT_EQ(target.localPosition, (Math::Vector3{3.0F, 0.0F, 0.0F}));
    // What the engine writes on the object named Wheel at that time.
    EXPECT_NEAR(SampleAnimationTrack(clip.objects[0].samples, player.currentTimeSeconds).position.x, 5.0F, 1e-4F);
}

TEST(AnimationSequencerTest, ValidationRejectsBadSettings) {
    AnimationSequencerComponent player;
    EXPECT_TRUE(IsAnimationSequencerComponentValid(player));
    player.blendInSeconds = -1.0F;
    EXPECT_FALSE(IsAnimationSequencerComponentValid(player));
    player = AnimationSequencerComponent{};
    player.loopMode = static_cast<AnimationLoopMode>(9);
    EXPECT_FALSE(IsAnimationSequencerComponentValid(player));
}

// ---- AnimationGraph graph ------------------------------------------------------------------------

/// Clips by guid, and a tree builder that keeps the tests to what each one is about.
class GraphFixture {
public:
    Asset::AssetGuid AddClip(const float distance, const double duration = 1.0) {
        const Asset::AssetGuid guid{m_clips.size() + 100U};
        m_clips.emplace(guid.value, MakeSlideClip(distance, duration));
        return guid;
    }

    [[nodiscard]] AnimationClipResolver Resolver() const {
        return [this](const Asset::AssetGuid guid) -> const Asset::AnimationClipAsset* {
            const auto found = m_clips.find(guid.value);
            return found == m_clips.end() ? nullptr : &found->second;
        };
    }

    [[nodiscard]] static AnimationGraphNode Node(const std::uint32_t id, const AnimationGraphNodeKind kind,
                                                      std::vector<std::uint32_t> inputs = {}) {
        AnimationGraphNode object;
        object.id = id;
        object.kind = kind;
        object.inputs = std::move(inputs);
        return object;
    }

    [[nodiscard]] static AnimationGraphNode ClipNode(const std::uint32_t id, const Asset::AssetGuid clip,
                                                          const bool loop = true, std::string name = {}) {
        AnimationGraphNode object = Node(id, AnimationGraphNodeKind::Clip);
        object.clip = clip;
        object.loop = loop;
        object.name = std::move(name);
        return object;
    }

    [[nodiscard]] float Step(AnimationGraphComponent& tree, const float seconds) {
        EXPECT_TRUE(IsAnimationGraphComponentValid(tree)) << DescribeAnimationGraphProblem(tree);
        EXPECT_TRUE(StepAnimationGraph(tree, Resolver(), seconds, m_target, mixer));
        return m_target.localPosition.x;
    }

    /// Adds a clip as it is, for skeletal graphs.
    Asset::AssetGuid Add(Asset::AnimationClipAsset clip) {
        const Asset::AssetGuid guid{m_clips.size() + 100U};
        m_clips.emplace(guid.value, std::move(clip));
        return guid;
    }

    AnimationDriverComponent mixer;

private:
    std::unordered_map<std::uint64_t, Asset::AnimationClipAsset> m_clips;
    TransformComponent m_target;
};

using Kind = AnimationGraphNodeKind;

TEST(AnimationGraphTest, TheDefaultGraphPlaysItsClip) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    ASSERT_TRUE(IsAnimationGraphComponentValid(tree));
    tree.nodes[1].clip = fixture.AddClip(10.0F);
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-4F);
}

TEST(AnimationGraphTest, BlendMixesByItsParameter) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"blend", AnimationParameterType::Float, 0.5F}};
    AnimationGraphNode blend = GraphFixture::Node(2U, Kind::Blend2, {3U, 4U});
    blend.parameter = "blend";
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), blend,
                  GraphFixture::ClipNode(3U, fixture.AddClip(10.0F)),
                  GraphFixture::ClipNode(4U, fixture.AddClip(20.0F))};
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 7.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "blend", 1.0F));
    EXPECT_NEAR(fixture.Step(tree, 0.25F), 15.0F, 1e-4F);
    EXPECT_FALSE(SetAnimationGraphParameter(tree, "missing", 1.0F));
}

TEST(AnimationGraphTest, BlendSpaceMixesTheTwoPointsAroundItsValue) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"speed", AnimationParameterType::Float, 1.5F}};
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    // The space holds its animations itself: no inputs.
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(10.0F)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(20.0F)},
                         AnimationBlendPoint{{2.0F, 0.0F}, fixture.AddClip(30.0F)}};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    // Halfway between walk (20 -> 10 at t=0.5) and run (30 -> 15): 12.5.
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 12.5F, 1e-4F);
}

TEST(AnimationGraphTest, StateMachineMovesOnConditionsAndConsumesTriggers) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"speed", AnimationParameterType::Float, 0.0F},
                       AnimationParameter{"stop", AnimationParameterType::Trigger, 0.0F}};
    AnimationGraphNode machine = GraphFixture::Node(2U, Kind::StateMachine, {3U, 4U});
    AnimationGraphTransition go;
    go.fromState = 0U;
    go.toState = 1U;
    go.conditions = {AnimationTransitionCondition{AnimationCondition::ParameterGreater, "speed", 0.5F}};
    go.fadeSeconds = 0.0F;
    AnimationGraphTransition stop;
    stop.fromState = kAnyAnimationState;
    stop.toState = 0U;
    stop.conditions = {AnimationTransitionCondition{AnimationCondition::Triggered, "stop", 0.0F}};
    stop.fadeSeconds = 0.0F;
    machine.transitions = {go, stop};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), machine,
                  GraphFixture::ClipNode(3U, fixture.AddClip(1.0F), true, "Idle"),
                  GraphFixture::ClipNode(4U, fixture.AddClip(10.0F), true, "Run")};

    EXPECT_NEAR(fixture.Step(tree, 0.5F), 0.5F, 1e-4F);
    EXPECT_EQ(tree.activeStates, "Idle");
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.1F)); // the move happens on this step
    EXPECT_EQ(tree.activeStates, "Run");
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-4F); // Run started from its beginning

    ASSERT_TRUE(SetAnimationGraphParameter(tree, "stop", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Idle");
    EXPECT_EQ(tree.parameters[1].value, 0.0F); // the trigger was used up
}

TEST(AnimationGraphTest, StateMachineCrossfadesAndCanWaitForTheEnd) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    AnimationGraphNode machine = GraphFixture::Node(2U, Kind::StateMachine, {3U, 4U});
    AnimationGraphTransition atEnd;
    atEnd.fromState = 0U;
    atEnd.toState = 1U;
    atEnd.conditions = {AnimationTransitionCondition{AnimationCondition::AtEnd, {}, 0.0F}};
    atEnd.fadeSeconds = 1.0F;
    machine.transitions = {atEnd};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), machine,
                  GraphFixture::ClipNode(3U, fixture.AddClip(10.0F), false, "Jump"),
                  GraphFixture::ClipNode(4U, fixture.AddClip(20.0F), true, "Land")};
    fixture.mixer.transition = AnimationTransitionMode::Crossfade;
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-4F);
    EXPECT_EQ(tree.activeStates, "Jump");
    EXPECT_NEAR(fixture.Step(tree, 0.6F), 10.0F, 1e-4F); // reached the end: the move is made
    EXPECT_EQ(tree.activeStates, "Land");
    // Half a second into a one-second fade: halfway from Jump's held end (10) to Land at 0.5 (10).
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 10.0F, 1e-4F);
    // Three quarters through the fade: Jump's 10 mixed 75% toward Land's 15.
    EXPECT_NEAR(fixture.Step(tree, 0.25F), 13.75F, 1e-3F);
}

TEST(AnimationGraphTest, StateMachineInertializesByDefault) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    AnimationGraphNode machine = GraphFixture::Node(2U, Kind::StateMachine, {3U, 4U});
    AnimationGraphTransition atEnd;
    atEnd.fromState = 0U;
    atEnd.toState = 1U;
    atEnd.conditions = {AnimationTransitionCondition{AnimationCondition::AtEnd, {}, 0.0F}};
    atEnd.fadeSeconds = 1.0F;
    machine.transitions = {atEnd};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), machine,
                  GraphFixture::ClipNode(3U, fixture.AddClip(10.0F), false, "Jump"),
                  GraphFixture::ClipNode(4U, fixture.AddClip(20.0F), true, "Land")};
    static_cast<void>(fixture.Step(tree, 0.5F));
    EXPECT_NEAR(fixture.Step(tree, 0.6F), 10.0F, 1e-4F) << "the step that switches still shows Jump";
    EXPECT_EQ(tree.activeStates, "Land");
    // Land alone would be at 10 after 0.5 s; the hand-over's 10 of difference is half decayed.
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 15.0F, 1e-3F);
    // Once the second is up only Land shows: 1.25 s wraps to 0.25 s, 5.
    static_cast<void>(fixture.Step(tree, 0.25F));
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-3F);
}

/// Idle (0 m) and Run (10 m per second) as a two-state machine from Idle to Run, crossfading.
[[nodiscard]] AnimationGraphComponent TwoStateMachine(GraphFixture& fixture, const AnimationGraphTransition& transition,
                                                          const double runSeconds = 1.0) {
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"speed", AnimationParameterType::Float, 0.0F},
                       AnimationParameter{"grounded", AnimationParameterType::Bool, 0.0F},
                       AnimationParameter{"jump", AnimationParameterType::Trigger, 0.0F}};
    AnimationGraphNode machine = GraphFixture::Node(2U, Kind::StateMachine, {3U, 4U});
    machine.transitions = {transition};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), machine,
                  GraphFixture::ClipNode(3U, fixture.AddClip(0.0F), true, "Idle"),
                  GraphFixture::ClipNode(4U, fixture.AddClip(10.0F * static_cast<float>(runSeconds), runSeconds), true, "Run")};
    fixture.mixer.transition = AnimationTransitionMode::Crossfade;
    return tree;
}

TEST(AnimationGraphTest, TransitionsNeedAllTheirConditionsAndKeepTheTriggerUntilTaken) {
    GraphFixture fixture;
    AnimationGraphTransition go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 0.0F;
    go.conditions = {AnimationTransitionCondition{AnimationCondition::Triggered, "jump", 0.0F},
                     AnimationTransitionCondition{AnimationCondition::ParameterTrue, "grounded", 0.0F}};
    AnimationGraphComponent tree = TwoStateMachine(fixture, go);
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "jump", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Idle") << "not grounded: the other condition fails";
    EXPECT_EQ(tree.parameters[2].value, 1.0F) << "so the trigger is still waiting";
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "grounded", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Run");
    EXPECT_EQ(tree.parameters[2].value, 0.0F) << "used up by the transition taken";
    EXPECT_EQ(tree.nodeStates[1].lastTransition, 0U);
}

TEST(AnimationGraphTest, AnOffTransitionIsNeverTaken) {
    GraphFixture fixture;
    AnimationGraphTransition go;
    go.fromState = 0U;
    go.toState = 1U;
    go.enabled = false;
    AnimationGraphComponent tree = TwoStateMachine(fixture, go);
    static_cast<void>(fixture.Step(tree, 0.1F));
    static_cast<void>(fixture.Step(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Idle");
}

TEST(AnimationGraphTest, LeaveAfterWaitsForThePhase) {
    GraphFixture fixture;
    AnimationGraphTransition go;
    go.fromState = 1U; // Run to Idle, once Run is three quarters through
    go.toState = 0U;
    go.fadeSeconds = 0.0F;
    go.exitPhase = 0.75F;
    AnimationGraphComponent tree = TwoStateMachine(fixture, go);
    tree.nodes[1].entryState = 1U;
    static_cast<void>(fixture.Step(tree, 0.5F));
    EXPECT_EQ(tree.activeStates, "Run") << "half way: not yet";
    static_cast<void>(fixture.Step(tree, 0.3F));
    EXPECT_EQ(tree.activeStates, "Idle") << "past 75%: taken";
}

TEST(AnimationGraphTest, InStepStartsTheNextStateAtThePhaseLeft) {
    GraphFixture fixture;
    AnimationGraphTransition go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 0.0F;
    go.start = AnimationTransitionStart::InStep;
    go.conditions = {AnimationTransitionCondition{AnimationCondition::ParameterGreater, "speed", 0.5F}};
    AnimationGraphComponent tree = TwoStateMachine(fixture, go, 2.0);
    static_cast<void>(fixture.Step(tree, 0.25F)); // Idle a quarter through its 1 s
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.0F));
    ASSERT_EQ(tree.activeStates, "Run");
    EXPECT_NEAR(tree.nodeStates[3].timeSeconds, 0.5, 1e-6) << "a quarter through Run's 2 s";
}

TEST(AnimationGraphTest, ContinuePicksUpWhereTheStateWasLeft) {
    GraphFixture fixture;
    AnimationGraphTransition back;
    back.fromState = 1U;
    back.toState = 0U;
    back.fadeSeconds = 0.0F;
    back.conditions = {AnimationTransitionCondition{AnimationCondition::ParameterLess, "speed", 0.5F}};
    AnimationGraphTransition go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 0.0F;
    go.start = AnimationTransitionStart::Continue;
    go.conditions = {AnimationTransitionCondition{AnimationCondition::ParameterGreater, "speed", 0.5F}};
    AnimationGraphComponent tree = TwoStateMachine(fixture, back, 4.0);
    tree.nodes[1].transitions.push_back(go);
    tree.nodes[1].entryState = 1U;
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.5F)); // Run at 0.5 s
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 0.0F));
    static_cast<void>(fixture.Step(tree, 0.0F));
    ASSERT_EQ(tree.activeStates, "Idle");
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.0F));
    ASSERT_EQ(tree.activeStates, "Run");
    EXPECT_NEAR(tree.nodeStates[3].timeSeconds, 0.5, 1e-6) << "Run carries on from 0.5 s";
}

TEST(AnimationGraphTest, AFadeThatCannotBeInterruptedHoldsTheMachine) {
    GraphFixture fixture;
    AnimationGraphTransition go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 1.0F;
    go.interruptible = false;
    go.conditions = {AnimationTransitionCondition{AnimationCondition::ParameterGreater, "speed", 0.5F}};
    AnimationGraphTransition back = go;
    back.fromState = 1U;
    back.toState = 0U;
    back.conditions = {AnimationTransitionCondition{AnimationCondition::ParameterLess, "speed", 0.5F}};
    AnimationGraphComponent tree = TwoStateMachine(fixture, go);
    tree.nodes[1].transitions.push_back(back);
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.1F));
    ASSERT_EQ(tree.activeStates, "Run");
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 0.0F));
    static_cast<void>(fixture.Step(tree, 0.4F));
    EXPECT_EQ(tree.activeStates, "Run") << "the fade is under way and may not be interrupted";
    static_cast<void>(fixture.Step(tree, 0.7F));
    EXPECT_EQ(tree.activeStates, "Idle") << "once it is over, the next one is taken";
}

TEST(AnimationGraphTest, CrossfadeCurvesShapeTheWeight) {
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeight(AnimationTransitionCurve::Linear, 0.25F), 0.25F);
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeight(AnimationTransitionCurve::EaseIn, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeight(AnimationTransitionCurve::EaseOut, 0.5F), 0.75F);
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeight(AnimationTransitionCurve::EaseInOut, 0.5F), 0.5F);
    for (const auto curve : {AnimationTransitionCurve::Linear, AnimationTransitionCurve::EaseIn,
                             AnimationTransitionCurve::EaseOut, AnimationTransitionCurve::EaseInOut}) {
        EXPECT_EQ(AnimationTransitionCurveWeight(curve, 0.0F), 0.0F);
        EXPECT_EQ(AnimationTransitionCurveWeight(curve, 1.0F), 1.0F);
        EXPECT_EQ(AnimationTransitionCurveWeight(curve, 2.0F), 1.0F) << "clamped";
    }
}

TEST(AnimationGraphTest, OneShotPlaysOverTheBaseAndHandsBack) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"attack", AnimationParameterType::Trigger, 0.0F}};
    AnimationGraphNode shot = GraphFixture::Node(2U, Kind::OneShot, {3U, 4U});
    shot.parameter = "attack";
    shot.fadeSeconds = 0.0F;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), shot,
                  GraphFixture::ClipNode(3U, fixture.AddClip(10.0F)),
                  GraphFixture::ClipNode(4U, fixture.AddClip(100.0F), false)};
    EXPECT_NEAR(fixture.Step(tree, 0.25F), 2.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "attack", 1.0F));
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 50.0F, 1e-4F);
    EXPECT_EQ(tree.parameters[0].value, 0.0F);
    static_cast<void>(fixture.Step(tree, 0.6F)); // the shot ends here
    EXPECT_NEAR(fixture.Step(tree, 0.1F), 4.5F, 1e-3F); // base alone again (1.45 s wrapped)
}

TEST(AnimationGraphTest, AClipStartsAtItsStartTime) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    AnimationGraphNode clip = GraphFixture::ClipNode(2U, fixture.AddClip(10.0F));
    clip.startSeconds = 0.5F;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), clip};
    EXPECT_NEAR(fixture.Step(tree, 0.1F), 6.0F, 1e-4F) << "0.5 s in, then 0.1 s on";
}

TEST(AnimationGraphTest, OneShotRestartsOnItsOwnAfterItsDelay) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"attack", AnimationParameterType::Trigger, 0.0F}};
    AnimationGraphNode shot = GraphFixture::Node(2U, Kind::OneShot, {3U, 4U});
    shot.parameter = "attack";
    shot.fadeSeconds = 0.0F;
    shot.autoRestart = true;
    shot.autoRestartDelaySeconds = 0.5F;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), shot,
                  GraphFixture::ClipNode(3U, fixture.AddClip(10.0F)),
                  GraphFixture::ClipNode(4U, fixture.AddClip(100.0F), false)};
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "attack", 1.0F));
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 50.0F, 1e-4F);
    static_cast<void>(fixture.Step(tree, 0.6F)); // the shot ends here
    EXPECT_LT(fixture.Step(tree, 0.2F), 20.0F) << "the base, while the delay runs";
    EXPECT_NEAR(fixture.Step(tree, 0.4F), 40.0F, 1e-3F) << "fired again on its own, 0.4 s in";
}

TEST(AnimationGraphTest, AdditiveLayersAndTimeScaleRescales) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    AnimationGraphNode additive = GraphFixture::Node(3U, Kind::Additive, {4U, 5U});
    additive.value = 0.5F;
    AnimationGraphNode scale = GraphFixture::Node(2U, Kind::TimeScale, {3U});
    scale.speed = 2.0F;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), scale, additive,
                  GraphFixture::ClipNode(4U, fixture.AddClip(10.0F)),
                  GraphFixture::ClipNode(5U, fixture.AddClip(2.0F))};
    // 0.25 s at double speed is 0.5 s in: base 5 plus half of the layer's 1.
    EXPECT_NEAR(fixture.Step(tree, 0.25F), 5.5F, 1e-4F);
}

TEST(AnimationGraphTest, ValidationCatchesMalformedGraphsButAllowsEmptySlots) {
    AnimationGraphComponent tree;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {0U})};
    EXPECT_TRUE(IsAnimationGraphComponentValid(tree)); // being built: nothing wired yet

    tree.nodes.push_back(GraphFixture::Node(2U, Kind::Output, {0U}));
    EXPECT_FALSE(IsAnimationGraphComponentValid(tree)); // two outputs

    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), GraphFixture::Node(2U, Kind::Blend2, {3U, 3U}),
                  GraphFixture::Node(3U, Kind::Clip)};
    EXPECT_FALSE(IsAnimationGraphComponentValid(tree)); // one object feeding two inputs

    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), GraphFixture::Node(2U, Kind::TimeScale, {3U}),
                  GraphFixture::Node(3U, Kind::TimeScale, {2U})};
    EXPECT_FALSE(IsAnimationGraphComponentValid(tree)); // a loop

    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace1D);
    space.blendPoints = {AnimationBlendPoint{{1.0F, 0.0F}}, AnimationBlendPoint{{0.0F, 0.0F}}};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    EXPECT_FALSE(IsAnimationGraphComponentValid(tree)); // points going backwards

    tree = AnimationGraphComponent{};
    tree.parameters = {AnimationParameter{"a", AnimationParameterType::Float, 0.0F},
                       AnimationParameter{"a", AnimationParameterType::Bool, 0.0F}};
    EXPECT_FALSE(IsAnimationGraphComponentValid(tree)); // two parameters with one name
}

TEST(AnimationSequencerTest, SampleAnimationTrackOnEmptyVectorReturnsIdentityPose) {
    const std::vector<Asset::AnimationAssetSample> empty;
    const Core::TransformPose pose = SampleAnimationTrack(empty, 0.5);
    EXPECT_EQ(pose.position, (Math::Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(pose.scale, (Math::Vector3{1.0F, 1.0F, 1.0F}));
}

TEST(AnimationGraphTest, CyclicGraphTerminatesSafelyAndSameSizeRewireInvalidatesCache) {
    GraphFixture fixture;
    // 1. Cyclic graph evaluated directly must terminate cleanly without stack overflow.
    AnimationGraphComponent cyclic;
    cyclic.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}),
                    GraphFixture::Node(2U, Kind::TimeScale, {3U}),
                    GraphFixture::Node(3U, Kind::TimeScale, {2U})};
    TransformComponent target;
    AnimationDriverComponent driver;
    EXPECT_FALSE(StepAnimationGraph(cyclic, fixture.Resolver(), 0.1F, target, driver));

    // 2. Rewiring inputs while keeping nodes.size() identical must invalidate topology cache.
    const Asset::AssetGuid clipA = fixture.AddClip(10.0F);
    const Asset::AssetGuid clipB = fixture.AddClip(40.0F);
    AnimationGraphComponent tree;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}),
                  GraphFixture::ClipNode(2U, clipA),
                  GraphFixture::ClipNode(3U, clipB)};
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-4F);
    // Rewire Output from node 2 to node 3 with identical nodes.size() == 3.
    tree.nodes[0].inputs = {3U};
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 20.0F, 1e-4F);
}

// ---- Skeletal clips ---------------------------------------------------------------------------

/// Two bones at rest one metre apart; a one-second clip that raises "Hips" by `lift` and has no
/// track for "Spine".
[[nodiscard]] Skeleton3DComponent MakeTwoBoneSkeleton() {
    Skeleton3DComponent skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";
    SkeletonBone hips;
    hips.name = "Hips";
    hips.localPosition = Math::Vector3{0.0F, 1.0F, 0.0F};
    SkeletonBone spine;
    spine.name = "Spine";
    spine.parentIndex = 0;
    spine.localPosition = Math::Vector3{0.0F, 0.3F, 0.0F};
    skeleton.bones = {hips, spine};
    return skeleton;
}

[[nodiscard]] Asset::AnimationClipAsset MakeLiftClip(const float lift) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "lift";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample start;
    start.pose.position = Math::Vector3{0.0F, 1.0F, 0.0F};
    Asset::AnimationAssetSample end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3{0.0F, 1.0F + lift, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {start, end}}};
    return clip;
}

TEST(AnimationSequencerTest, ASkeletalClipPosesEachBoneByName) {
    const Asset::AnimationClipAsset clip = MakeLiftClip(1.0F);
    Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    EXPECT_EQ(GetSkeletonCurrentPose(skeleton).size(), 2U) << "no pose yet means the rest pose";
    AnimationSequencerComponent settings;
    settings.loopMode = AnimationLoopMode::Once;
    AnimationSequencerComponent player = Started(settings, TransformComponent{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.5F, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 2U);
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.5F, 1e-4F) << "halfway up";
    EXPECT_NEAR(skeleton.pose[1].position.y, 0.3F, 1e-4F) << "no track: the rest pose";
    EXPECT_EQ(skeleton, MakeTwoBoneSkeleton()) << "the pose is runtime state, not an edit";

    // The end of a Once clip holds its last pose.
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 1.0F, skeleton));
    EXPECT_FALSE(player.isPlaying);
    EXPECT_TRUE(player.finished);
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-4F);
}

TEST(AnimationSequencerTest, ASkeletalClipBlendsInReturnsToRestAndHonoursTheMixer) {
    const Asset::AnimationClipAsset clip = MakeLiftClip(2.0F);
    // Blend In: half the blend time in, the pose is halfway from rest to the clip.
    Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    AnimationSequencerComponent settings;
    settings.blendInSeconds = 1.0F;
    settings.startOffsetSeconds = 1.0F; // the clip holds y = 3 from here on
    settings.loopMode = AnimationLoopMode::Once;
    AnimationSequencerComponent player = Started(settings, TransformComponent{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.5F, skeleton));
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-4F);

    // Return To Start puts every bone back at rest when the clip ends.
    Skeleton3DComponent back = MakeTwoBoneSkeleton();
    settings = AnimationSequencerComponent{};
    settings.loopMode = AnimationLoopMode::Once;
    settings.onFinish = AnimationFinishAction::ReturnToStart;
    player = Started(settings, TransformComponent{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 2.0F, back));
    EXPECT_NEAR(back.pose[0].position.y, 1.0F, 1e-4F);

    // A mixer that leaves position alone leaves it at rest.
    Skeleton3DComponent masked = MakeTwoBoneSkeleton();
    AnimationDriverComponent mixer;
    mixer.animatePosition = false;
    player = Started(AnimationSequencerComponent{}, TransformComponent{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.5F, masked, mixer));
    EXPECT_NEAR(masked.pose[0].position.y, 1.0F, 1e-4F);

    // A clip with no bone tracks cannot pose a skeleton.
    Asset::AnimationClipAsset objectClip = MakeSlideClip();
    player = Started(AnimationSequencerComponent{}, TransformComponent{}, objectClip);
    EXPECT_FALSE(StepSkeletalAnimationSequencer(player, objectClip, 0.1F, masked));
    EXPECT_FALSE(player.isPlaying);
}

/// Hips travel 2 m forward (+Z) over one second while a still root bone stays put.
[[nodiscard]] Asset::AnimationClipAsset MakeRunClip() {
    Asset::AnimationClipAsset clip;
    clip.clipId = "run";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample start;
    start.pose.position = Math::Vector3{0.0F, 1.0F, 0.0F};
    Asset::AnimationAssetSample end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3{0.0F, 1.0F, 2.0F};
    Asset::AnimationAssetSample spine;
    spine.pose.position = Math::Vector3{0.0F, 0.3F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Spine", {spine}},
                  Asset::AnimationAssetBoneTrack{"Hips", {start, end}}};
    return clip;
}

TEST(AnimationSequencerTest, RootMotionPicksTheTravellingBone) {
    const Asset::AnimationClipAsset clip = MakeRunClip();
    const Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    EXPECT_EQ(ResolveRootMotionBone(skeleton, clip, ""), std::optional<std::size_t>{0U});
    EXPECT_EQ(ResolveRootMotionBone(skeleton, clip, "Spine"), std::optional<std::size_t>{1U});
    EXPECT_EQ(ResolveRootMotionBone(skeleton, clip, "Tail"), std::nullopt) << "no such bone";
    EXPECT_EQ(ResolveRootMotionBone(skeleton, MakeLiftClip(1.0F), ""), std::nullopt)
        << "moving only up is not ground travel";
}

TEST(AnimationSequencerTest, RootMotionFindsTheRootBoneUnderANamespace) {
    // A rig whose bones carry a namespace ("mixamorig:Hips") plays a clip naming them bare, and the
    // other way round: root motion finds the bone the pose does.
    Asset::AnimationClipAsset clip = MakeRunClip();
    Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    skeleton.bones[0].name = "mixamorig:Hips";
    EXPECT_EQ(ResolveRootMotionBone(skeleton, clip, ""), std::optional<std::size_t>{0U});
    EXPECT_EQ(ResolveRootMotionBone(skeleton, clip, "hips"), std::optional<std::size_t>{0U});
    clip.bones[1].bone = "mixamorig:Hips";
    skeleton.bones[0].name = "Hips";
    EXPECT_EQ(ResolveRootMotionBone(skeleton, clip, ""), std::optional<std::size_t>{0U});

    AnimationDriverComponent mixer;
    mixer.rootMotion = AnimationRootMotionMode::ApplyToTarget;
    AnimationSequencerComponent player = Started(AnimationSequencerComponent{}, TransformComponent{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.25F, skeleton, mixer));
    EXPECT_GT(player.rootMotionDelta.z, 0.1F) << "the namespaced track still gives root motion";
}

TEST(AnimationSequencerTest, RootMotionKeepsThePoseInPlaceAndReportsTravelAcrossTheLoop) {
    const Asset::AnimationClipAsset clip = MakeRunClip();
    Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    AnimationDriverComponent mixer;
    mixer.rootMotion = AnimationRootMotionMode::ApplyToTarget;
    AnimationSequencerComponent player = Started(AnimationSequencerComponent{}, TransformComponent{}, clip);

    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.25F, skeleton, mixer));
    EXPECT_NEAR(player.rootMotionDelta.z, 0.5F, 1e-4F);
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "the travel is taken out of the pose";
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.0F, 1e-4F) << "height stays in the pose";

    // 0.25 -> 0.85 is 1.2 m; 0.85 wraps to 0.1: 0.3 m to the end plus 0.2 m from the start.
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.6F, skeleton, mixer));
    EXPECT_NEAR(player.rootMotionDelta.z, 1.2F, 1e-4F);
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.25F, skeleton, mixer));
    EXPECT_NEAR(player.currentTimeSeconds, 0.1F, 1e-4F);
    EXPECT_NEAR(player.rootMotionDelta.z, 0.5F, 1e-4F) << "never a jump back at the wrap";

    // Off: the bone travels in the pose and nothing is reported.
    Skeleton3DComponent authored = MakeTwoBoneSkeleton();
    player = Started(AnimationSequencerComponent{}, TransformComponent{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.25F, authored));
    EXPECT_NEAR(authored.pose[0].position.z, 0.5F, 1e-4F);
    EXPECT_NEAR(player.rootMotionDelta.z, 0.0F, 1e-6F);
}

TEST(AnimationSequencerTest, ScrubbingPosesTheSkeletonAtATimeWithoutAPlayer) {
    const Asset::AnimationClipAsset clip = MakeRunClip();
    Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    ASSERT_TRUE(PoseSkeletonAtTime(clip, 0.5, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 2U);
    EXPECT_NEAR(skeleton.pose[0].position.z, 1.0F, 1e-4F);
    EXPECT_NEAR(skeleton.pose[1].position.y, 0.3F, 1e-4F);
    ASSERT_TRUE(PoseSkeletonAtTime(clip, 9.0, skeleton)) << "past the end holds the last frame";
    EXPECT_NEAR(skeleton.pose[0].position.z, 2.0F, 1e-4F);

    // With root motion on, the scrubbed pose runs in place like the played one.
    AnimationDriverComponent mixer;
    mixer.rootMotion = AnimationRootMotionMode::InPlace;
    ASSERT_TRUE(PoseSkeletonAtTime(clip, 0.5, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F);
    EXPECT_EQ(skeleton, MakeTwoBoneSkeleton()) << "scrubbing is not an edit";

    // A clip with no bones leaves the skeleton alone.
    Skeleton3DComponent untouched = MakeTwoBoneSkeleton();
    EXPECT_FALSE(PoseSkeletonAtTime(MakeSlideClip(), 0.5, untouched));
    EXPECT_TRUE(untouched.pose.empty());
}

TEST(AnimationSequencerTest, CollectsTheEventsThePlayheadPasses) {
    const std::vector<Asset::AnimationAssetEvent> events = {
        {0.75, "right"}, {0.0, "start"}, {0.25, "left"}};
    using List = std::vector<std::string>;
    EXPECT_EQ(CollectPassedAnimationEvents(events, 0.0, 0.5, 1.0, true, false, true), (List{"start", "left"}));
    EXPECT_EQ(CollectPassedAnimationEvents(events, 0.0, 0.5, 1.0, true, false, false), (List{"left"}))
        << "the start event only on the first step";
    EXPECT_EQ(CollectPassedAnimationEvents(events, 0.25, 0.5, 1.0, true, false, false), (List{}))
        << "an event already passed is not passed again";
    EXPECT_EQ(CollectPassedAnimationEvents(events, 0.5, 0.3, 1.0, true, true, false), (List{"right", "start", "left"}))
        << "a loop's wrap passes the end, then the start";
    EXPECT_EQ(CollectPassedAnimationEvents(events, 0.8, 0.1, 1.0, false, false, false), (List{"right", "left"}))
        << "backwards, in the order passed";
}

TEST(AnimationSequencerTest, InertializationCarriesTheOldPoseAndFadesItOutSmoothly) {
    // A clip that holds the hips still at z = 0; the skeleton was left at z = 5 by the last clip.
    Asset::AnimationClipAsset clip;
    clip.clipId = "idle";
    clip.durationSeconds = 2.0;
    Asset::AnimationAssetSample still;
    still.pose.position = Math::Vector3{0.0F, 1.0F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {still}}};
    Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    skeleton.pose = GetSkeletonCurrentPose(skeleton);
    skeleton.pose[0].position = Math::Vector3{0.0F, 1.0F, 5.0F};

    AnimationSequencerComponent settings;
    settings.blendInSeconds = 1.0F;
    AnimationSequencerComponent player = Started(settings, TransformComponent{}, clip);
    AnimationDriverComponent mixer; // Inertialize is the default
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.001F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 5.0F, 1e-3F) << "no pop: it starts where it was";
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.499F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 2.5F, 1e-3F) << "halfway through the fade, half the difference";
    ASSERT_TRUE(StepSkeletalAnimationSequencer(player, clip, 0.5F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "the new clip, exactly, when the blend ends";
    EXPECT_TRUE(player.inertialPosition.empty()) << "the carried difference is dropped once it has faded";
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

// ---- AnimationGraph on a skeleton ---------------------------------------------------------------

[[nodiscard]] AnimationGraphNode TreeNode(const std::uint32_t id, const AnimationGraphNodeKind kind,
                                                std::vector<std::uint32_t> inputs = {}) {
    AnimationGraphNode object;
    object.id = id;
    object.kind = kind;
    object.inputs = std::move(inputs);
    return object;
}

[[nodiscard]] AnimationGraphNode TreeClip(const std::uint32_t id, const Asset::AssetGuid clip) {
    AnimationGraphNode object = TreeNode(id, AnimationGraphNodeKind::Clip);
    object.clip = clip;
    return object;
}

/// "Hips" walking along X from 0 to `distance` over `duration`, one sample per tenth.
[[nodiscard]] Asset::AnimationClipAsset MakeWalkClip(const float distance, const double duration = 1.0) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = duration;
    std::vector<Asset::AnimationAssetSample> samples;
    for (int step = 0; step <= 10; ++step) {
        Asset::AnimationAssetSample sample;
        sample.timeSeconds = duration * step / 10.0;
        sample.pose.position = Math::Vector3{distance * static_cast<float>(step) / 10.0F, 1.0F, 0.0F};
        samples.push_back(sample);
    }
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", samples}};
    return clip;
}

struct SkeletalGraph {
    std::unordered_map<std::uint64_t, Asset::AnimationClipAsset> clips;
    Skeleton3DComponent skeleton = MakeTwoBoneSkeleton();
    AnimationDriverComponent mixer;

    Asset::AssetGuid Add(Asset::AnimationClipAsset clip) {
        const Asset::AssetGuid guid{clips.size() + 500U};
        clips.emplace(guid.value, std::move(clip));
        return guid;
    }

    bool Step(AnimationGraphComponent& tree, const float seconds) {
        EXPECT_TRUE(IsAnimationGraphComponentValid(tree)) << DescribeAnimationGraphProblem(tree);
        return StepSkeletalAnimationGraph(
            tree,
            [this](const Asset::AssetGuid guid) -> const Asset::AnimationClipAsset* {
                const auto found = clips.find(guid.value);
                return found == clips.end() ? nullptr : &found->second;
            },
            seconds, skeleton, mixer);
    }
};

TEST(AnimationGraphTest, ASkeletalTreeBlendsBoneByBoneAndKeepsUntrackedBonesAtRest) {
    SkeletalGraph graph;
    AnimationGraphComponent tree;
    AnimationGraphNode blend = TreeNode(2U, AnimationGraphNodeKind::Blend2, {3U, 4U});
    blend.value = 0.5F;
    tree.nodes = {TreeNode(1U, AnimationGraphNodeKind::Output, {2U}), blend,
                  TreeClip(3U, graph.Add(MakeLiftClip(0.0F))), TreeClip(4U, graph.Add(MakeLiftClip(2.0F)))};
    ASSERT_TRUE(graph.Step(tree, 0.5F));
    ASSERT_EQ(graph.skeleton.pose.size(), 2U);
    // Lift 0 and lift 2 at half time (0 and 1), mixed half and half: hips at 1 + 0.5.
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.5F, 1e-4F);
    EXPECT_NEAR(graph.skeleton.pose[1].position.y, 0.3F, 1e-6F) << "no track: the rest pose";
    EXPECT_NEAR(tree.nodeStates[2].weight, 0.5F, 1e-6F) << "each clip counted half";
}

TEST(AnimationGraphTest, ASkeletalTreeHandsOverRootMotion) {
    SkeletalGraph graph;
    graph.mixer.rootMotion = AnimationRootMotionMode::InPlace;
    AnimationGraphComponent tree;
    tree.nodes[1].clip = graph.Add(MakeWalkClip(2.0F));
    ASSERT_TRUE(graph.Step(tree, 0.0F));
    ASSERT_TRUE(graph.Step(tree, 0.25F));
    EXPECT_NEAR(tree.rootMotionDelta.x, 0.5F, 1e-4F);
    EXPECT_NEAR(graph.skeleton.pose[0].position.x, 0.0F, 1e-6F) << "the hips stay over their start";
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-6F) << "only the ground travel is taken";
    ASSERT_TRUE(graph.Step(tree, 0.9F)); // 0.25 -> 1.15, wrapping at 1
    EXPECT_NEAR(tree.rootMotionDelta.x, 1.8F, 1e-3F) << "the travel carries across the wrap";
}

TEST(AnimationGraphTest, SyncedBlendKeepsItsInputsInStep) {
    SkeletalGraph graph;
    AnimationGraphComponent tree;
    AnimationGraphNode blend = TreeNode(2U, AnimationGraphNodeKind::Blend2, {3U, 4U});
    blend.value = 0.25F;
    blend.sync = true;
    tree.nodes = {TreeNode(1U, AnimationGraphNodeKind::Output, {2U}), blend,
                  TreeClip(3U, graph.Add(MakeWalkClip(1.0F, 1.0))),
                  TreeClip(4U, graph.Add(MakeWalkClip(4.0F, 2.0)))};
    ASSERT_TRUE(graph.Step(tree, 0.4F));
    // The 1 s walk leads (it counts 75%) at 40%; the 2 s run is put at 40% too, not at 0.4 s.
    EXPECT_NEAR(tree.nodeStates[2].timeSeconds, 0.4, 1e-6);
    EXPECT_NEAR(tree.nodeStates[3].timeSeconds, 0.8, 1e-6);
    blend.sync = false;
    tree.nodes[1] = blend;
    tree.nodeStates.clear();
    ASSERT_TRUE(graph.Step(tree, 0.4F));
    EXPECT_NEAR(tree.nodeStates[3].timeSeconds, 0.4, 1e-6) << "unsynced, each runs on its own clock";
}

TEST(AnimationGraphTest, EventsComeFromTheClipsThatCount) {
    SkeletalGraph graph;
    Asset::AnimationClipAsset step = MakeWalkClip(1.0F);
    step.events = {Asset::AnimationAssetEvent{0.5, "footstep"}};
    Asset::AnimationClipAsset wave = MakeWalkClip(1.0F);
    wave.events = {Asset::AnimationAssetEvent{0.5, "wave"}};
    AnimationGraphComponent tree;
    AnimationGraphNode blend = TreeNode(2U, AnimationGraphNodeKind::Blend2, {3U, 4U});
    blend.value = 0.2F;
    tree.nodes = {TreeNode(1U, AnimationGraphNodeKind::Output, {2U}), blend, TreeClip(3U, graph.Add(step)),
                  TreeClip(4U, graph.Add(wave))};
    ASSERT_TRUE(graph.Step(tree, 0.3F));
    EXPECT_TRUE(tree.firedEvents.empty());
    ASSERT_TRUE(graph.Step(tree, 0.3F));
    EXPECT_EQ(tree.firedEvents, (std::vector<std::string>{"footstep"})) << "the 20% clip's event is not raised";
}

TEST(AnimationGraphTest, ASkeletalTreeWritesOnlyTheMixersChannels) {
    SkeletalGraph graph;
    graph.mixer.animatePosition = false;
    AnimationGraphComponent tree;
    tree.nodes[1].clip = graph.Add(MakeLiftClip(1.0F));
    ASSERT_TRUE(graph.Step(tree, 0.5F));
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-6F) << "position is not animated";
    graph.skeleton.bones.clear();
    EXPECT_FALSE(graph.Step(tree, 0.1F)) << "no bones, nothing to pose";
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

// ---- New objects: Blend Space 2D, Select, Layered Blend, Time Seek -------------------------------

TEST(AnimationGraphTest, BlendSpace2DTriangulatesItsPoints) {
    EXPECT_TRUE(TriangulateBlendSpace({{0.0F, 0.0F}, {1.0F, 0.0F}}).empty()) << "two points make no triangle";
    EXPECT_TRUE(TriangulateBlendSpace({{0.0F, 0.0F}, {1.0F, 0.0F}, {2.0F, 0.0F}, {3.0F, 0.0F}}).empty())
        << "points on one line make none";
    const auto one = TriangulateBlendSpace({{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}});
    ASSERT_EQ(one.size(), 1U);
    EXPECT_EQ(one[0], (std::array<std::uint32_t, 3>{0U, 1U, 2U})) << "counter-clockwise, lowest index first";
    EXPECT_EQ(TriangulateBlendSpace({{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}}).size(), 2U);
    // Idle in the middle, walks at the four sides: a fan of four around the centre.
    const std::vector<Math::Vector2> cross{{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {-1.0F, 0.0F}};
    const auto fan = TriangulateBlendSpace(cross);
    ASSERT_EQ(fan.size(), 4U);
    for (const auto& triangle : fan) {
        EXPECT_EQ(triangle[0], 0U) << "every triangle has the centre";
    }
    EXPECT_EQ(TriangulateBlendSpace(cross), fan) << "the same points give the same triangles";
}

TEST(AnimationGraphTest, BlendSpace2DWeightsAreTheTrianglesCorners) {
    const std::vector<Math::Vector2> points{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
    const auto triangles = TriangulateBlendSpace(points);
    const std::vector<float> onPoint = AnimationBlendSpace2DWeights(points, triangles, Math::Vector2{1.0F, 0.0F});
    EXPECT_NEAR(onPoint[1], 1.0F, 1e-5F) << "on a point: that point alone";
    const std::vector<float> inside = AnimationBlendSpace2DWeights(points, triangles, Math::Vector2{0.25F, 0.25F});
    EXPECT_NEAR(inside[0], 0.5F, 1e-5F);
    EXPECT_NEAR(inside[1], 0.25F, 1e-5F);
    EXPECT_NEAR(inside[2], 0.25F, 1e-5F);
    // Outside: the nearest place on the outline, here halfway along the long edge.
    const std::vector<float> outside = AnimationBlendSpace2DWeights(points, triangles, Math::Vector2{2.0F, 2.0F});
    EXPECT_NEAR(outside[0], 0.0F, 1e-5F);
    EXPECT_NEAR(outside[1], 0.5F, 1e-5F);
    EXPECT_NEAR(outside[2], 0.5F, 1e-5F);
    const std::vector<float> none = AnimationBlendSpace2DWeights(points, {}, Math::Vector2{0.25F, 0.25F});
    EXPECT_EQ(none, (std::vector<float>{0.0F, 0.0F, 0.0F})) << "no triangles, no blend";
}

TEST(AnimationGraphTest, BlendSpace2DMixesInsideItsTriangleAndNeedsOne) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"x", AnimationParameterType::Float, 0.25F}};
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace2D);
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(0.0F)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(40.0F)}};
    space.parameter = "x";
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    TransformComponent target;
    EXPECT_FALSE(StepAnimationGraph(tree, fixture.Resolver(), 0.5F, target, fixture.mixer))
        << "two points are a line, not a plane: nothing plays";
    tree.nodes[1].blendPoints.push_back(AnimationBlendPoint{{0.0F, 1.0F}, fixture.AddClip(0.0F)});
    tree.nodeStates.clear();
    // A quarter of the way to the 40 m clip, which is at 20 after half a second: 5.
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-3F);
    ASSERT_EQ(tree.nodeStates[1].triangles.size(), 1U) << "triangulated once and kept";
    EXPECT_NEAR(tree.nodeStates[1].pointWeights[1], 0.25F, 1e-5F);
}

TEST(AnimationGraphTest, BlendSpace2DTakesItsPositionFromOneVector3Parameter) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    AnimationParameter walk{"Walk", AnimationParameterType::Vector3, 0.0F};
    walk.vector = Math::Vector3{0.25F, 0.0F, 0.0F};
    tree.parameters = {walk};
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace2D);
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(0.0F)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(40.0F)},
                         AnimationBlendPoint{{0.0F, 1.0F}, fixture.AddClip(0.0F)}};
    space.parameter = "Walk";   // named after the node, as the editor makes it
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-3F) << "x 0.25 of the way to the 40 m clip";
}

TEST(AnimationGraphTest, AClipPlaysAtItsParametersRate) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"Run", AnimationParameterType::Float, 2.0F}};
    AnimationGraphNode clip = GraphFixture::ClipNode(2U, fixture.AddClip(10.0F));
    clip.parameter = "Run";
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), clip};
    EXPECT_NEAR(fixture.Step(tree, 0.1F), 2.0F, 1e-4F) << "0.1 s at double rate";
}

TEST(AnimationGraphTest, SmoothingEasesThePositionWithoutOvershoot) {
    Math::Vector2 at{};
    Math::Vector2 velocity{};
    SmoothBlendPosition(at, velocity, Math::Vector2{1.0F, 2.0F}, 0.5F, 0.5F);
    EXPECT_NEAR(at.x, 0.5F, 1e-4F) << "half the gap closed after one half-life";
    EXPECT_NEAR(at.y, 1.0F, 1e-4F);
    Math::Vector2 stepped{};
    Math::Vector2 steppedVelocity{};
    float previous = 0.0F;
    for (int step = 0; step < 300; ++step) {
        SmoothBlendPosition(stepped, steppedVelocity, Math::Vector2{1.0F, 2.0F}, 0.5F, 0.01F);
        EXPECT_GE(stepped.x, previous) << "it only ever moves toward the goal";
        EXPECT_LE(stepped.x, 1.0F) << "and never past it";
        previous = stepped.x;
        if (step == 49) {
            EXPECT_NEAR(stepped.x, 0.5F, 1e-3F) << "the same after fifty small steps as after one big one";
        }
    }
    Math::Vector2 instant{};
    Math::Vector2 instantVelocity{3.0F, 3.0F};
    SmoothBlendPosition(instant, instantVelocity, Math::Vector2{4.0F, 5.0F}, 0.0F, 0.016F);
    EXPECT_EQ(instant, (Math::Vector2{4.0F, 5.0F})) << "no smoothing: straight there";
    EXPECT_EQ(instantVelocity, (Math::Vector2{}));
}

TEST(AnimationGraphTest, SmoothedBlendSpaceFollowsItsParameterOverTime) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"speed", AnimationParameterType::Float, 0.0F}};
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.smoothingSeconds = 0.25F;
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(10.0F)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(20.0F)}};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    static_cast<void>(fixture.Step(tree, 0.0F));
    EXPECT_EQ(tree.nodeStates[1].blendAt.x, 0.0F) << "starts where its parameter is";
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.25F));
    EXPECT_NEAR(tree.nodeStates[1].blendAt.x, 0.5F, 1e-3F) << "halfway after one half-life";
    EXPECT_NEAR(tree.nodeStates[1].pointWeights[1], 0.5F, 1e-3F);
}

TEST(AnimationGraphTest, NearestPlaysOnePointAndSwitchesOnlyWhenClearlyNearer) {
    GraphFixture fixture;
    fixture.mixer.transition = AnimationTransitionMode::Crossfade;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"speed", AnimationParameterType::Float, 0.2F}};
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.blendMode = AnimationBlendMode::Nearest;
    space.fadeSeconds = 0.0F;
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(10.0F)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(20.0F)}};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-4F) << "the near point alone, not a mix";
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 0.52F));
    static_cast<void>(fixture.Step(tree, 0.1F));
    EXPECT_EQ(tree.nodeStates[1].nearestPoint, 0U) << "just past halfway is not clearly nearer: no flicker";
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 0.9F));
    static_cast<void>(fixture.Step(tree, 0.1F));
    EXPECT_EQ(tree.nodeStates[1].nearestPoint, 1U);
    EXPECT_NEAR(fixture.Step(tree, 0.25F), 5.0F, 1e-4F) << "the new point starts from its beginning: 20 m at 0.25";
}

TEST(AnimationGraphTest, NearestInStepCarriesThePhaseAcross) {
    GraphFixture fixture;
    fixture.mixer.transition = AnimationTransitionMode::Crossfade;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"speed", AnimationParameterType::Float, 0.0F}};
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.blendMode = AnimationBlendMode::NearestInStep;
    space.fadeSeconds = 0.0F;
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(10.0F, 1.0)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(10.0F, 2.0)}};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    static_cast<void>(fixture.Step(tree, 0.25F)); // a quarter through the 1 s cycle
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.0F));
    EXPECT_EQ(tree.nodeStates[1].nearestPoint, 1U);
    EXPECT_NEAR(tree.nodeStates[1].pointTimes[1], 0.5, 1e-6) << "a quarter through the 2 s cycle too";
}

TEST(AnimationGraphTest, NearestInertializesTheSwitchSoThereIsNoPop) {
    GraphFixture fixture; // the mixer inertializes by default
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"speed", AnimationParameterType::Float, 0.0F}};
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.blendMode = AnimationBlendMode::Nearest;
    space.fadeSeconds = 0.5F;
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(10.0F)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(-10.0F)}};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    const float before = fixture.Step(tree, 0.5F); // 5 m along the first clip
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "speed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.0F)); // the switch is seen here
    const float right = fixture.Step(tree, 0.01F);
    EXPECT_NEAR(right, before, 0.5F) << "just after the switch the pose is still where it was";
    const float settled = fixture.Step(tree, 0.6F);
    EXPECT_NEAR(settled, -6.1F, 1e-3F) << "and once the hand-over ends, the new point alone";
}

TEST(AnimationGraphTest, SelectPlaysThePickedInputAndFadesOnChange) {
    GraphFixture fixture;
    fixture.mixer.transition = AnimationTransitionMode::Crossfade;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"armed", AnimationParameterType::Bool, 0.0F}};
    AnimationGraphNode select = GraphFixture::Node(2U, Kind::Select, {3U, 4U});
    select.parameter = "armed";
    select.fadeSeconds = 1.0F;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), select,
                  GraphFixture::ClipNode(3U, fixture.AddClip(10.0F)),
                  GraphFixture::ClipNode(4U, fixture.AddClip(20.0F))};
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-4F) << "option 0";
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "armed", 1.0F));
    static_cast<void>(fixture.Step(tree, 0.0F)); // the pick is seen; option 1 restarts
    // Half a second into a one-second fade: option 0 at 1.0 s wraps to 0 -> 0, option 1 at 0.5 -> 10.
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 5.0F, 1e-3F);
    EXPECT_NEAR(fixture.Step(tree, 0.5F), 0.0F, 1e-3F) << "fade done: option 1 alone, wrapped to 0";
}

TEST(AnimationGraphTest, LayeredBlendTouchesOnlyTheChosenBranch) {
    SkeletalGraph graph;
    // Three bones: Hips > Spine > Head. The layer lifts every bone; only Spine's branch takes it.
    SkeletonBone head;
    head.name = "Head";
    head.parentIndex = 1;
    head.localPosition = Math::Vector3{0.0F, 0.2F, 0.0F};
    graph.skeleton.bones.push_back(head);
    Asset::AnimationClipAsset lifted;
    lifted.clipId = "lifted";
    lifted.durationSeconds = 1.0;
    for (const auto& [bone, y] : {std::pair<const char*, float>{"Hips", 2.0F}, {"Spine", 1.3F}, {"Head", 1.2F}}) {
        Asset::AnimationAssetSample sample;
        sample.pose.position = Math::Vector3{0.0F, y, 0.0F};
        lifted.bones.push_back(Asset::AnimationAssetBoneTrack{bone, {sample}});
    }
    AnimationGraphComponent tree;
    AnimationGraphNode layered = TreeNode(2U, AnimationGraphNodeKind::LayeredBlend, {3U, 4U});
    layered.value = 1.0F;
    layered.bones = {"Spine"};
    tree.nodes = {TreeNode(1U, AnimationGraphNodeKind::Output, {2U}), layered,
                  TreeClip(3U, graph.Add(MakeLiftClip(0.0F))), TreeClip(4U, graph.Add(lifted))};
    ASSERT_TRUE(graph.Step(tree, 0.1F));
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-5F) << "Hips: the base";
    EXPECT_NEAR(graph.skeleton.pose[1].position.y, 1.3F, 1e-5F) << "Spine: the layer";
    EXPECT_NEAR(graph.skeleton.pose[2].position.y, 1.2F, 1e-5F) << "Head, under Spine: the layer";
}

TEST(AnimationGraphTest, TimeSeekJumpsWhenItsTriggerFires) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    tree.parameters = {AnimationParameter{"rewind", AnimationParameterType::Trigger, 0.0F}};
    AnimationGraphNode seek = GraphFixture::Node(2U, Kind::TimeSeek, {3U});
    seek.parameter = "rewind";
    seek.value = 0.25F;
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), seek,
                  GraphFixture::ClipNode(3U, fixture.AddClip(10.0F))};
    EXPECT_NEAR(fixture.Step(tree, 0.75F), 7.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameter(tree, "rewind", 1.0F));
    EXPECT_NEAR(fixture.Step(tree, 0.1F), 2.5F, 1e-4F) << "at the seek point this step";
    EXPECT_EQ(tree.parameters[0].value, 0.0F) << "the trigger was used";
    EXPECT_NEAR(fixture.Step(tree, 0.1F), 3.5F, 1e-4F) << "and plays on from there";
}

TEST(AnimationGraphTest, TheNewKindsAreValidatedToo) {
    AnimationGraphComponent tree;
    AnimationGraphNode space;
    space.id = 3U;
    space.kind = AnimationGraphNodeKind::BlendSpace2D;
    space.inputs = {0U};
    tree.nodes.push_back(space);
    EXPECT_NE(DescribeAnimationGraphProblem(tree).find("wrong number of inputs"), std::string::npos)
        << "a blend space has no inputs: its points hold the animations";
    tree.nodes.back().inputs.clear();
    tree.nodes.back().blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}}, AnimationBlendPoint{{0.0F, 0.0F}}};
    EXPECT_NE(DescribeAnimationGraphProblem(tree).find("same place"), std::string::npos);
    tree.nodes.back().blendPoints.back().position = Math::Vector2{1.0F, 0.0F};
    EXPECT_TRUE(DescribeAnimationGraphProblem(tree).empty()) << DescribeAnimationGraphProblem(tree);
    AnimationGraphNode layered;
    layered.id = 4U;
    layered.kind = AnimationGraphNodeKind::LayeredBlend;
    layered.inputs = {0U};
    tree.nodes.push_back(layered);
    EXPECT_NE(DescribeAnimationGraphProblem(tree).find("wrong number of inputs"), std::string::npos);
    tree.nodes.back().inputs = {0U, 0U};
    tree.nodes.back().bones = {""};
    EXPECT_NE(DescribeAnimationGraphProblem(tree).find("layer bone"), std::string::npos);
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

TEST(AnimationGraphTest, SyncedBlendSpacePointsKeepInStep) {
    GraphFixture fixture;
    AnimationGraphComponent tree;
    AnimationGraphNode space = GraphFixture::Node(2U, Kind::BlendSpace1D);
    space.value = 0.25F;
    space.sync = true;
    space.blendPoints = {AnimationBlendPoint{{0.0F, 0.0F}, fixture.AddClip(10.0F, 1.0)},
                         AnimationBlendPoint{{1.0F, 0.0F}, fixture.AddClip(10.0F, 2.0)}};
    tree.nodes = {GraphFixture::Node(1U, Kind::Output, {2U}), space};
    static_cast<void>(fixture.Step(tree, 0.4F));
    ASSERT_EQ(tree.nodeStates[1].pointTimes.size(), 2U);
    EXPECT_NEAR(tree.nodeStates[1].pointTimes[0], 0.4, 1e-6) << "the heavier point leads";
    EXPECT_NEAR(tree.nodeStates[1].pointTimes[1], 0.8, 1e-6) << "the 2 s one follows at the same phase";
}

TEST(AnimationGraphTest, OldBlendSpaceInputsFoldIntoTheirPoints) {
    std::vector<AnimationGraphNode> objects = AnimationGraphComponent::MakeDefaultAnimationGraph();
    AnimationGraphNode space;
    space.id = 3U;
    space.kind = AnimationGraphNodeKind::BlendSpace1D;
    space.inputs = {2U, 4U, 0U}; // a Clip, a Time Scale, and an empty slot
    objects[0].inputs = {3U};
    objects[1].clip = Asset::AssetGuid{9U};
    objects[1].speed = 1.5F;
    objects[1].loop = false;
    AnimationGraphNode scale;
    scale.id = 4U;
    scale.kind = AnimationGraphNodeKind::TimeScale;
    scale.inputs = {0U};
    objects.push_back(space);
    objects.push_back(scale);
    const std::vector<std::vector<Math::Vector2>> positions{{}, {}, {{0.0F, 0.0F}, {1.0F, 0.0F}, {2.0F, 0.0F}}, {}};
    EXPECT_EQ(MigrateBlendSpaceInputs(objects, positions), 1U) << "the Time Scale could not be folded in";
    const auto folded = std::ranges::find(objects, 3U, &AnimationGraphNode::id);
    ASSERT_NE(folded, objects.end());
    EXPECT_TRUE(folded->inputs.empty());
    ASSERT_EQ(folded->blendPoints.size(), 3U);
    EXPECT_EQ(folded->blendPoints[0].clip, Asset::AssetGuid{9U});
    EXPECT_EQ(folded->blendPoints[0].speed, 1.5F);
    EXPECT_FALSE(folded->blendPoints[0].loop);
    EXPECT_EQ(folded->blendPoints[2].position.x, 2.0F);
    EXPECT_EQ(std::ranges::find(objects, 2U, &AnimationGraphNode::id), objects.end()) << "the folded Clip is gone";
    EXPECT_NE(std::ranges::find(objects, 4U, &AnimationGraphNode::id), objects.end()) << "the Time Scale stays";
    AnimationGraphComponent tree;
    tree.nodes = objects;
    EXPECT_TRUE(DescribeAnimationGraphProblem(tree).empty()) << DescribeAnimationGraphProblem(tree);
}

TEST(AnimationMorphSamplingTest, SampleMorphWeightTrack_InterpolatesLinearlyBetweenKeys) {
    Asset::AnimationAssetMorphTrack track;
    track.node = "Head";
    track.samples = {
        Asset::AnimationAssetMorphWeightSample{0.0, {0.0F, 1.0F}},
        Asset::AnimationAssetMorphWeightSample{1.0, {1.0F, 0.0F}},
    };

    std::vector<float> weights;
    ASSERT_TRUE(SampleMorphWeightTrack(track, 0.25, weights));
    ASSERT_EQ(weights.size(), 2U);
    EXPECT_FLOAT_EQ(weights[0], 0.25F);
    EXPECT_FLOAT_EQ(weights[1], 0.75F);

    // Both ends of the range are exact - a face has to be able to reach a shape fully.
    ASSERT_TRUE(SampleMorphWeightTrack(track, 1.0, weights));
    EXPECT_FLOAT_EQ(weights[0], 1.0F);
    EXPECT_FLOAT_EQ(weights[1], 0.0F);
}

TEST(AnimationMorphSamplingTest, SampleMorphWeightTrack_ClampsOutsideTheKeyRange) {
    Asset::AnimationAssetMorphTrack track;
    track.node = "Head";
    track.samples = {
        Asset::AnimationAssetMorphWeightSample{0.5, {0.2F}},
        Asset::AnimationAssetMorphWeightSample{1.5, {0.8F}},
    };
    std::vector<float> weights;
    // Before the first key and after the last, the nearest key holds - the same rule the pose
    // sampler uses, so a held facial expression behaves like a held pose.
    ASSERT_TRUE(SampleMorphWeightTrack(track, 0.0, weights));
    EXPECT_FLOAT_EQ(weights[0], 0.2F);
    ASSERT_TRUE(SampleMorphWeightTrack(track, 10.0, weights));
    EXPECT_FLOAT_EQ(weights[0], 0.8F);
}

TEST(AnimationMorphSamplingTest, SampleMorphWeightTrack_SingleKeyHoldsEverywhere) {
    Asset::AnimationAssetMorphTrack track;
    track.node = "Head";
    track.samples = {Asset::AnimationAssetMorphWeightSample{0.3, {0.4F, 0.6F}}};
    std::vector<float> weights;
    ASSERT_TRUE(SampleMorphWeightTrack(track, -5.0, weights));
    EXPECT_FLOAT_EQ(weights[0], 0.4F);
    ASSERT_TRUE(SampleMorphWeightTrack(track, 5.0, weights));
    EXPECT_FLOAT_EQ(weights[1], 0.6F);
}

TEST(AnimationMorphSamplingTest, SampleMorphWeightTrack_EmptyTrackFailsRatherThanRelaxing) {
    Asset::AnimationAssetMorphTrack track;
    track.node = "Head";
    std::vector<float> weights{7.0F};
    EXPECT_FALSE(SampleMorphWeightTrack(track, 0.0, weights));
    // Untouched on failure: a caller that keeps one buffer for the clip must not lose the previous
    // frame's expression because a track turned out to be empty.
    ASSERT_EQ(weights.size(), 1U);
    EXPECT_FLOAT_EQ(weights[0], 7.0F);
}

} // namespace
} // namespace UVE::Scene
