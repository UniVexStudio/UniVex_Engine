// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "animation_graph.h"
#include "animation_graph_component.h"
#include "animation_montage.h"
#include "animation_montage_editing.h"
#include "skeleton_3d.h"

namespace UVE::Scene {
namespace {

using Kind = MontageEventUVE::KindUVE;

/// Start 0-1, Attack 1-2, End 2-3; one clip per section.
[[nodiscard]] Asset::AnimationMontageAssetUVE MakeComboUVE() {
    Asset::AnimationMontageAssetUVE montage;
    for (std::uint64_t clip = 1U; clip <= 3U; ++clip) {
        Asset::AddMontageSegmentUVE(montage, Asset::AssetGuidUVE{clip}, 1.0);
    }
    EXPECT_TRUE(Asset::RenameMontageSectionUVE(montage, "Default", "Start"));
    static_cast<void>(Asset::AddMontageSectionUVE(montage, "Attack", 1.0));
    static_cast<void>(Asset::AddMontageSectionUVE(montage, "End", 2.0));
    montage.blendInSeconds = 0.0;
    montage.blendOutSeconds = 0.0;
    return montage;
}

[[nodiscard]] bool HasEventUVE(const std::vector<MontageEventUVE>& events, const Kind kind, const std::string& name) {
    for (const MontageEventUVE& e : events) {
        if (e.kind == kind && e.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(AnimationMontageUVETest, PlaysThroughSectionsAndEnds) {
    const Asset::AnimationMontageAssetUVE montage = MakeComboUVE();
    MontagePlaybackUVE playback;
    std::vector<MontageEventUVE> events;
    ASSERT_TRUE(StartMontageUVE(montage, playback));
    EXPECT_EQ(playback.section, "Start");
    EXPECT_FLOAT_EQ(playback.weight, 1.0F) << "no Blend In: full weight at once";
    StepMontageUVE(montage, playback, 1.5, events);
    EXPECT_EQ(playback.section, "Attack");
    EXPECT_NEAR(playback.time, 1.5, 1e-9);
    StepMontageUVE(montage, playback, 2.0, events);
    EXPECT_EQ(playback.phase, MontagePhaseUVE::Stopped);
    EXPECT_TRUE(HasEventUVE(events, Kind::Ended, ""));
    EXPECT_FALSE(StartMontageUVE(montage, playback, "Nowhere"));
}

TEST(AnimationMontageUVETest, ALoopingSectionLoopsUntilJumpedOut) {
    Asset::AnimationMontageAssetUVE montage = MakeComboUVE();
    ASSERT_TRUE(Asset::SetMontageSectionNextUVE(montage, "Attack", "Attack"));
    MontagePlaybackUVE playback;
    std::vector<MontageEventUVE> events;
    ASSERT_TRUE(StartMontageUVE(montage, playback, "Attack"));
    StepMontageUVE(montage, playback, 2.25, events);
    EXPECT_EQ(playback.section, "Attack");
    EXPECT_NEAR(playback.time, 1.25, 1e-9) << "looped twice and a quarter into the third";
    EXPECT_NE(playback.phase, MontagePhaseUVE::Stopped);
    ASSERT_TRUE(JumpToMontageSectionUVE(montage, playback, "End"));
    EXPECT_NEAR(playback.time, 2.0, 1e-9);
    StepMontageUVE(montage, playback, 1.5, events);
    EXPECT_EQ(playback.phase, MontagePhaseUVE::Stopped);
}

TEST(AnimationMontageUVETest, BlendsInAndOut) {
    Asset::AnimationMontageAssetUVE montage = MakeComboUVE();
    montage.blendInSeconds = 0.5;
    montage.blendOutSeconds = 0.5;
    MontagePlaybackUVE playback;
    std::vector<MontageEventUVE> events;
    ASSERT_TRUE(StartMontageUVE(montage, playback));
    EXPECT_FLOAT_EQ(playback.weight, 0.0F);
    StepMontageUVE(montage, playback, 0.25, events);
    EXPECT_NEAR(playback.weight, 0.5F, 1e-5);
    StepMontageUVE(montage, playback, 0.5, events);
    EXPECT_EQ(playback.phase, MontagePhaseUVE::Playing);
    StepMontageUVE(montage, playback, 1.9, events);   // 2.65: within 0.5 s of the end (3.0)
    EXPECT_EQ(playback.phase, MontagePhaseUVE::BlendOut) << "Auto Blend Out starts before the end";
    StopMontageUVE(montage, playback, events);
    StepMontageUVE(montage, playback, 0.6, events);
    EXPECT_EQ(playback.phase, MontagePhaseUVE::Stopped);
    EXPECT_TRUE(HasEventUVE(events, Kind::Ended, ""));
}

TEST(AnimationMontageUVETest, RaisesNotifiesAndNotifyStates) {
    Asset::AnimationMontageAssetUVE montage = MakeComboUVE();
    Asset::AddMontageNotifyUVE(montage, "hit", 1.2, 0U);
    Asset::AddMontageNotifyStateUVE(montage, "hit_window", 1.1, 1.4, 0U);
    MontagePlaybackUVE playback;
    std::vector<MontageEventUVE> events;
    ASSERT_TRUE(StartMontageUVE(montage, playback, "Attack"));
    StepMontageUVE(montage, playback, 0.15, events);   // 1.15
    EXPECT_TRUE(HasEventUVE(events, Kind::NotifyBegin, "hit_window"));
    EXPECT_FALSE(HasEventUVE(events, Kind::Notify, "hit"));
    events.clear();
    StepMontageUVE(montage, playback, 0.1, events);    // 1.25
    EXPECT_TRUE(HasEventUVE(events, Kind::Notify, "hit"));
    events.clear();
    StepMontageUVE(montage, playback, 0.2, events);    // 1.45
    EXPECT_TRUE(HasEventUVE(events, Kind::NotifyEnd, "hit_window"));
}

TEST(AnimationMontageUVETest, BlendsItsPoseOverTheSkeleton) {
    Asset::AnimationClipAssetUVE clip;
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSampleUVE a;
    a.pose.position = Math::Vector3UVE{0.0F, 2.0F, 0.0F};
    Asset::AnimationAssetSampleUVE b = a;
    b.timeSeconds = 1.0;
    clip.bones = {Asset::AnimationAssetBoneTrackUVE{"Hips", {a, b}}};
    Asset::AnimationMontageAssetUVE montage;
    Asset::AddMontageSegmentUVE(montage, Asset::AssetGuidUVE{7U}, 1.0);

    Skeleton3DComponentUVE skeleton;
    SkeletonBoneUVE hips;
    hips.name = "Hips";
    skeleton.bones = {hips};
    const AnimationClipResolverUVE clipFor = [&clip](const Asset::AssetGuidUVE guid) -> const Asset::AnimationClipAssetUVE* {
        return guid.value == 7U ? &clip : nullptr;
    };
    ASSERT_TRUE(BlendMontagePoseUVE(montage, 0.5, 0.5F, clipFor, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 1U);
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.0F, 1e-5) << "halfway between the rest pose and the montage";
    EXPECT_FALSE(BlendMontagePoseUVE(montage, 0.5, 0.0F, clipFor, skeleton)) << "no weight, no change";
}


TEST(AnimationMontageUVETest, AGraphSlotNodeLaysTheMontageOverItsInput) {
    Asset::AnimationClipAssetUVE clip;
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSampleUVE a;
    a.pose.position = Math::Vector3UVE{0.0F, 2.0F, 0.0F};
    Asset::AnimationAssetSampleUVE b = a;
    b.timeSeconds = 1.0;
    clip.bones = {Asset::AnimationAssetBoneTrackUVE{"Hips", {a, b}}};
    const AnimationClipResolverUVE clipFor = [&clip](const Asset::AssetGuidUVE guid) -> const Asset::AnimationClipAssetUVE* {
        return guid.value == 7U ? &clip : nullptr;
    };

    AnimationGraphComponentUVE graph;
    AnimationGraphNodeUVE output;
    output.id = 1U;
    output.kind = AnimationGraphNodeKindUVE::Output;
    output.name = "Output";
    output.inputs = {2U};
    AnimationGraphNodeUVE slot;
    slot.id = 2U;
    slot.kind = AnimationGraphNodeKindUVE::Slot;
    slot.name = "Slot";
    slot.parameter = "DefaultSlot";
    slot.inputs = {3U};
    AnimationGraphNodeUVE walk;
    walk.id = 3U;
    walk.kind = AnimationGraphNodeKindUVE::Clip;
    walk.name = "Walk";
    walk.clip = Asset::AssetGuidUVE{7U};
    graph.nodes = {output, slot, walk};

    Skeleton3DComponentUVE skeleton;
    SkeletonBoneUVE hips;
    hips.name = "Hips";
    skeleton.bones = {hips};
    ASSERT_TRUE(StepSkeletalAnimationGraphUVE(graph, clipFor, 0.1F, skeleton, AnimationDriverComponentUVE{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-5) << "no montage: the input passes through";
    EXPECT_FALSE(graph.slotUsed);

    AnimationGraphSlotPoseUVE attack;
    attack.slot = "DefaultSlot";
    attack.weight = 1.0F;
    attack.bones = {AnimationGraphSlotBoneUVE{Math::Vector3UVE{0.0F, 5.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
    graph.slotPoses = {attack};
    ASSERT_TRUE(StepSkeletalAnimationGraphUVE(graph, clipFor, 0.1F, skeleton, AnimationDriverComponentUVE{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 5.0F, 1e-5) << "the montage at full weight";
    EXPECT_TRUE(graph.slotUsed);
    graph.slotPoses[0].weight = 0.5F;
    ASSERT_TRUE(StepSkeletalAnimationGraphUVE(graph, clipFor, 0.1F, skeleton, AnimationDriverComponentUVE{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 3.5F, 1e-5) << "half way between the walk and the montage";
    graph.slotPoses[0].slot = "UpperBody";
    ASSERT_TRUE(StepSkeletalAnimationGraphUVE(graph, clipFor, 0.1F, skeleton, AnimationDriverComponentUVE{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-5) << "another slot's montage leaves this one alone";
}

} // namespace UVE::Scene
