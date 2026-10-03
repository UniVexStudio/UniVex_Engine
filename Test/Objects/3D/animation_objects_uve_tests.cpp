// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_sequencer_component_uve.h"
#include "uve/component/animation_graph_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/objects/3d/animation_sequencer_uve.h"
#include "uve/objects/3d/animation_graph_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

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

[[nodiscard]] AnimationSequencerComponentUVE StartedUVE(AnimationSequencerComponentUVE player,
                                                     const TransformComponentUVE& target,
                                                     const Asset::AnimationClipAssetUVE& clip) {
    PlayAnimationSequencerUVE(player, target, clip.durationSeconds);
    return player;
}

TEST(AnimationSequencerUVETest, LoopWrapsBackToTheStart) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationSequencerComponentUVE player = StartedUVE(AnimationSequencerComponentUVE{}, target, clip);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 5.0F, 1e-4F);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 0.75F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_NEAR(target.localPosition.x, 2.5F, 1e-4F);
    EXPECT_TRUE(player.isPlaying);
}

TEST(AnimationSequencerUVETest, PingPongTurnsRoundAtEachEnd) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationSequencerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::PingPong;
    AnimationSequencerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 1.25F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.75F, 1e-4F);
    EXPECT_EQ(player.direction, -1.0F);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 1.0F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_EQ(player.direction, 1.0F);
}

TEST(AnimationSequencerUVETest, OnceStopsAtTheEndAndCanReturnToStart) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{-3.0F, 1.0F, 0.0F};
    AnimationSequencerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::Once;
    AnimationSequencerComponentUVE hold = StartedUVE(settings, target, clip);
    TransformComponentUVE held = target;
    ASSERT_TRUE(StepAnimationSequencerUVE(hold, clip, 2.0F, held));
    EXPECT_FALSE(hold.isPlaying);
    EXPECT_TRUE(hold.finished);
    EXPECT_NEAR(held.localPosition.x, 10.0F, 1e-4F);
    EXPECT_FALSE(StepAnimationSequencerUVE(hold, clip, 0.1F, held)); // stopped: nothing more written

    settings.onFinish = AnimationFinishActionUVE::ReturnToStart;
    AnimationSequencerComponentUVE back = StartedUVE(settings, target, clip);
    TransformComponentUVE returned = target;
    ASSERT_TRUE(StepAnimationSequencerUVE(back, clip, 2.0F, returned));
    EXPECT_EQ(returned.localPosition, target.localPosition);
}

TEST(AnimationSequencerUVETest, NegativeSpeedPlaysBackwardsFromTheEnd) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationSequencerComponentUVE settings;
    settings.speed = -1.0F;
    AnimationSequencerComponentUVE player = StartedUVE(settings, target, clip);
    EXPECT_NEAR(player.currentTimeSeconds, 1.0F, 1e-6F);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 0.25F, target));
    EXPECT_NEAR(target.localPosition.x, 7.5F, 1e-4F);
}

TEST(AnimationSequencerUVETest, BlendInEasesFromWhereTheTargetWas) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE(10.0F, 10.0);
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{0.0F, 4.0F, 0.0F};
    AnimationSequencerComponentUVE settings;
    settings.blendInSeconds = 1.0F;
    settings.startOffsetSeconds = 5.0F; // the clip is at x=5, y=0 there
    AnimationSequencerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 0.5F, target));
    // Halfway through the blend: halfway between the start (0,4) and the clip (5.5,0).
    EXPECT_NEAR(target.localPosition.x, 2.75F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 2.0F, 1e-3F);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 1.0F, target));
    EXPECT_NEAR(target.localPosition.x, 6.5F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 0.0F, 1e-3F);
}

TEST(AnimationSequencerUVETest, RelativeLaysTheMotionOnTopOfTheStartPose) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{100.0F, 0.0F, 7.0F};
    AnimationSequencerComponentUVE settings;
    settings.relative = true;
    AnimationSequencerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 105.0F, 1e-3F);
    EXPECT_NEAR(target.localPosition.z, 7.0F, 1e-3F);
}

TEST(AnimationSequencerUVETest, ChannelMasksLeaveTheOtherChannelsAlone) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{-1.0F, -1.0F, -1.0F};
    AnimatedObjectComponentUVE mixer;
    mixer.animatePosition = false;
    AnimationSequencerComponentUVE player = StartedUVE(AnimationSequencerComponentUVE{}, target, clip);
    ASSERT_TRUE(StepAnimationSequencerUVE(player, clip, 0.5F, target, mixer));
    EXPECT_EQ(target.localPosition, (Math::Vector3UVE{-1.0F, -1.0F, -1.0F}));
    EXPECT_NEAR(target.localScale.x, 1.5F, 1e-4F);
}

TEST(AnimationSequencerUVETest, AnEmptyClipStopsWithoutWriting) {
    Asset::AnimationClipAssetUVE empty;
    TransformComponentUVE target;
    AnimationSequencerComponentUVE player = StartedUVE(AnimationSequencerComponentUVE{}, target, empty);
    EXPECT_FALSE(StepAnimationSequencerUVE(player, empty, 0.1F, target));
    EXPECT_FALSE(player.isPlaying);
}

TEST(AnimationSequencerUVETest, ValidationRejectsBadSettings) {
    AnimationSequencerComponentUVE player;
    EXPECT_TRUE(IsAnimationSequencerComponentValidUVE(player));
    player.blendInSeconds = -1.0F;
    EXPECT_FALSE(IsAnimationSequencerComponentValidUVE(player));
    player = AnimationSequencerComponentUVE{};
    player.loopMode = static_cast<AnimationLoopModeUVE>(9);
    EXPECT_FALSE(IsAnimationSequencerComponentValidUVE(player));
}

// ---- AnimationGraph graph ------------------------------------------------------------------------

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

    [[nodiscard]] static AnimationGraphObjectUVE ObjectUVE(const std::uint32_t id, const AnimationGraphObjectKindUVE kind,
                                                      std::vector<std::uint32_t> inputs = {}) {
        AnimationGraphObjectUVE object;
        object.id = id;
        object.kind = kind;
        object.inputs = std::move(inputs);
        return object;
    }

    [[nodiscard]] static AnimationGraphObjectUVE ClipObjectUVE(const std::uint32_t id, const Asset::AssetGuidUVE clip,
                                                          const bool loop = true, std::string name = {}) {
        AnimationGraphObjectUVE object = ObjectUVE(id, AnimationGraphObjectKindUVE::Clip);
        object.clip = clip;
        object.loop = loop;
        object.name = std::move(name);
        return object;
    }

    [[nodiscard]] float StepUVE(AnimationGraphComponentUVE& tree, const float seconds) {
        EXPECT_TRUE(IsAnimationGraphComponentValidUVE(tree)) << DescribeAnimationGraphProblemUVE(tree);
        EXPECT_TRUE(StepAnimationGraphUVE(tree, ResolverUVE(), seconds, m_target, mixer));
        return m_target.localPosition.x;
    }

    /// Adds a clip as it is, for skeletal graphs.
    Asset::AssetGuidUVE AddUVE(Asset::AnimationClipAssetUVE clip) {
        const Asset::AssetGuidUVE guid{m_clips.size() + 100U};
        m_clips.emplace(guid.value, std::move(clip));
        return guid;
    }

    AnimatedObjectComponentUVE mixer;

private:
    std::unordered_map<std::uint64_t, Asset::AnimationClipAssetUVE> m_clips;
    TransformComponentUVE m_target;
};

using Kind = AnimationGraphObjectKindUVE;

TEST(AnimationGraphUVETest, TheDefaultGraphPlaysItsClip) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    ASSERT_TRUE(IsAnimationGraphComponentValidUVE(tree));
    tree.objects[1].clip = fixture.AddClipUVE(10.0F);
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F);
}

TEST(AnimationGraphUVETest, BlendMixesByItsParameter) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"blend", AnimationParameterTypeUVE::Float, 0.5F}};
    AnimationGraphObjectUVE blend = GraphFixtureUVE::ObjectUVE(2U, Kind::Blend2, {3U, 4U});
    blend.parameter = "blend";
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), blend,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(20.0F))};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 7.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "blend", 1.0F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 15.0F, 1e-4F);
    EXPECT_FALSE(SetAnimationGraphParameterUVE(tree, "missing", 1.0F));
}

TEST(AnimationGraphUVETest, BlendSpaceMixesTheTwoPointsAroundItsValue) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 1.5F}};
    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    // The space holds its animations itself: no inputs.
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, fixture.AddClipUVE(10.0F)},
                         AnimationBlendPointUVE{{1.0F, 0.0F}, fixture.AddClipUVE(20.0F)},
                         AnimationBlendPointUVE{{2.0F, 0.0F}, fixture.AddClipUVE(30.0F)}};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    // Halfway between walk (20 -> 10 at t=0.5) and run (30 -> 15): 12.5.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 12.5F, 1e-4F);
}

TEST(AnimationGraphUVETest, StateMachineMovesOnConditionsAndConsumesTriggers) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.0F},
                       AnimationParameterUVE{"stop", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphObjectUVE machine = GraphFixtureUVE::ObjectUVE(2U, Kind::StateMachine, {3U, 4U});
    AnimationTransitionUVE go;
    go.fromState = 0U;
    go.toState = 1U;
    go.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterGreater, "speed", 0.5F}};
    go.fadeSeconds = 0.0F;
    AnimationTransitionUVE stop;
    stop.fromState = kAnyAnimationStateUVE;
    stop.toState = 0U;
    stop.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::Triggered, "stop", 0.0F}};
    stop.fadeSeconds = 0.0F;
    machine.transitions = {go, stop};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), machine,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(1.0F), true, "Idle"),
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(10.0F), true, "Run")};

    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 0.5F, 1e-4F);
    EXPECT_EQ(tree.activeStates, "Idle");
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F)); // the move happens on this step
    EXPECT_EQ(tree.activeStates, "Run");
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F); // Run started from its beginning

    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "stop", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Idle");
    EXPECT_EQ(tree.parameters[1].value, 0.0F); // the trigger was used up
}

TEST(AnimationGraphUVETest, StateMachineCrossfadesAndCanWaitForTheEnd) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE machine = GraphFixtureUVE::ObjectUVE(2U, Kind::StateMachine, {3U, 4U});
    AnimationTransitionUVE atEnd;
    atEnd.fromState = 0U;
    atEnd.toState = 1U;
    atEnd.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::AtEnd, {}, 0.0F}};
    atEnd.fadeSeconds = 1.0F;
    machine.transitions = {atEnd};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), machine,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(10.0F), false, "Jump"),
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(20.0F), true, "Land")};
    fixture.mixer.transition = AnimationTransitionModeUVE::Crossfade;
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F);
    EXPECT_EQ(tree.activeStates, "Jump");
    EXPECT_NEAR(fixture.StepUVE(tree, 0.6F), 10.0F, 1e-4F); // reached the end: the move is made
    EXPECT_EQ(tree.activeStates, "Land");
    // Half a second into a one-second fade: halfway from Jump's held end (10) to Land at 0.5 (10).
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 10.0F, 1e-4F);
    // Three quarters through the fade: Jump's 10 mixed 75% toward Land's 15.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 13.75F, 1e-3F);
}

TEST(AnimationGraphUVETest, StateMachineInertializesByDefault) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE machine = GraphFixtureUVE::ObjectUVE(2U, Kind::StateMachine, {3U, 4U});
    AnimationTransitionUVE atEnd;
    atEnd.fromState = 0U;
    atEnd.toState = 1U;
    atEnd.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::AtEnd, {}, 0.0F}};
    atEnd.fadeSeconds = 1.0F;
    machine.transitions = {atEnd};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), machine,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(10.0F), false, "Jump"),
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(20.0F), true, "Land")};
    static_cast<void>(fixture.StepUVE(tree, 0.5F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.6F), 10.0F, 1e-4F) << "the step that switches still shows Jump";
    EXPECT_EQ(tree.activeStates, "Land");
    // Land alone would be at 10 after 0.5 s; the hand-over's 10 of difference is half decayed.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 15.0F, 1e-3F);
    // Once the second is up only Land shows: 1.25 s wraps to 0.25 s, 5.
    static_cast<void>(fixture.StepUVE(tree, 0.25F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-3F);
}

/// Idle (0 m) and Run (10 m per second) as a two-state machine from Idle to Run, crossfading.
[[nodiscard]] AnimationGraphComponentUVE TwoStateMachineUVE(GraphFixtureUVE& fixture, const AnimationTransitionUVE& transition,
                                                          const double runSeconds = 1.0) {
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.0F},
                       AnimationParameterUVE{"grounded", AnimationParameterTypeUVE::Bool, 0.0F},
                       AnimationParameterUVE{"jump", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphObjectUVE machine = GraphFixtureUVE::ObjectUVE(2U, Kind::StateMachine, {3U, 4U});
    machine.transitions = {transition};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), machine,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(0.0F), true, "Idle"),
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(10.0F * static_cast<float>(runSeconds), runSeconds), true, "Run")};
    fixture.mixer.transition = AnimationTransitionModeUVE::Crossfade;
    return tree;
}

TEST(AnimationGraphUVETest, TransitionsNeedAllTheirConditionsAndKeepTheTriggerUntilTaken) {
    GraphFixtureUVE fixture;
    AnimationTransitionUVE go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 0.0F;
    go.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::Triggered, "jump", 0.0F},
                     AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterTrue, "grounded", 0.0F}};
    AnimationGraphComponentUVE tree = TwoStateMachineUVE(fixture, go);
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "jump", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Idle") << "not grounded: the other condition fails";
    EXPECT_EQ(tree.parameters[2].value, 1.0F) << "so the trigger is still waiting";
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "grounded", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Run");
    EXPECT_EQ(tree.parameters[2].value, 0.0F) << "used up by the transition taken";
    EXPECT_EQ(tree.objectStates[1].lastTransition, 0U);
}

TEST(AnimationGraphUVETest, AnOffTransitionIsNeverTaken) {
    GraphFixtureUVE fixture;
    AnimationTransitionUVE go;
    go.fromState = 0U;
    go.toState = 1U;
    go.enabled = false;
    AnimationGraphComponentUVE tree = TwoStateMachineUVE(fixture, go);
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    EXPECT_EQ(tree.activeStates, "Idle");
}

TEST(AnimationGraphUVETest, LeaveAfterWaitsForThePhase) {
    GraphFixtureUVE fixture;
    AnimationTransitionUVE go;
    go.fromState = 1U; // Run to Idle, once Run is three quarters through
    go.toState = 0U;
    go.fadeSeconds = 0.0F;
    go.exitPhase = 0.75F;
    AnimationGraphComponentUVE tree = TwoStateMachineUVE(fixture, go);
    tree.objects[1].entryState = 1U;
    static_cast<void>(fixture.StepUVE(tree, 0.5F));
    EXPECT_EQ(tree.activeStates, "Run") << "half way: not yet";
    static_cast<void>(fixture.StepUVE(tree, 0.3F));
    EXPECT_EQ(tree.activeStates, "Idle") << "past 75%: taken";
}

TEST(AnimationGraphUVETest, InStepStartsTheNextStateAtThePhaseLeft) {
    GraphFixtureUVE fixture;
    AnimationTransitionUVE go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 0.0F;
    go.start = AnimationTransitionStartUVE::InStep;
    go.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterGreater, "speed", 0.5F}};
    AnimationGraphComponentUVE tree = TwoStateMachineUVE(fixture, go, 2.0);
    static_cast<void>(fixture.StepUVE(tree, 0.25F)); // Idle a quarter through its 1 s
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.0F));
    ASSERT_EQ(tree.activeStates, "Run");
    EXPECT_NEAR(tree.objectStates[3].timeSeconds, 0.5, 1e-6) << "a quarter through Run's 2 s";
}

TEST(AnimationGraphUVETest, ContinuePicksUpWhereTheStateWasLeft) {
    GraphFixtureUVE fixture;
    AnimationTransitionUVE back;
    back.fromState = 1U;
    back.toState = 0U;
    back.fadeSeconds = 0.0F;
    back.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterLess, "speed", 0.5F}};
    AnimationTransitionUVE go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 0.0F;
    go.start = AnimationTransitionStartUVE::Continue;
    go.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterGreater, "speed", 0.5F}};
    AnimationGraphComponentUVE tree = TwoStateMachineUVE(fixture, back, 4.0);
    tree.objects[1].transitions.push_back(go);
    tree.objects[1].entryState = 1U;
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.5F)); // Run at 0.5 s
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 0.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.0F));
    ASSERT_EQ(tree.activeStates, "Idle");
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.0F));
    ASSERT_EQ(tree.activeStates, "Run");
    EXPECT_NEAR(tree.objectStates[3].timeSeconds, 0.5, 1e-6) << "Run carries on from 0.5 s";
}

TEST(AnimationGraphUVETest, AFadeThatCannotBeInterruptedHoldsTheMachine) {
    GraphFixtureUVE fixture;
    AnimationTransitionUVE go;
    go.fromState = 0U;
    go.toState = 1U;
    go.fadeSeconds = 1.0F;
    go.interruptible = false;
    go.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterGreater, "speed", 0.5F}};
    AnimationTransitionUVE back = go;
    back.fromState = 1U;
    back.toState = 0U;
    back.conditions = {AnimationTransitionConditionUVE{AnimationConditionUVE::ParameterLess, "speed", 0.5F}};
    AnimationGraphComponentUVE tree = TwoStateMachineUVE(fixture, go);
    tree.objects[1].transitions.push_back(back);
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    ASSERT_EQ(tree.activeStates, "Run");
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 0.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.4F));
    EXPECT_EQ(tree.activeStates, "Run") << "the fade is under way and may not be interrupted";
    static_cast<void>(fixture.StepUVE(tree, 0.7F));
    EXPECT_EQ(tree.activeStates, "Idle") << "once it is over, the next one is taken";
}

TEST(AnimationGraphUVETest, CrossfadeCurvesShapeTheWeight) {
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeightUVE(AnimationTransitionCurveUVE::Linear, 0.25F), 0.25F);
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeightUVE(AnimationTransitionCurveUVE::EaseIn, 0.5F), 0.25F);
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeightUVE(AnimationTransitionCurveUVE::EaseOut, 0.5F), 0.75F);
    EXPECT_FLOAT_EQ(AnimationTransitionCurveWeightUVE(AnimationTransitionCurveUVE::EaseInOut, 0.5F), 0.5F);
    for (const auto curve : {AnimationTransitionCurveUVE::Linear, AnimationTransitionCurveUVE::EaseIn,
                             AnimationTransitionCurveUVE::EaseOut, AnimationTransitionCurveUVE::EaseInOut}) {
        EXPECT_EQ(AnimationTransitionCurveWeightUVE(curve, 0.0F), 0.0F);
        EXPECT_EQ(AnimationTransitionCurveWeightUVE(curve, 1.0F), 1.0F);
        EXPECT_EQ(AnimationTransitionCurveWeightUVE(curve, 2.0F), 1.0F) << "clamped";
    }
}

TEST(AnimationGraphUVETest, OneShotPlaysOverTheBaseAndHandsBack) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"attack", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphObjectUVE shot = GraphFixtureUVE::ObjectUVE(2U, Kind::OneShot, {3U, 4U});
    shot.parameter = "attack";
    shot.fadeSeconds = 0.0F;
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), shot,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(100.0F), false)};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 2.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "attack", 1.0F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 50.0F, 1e-4F);
    EXPECT_EQ(tree.parameters[0].value, 0.0F);
    static_cast<void>(fixture.StepUVE(tree, 0.6F)); // the shot ends here
    EXPECT_NEAR(fixture.StepUVE(tree, 0.1F), 4.5F, 1e-3F); // base alone again (1.45 s wrapped)
}

TEST(AnimationGraphUVETest, AdditiveLayersAndTimeScaleRescales) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE additive = GraphFixtureUVE::ObjectUVE(3U, Kind::Additive, {4U, 5U});
    additive.value = 0.5F;
    AnimationGraphObjectUVE scale = GraphFixtureUVE::ObjectUVE(2U, Kind::TimeScale, {3U});
    scale.speed = 2.0F;
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), scale, additive,
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipObjectUVE(5U, fixture.AddClipUVE(2.0F))};
    // 0.25 s at double speed is 0.5 s in: base 5 plus half of the layer's 1.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 5.5F, 1e-4F);
}

TEST(AnimationGraphUVETest, ValidationCatchesMalformedGraphsButAllowsEmptySlots) {
    AnimationGraphComponentUVE tree;
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {0U})};
    EXPECT_TRUE(IsAnimationGraphComponentValidUVE(tree)); // being built: nothing wired yet

    tree.objects.push_back(GraphFixtureUVE::ObjectUVE(2U, Kind::Output, {0U}));
    EXPECT_FALSE(IsAnimationGraphComponentValidUVE(tree)); // two outputs

    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), GraphFixtureUVE::ObjectUVE(2U, Kind::Blend2, {3U, 3U}),
                  GraphFixtureUVE::ObjectUVE(3U, Kind::Clip)};
    EXPECT_FALSE(IsAnimationGraphComponentValidUVE(tree)); // one object feeding two inputs

    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), GraphFixtureUVE::ObjectUVE(2U, Kind::TimeScale, {3U}),
                  GraphFixtureUVE::ObjectUVE(3U, Kind::TimeScale, {2U})};
    EXPECT_FALSE(IsAnimationGraphComponentValidUVE(tree)); // a loop

    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace1D);
    space.blendPoints = {AnimationBlendPointUVE{{1.0F, 0.0F}}, AnimationBlendPointUVE{{0.0F, 0.0F}}};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    EXPECT_FALSE(IsAnimationGraphComponentValidUVE(tree)); // points going backwards

    tree = AnimationGraphComponentUVE{};
    tree.parameters = {AnimationParameterUVE{"a", AnimationParameterTypeUVE::Float, 0.0F},
                       AnimationParameterUVE{"a", AnimationParameterTypeUVE::Bool, 0.0F}};
    EXPECT_FALSE(IsAnimationGraphComponentValidUVE(tree)); // two parameters with one name
}

// ---- Skeletal clips ---------------------------------------------------------------------------

/// Two bones at rest one metre apart; a one-second clip that raises "Hips" by `lift` and has no
/// track for "Spine".
[[nodiscard]] Skeleton3DComponentUVE MakeTwoBoneSkeletonUVE() {
    Skeleton3DComponentUVE skeleton;
    skeleton.skeletonAssetPath = "Hero.fbx";
    SkeletonBoneUVE hips;
    hips.name = "Hips";
    hips.localPosition = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    SkeletonBoneUVE spine;
    spine.name = "Spine";
    spine.parentIndex = 0;
    spine.localPosition = Math::Vector3UVE{0.0F, 0.3F, 0.0F};
    skeleton.bones = {hips, spine};
    return skeleton;
}

[[nodiscard]] Asset::AnimationClipAssetUVE MakeLiftClipUVE(const float lift) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "lift";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSampleUVE start;
    start.pose.position = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    Asset::AnimationAssetSampleUVE end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3UVE{0.0F, 1.0F + lift, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrackUVE{"Hips", {start, end}}};
    return clip;
}

TEST(AnimationSequencerUVETest, ASkeletalClipPosesEachBoneByName) {
    const Asset::AnimationClipAssetUVE clip = MakeLiftClipUVE(1.0F);
    Skeleton3DComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    EXPECT_EQ(GetSkeletonCurrentPoseUVE(skeleton).size(), 2U) << "no pose yet means the rest pose";
    AnimationSequencerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::Once;
    AnimationSequencerComponentUVE player = StartedUVE(settings, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.5F, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 2U);
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.5F, 1e-4F) << "halfway up";
    EXPECT_NEAR(skeleton.pose[1].position.y, 0.3F, 1e-4F) << "no track: the rest pose";
    EXPECT_EQ(skeleton, MakeTwoBoneSkeletonUVE()) << "the pose is runtime state, not an edit";

    // The end of a Once clip holds its last pose.
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 1.0F, skeleton));
    EXPECT_FALSE(player.isPlaying);
    EXPECT_TRUE(player.finished);
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-4F);
}

TEST(AnimationSequencerUVETest, ASkeletalClipBlendsInReturnsToRestAndHonoursTheMixer) {
    const Asset::AnimationClipAssetUVE clip = MakeLiftClipUVE(2.0F);
    // Blend In: half the blend time in, the pose is halfway from rest to the clip.
    Skeleton3DComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    AnimationSequencerComponentUVE settings;
    settings.blendInSeconds = 1.0F;
    settings.startOffsetSeconds = 1.0F; // the clip holds y = 3 from here on
    settings.loopMode = AnimationLoopModeUVE::Once;
    AnimationSequencerComponentUVE player = StartedUVE(settings, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.5F, skeleton));
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-4F);

    // Return To Start puts every bone back at rest when the clip ends.
    Skeleton3DComponentUVE back = MakeTwoBoneSkeletonUVE();
    settings = AnimationSequencerComponentUVE{};
    settings.loopMode = AnimationLoopModeUVE::Once;
    settings.onFinish = AnimationFinishActionUVE::ReturnToStart;
    player = StartedUVE(settings, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 2.0F, back));
    EXPECT_NEAR(back.pose[0].position.y, 1.0F, 1e-4F);

    // A mixer that leaves position alone leaves it at rest.
    Skeleton3DComponentUVE masked = MakeTwoBoneSkeletonUVE();
    AnimatedObjectComponentUVE mixer;
    mixer.animatePosition = false;
    player = StartedUVE(AnimationSequencerComponentUVE{}, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.5F, masked, mixer));
    EXPECT_NEAR(masked.pose[0].position.y, 1.0F, 1e-4F);

    // A clip with no bone tracks cannot pose a skeleton.
    Asset::AnimationClipAssetUVE objectClip = MakeSlideClipUVE();
    player = StartedUVE(AnimationSequencerComponentUVE{}, TransformComponentUVE{}, objectClip);
    EXPECT_FALSE(StepSkeletalAnimationSequencerUVE(player, objectClip, 0.1F, masked));
    EXPECT_FALSE(player.isPlaying);
}

/// Hips travel 2 m forward (+Z) over one second while a still root bone stays put.
[[nodiscard]] Asset::AnimationClipAssetUVE MakeRunClipUVE() {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "run";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSampleUVE start;
    start.pose.position = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    Asset::AnimationAssetSampleUVE end;
    end.timeSeconds = 1.0;
    end.pose.position = Math::Vector3UVE{0.0F, 1.0F, 2.0F};
    Asset::AnimationAssetSampleUVE spine;
    spine.pose.position = Math::Vector3UVE{0.0F, 0.3F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrackUVE{"Spine", {spine}},
                  Asset::AnimationAssetBoneTrackUVE{"Hips", {start, end}}};
    return clip;
}

TEST(AnimationSequencerUVETest, RootMotionPicksTheTravellingBone) {
    const Asset::AnimationClipAssetUVE clip = MakeRunClipUVE();
    const Skeleton3DComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, clip, ""), std::optional<std::size_t>{0U});
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, clip, "Spine"), std::optional<std::size_t>{1U});
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, clip, "Tail"), std::nullopt) << "no such bone";
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, MakeLiftClipUVE(1.0F), ""), std::nullopt)
        << "moving only up is not ground travel";
}

TEST(AnimationSequencerUVETest, RootMotionKeepsThePoseInPlaceAndReportsTravelAcrossTheLoop) {
    const Asset::AnimationClipAssetUVE clip = MakeRunClipUVE();
    Skeleton3DComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    AnimatedObjectComponentUVE mixer;
    mixer.rootMotion = AnimationRootMotionModeUVE::ApplyToTarget;
    AnimationSequencerComponentUVE player = StartedUVE(AnimationSequencerComponentUVE{}, TransformComponentUVE{}, clip);

    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.25F, skeleton, mixer));
    EXPECT_NEAR(player.rootMotionDelta.z, 0.5F, 1e-4F);
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "the travel is taken out of the pose";
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.0F, 1e-4F) << "height stays in the pose";

    // 0.25 -> 0.85 is 1.2 m; 0.85 wraps to 0.1: 0.3 m to the end plus 0.2 m from the start.
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.6F, skeleton, mixer));
    EXPECT_NEAR(player.rootMotionDelta.z, 1.2F, 1e-4F);
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.25F, skeleton, mixer));
    EXPECT_NEAR(player.currentTimeSeconds, 0.1F, 1e-4F);
    EXPECT_NEAR(player.rootMotionDelta.z, 0.5F, 1e-4F) << "never a jump back at the wrap";

    // Off: the bone travels in the pose and nothing is reported.
    Skeleton3DComponentUVE authored = MakeTwoBoneSkeletonUVE();
    player = StartedUVE(AnimationSequencerComponentUVE{}, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.25F, authored));
    EXPECT_NEAR(authored.pose[0].position.z, 0.5F, 1e-4F);
    EXPECT_NEAR(player.rootMotionDelta.z, 0.0F, 1e-6F);
}

TEST(AnimationSequencerUVETest, ScrubbingPosesTheSkeletonAtATimeWithoutAPlayer) {
    const Asset::AnimationClipAssetUVE clip = MakeRunClipUVE();
    Skeleton3DComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    ASSERT_TRUE(PoseSkeletonAtTimeUVE(clip, 0.5, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 2U);
    EXPECT_NEAR(skeleton.pose[0].position.z, 1.0F, 1e-4F);
    EXPECT_NEAR(skeleton.pose[1].position.y, 0.3F, 1e-4F);
    ASSERT_TRUE(PoseSkeletonAtTimeUVE(clip, 9.0, skeleton)) << "past the end holds the last frame";
    EXPECT_NEAR(skeleton.pose[0].position.z, 2.0F, 1e-4F);

    // With root motion on, the scrubbed pose runs in place like the played one.
    AnimatedObjectComponentUVE mixer;
    mixer.rootMotion = AnimationRootMotionModeUVE::InPlace;
    ASSERT_TRUE(PoseSkeletonAtTimeUVE(clip, 0.5, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F);
    EXPECT_EQ(skeleton, MakeTwoBoneSkeletonUVE()) << "scrubbing is not an edit";

    // A clip with no bones leaves the skeleton alone.
    Skeleton3DComponentUVE untouched = MakeTwoBoneSkeletonUVE();
    EXPECT_FALSE(PoseSkeletonAtTimeUVE(MakeSlideClipUVE(), 0.5, untouched));
    EXPECT_TRUE(untouched.pose.empty());
}

TEST(AnimationSequencerUVETest, CollectsTheEventsThePlayheadPasses) {
    const std::vector<Asset::AnimationAssetEventUVE> events = {
        {0.75, "right"}, {0.0, "start"}, {0.25, "left"}};
    using List = std::vector<std::string>;
    EXPECT_EQ(CollectPassedAnimationEventsUVE(events, 0.0, 0.5, 1.0, true, false, true), (List{"start", "left"}));
    EXPECT_EQ(CollectPassedAnimationEventsUVE(events, 0.0, 0.5, 1.0, true, false, false), (List{"left"}))
        << "the start event only on the first step";
    EXPECT_EQ(CollectPassedAnimationEventsUVE(events, 0.25, 0.5, 1.0, true, false, false), (List{}))
        << "an event already passed is not passed again";
    EXPECT_EQ(CollectPassedAnimationEventsUVE(events, 0.5, 0.3, 1.0, true, true, false), (List{"right", "start", "left"}))
        << "a loop's wrap passes the end, then the start";
    EXPECT_EQ(CollectPassedAnimationEventsUVE(events, 0.8, 0.1, 1.0, false, false, false), (List{"right", "left"}))
        << "backwards, in the order passed";
}

TEST(AnimationSequencerUVETest, InertializationCarriesTheOldPoseAndFadesItOutSmoothly) {
    // A clip that holds the hips still at z = 0; the skeleton was left at z = 5 by the last clip.
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "idle";
    clip.durationSeconds = 2.0;
    Asset::AnimationAssetSampleUVE still;
    still.pose.position = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrackUVE{"Hips", {still}}};
    Skeleton3DComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    skeleton.pose = GetSkeletonCurrentPoseUVE(skeleton);
    skeleton.pose[0].position = Math::Vector3UVE{0.0F, 1.0F, 5.0F};

    AnimationSequencerComponentUVE settings;
    settings.blendInSeconds = 1.0F;
    AnimationSequencerComponentUVE player = StartedUVE(settings, TransformComponentUVE{}, clip);
    AnimatedObjectComponentUVE mixer; // Inertialize is the default
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.001F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 5.0F, 1e-3F) << "no pop: it starts where it was";
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.499F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 2.5F, 1e-3F) << "halfway through the fade, half the difference";
    ASSERT_TRUE(StepSkeletalAnimationSequencerUVE(player, clip, 0.5F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "the new clip, exactly, when the blend ends";
    EXPECT_TRUE(player.inertialPosition.empty()) << "the carried difference is dropped once it has faded";
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

// ---- AnimationGraph on a skeleton ---------------------------------------------------------------

[[nodiscard]] AnimationGraphObjectUVE TreeObjectUVE(const std::uint32_t id, const AnimationGraphObjectKindUVE kind,
                                                std::vector<std::uint32_t> inputs = {}) {
    AnimationGraphObjectUVE object;
    object.id = id;
    object.kind = kind;
    object.inputs = std::move(inputs);
    return object;
}

[[nodiscard]] AnimationGraphObjectUVE TreeClipUVE(const std::uint32_t id, const Asset::AssetGuidUVE clip) {
    AnimationGraphObjectUVE object = TreeObjectUVE(id, AnimationGraphObjectKindUVE::Clip);
    object.clip = clip;
    return object;
}

/// "Hips" walking along X from 0 to `distance` over `duration`, one sample per tenth.
[[nodiscard]] Asset::AnimationClipAssetUVE MakeWalkClipUVE(const float distance, const double duration = 1.0) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = duration;
    std::vector<Asset::AnimationAssetSampleUVE> samples;
    for (int step = 0; step <= 10; ++step) {
        Asset::AnimationAssetSampleUVE sample;
        sample.timeSeconds = duration * step / 10.0;
        sample.pose.position = Math::Vector3UVE{distance * static_cast<float>(step) / 10.0F, 1.0F, 0.0F};
        samples.push_back(sample);
    }
    clip.bones = {Asset::AnimationAssetBoneTrackUVE{"Hips", samples}};
    return clip;
}

struct SkeletalGraphUVE {
    std::unordered_map<std::uint64_t, Asset::AnimationClipAssetUVE> clips;
    Skeleton3DComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    AnimatedObjectComponentUVE mixer;

    Asset::AssetGuidUVE AddUVE(Asset::AnimationClipAssetUVE clip) {
        const Asset::AssetGuidUVE guid{clips.size() + 500U};
        clips.emplace(guid.value, std::move(clip));
        return guid;
    }

    bool StepUVE(AnimationGraphComponentUVE& tree, const float seconds) {
        EXPECT_TRUE(IsAnimationGraphComponentValidUVE(tree)) << DescribeAnimationGraphProblemUVE(tree);
        return StepSkeletalAnimationGraphUVE(
            tree,
            [this](const Asset::AssetGuidUVE guid) -> const Asset::AnimationClipAssetUVE* {
                const auto found = clips.find(guid.value);
                return found == clips.end() ? nullptr : &found->second;
            },
            seconds, skeleton, mixer);
    }
};

TEST(AnimationGraphUVETest, ASkeletalTreeBlendsBoneByBoneAndKeepsUntrackedBonesAtRest) {
    SkeletalGraphUVE graph;
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE blend = TreeObjectUVE(2U, AnimationGraphObjectKindUVE::Blend2, {3U, 4U});
    blend.value = 0.5F;
    tree.objects = {TreeObjectUVE(1U, AnimationGraphObjectKindUVE::Output, {2U}), blend,
                  TreeClipUVE(3U, graph.AddUVE(MakeLiftClipUVE(0.0F))), TreeClipUVE(4U, graph.AddUVE(MakeLiftClipUVE(2.0F)))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.5F));
    ASSERT_EQ(graph.skeleton.pose.size(), 2U);
    // Lift 0 and lift 2 at half time (0 and 1), mixed half and half: hips at 1 + 0.5.
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.5F, 1e-4F);
    EXPECT_NEAR(graph.skeleton.pose[1].position.y, 0.3F, 1e-6F) << "no track: the rest pose";
    EXPECT_NEAR(tree.objectStates[2].weight, 0.5F, 1e-6F) << "each clip counted half";
}

TEST(AnimationGraphUVETest, ASkeletalTreeHandsOverRootMotion) {
    SkeletalGraphUVE graph;
    graph.mixer.rootMotion = AnimationRootMotionModeUVE::InPlace;
    AnimationGraphComponentUVE tree;
    tree.objects[1].clip = graph.AddUVE(MakeWalkClipUVE(2.0F));
    ASSERT_TRUE(graph.StepUVE(tree, 0.0F));
    ASSERT_TRUE(graph.StepUVE(tree, 0.25F));
    EXPECT_NEAR(tree.rootMotionDelta.x, 0.5F, 1e-4F);
    EXPECT_NEAR(graph.skeleton.pose[0].position.x, 0.0F, 1e-6F) << "the hips stay over their start";
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-6F) << "only the ground travel is taken";
    ASSERT_TRUE(graph.StepUVE(tree, 0.9F)); // 0.25 -> 1.15, wrapping at 1
    EXPECT_NEAR(tree.rootMotionDelta.x, 1.8F, 1e-3F) << "the travel carries across the wrap";
}

TEST(AnimationGraphUVETest, SyncedBlendKeepsItsInputsInStep) {
    SkeletalGraphUVE graph;
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE blend = TreeObjectUVE(2U, AnimationGraphObjectKindUVE::Blend2, {3U, 4U});
    blend.value = 0.25F;
    blend.sync = true;
    tree.objects = {TreeObjectUVE(1U, AnimationGraphObjectKindUVE::Output, {2U}), blend,
                  TreeClipUVE(3U, graph.AddUVE(MakeWalkClipUVE(1.0F, 1.0))),
                  TreeClipUVE(4U, graph.AddUVE(MakeWalkClipUVE(4.0F, 2.0)))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.4F));
    // The 1 s walk leads (it counts 75%) at 40%; the 2 s run is put at 40% too, not at 0.4 s.
    EXPECT_NEAR(tree.objectStates[2].timeSeconds, 0.4, 1e-6);
    EXPECT_NEAR(tree.objectStates[3].timeSeconds, 0.8, 1e-6);
    blend.sync = false;
    tree.objects[1] = blend;
    tree.objectStates.clear();
    ASSERT_TRUE(graph.StepUVE(tree, 0.4F));
    EXPECT_NEAR(tree.objectStates[3].timeSeconds, 0.4, 1e-6) << "unsynced, each runs on its own clock";
}

TEST(AnimationGraphUVETest, EventsComeFromTheClipsThatCount) {
    SkeletalGraphUVE graph;
    Asset::AnimationClipAssetUVE step = MakeWalkClipUVE(1.0F);
    step.events = {Asset::AnimationAssetEventUVE{0.5, "footstep"}};
    Asset::AnimationClipAssetUVE wave = MakeWalkClipUVE(1.0F);
    wave.events = {Asset::AnimationAssetEventUVE{0.5, "wave"}};
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE blend = TreeObjectUVE(2U, AnimationGraphObjectKindUVE::Blend2, {3U, 4U});
    blend.value = 0.2F;
    tree.objects = {TreeObjectUVE(1U, AnimationGraphObjectKindUVE::Output, {2U}), blend, TreeClipUVE(3U, graph.AddUVE(step)),
                  TreeClipUVE(4U, graph.AddUVE(wave))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.3F));
    EXPECT_TRUE(tree.firedEvents.empty());
    ASSERT_TRUE(graph.StepUVE(tree, 0.3F));
    EXPECT_EQ(tree.firedEvents, (std::vector<std::string>{"footstep"})) << "the 20% clip's event is not raised";
}

TEST(AnimationGraphUVETest, ASkeletalTreeWritesOnlyTheMixersChannels) {
    SkeletalGraphUVE graph;
    graph.mixer.animatePosition = false;
    AnimationGraphComponentUVE tree;
    tree.objects[1].clip = graph.AddUVE(MakeLiftClipUVE(1.0F));
    ASSERT_TRUE(graph.StepUVE(tree, 0.5F));
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-6F) << "position is not animated";
    graph.skeleton.bones.clear();
    EXPECT_FALSE(graph.StepUVE(tree, 0.1F)) << "no bones, nothing to pose";
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

// ---- New objects: Blend Space 2D, Select, Layered Blend, Time Seek -------------------------------

TEST(AnimationGraphUVETest, BlendSpace2DTriangulatesItsPoints) {
    EXPECT_TRUE(TriangulateBlendSpaceUVE({{0.0F, 0.0F}, {1.0F, 0.0F}}).empty()) << "two points make no triangle";
    EXPECT_TRUE(TriangulateBlendSpaceUVE({{0.0F, 0.0F}, {1.0F, 0.0F}, {2.0F, 0.0F}, {3.0F, 0.0F}}).empty())
        << "points on one line make none";
    const auto one = TriangulateBlendSpaceUVE({{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}});
    ASSERT_EQ(one.size(), 1U);
    EXPECT_EQ(one[0], (std::array<std::uint32_t, 3>{0U, 1U, 2U})) << "counter-clockwise, lowest index first";
    EXPECT_EQ(TriangulateBlendSpaceUVE({{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}}).size(), 2U);
    // Idle in the middle, walks at the four sides: a fan of four around the centre.
    const std::vector<Math::Vector2UVE> cross{{0.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 0.0F}, {0.0F, -1.0F}, {-1.0F, 0.0F}};
    const auto fan = TriangulateBlendSpaceUVE(cross);
    ASSERT_EQ(fan.size(), 4U);
    for (const auto& triangle : fan) {
        EXPECT_EQ(triangle[0], 0U) << "every triangle has the centre";
    }
    EXPECT_EQ(TriangulateBlendSpaceUVE(cross), fan) << "the same points give the same triangles";
}

TEST(AnimationGraphUVETest, BlendSpace2DWeightsAreTheTrianglesCorners) {
    const std::vector<Math::Vector2UVE> points{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}};
    const auto triangles = TriangulateBlendSpaceUVE(points);
    const std::vector<float> onPoint = AnimationBlendSpace2DWeightsUVE(points, triangles, Math::Vector2UVE{1.0F, 0.0F});
    EXPECT_NEAR(onPoint[1], 1.0F, 1e-5F) << "on a point: that point alone";
    const std::vector<float> inside = AnimationBlendSpace2DWeightsUVE(points, triangles, Math::Vector2UVE{0.25F, 0.25F});
    EXPECT_NEAR(inside[0], 0.5F, 1e-5F);
    EXPECT_NEAR(inside[1], 0.25F, 1e-5F);
    EXPECT_NEAR(inside[2], 0.25F, 1e-5F);
    // Outside: the nearest place on the outline, here halfway along the long edge.
    const std::vector<float> outside = AnimationBlendSpace2DWeightsUVE(points, triangles, Math::Vector2UVE{2.0F, 2.0F});
    EXPECT_NEAR(outside[0], 0.0F, 1e-5F);
    EXPECT_NEAR(outside[1], 0.5F, 1e-5F);
    EXPECT_NEAR(outside[2], 0.5F, 1e-5F);
    const std::vector<float> none = AnimationBlendSpace2DWeightsUVE(points, {}, Math::Vector2UVE{0.25F, 0.25F});
    EXPECT_EQ(none, (std::vector<float>{0.0F, 0.0F, 0.0F})) << "no triangles, no blend";
}

TEST(AnimationGraphUVETest, BlendSpace2DMixesInsideItsTriangleAndNeedsOne) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"x", AnimationParameterTypeUVE::Float, 0.25F}};
    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace2D);
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, fixture.AddClipUVE(0.0F)},
                         AnimationBlendPointUVE{{1.0F, 0.0F}, fixture.AddClipUVE(40.0F)}};
    space.parameter = "x";
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    TransformComponentUVE target;
    EXPECT_FALSE(StepAnimationGraphUVE(tree, fixture.ResolverUVE(), 0.5F, target, fixture.mixer))
        << "two points are a line, not a plane: nothing plays";
    tree.objects[1].blendPoints.push_back(AnimationBlendPointUVE{{0.0F, 1.0F}, fixture.AddClipUVE(0.0F)});
    tree.objectStates.clear();
    // A quarter of the way to the 40 m clip, which is at 20 after half a second: 5.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-3F);
    ASSERT_EQ(tree.objectStates[1].triangles.size(), 1U) << "triangulated once and kept";
    EXPECT_NEAR(tree.objectStates[1].pointWeights[1], 0.25F, 1e-5F);
}

TEST(AnimationGraphUVETest, SmoothingEasesThePositionWithoutOvershoot) {
    Math::Vector2UVE at{};
    Math::Vector2UVE velocity{};
    SmoothBlendPositionUVE(at, velocity, Math::Vector2UVE{1.0F, 2.0F}, 0.5F, 0.5F);
    EXPECT_NEAR(at.x, 0.5F, 1e-4F) << "half the gap closed after one half-life";
    EXPECT_NEAR(at.y, 1.0F, 1e-4F);
    Math::Vector2UVE stepped{};
    Math::Vector2UVE steppedVelocity{};
    float previous = 0.0F;
    for (int step = 0; step < 300; ++step) {
        SmoothBlendPositionUVE(stepped, steppedVelocity, Math::Vector2UVE{1.0F, 2.0F}, 0.5F, 0.01F);
        EXPECT_GE(stepped.x, previous) << "it only ever moves toward the goal";
        EXPECT_LE(stepped.x, 1.0F) << "and never past it";
        previous = stepped.x;
        if (step == 49) {
            EXPECT_NEAR(stepped.x, 0.5F, 1e-3F) << "the same after fifty small steps as after one big one";
        }
    }
    Math::Vector2UVE instant{};
    Math::Vector2UVE instantVelocity{3.0F, 3.0F};
    SmoothBlendPositionUVE(instant, instantVelocity, Math::Vector2UVE{4.0F, 5.0F}, 0.0F, 0.016F);
    EXPECT_EQ(instant, (Math::Vector2UVE{4.0F, 5.0F})) << "no smoothing: straight there";
    EXPECT_EQ(instantVelocity, (Math::Vector2UVE{}));
}

TEST(AnimationGraphUVETest, SmoothedBlendSpaceFollowsItsParameterOverTime) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.0F}};
    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.smoothingSeconds = 0.25F;
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, fixture.AddClipUVE(10.0F)},
                         AnimationBlendPointUVE{{1.0F, 0.0F}, fixture.AddClipUVE(20.0F)}};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    static_cast<void>(fixture.StepUVE(tree, 0.0F));
    EXPECT_EQ(tree.objectStates[1].blendAt.x, 0.0F) << "starts where its parameter is";
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.25F));
    EXPECT_NEAR(tree.objectStates[1].blendAt.x, 0.5F, 1e-3F) << "halfway after one half-life";
    EXPECT_NEAR(tree.objectStates[1].pointWeights[1], 0.5F, 1e-3F);
}

TEST(AnimationGraphUVETest, NearestPlaysOnePointAndSwitchesOnlyWhenClearlyNearer) {
    GraphFixtureUVE fixture;
    fixture.mixer.transition = AnimationTransitionModeUVE::Crossfade;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.2F}};
    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.blendMode = AnimationBlendModeUVE::Nearest;
    space.fadeSeconds = 0.0F;
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, fixture.AddClipUVE(10.0F)},
                         AnimationBlendPointUVE{{1.0F, 0.0F}, fixture.AddClipUVE(20.0F)}};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F) << "the near point alone, not a mix";
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 0.52F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    EXPECT_EQ(tree.objectStates[1].nearestPoint, 0U) << "just past halfway is not clearly nearer: no flicker";
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 0.9F));
    static_cast<void>(fixture.StepUVE(tree, 0.1F));
    EXPECT_EQ(tree.objectStates[1].nearestPoint, 1U);
    EXPECT_NEAR(fixture.StepUVE(tree, 0.25F), 5.0F, 1e-4F) << "the new point starts from its beginning: 20 m at 0.25";
}

TEST(AnimationGraphUVETest, NearestInStepCarriesThePhaseAcross) {
    GraphFixtureUVE fixture;
    fixture.mixer.transition = AnimationTransitionModeUVE::Crossfade;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.0F}};
    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.blendMode = AnimationBlendModeUVE::NearestInStep;
    space.fadeSeconds = 0.0F;
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, fixture.AddClipUVE(10.0F, 1.0)},
                         AnimationBlendPointUVE{{1.0F, 0.0F}, fixture.AddClipUVE(10.0F, 2.0)}};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    static_cast<void>(fixture.StepUVE(tree, 0.25F)); // a quarter through the 1 s cycle
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.0F));
    EXPECT_EQ(tree.objectStates[1].nearestPoint, 1U);
    EXPECT_NEAR(tree.objectStates[1].pointTimes[1], 0.5, 1e-6) << "a quarter through the 2 s cycle too";
}

TEST(AnimationGraphUVETest, NearestInertializesTheSwitchSoThereIsNoPop) {
    GraphFixtureUVE fixture; // the mixer inertializes by default
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"speed", AnimationParameterTypeUVE::Float, 0.0F}};
    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace1D);
    space.parameter = "speed";
    space.blendMode = AnimationBlendModeUVE::Nearest;
    space.fadeSeconds = 0.5F;
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, fixture.AddClipUVE(10.0F)},
                         AnimationBlendPointUVE{{1.0F, 0.0F}, fixture.AddClipUVE(-10.0F)}};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    const float before = fixture.StepUVE(tree, 0.5F); // 5 m along the first clip
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "speed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.0F)); // the switch is seen here
    const float right = fixture.StepUVE(tree, 0.01F);
    EXPECT_NEAR(right, before, 0.5F) << "just after the switch the pose is still where it was";
    const float settled = fixture.StepUVE(tree, 0.6F);
    EXPECT_NEAR(settled, -6.1F, 1e-3F) << "and once the hand-over ends, the new point alone";
}

TEST(AnimationGraphUVETest, SelectPlaysThePickedInputAndFadesOnChange) {
    GraphFixtureUVE fixture;
    fixture.mixer.transition = AnimationTransitionModeUVE::Crossfade;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"armed", AnimationParameterTypeUVE::Bool, 0.0F}};
    AnimationGraphObjectUVE select = GraphFixtureUVE::ObjectUVE(2U, Kind::Select, {3U, 4U});
    select.parameter = "armed";
    select.fadeSeconds = 1.0F;
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), select,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipObjectUVE(4U, fixture.AddClipUVE(20.0F))};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F) << "option 0";
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "armed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.0F)); // the pick is seen; option 1 restarts
    // Half a second into a one-second fade: option 0 at 1.0 s wraps to 0 -> 0, option 1 at 0.5 -> 10.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-3F);
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 0.0F, 1e-3F) << "fade done: option 1 alone, wrapped to 0";
}

TEST(AnimationGraphUVETest, LayeredBlendTouchesOnlyTheChosenBranch) {
    SkeletalGraphUVE graph;
    // Three bones: Hips > Spine > Head. The layer lifts every bone; only Spine's branch takes it.
    SkeletonBoneUVE head;
    head.name = "Head";
    head.parentIndex = 1;
    head.localPosition = Math::Vector3UVE{0.0F, 0.2F, 0.0F};
    graph.skeleton.bones.push_back(head);
    Asset::AnimationClipAssetUVE lifted;
    lifted.clipId = "lifted";
    lifted.durationSeconds = 1.0;
    for (const auto& [bone, y] : {std::pair<const char*, float>{"Hips", 2.0F}, {"Spine", 1.3F}, {"Head", 1.2F}}) {
        Asset::AnimationAssetSampleUVE sample;
        sample.pose.position = Math::Vector3UVE{0.0F, y, 0.0F};
        lifted.bones.push_back(Asset::AnimationAssetBoneTrackUVE{bone, {sample}});
    }
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE layered = TreeObjectUVE(2U, AnimationGraphObjectKindUVE::LayeredBlend, {3U, 4U});
    layered.value = 1.0F;
    layered.bones = {"Spine"};
    tree.objects = {TreeObjectUVE(1U, AnimationGraphObjectKindUVE::Output, {2U}), layered,
                  TreeClipUVE(3U, graph.AddUVE(MakeLiftClipUVE(0.0F))), TreeClipUVE(4U, graph.AddUVE(lifted))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.1F));
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-5F) << "Hips: the base";
    EXPECT_NEAR(graph.skeleton.pose[1].position.y, 1.3F, 1e-5F) << "Spine: the layer";
    EXPECT_NEAR(graph.skeleton.pose[2].position.y, 1.2F, 1e-5F) << "Head, under Spine: the layer";
}

TEST(AnimationGraphUVETest, TimeSeekJumpsWhenItsTriggerFires) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"rewind", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphObjectUVE seek = GraphFixtureUVE::ObjectUVE(2U, Kind::TimeSeek, {3U});
    seek.parameter = "rewind";
    seek.value = 0.25F;
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), seek,
                  GraphFixtureUVE::ClipObjectUVE(3U, fixture.AddClipUVE(10.0F))};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.75F), 7.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationGraphParameterUVE(tree, "rewind", 1.0F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.1F), 2.5F, 1e-4F) << "at the seek point this step";
    EXPECT_EQ(tree.parameters[0].value, 0.0F) << "the trigger was used";
    EXPECT_NEAR(fixture.StepUVE(tree, 0.1F), 3.5F, 1e-4F) << "and plays on from there";
}

TEST(AnimationGraphUVETest, TheNewKindsAreValidatedToo) {
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE space;
    space.id = 3U;
    space.kind = AnimationGraphObjectKindUVE::BlendSpace2D;
    space.inputs = {0U};
    tree.objects.push_back(space);
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("wrong number of inputs"), std::string::npos)
        << "a blend space has no inputs: its points hold the animations";
    tree.objects.back().inputs.clear();
    tree.objects.back().blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}}, AnimationBlendPointUVE{{0.0F, 0.0F}}};
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("same place"), std::string::npos);
    tree.objects.back().blendPoints.back().position = Math::Vector2UVE{1.0F, 0.0F};
    EXPECT_TRUE(DescribeAnimationGraphProblemUVE(tree).empty()) << DescribeAnimationGraphProblemUVE(tree);
    AnimationGraphObjectUVE layered;
    layered.id = 4U;
    layered.kind = AnimationGraphObjectKindUVE::LayeredBlend;
    layered.inputs = {0U};
    tree.objects.push_back(layered);
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("wrong number of inputs"), std::string::npos);
    tree.objects.back().inputs = {0U, 0U};
    tree.objects.back().bones = {""};
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("layer bone"), std::string::npos);
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

TEST(AnimationGraphUVETest, SyncedBlendSpacePointsKeepInStep) {
    GraphFixtureUVE fixture;
    AnimationGraphComponentUVE tree;
    AnimationGraphObjectUVE space = GraphFixtureUVE::ObjectUVE(2U, Kind::BlendSpace1D);
    space.value = 0.25F;
    space.sync = true;
    space.blendPoints = {AnimationBlendPointUVE{{0.0F, 0.0F}, fixture.AddClipUVE(10.0F, 1.0)},
                         AnimationBlendPointUVE{{1.0F, 0.0F}, fixture.AddClipUVE(10.0F, 2.0)}};
    tree.objects = {GraphFixtureUVE::ObjectUVE(1U, Kind::Output, {2U}), space};
    static_cast<void>(fixture.StepUVE(tree, 0.4F));
    ASSERT_EQ(tree.objectStates[1].pointTimes.size(), 2U);
    EXPECT_NEAR(tree.objectStates[1].pointTimes[0], 0.4, 1e-6) << "the heavier point leads";
    EXPECT_NEAR(tree.objectStates[1].pointTimes[1], 0.8, 1e-6) << "the 2 s one follows at the same phase";
}

TEST(AnimationGraphUVETest, OldBlendSpaceInputsFoldIntoTheirPoints) {
    std::vector<AnimationGraphObjectUVE> objects = AnimationGraphComponentUVE::MakeDefaultAnimationGraphUVE();
    AnimationGraphObjectUVE space;
    space.id = 3U;
    space.kind = AnimationGraphObjectKindUVE::BlendSpace1D;
    space.inputs = {2U, 4U, 0U}; // a Clip, a Time Scale, and an empty slot
    objects[0].inputs = {3U};
    objects[1].clip = Asset::AssetGuidUVE{9U};
    objects[1].speed = 1.5F;
    objects[1].loop = false;
    AnimationGraphObjectUVE scale;
    scale.id = 4U;
    scale.kind = AnimationGraphObjectKindUVE::TimeScale;
    scale.inputs = {0U};
    objects.push_back(space);
    objects.push_back(scale);
    const std::vector<std::vector<Math::Vector2UVE>> positions{{}, {}, {{0.0F, 0.0F}, {1.0F, 0.0F}, {2.0F, 0.0F}}, {}};
    EXPECT_EQ(MigrateBlendSpaceInputsUVE(objects, positions), 1U) << "the Time Scale could not be folded in";
    const auto folded = std::ranges::find(objects, 3U, &AnimationGraphObjectUVE::id);
    ASSERT_NE(folded, objects.end());
    EXPECT_TRUE(folded->inputs.empty());
    ASSERT_EQ(folded->blendPoints.size(), 3U);
    EXPECT_EQ(folded->blendPoints[0].clip, Asset::AssetGuidUVE{9U});
    EXPECT_EQ(folded->blendPoints[0].speed, 1.5F);
    EXPECT_FALSE(folded->blendPoints[0].loop);
    EXPECT_EQ(folded->blendPoints[2].position.x, 2.0F);
    EXPECT_EQ(std::ranges::find(objects, 2U, &AnimationGraphObjectUVE::id), objects.end()) << "the folded Clip is gone";
    EXPECT_NE(std::ranges::find(objects, 4U, &AnimationGraphObjectUVE::id), objects.end()) << "the Time Scale stays";
    AnimationGraphComponentUVE tree;
    tree.objects = objects;
    EXPECT_TRUE(DescribeAnimationGraphProblemUVE(tree).empty()) << DescribeAnimationGraphProblemUVE(tree);
}

} // namespace
} // namespace UVE::Scene
