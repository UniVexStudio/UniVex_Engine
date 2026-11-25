// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "animation_graph.h"
#include "animation_graph_component.h"
#include "motion_montage.h"
#include "motion_montage_asset.h"
#include "motion_montage_editing.h"
#include "skeleton_3d.h"

namespace UVE::Scene {
namespace {

using Kind = MontageEvent::Kind;

/// Start 0-1, Attack 1-2, End 2-3; one clip per section.
[[nodiscard]] Asset::MotionMontageAsset MakeCombo() {
    Asset::MotionMontageAsset montage;
    for (std::uint64_t clip = 1U; clip <= 3U; ++clip) {
        Asset::AddMontageSegment(montage, Asset::AssetGuid{clip}, 1.0);
    }
    EXPECT_TRUE(Asset::RenameMontageSection(montage, "Default", "Start"));
    static_cast<void>(Asset::AddMontageSection(montage, "Attack", 1.0));
    static_cast<void>(Asset::AddMontageSection(montage, "End", 2.0));
    montage.blendInSeconds = 0.0;
    montage.blendOutSeconds = 0.0;
    return montage;
}

[[nodiscard]] bool HasEvent(const std::vector<MontageEvent>& events, const Kind kind, const std::string& name) {
    for (const MontageEvent& e : events) {
        if (e.kind == kind && e.name == name) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(MotionMontageTest, PlaysThroughSectionsAndEnds) {
    const Asset::MotionMontageAsset montage = MakeCombo();
    MontagePlayback playback;
    std::vector<MontageEvent> events;
    ASSERT_TRUE(StartMontage(montage, playback));
    EXPECT_EQ(playback.section, "Start");
    EXPECT_FLOAT_EQ(playback.weight, 1.0F) << "no Blend In: full weight at once";
    StepMontage(montage, playback, 1.5, events);
    EXPECT_EQ(playback.section, "Attack");
    EXPECT_NEAR(playback.time, 1.5, 1e-9);
    StepMontage(montage, playback, 2.0, events);
    EXPECT_EQ(playback.phase, MontagePhase::Stopped);
    EXPECT_TRUE(HasEvent(events, Kind::Ended, ""));
    EXPECT_FALSE(StartMontage(montage, playback, "Nowhere"));
}

TEST(MotionMontageTest, ALoopingSectionLoopsUntilJumpedOut) {
    Asset::MotionMontageAsset montage = MakeCombo();
    ASSERT_TRUE(Asset::SetMontageSectionNext(montage, "Attack", "Attack"));
    MontagePlayback playback;
    std::vector<MontageEvent> events;
    ASSERT_TRUE(StartMontage(montage, playback, "Attack"));
    StepMontage(montage, playback, 2.25, events);
    EXPECT_EQ(playback.section, "Attack");
    EXPECT_NEAR(playback.time, 1.25, 1e-9) << "looped twice and a quarter into the third";
    EXPECT_NE(playback.phase, MontagePhase::Stopped);
    ASSERT_TRUE(JumpToMontageSection(montage, playback, "End"));
    EXPECT_NEAR(playback.time, 2.0, 1e-9);
    StepMontage(montage, playback, 1.5, events);
    EXPECT_EQ(playback.phase, MontagePhase::Stopped);
}

TEST(MotionMontageTest, BlendsInAndOut) {
    Asset::MotionMontageAsset montage = MakeCombo();
    montage.blendInSeconds = 0.5;
    montage.blendOutSeconds = 0.5;
    MontagePlayback playback;
    std::vector<MontageEvent> events;
    ASSERT_TRUE(StartMontage(montage, playback));
    EXPECT_FLOAT_EQ(playback.weight, 0.0F);
    StepMontage(montage, playback, 0.25, events);
    EXPECT_NEAR(playback.weight, 0.5F, 1e-5);
    StepMontage(montage, playback, 0.5, events);
    EXPECT_EQ(playback.phase, MontagePhase::Playing);
    StepMontage(montage, playback, 1.9, events);   // 2.65: within 0.5 s of the end (3.0)
    EXPECT_EQ(playback.phase, MontagePhase::BlendOut) << "Auto Blend Out starts before the end";
    StopMontage(montage, playback, events);
    StepMontage(montage, playback, 0.6, events);
    EXPECT_EQ(playback.phase, MontagePhase::Stopped);
    EXPECT_TRUE(HasEvent(events, Kind::Ended, ""));
}

TEST(MotionMontageTest, RaisesNotifiesAndNotifyStates) {
    Asset::MotionMontageAsset montage = MakeCombo();
    Asset::AddMontageNotify(montage, "hit", 1.2, 0U);
    Asset::AddMontageNotifyState(montage, "hit_window", 1.1, 1.4, 0U);
    MontagePlayback playback;
    std::vector<MontageEvent> events;
    ASSERT_TRUE(StartMontage(montage, playback, "Attack"));
    StepMontage(montage, playback, 0.15, events);   // 1.15
    EXPECT_TRUE(HasEvent(events, Kind::NotifyBegin, "hit_window"));
    EXPECT_FALSE(HasEvent(events, Kind::Notify, "hit"));
    events.clear();
    StepMontage(montage, playback, 0.1, events);    // 1.25
    EXPECT_TRUE(HasEvent(events, Kind::Notify, "hit"));
    events.clear();
    StepMontage(montage, playback, 0.2, events);    // 1.45
    EXPECT_TRUE(HasEvent(events, Kind::NotifyEnd, "hit_window"));
}

TEST(MotionMontageTest, ABlendCurveShapesTheWeight) {
    EXPECT_FLOAT_EQ(Asset::MontageBlendCurveWeight(Asset::MontageBlendCurve::Linear, 0.5), 0.5F);
    EXPECT_FLOAT_EQ(Asset::MontageBlendCurveWeight(Asset::MontageBlendCurve::EaseIn, 0.5), 0.25F);
    EXPECT_FLOAT_EQ(Asset::MontageBlendCurveWeight(Asset::MontageBlendCurve::EaseOut, 0.5), 0.75F);
    EXPECT_FLOAT_EQ(Asset::MontageBlendCurveWeight(Asset::MontageBlendCurve::EaseInOut, 2.0), 1.0F) << "clamped";
    Asset::MotionMontageAsset montage = MakeCombo();
    montage.blendInSeconds = 1.0;
    montage.blendInCurve = Asset::MontageBlendCurve::EaseIn;
    MontagePlayback playback;
    std::vector<MontageEvent> events;
    ASSERT_TRUE(StartMontage(montage, playback));
    StepMontage(montage, playback, 0.5, events);
    EXPECT_NEAR(playback.weight, 0.25F, 1e-5) << "eased in: a quarter at half time";
}

TEST(MotionMontageTest, ANotifyKeepsToItsChanceAndWeight) {
    Asset::MotionMontageAsset montage = MakeCombo();
    Asset::AddMontageNotify(montage, "never", 1.2, 0U);
    Asset::AddMontageNotify(montage, "heavy", 1.3, 0U);
    montage.notifies[0].triggerChance = 0.0F;
    montage.notifies[1].minWeight = 0.9F;
    montage.blendInSeconds = 1.0;
    MontagePlayback playback;
    std::vector<MontageEvent> events;
    ASSERT_TRUE(StartMontage(montage, playback, "Attack"));
    StepMontage(montage, playback, 0.5, events);   // 1.5, weight 0.5
    EXPECT_FALSE(HasEvent(events, Kind::Notify, "never")) << "a 0 chance is never raised";
    EXPECT_FALSE(HasEvent(events, Kind::Notify, "heavy")) << "passed at half weight: below its threshold";
    // Saved and read back.
    const std::filesystem::path file = std::filesystem::temp_directory_path() / "uve_montage_notify_chance_test.uvmontage";
    ASSERT_TRUE(Asset::SaveMotionMontageAsset(montage, file));
    Asset::MotionMontageAsset copy;
    ASSERT_TRUE(Asset::LoadMotionMontageAsset(file, copy));
    std::error_code ignored;
    std::filesystem::remove(file, ignored);
    EXPECT_EQ(copy.notifies[0].triggerChance, 0.0F);
    EXPECT_EQ(copy.notifies[1].minWeight, 0.9F);
}

TEST(MotionMontageTest, BlendsItsPoseOverTheSkeleton) {
    Asset::AnimationClipAsset clip;
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample a;
    a.pose.position = Math::Vector3{0.0F, 2.0F, 0.0F};
    Asset::AnimationAssetSample b = a;
    b.timeSeconds = 1.0;
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {a, b}}};
    Asset::MotionMontageAsset montage;
    Asset::AddMontageSegment(montage, Asset::AssetGuid{7U}, 1.0);

    Skeleton3DComponent skeleton;
    SkeletonBone hips;
    hips.name = "Hips";
    skeleton.bones = {hips};
    const AnimationClipResolver clipFor = [&clip](const Asset::AssetGuid guid) -> const Asset::AnimationClipAsset* {
        return guid.value == 7U ? &clip : nullptr;
    };
    ASSERT_TRUE(BlendMontagePose(montage, 0.5, 0.5F, clipFor, skeleton));
    ASSERT_EQ(skeleton.pose.size(), 1U);
    EXPECT_NEAR(skeleton.pose[0].position.y, 1.0F, 1e-5) << "halfway between the rest pose and the montage";
    EXPECT_FALSE(BlendMontagePose(montage, 0.5, 0.0F, clipFor, skeleton)) << "no weight, no change";
}


TEST(MotionMontageTest, AGraphSlotNodeLaysTheMontageOverItsInput) {
    Asset::AnimationClipAsset clip;
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample a;
    a.pose.position = Math::Vector3{0.0F, 2.0F, 0.0F};
    Asset::AnimationAssetSample b = a;
    b.timeSeconds = 1.0;
    clip.bones = {Asset::AnimationAssetBoneTrack{"Hips", {a, b}}};
    const AnimationClipResolver clipFor = [&clip](const Asset::AssetGuid guid) -> const Asset::AnimationClipAsset* {
        return guid.value == 7U ? &clip : nullptr;
    };

    AnimationGraphComponent graph;
    AnimationGraphNode output;
    output.id = 1U;
    output.kind = AnimationGraphNodeKind::Output;
    output.name = "Output";
    output.inputs = {2U};
    AnimationGraphNode slot;
    slot.id = 2U;
    slot.kind = AnimationGraphNodeKind::Slot;
    slot.name = "Slot";
    slot.parameter = "DefaultSlot";
    slot.inputs = {3U};
    AnimationGraphNode walk;
    walk.id = 3U;
    walk.kind = AnimationGraphNodeKind::Clip;
    walk.name = "Walk";
    walk.clip = Asset::AssetGuid{7U};
    graph.nodes = {output, slot, walk};

    Skeleton3DComponent skeleton;
    SkeletonBone hips;
    hips.name = "Hips";
    skeleton.bones = {hips};
    ASSERT_TRUE(StepSkeletalAnimationGraph(graph, clipFor, 0.1F, skeleton, AnimationDriverComponent{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-5) << "no montage: the input passes through";
    EXPECT_FALSE(graph.slotUsed);

    AnimationGraphSlotPose attack;
    attack.slot = "DefaultSlot";
    attack.weight = 1.0F;
    attack.bones = {AnimationGraphSlotBone{Math::Vector3{0.0F, 5.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
    graph.slotPoses = {attack};
    ASSERT_TRUE(StepSkeletalAnimationGraph(graph, clipFor, 0.1F, skeleton, AnimationDriverComponent{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 5.0F, 1e-5) << "the montage at full weight";
    EXPECT_TRUE(graph.slotUsed);
    graph.slotPoses[0].weight = 0.5F;
    ASSERT_TRUE(StepSkeletalAnimationGraph(graph, clipFor, 0.1F, skeleton, AnimationDriverComponent{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 3.5F, 1e-5) << "half way between the walk and the montage";
    graph.slotPoses[0].slot = "UpperBody";
    ASSERT_TRUE(StepSkeletalAnimationGraph(graph, clipFor, 0.1F, skeleton, AnimationDriverComponent{}));
    EXPECT_NEAR(skeleton.pose[0].position.y, 2.0F, 1e-5) << "another slot's montage leaves this one alone";
}

} // namespace UVE::Scene
