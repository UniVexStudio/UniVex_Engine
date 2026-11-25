// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "motion_montage_asset.h"
#include "motion_montage_editing.h"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] MotionMontageAsset MakeAttack() {
    MotionMontageAsset montage;
    AddMontageSegment(montage, AssetGuid{11U}, 1.0);   // 0.0 - 1.0
    AddMontageSegment(montage, AssetGuid{22U}, 0.5);   // 1.0 - 1.5
    montage.segments[1].loops = 2U;                          // 1.0 - 2.0
    return montage;
}

} // namespace

TEST(MotionMontageAssetTest, SegmentsPlayEndToEnd) {
    MotionMontageAsset montage = MakeAttack();
    EXPECT_DOUBLE_EQ(MontageLength(montage), 2.0);
    EXPECT_DOUBLE_EQ(MontageSegmentStart(montage, 1U), 1.0);
    ASSERT_EQ(montage.sections.size(), 1U) << "the first clip brings a Default section";
    EXPECT_EQ(montage.sections.front().name, "Default");

    auto at = MapMontageTime(montage, 0.25);
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(at->segment, 0U);
    EXPECT_DOUBLE_EQ(at->clipTime, 0.25);
    at = MapMontageTime(montage, 1.75);   // second loop of the 0.5 s clip
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(at->segment, 1U);
    EXPECT_NEAR(at->clipTime, 0.25, 1e-9);
    at = MapMontageTime(montage, 2.0);
    ASSERT_TRUE(at.has_value());
    EXPECT_NEAR(at->clipTime, 0.5, 1e-9) << "the very end shows the clip's last pose";

    montage.segments[0].rate = 2.0;   // the first clip now takes half the time
    EXPECT_DOUBLE_EQ(MontageLength(montage), 1.5);
    ASSERT_TRUE(MoveMontageSegment(montage, 1U, 0U));
    EXPECT_EQ(montage.segments[0].clip.value, 22U);
    ASSERT_TRUE(RemoveMontageSegment(montage, 0U));
    EXPECT_DOUBLE_EQ(MontageLength(montage), 0.5);
}

TEST(MotionMontageAssetTest, SectionsLinkRenameAndRemove) {
    MotionMontageAsset montage = MakeAttack();
    ASSERT_TRUE(RenameMontageSection(montage, "Default", "Start"));
    const std::string attack1 = AddMontageSection(montage, "Attack1", 0.5);
    const std::string attack2 = AddMontageSection(montage, "Attack2", 1.0);
    EXPECT_EQ(attack1, "Attack1");
    EXPECT_EQ(FindMontageSection(montage, "Start")->next, "Attack1") << "a new section continues the one it cuts";
    EXPECT_EQ(FindMontageSection(montage, "Attack1")->next, "Attack2");
    EXPECT_EQ(AddMontageSection(montage, "Attack1", 1.5), "Attack1 2") << "names stay unique";
    EXPECT_EQ(MontageSectionAt(montage, 0.75)->name, "Attack1");
    EXPECT_DOUBLE_EQ(MontageSectionEnd(montage, "Attack1"), 1.0);

    ASSERT_TRUE(SetMontageSectionNext(montage, "Attack2", "Attack2"));   // loop
    EXPECT_FALSE(SetMontageSectionNext(montage, "Attack2", "Nowhere"));
    ASSERT_TRUE(RenameMontageSection(montage, "Attack2", "Combo"));
    EXPECT_EQ(FindMontageSection(montage, "Attack1")->next, "Combo");
    EXPECT_EQ(FindMontageSection(montage, "Combo")->next, "Combo");
    EXPECT_FALSE(RenameMontageSection(montage, "Combo", "Start")) << "a taken name is refused";

    ASSERT_TRUE(RemoveMontageSection(montage, "Attack1"));
    EXPECT_EQ(FindMontageSection(montage, "Start")->next, "Combo") << "what led to it now leads where it led";
}

TEST(MotionMontageAssetTest, CurvesAreLinearAndClamped) {
    MotionMontageAsset montage = MakeAttack();
    SetMontageCurveKey(montage, "Damage", 0.0, 0.0F);
    SetMontageCurveKey(montage, "Damage", 1.0, 10.0F);
    SetMontageCurveKey(montage, "Damage", 1.0, 20.0F);   // replaces
    EXPECT_FLOAT_EQ(EvaluateMontageCurve(montage, "Damage", 0.5), 10.0F);
    EXPECT_FLOAT_EQ(EvaluateMontageCurve(montage, "Damage", 5.0), 20.0F);
    EXPECT_FLOAT_EQ(EvaluateMontageCurve(montage, "Missing", 0.5), 0.0F);
}

TEST(MotionMontageAssetTest, SaveThenLoadRoundTrips) {
    const std::filesystem::path path = "uve_motion_montage_round_trip.uvmontage";
    std::filesystem::remove(path);
    MotionMontageAsset montage = MakeAttack();
    static_cast<void>(AddMontageSection(montage, "Attack", 1.0));
    ASSERT_TRUE(SetMontageSectionNext(montage, "Attack", "Attack"));
    AddMontageNotify(montage, "hit", 1.2, 0U);
    AddMontageNotifyState(montage, "hit_window", 1.1, 1.4, 1U);
    SetMontageCurveKey(montage, "Speed", 0.5, 2.0F);
    montage.previewEntity = AssetGuid{99U};
    montage.blendInSeconds = 0.1;
    montage.autoBlendOut = false;
    montage.rootMotion = MontageRootMotion::InPlace;
    montage.slot = "UpperBody";
    ASSERT_TRUE(SaveMotionMontageAsset(montage, path));

    MotionMontageAsset loaded;
    ASSERT_TRUE(LoadMotionMontageAsset(path, loaded));
    EXPECT_EQ(loaded, montage);
    EXPECT_EQ(loaded.notifyTracks, 2U);

    MotionMontageAsset broken = montage;
    broken.sections.push_back(broken.sections.front());   // a duplicate name
    EXPECT_FALSE(SaveMotionMontageAsset(broken, path));
    std::filesystem::remove(path);
}

} // namespace UVE::Asset::Tests

namespace UVE::Asset::Tests {

TEST(MotionMontageAssetTest, NotifiesCanBeLaidOutBeforeAnyClip) {
    MotionMontageAsset montage;
    const std::size_t notify = AddMontageNotify(montage, "step", 0.6, 0U);
    EXPECT_DOUBLE_EQ(montage.notifies[notify].time, 0.6) << "not pulled back to 0 when the montage has no clip yet";
    AddMontageNotify(montage, "late", 5.0, 0U);
    EXPECT_DOUBLE_EQ(montage.notifies.back().time, kMontageEmptyTimelineSeconds);
}

TEST(MotionMontageAssetTest, NotifyTracksAreNamedAndRemoved) {
    MotionMontageAsset montage = MakeAttack();
    montage.notifyTracks = 3U;
    AddMontageNotify(montage, "a", 0.1, 0U);
    AddMontageNotify(montage, "b", 0.2, 1U);
    AddMontageNotifyState(montage, "c", 0.3, 0.5, 2U);
    EXPECT_EQ(MontageNotifyTrackName(montage, 1U), "Notify 2");
    SetMontageNotifyTrackName(montage, 2U, "Sounds");
    EXPECT_EQ(MontageNotifyTrackName(montage, 2U), "Sounds");
    ASSERT_TRUE(RemoveMontageNotifyTrack(montage, 1U));
    EXPECT_EQ(montage.notifyTracks, 2U);
    ASSERT_EQ(montage.notifies.size(), 1U) << "what was on the removed track goes with it";
    ASSERT_EQ(montage.notifyStates.size(), 1U);
    EXPECT_EQ(montage.notifyStates.front().track, 1U) << "the tracks below move up";
    EXPECT_EQ(MontageNotifyTrackName(montage, 1U), "Sounds") << "and keep their names";
    EXPECT_TRUE(RemoveMontageNotifyTrack(montage, 1U));
    EXPECT_FALSE(RemoveMontageNotifyTrack(montage, 0U)) << "the last track stays";
}

TEST(MotionMontageAssetTest, NotifyColoursAndTrackNamesRoundTrip) {
    MotionMontageAsset montage = MakeAttack();
    AddMontageNotify(montage, "hit", 0.5, 0U);
    montage.notifies.front().colour = 0xE05A5AU;
    SetMontageNotifyTrackName(montage, 0U, "Combat");
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "uve_montage_colour_test.uvmontage";
    ASSERT_TRUE(SaveMotionMontageAsset(montage, path));
    MotionMontageAsset read;
    ASSERT_TRUE(LoadMotionMontageAsset(path, read));
    EXPECT_EQ(read, montage);
    std::filesystem::remove(path);
}

} // namespace UVE::Asset::Tests
