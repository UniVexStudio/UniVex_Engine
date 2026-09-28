// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/animation_tree_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"

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
    AnimationMixerComponentUVE mixer;
    mixer.animatePosition = false;
    AnimationPlayerComponentUVE player = StartedUVE(AnimationPlayerComponentUVE{}, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target, mixer));
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
        EXPECT_TRUE(StepAnimationTreeUVE(tree, ResolverUVE(), seconds, m_target, mixer));
        return m_target.localPosition.x;
    }

    /// Adds a clip as it is, for skeletal graphs.
    Asset::AssetGuidUVE AddUVE(Asset::AnimationClipAssetUVE clip) {
        const Asset::AssetGuidUVE guid{m_clips.size() + 100U};
        m_clips.emplace(guid.value, std::move(clip));
        return guid;
    }

    AnimationMixerComponentUVE mixer;

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

TEST(AnimationTreeUVETest, StateMachineInertializesByDefault) {
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
    static_cast<void>(fixture.StepUVE(tree, 0.5F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.6F), 10.0F, 1e-4F) << "the step that switches still shows Jump";
    EXPECT_EQ(tree.activeStates, "Land");
    // Land alone would be at 10 after 0.5 s; the hand-over's 10 of difference is half decayed.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 15.0F, 1e-3F);
    // Once the second is up only Land shows: 1.25 s wraps to 0.25 s, 5.
    static_cast<void>(fixture.StepUVE(tree, 0.25F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-3F);
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

// ---- Skeletal clips ---------------------------------------------------------------------------

/// Two bones at rest one metre apart; a one-second clip that raises "Hips" by `lift` and has no
/// track for "Spine".
[[nodiscard]] Skeleton3DNodeComponentUVE MakeTwoBoneSkeletonUVE() {
    Skeleton3DNodeComponentUVE skeleton;
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

TEST(AnimationPlayerUVETest, ASkeletalClipPosesEachBoneByName) {
    const Asset::AnimationClipAssetUVE clip = MakeLiftClipUVE(1.0F);
    Skeleton3DNodeComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    EXPECT_EQ(GetSkeletonCurrentPoseUVE(skeleton).size(), 2U) << "no pose yet means the rest pose";
    AnimationPlayerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::Once;
    AnimationPlayerComponentUVE player = StartedUVE(settings, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.5F, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 2U);
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.5F, 1e-4F) << "halfway up";
    EXPECT_NEAR(skeleton.pose[1].position.y, 0.3F, 1e-4F) << "no track: the rest pose";
    EXPECT_EQ(skeleton, MakeTwoBoneSkeletonUVE()) << "the pose is runtime state, not an edit";

    // The end of a Once clip holds its last pose.
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 1.0F, skeleton));
    EXPECT_FALSE(player.isPlaying);
    EXPECT_TRUE(player.finished);
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-4F);
}

TEST(AnimationPlayerUVETest, ASkeletalClipBlendsInReturnsToRestAndHonoursTheMixer) {
    const Asset::AnimationClipAssetUVE clip = MakeLiftClipUVE(2.0F);
    // Blend In: half the blend time in, the pose is halfway from rest to the clip.
    Skeleton3DNodeComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    AnimationPlayerComponentUVE settings;
    settings.blendInSeconds = 1.0F;
    settings.startOffsetSeconds = 1.0F; // the clip holds y = 3 from here on
    settings.loopMode = AnimationLoopModeUVE::Once;
    AnimationPlayerComponentUVE player = StartedUVE(settings, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.5F, skeleton));
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-4F);

    // Return To Start puts every bone back at rest when the clip ends.
    Skeleton3DNodeComponentUVE back = MakeTwoBoneSkeletonUVE();
    settings = AnimationPlayerComponentUVE{};
    settings.loopMode = AnimationLoopModeUVE::Once;
    settings.onFinish = AnimationFinishActionUVE::ReturnToStart;
    player = StartedUVE(settings, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 2.0F, back));
    EXPECT_NEAR(back.pose[0].position.y, 1.0F, 1e-4F);

    // A mixer that leaves position alone leaves it at rest.
    Skeleton3DNodeComponentUVE masked = MakeTwoBoneSkeletonUVE();
    AnimationMixerComponentUVE mixer;
    mixer.animatePosition = false;
    player = StartedUVE(AnimationPlayerComponentUVE{}, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.5F, masked, mixer));
    EXPECT_NEAR(masked.pose[0].position.y, 1.0F, 1e-4F);

    // A clip with no bone tracks cannot pose a skeleton.
    Asset::AnimationClipAssetUVE nodeClip = MakeSlideClipUVE();
    player = StartedUVE(AnimationPlayerComponentUVE{}, TransformComponentUVE{}, nodeClip);
    EXPECT_FALSE(StepSkeletalAnimationPlayerUVE(player, nodeClip, 0.1F, masked));
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

TEST(AnimationPlayerUVETest, RootMotionPicksTheTravellingBone) {
    const Asset::AnimationClipAssetUVE clip = MakeRunClipUVE();
    const Skeleton3DNodeComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, clip, ""), std::optional<std::size_t>{0U});
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, clip, "Spine"), std::optional<std::size_t>{1U});
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, clip, "Tail"), std::nullopt) << "no such bone";
    EXPECT_EQ(ResolveRootMotionBoneUVE(skeleton, MakeLiftClipUVE(1.0F), ""), std::nullopt)
        << "moving only up is not ground travel";
}

TEST(AnimationPlayerUVETest, RootMotionKeepsThePoseInPlaceAndReportsTravelAcrossTheLoop) {
    const Asset::AnimationClipAssetUVE clip = MakeRunClipUVE();
    Skeleton3DNodeComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    AnimationMixerComponentUVE mixer;
    mixer.rootMotion = AnimationRootMotionModeUVE::ApplyToTarget;
    AnimationPlayerComponentUVE player = StartedUVE(AnimationPlayerComponentUVE{}, TransformComponentUVE{}, clip);

    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.25F, skeleton, mixer));
    EXPECT_NEAR(player.rootMotionDelta.z, 0.5F, 1e-4F);
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "the travel is taken out of the pose";
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.0F, 1e-4F) << "height stays in the pose";

    // 0.25 -> 0.85 is 1.2 m; 0.85 wraps to 0.1: 0.3 m to the end plus 0.2 m from the start.
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.6F, skeleton, mixer));
    EXPECT_NEAR(player.rootMotionDelta.z, 1.2F, 1e-4F);
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.25F, skeleton, mixer));
    EXPECT_NEAR(player.currentTimeSeconds, 0.1F, 1e-4F);
    EXPECT_NEAR(player.rootMotionDelta.z, 0.5F, 1e-4F) << "never a jump back at the wrap";

    // Off: the bone travels in the pose and nothing is reported.
    Skeleton3DNodeComponentUVE authored = MakeTwoBoneSkeletonUVE();
    player = StartedUVE(AnimationPlayerComponentUVE{}, TransformComponentUVE{}, clip);
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.25F, authored));
    EXPECT_NEAR(authored.pose[0].position.z, 0.5F, 1e-4F);
    EXPECT_NEAR(player.rootMotionDelta.z, 0.0F, 1e-6F);
}

TEST(AnimationPlayerUVETest, ScrubbingPosesTheSkeletonAtATimeWithoutAPlayer) {
    const Asset::AnimationClipAssetUVE clip = MakeRunClipUVE();
    Skeleton3DNodeComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    ASSERT_TRUE(PoseSkeletonAtTimeUVE(clip, 0.5, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 2U);
    EXPECT_NEAR(skeleton.pose[0].position.z, 1.0F, 1e-4F);
    EXPECT_NEAR(skeleton.pose[1].position.y, 0.3F, 1e-4F);
    ASSERT_TRUE(PoseSkeletonAtTimeUVE(clip, 9.0, skeleton)) << "past the end holds the last frame";
    EXPECT_NEAR(skeleton.pose[0].position.z, 2.0F, 1e-4F);

    // With root motion on, the scrubbed pose runs in place like the played one.
    AnimationMixerComponentUVE mixer;
    mixer.rootMotion = AnimationRootMotionModeUVE::InPlace;
    ASSERT_TRUE(PoseSkeletonAtTimeUVE(clip, 0.5, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F);
    EXPECT_EQ(skeleton, MakeTwoBoneSkeletonUVE()) << "scrubbing is not an edit";

    // A clip with no bones leaves the skeleton alone.
    Skeleton3DNodeComponentUVE untouched = MakeTwoBoneSkeletonUVE();
    EXPECT_FALSE(PoseSkeletonAtTimeUVE(MakeSlideClipUVE(), 0.5, untouched));
    EXPECT_TRUE(untouched.pose.empty());
}

TEST(AnimationPlayerUVETest, CollectsTheEventsThePlayheadPasses) {
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

TEST(AnimationPlayerUVETest, InertializationCarriesTheOldPoseAndFadesItOutSmoothly) {
    // A clip that holds the hips still at z = 0; the skeleton was left at z = 5 by the last clip.
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "idle";
    clip.durationSeconds = 2.0;
    Asset::AnimationAssetSampleUVE still;
    still.pose.position = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    clip.bones = {Asset::AnimationAssetBoneTrackUVE{"Hips", {still}}};
    Skeleton3DNodeComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    skeleton.pose = GetSkeletonCurrentPoseUVE(skeleton);
    skeleton.pose[0].position = Math::Vector3UVE{0.0F, 1.0F, 5.0F};

    AnimationPlayerComponentUVE settings;
    settings.blendInSeconds = 1.0F;
    AnimationPlayerComponentUVE player = StartedUVE(settings, TransformComponentUVE{}, clip);
    AnimationMixerComponentUVE mixer; // Inertialize is the default
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.001F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 5.0F, 1e-3F) << "no pop: it starts where it was";
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.499F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 2.5F, 1e-3F) << "halfway through the fade, half the difference";
    ASSERT_TRUE(StepSkeletalAnimationPlayerUVE(player, clip, 0.5F, skeleton, mixer));
    EXPECT_NEAR(skeleton.pose[0].position.z, 0.0F, 1e-4F) << "the new clip, exactly, when the blend ends";
    EXPECT_TRUE(player.inertialPosition.empty()) << "the carried difference is dropped once it has faded";
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

// ---- AnimationTree on a skeleton ---------------------------------------------------------------

[[nodiscard]] AnimationGraphNodeUVE TreeNodeUVE(const std::uint32_t id, const AnimationGraphNodeKindUVE kind,
                                                std::vector<std::uint32_t> inputs = {}) {
    AnimationGraphNodeUVE node;
    node.id = id;
    node.kind = kind;
    node.inputs = std::move(inputs);
    return node;
}

[[nodiscard]] AnimationGraphNodeUVE TreeClipUVE(const std::uint32_t id, const Asset::AssetGuidUVE clip) {
    AnimationGraphNodeUVE node = TreeNodeUVE(id, AnimationGraphNodeKindUVE::Clip);
    node.clip = clip;
    return node;
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
    Skeleton3DNodeComponentUVE skeleton = MakeTwoBoneSkeletonUVE();
    AnimationMixerComponentUVE mixer;

    Asset::AssetGuidUVE AddUVE(Asset::AnimationClipAssetUVE clip) {
        const Asset::AssetGuidUVE guid{clips.size() + 500U};
        clips.emplace(guid.value, std::move(clip));
        return guid;
    }

    bool StepUVE(AnimationTreeComponentUVE& tree, const float seconds) {
        EXPECT_TRUE(IsAnimationTreeComponentValidUVE(tree)) << DescribeAnimationGraphProblemUVE(tree);
        return StepSkeletalAnimationTreeUVE(
            tree,
            [this](const Asset::AssetGuidUVE guid) -> const Asset::AnimationClipAssetUVE* {
                const auto found = clips.find(guid.value);
                return found == clips.end() ? nullptr : &found->second;
            },
            seconds, skeleton, mixer);
    }
};

TEST(AnimationTreeUVETest, ASkeletalTreeBlendsBoneByBoneAndKeepsUntrackedBonesAtRest) {
    SkeletalGraphUVE graph;
    AnimationTreeComponentUVE tree;
    AnimationGraphNodeUVE blend = TreeNodeUVE(2U, AnimationGraphNodeKindUVE::Blend2, {3U, 4U});
    blend.value = 0.5F;
    tree.nodes = {TreeNodeUVE(1U, AnimationGraphNodeKindUVE::Output, {2U}), blend,
                  TreeClipUVE(3U, graph.AddUVE(MakeLiftClipUVE(0.0F))), TreeClipUVE(4U, graph.AddUVE(MakeLiftClipUVE(2.0F)))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.5F));
    ASSERT_EQ(graph.skeleton.pose.size(), 2U);
    // Lift 0 and lift 2 at half time (0 and 1), mixed half and half: hips at 1 + 0.5.
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.5F, 1e-4F);
    EXPECT_NEAR(graph.skeleton.pose[1].position.y, 0.3F, 1e-6F) << "no track: the rest pose";
    EXPECT_NEAR(tree.nodeStates[2].weight, 0.5F, 1e-6F) << "each clip counted half";
}

TEST(AnimationTreeUVETest, ASkeletalTreeHandsOverRootMotion) {
    SkeletalGraphUVE graph;
    graph.mixer.rootMotion = AnimationRootMotionModeUVE::InPlace;
    AnimationTreeComponentUVE tree;
    tree.nodes[1].clip = graph.AddUVE(MakeWalkClipUVE(2.0F));
    ASSERT_TRUE(graph.StepUVE(tree, 0.0F));
    ASSERT_TRUE(graph.StepUVE(tree, 0.25F));
    EXPECT_NEAR(tree.rootMotionDelta.x, 0.5F, 1e-4F);
    EXPECT_NEAR(graph.skeleton.pose[0].position.x, 0.0F, 1e-6F) << "the hips stay over their start";
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-6F) << "only the ground travel is taken";
    ASSERT_TRUE(graph.StepUVE(tree, 0.9F)); // 0.25 -> 1.15, wrapping at 1
    EXPECT_NEAR(tree.rootMotionDelta.x, 1.8F, 1e-3F) << "the travel carries across the wrap";
}

TEST(AnimationTreeUVETest, SyncedBlendKeepsItsInputsInStep) {
    SkeletalGraphUVE graph;
    AnimationTreeComponentUVE tree;
    AnimationGraphNodeUVE blend = TreeNodeUVE(2U, AnimationGraphNodeKindUVE::Blend2, {3U, 4U});
    blend.value = 0.25F;
    blend.sync = true;
    tree.nodes = {TreeNodeUVE(1U, AnimationGraphNodeKindUVE::Output, {2U}), blend,
                  TreeClipUVE(3U, graph.AddUVE(MakeWalkClipUVE(1.0F, 1.0))),
                  TreeClipUVE(4U, graph.AddUVE(MakeWalkClipUVE(4.0F, 2.0)))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.4F));
    // The 1 s walk leads (it counts 75%) at 40%; the 2 s run is put at 40% too, not at 0.4 s.
    EXPECT_NEAR(tree.nodeStates[2].timeSeconds, 0.4, 1e-6);
    EXPECT_NEAR(tree.nodeStates[3].timeSeconds, 0.8, 1e-6);
    blend.sync = false;
    tree.nodes[1] = blend;
    tree.nodeStates.clear();
    ASSERT_TRUE(graph.StepUVE(tree, 0.4F));
    EXPECT_NEAR(tree.nodeStates[3].timeSeconds, 0.4, 1e-6) << "unsynced, each runs on its own clock";
}

TEST(AnimationTreeUVETest, EventsComeFromTheClipsThatCount) {
    SkeletalGraphUVE graph;
    Asset::AnimationClipAssetUVE step = MakeWalkClipUVE(1.0F);
    step.events = {Asset::AnimationAssetEventUVE{0.5, "footstep"}};
    Asset::AnimationClipAssetUVE wave = MakeWalkClipUVE(1.0F);
    wave.events = {Asset::AnimationAssetEventUVE{0.5, "wave"}};
    AnimationTreeComponentUVE tree;
    AnimationGraphNodeUVE blend = TreeNodeUVE(2U, AnimationGraphNodeKindUVE::Blend2, {3U, 4U});
    blend.value = 0.2F;
    tree.nodes = {TreeNodeUVE(1U, AnimationGraphNodeKindUVE::Output, {2U}), blend, TreeClipUVE(3U, graph.AddUVE(step)),
                  TreeClipUVE(4U, graph.AddUVE(wave))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.3F));
    EXPECT_TRUE(tree.firedEvents.empty());
    ASSERT_TRUE(graph.StepUVE(tree, 0.3F));
    EXPECT_EQ(tree.firedEvents, (std::vector<std::string>{"footstep"})) << "the 20% clip's event is not raised";
}

TEST(AnimationTreeUVETest, ASkeletalTreeWritesOnlyTheMixersChannels) {
    SkeletalGraphUVE graph;
    graph.mixer.animatePosition = false;
    AnimationTreeComponentUVE tree;
    tree.nodes[1].clip = graph.AddUVE(MakeLiftClipUVE(1.0F));
    ASSERT_TRUE(graph.StepUVE(tree, 0.5F));
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-6F) << "position is not animated";
    graph.skeleton.bones.clear();
    EXPECT_FALSE(graph.StepUVE(tree, 0.1F)) << "no bones, nothing to pose";
}

} // namespace
} // namespace UVE::Scene

namespace UVE::Scene {
namespace {

// ---- New nodes: Blend Space 2D, Select, Layered Blend, Time Seek -------------------------------

TEST(AnimationTreeUVETest, BlendSpace2DWeightsFavourTheNearestPointsAndAddUpToOne) {
    const std::vector<Math::Vector2UVE> points{{0.0F, 0.0F}, {1.0F, 0.0F}, {0.0F, 1.0F}, {1.0F, 1.0F}};
    const std::vector<float> onPoint = AnimationBlendSpace2DWeightsUVE(points, Math::Vector2UVE{1.0F, 0.0F});
    EXPECT_NEAR(onPoint[1], 1.0F, 1e-5F) << "on a point: that point alone";
    const std::vector<float> middle = AnimationBlendSpace2DWeightsUVE(points, Math::Vector2UVE{0.5F, 0.5F});
    for (const float weight : middle) {
        EXPECT_NEAR(weight, 0.25F, 1e-5F) << "the middle of a square: all four the same";
    }
    const std::vector<float> edge = AnimationBlendSpace2DWeightsUVE(points, Math::Vector2UVE{0.25F, 0.0F});
    EXPECT_NEAR(edge[0], 0.75F, 1e-5F);
    EXPECT_NEAR(edge[1], 0.25F, 1e-5F);
    EXPECT_NEAR(edge[2] + edge[3], 0.0F, 1e-5F) << "the far side of the square takes nothing";
    const std::vector<float> outside = AnimationBlendSpace2DWeightsUVE(points, Math::Vector2UVE{5.0F, -3.0F});
    float total = 0.0F;
    for (const float weight : outside) {
        EXPECT_GE(weight, 0.0F);
        total += weight;
    }
    EXPECT_NEAR(total, 1.0F, 1e-5F) << "outside the points still adds up";
}

TEST(AnimationTreeUVETest, BlendSpace2DMixesItsInputs) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"x", AnimationParameterTypeUVE::Float, 0.25F}};
    AnimationGraphNodeUVE space = GraphFixtureUVE::NodeUVE(2U, Kind::BlendSpace2D, {3U, 4U});
    space.points2D = {Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{1.0F, 0.0F}};
    space.parameter = "x";
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), space,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(0.0F)),
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(40.0F))};
    // A quarter of the way to the 40 m clip, which is at 20 after half a second: 5.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-3F);
}

TEST(AnimationTreeUVETest, SelectPlaysThePickedInputAndFadesOnChange) {
    GraphFixtureUVE fixture;
    fixture.mixer.transition = AnimationTransitionModeUVE::Crossfade;
    AnimationTreeComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"armed", AnimationParameterTypeUVE::Bool, 0.0F}};
    AnimationGraphNodeUVE select = GraphFixtureUVE::NodeUVE(2U, Kind::Select, {3U, 4U});
    select.parameter = "armed";
    select.fadeSeconds = 1.0F;
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), select,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(10.0F)),
                  GraphFixtureUVE::ClipNodeUVE(4U, fixture.AddClipUVE(20.0F))};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-4F) << "option 0";
    ASSERT_TRUE(SetAnimationTreeParameterUVE(tree, "armed", 1.0F));
    static_cast<void>(fixture.StepUVE(tree, 0.0F)); // the pick is seen; option 1 restarts
    // Half a second into a one-second fade: option 0 at 1.0 s wraps to 0 -> 0, option 1 at 0.5 -> 10.
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 5.0F, 1e-3F);
    EXPECT_NEAR(fixture.StepUVE(tree, 0.5F), 0.0F, 1e-3F) << "fade done: option 1 alone, wrapped to 0";
}

TEST(AnimationTreeUVETest, LayeredBlendTouchesOnlyTheChosenBranch) {
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
    AnimationTreeComponentUVE tree;
    AnimationGraphNodeUVE layered = TreeNodeUVE(2U, AnimationGraphNodeKindUVE::LayeredBlend, {3U, 4U});
    layered.value = 1.0F;
    layered.bones = {"Spine"};
    tree.nodes = {TreeNodeUVE(1U, AnimationGraphNodeKindUVE::Output, {2U}), layered,
                  TreeClipUVE(3U, graph.AddUVE(MakeLiftClipUVE(0.0F))), TreeClipUVE(4U, graph.AddUVE(lifted))};
    ASSERT_TRUE(graph.StepUVE(tree, 0.1F));
    EXPECT_NEAR(graph.skeleton.pose[0].position.y, 1.0F, 1e-5F) << "Hips: the base";
    EXPECT_NEAR(graph.skeleton.pose[1].position.y, 1.3F, 1e-5F) << "Spine: the layer";
    EXPECT_NEAR(graph.skeleton.pose[2].position.y, 1.2F, 1e-5F) << "Head, under Spine: the layer";
}

TEST(AnimationTreeUVETest, TimeSeekJumpsWhenItsTriggerFires) {
    GraphFixtureUVE fixture;
    AnimationTreeComponentUVE tree;
    tree.parameters = {AnimationParameterUVE{"rewind", AnimationParameterTypeUVE::Trigger, 0.0F}};
    AnimationGraphNodeUVE seek = GraphFixtureUVE::NodeUVE(2U, Kind::TimeSeek, {3U});
    seek.parameter = "rewind";
    seek.value = 0.25F;
    tree.nodes = {GraphFixtureUVE::NodeUVE(1U, Kind::Output, {2U}), seek,
                  GraphFixtureUVE::ClipNodeUVE(3U, fixture.AddClipUVE(10.0F))};
    EXPECT_NEAR(fixture.StepUVE(tree, 0.75F), 7.5F, 1e-4F);
    ASSERT_TRUE(SetAnimationTreeParameterUVE(tree, "rewind", 1.0F));
    EXPECT_NEAR(fixture.StepUVE(tree, 0.1F), 2.5F, 1e-4F) << "at the seek point this step";
    EXPECT_EQ(tree.parameters[0].value, 0.0F) << "the trigger was used";
    EXPECT_NEAR(fixture.StepUVE(tree, 0.1F), 3.5F, 1e-4F) << "and plays on from there";
}

TEST(AnimationTreeUVETest, TheNewKindsAreValidatedToo) {
    AnimationTreeComponentUVE tree;
    AnimationGraphNodeUVE space;
    space.id = 3U;
    space.kind = AnimationGraphNodeKindUVE::BlendSpace2D;
    space.inputs = {0U, 0U};
    space.points2D = {Math::Vector2UVE{0.0F, 0.0F}};
    tree.nodes.push_back(space);
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("one point per input"), std::string::npos);
    tree.nodes.back().points2D.push_back(Math::Vector2UVE{0.0F, 0.0F});
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("same place"), std::string::npos);
    tree.nodes.back().points2D.back() = Math::Vector2UVE{1.0F, 0.0F};
    EXPECT_TRUE(DescribeAnimationGraphProblemUVE(tree).empty()) << DescribeAnimationGraphProblemUVE(tree);
    AnimationGraphNodeUVE layered;
    layered.id = 4U;
    layered.kind = AnimationGraphNodeKindUVE::LayeredBlend;
    layered.inputs = {0U};
    tree.nodes.push_back(layered);
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("wrong number of inputs"), std::string::npos);
    tree.nodes.back().inputs = {0U, 0U};
    tree.nodes.back().bones = {""};
    EXPECT_NE(DescribeAnimationGraphProblemUVE(tree).find("layer bone"), std::string::npos);
}

} // namespace
} // namespace UVE::Scene
