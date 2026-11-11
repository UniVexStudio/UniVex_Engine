// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "animation_montage_asset.h"
#include "animation_montage_editing.h"

#include <filesystem>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] AnimationMontageAssetUVE MakeAttackUVE() {
    AnimationMontageAssetUVE montage;
    AddMontageSegmentUVE(montage, AssetGuidUVE{11U}, 1.0);   // 0.0 - 1.0
    AddMontageSegmentUVE(montage, AssetGuidUVE{22U}, 0.5);   // 1.0 - 1.5
    montage.segments[1].loops = 2U;                          // 1.0 - 2.0
    return montage;
}

} // namespace

TEST(AnimationMontageAssetUVETest, SegmentsPlayEndToEnd) {
    AnimationMontageAssetUVE montage = MakeAttackUVE();
    EXPECT_DOUBLE_EQ(MontageLengthUVE(montage), 2.0);
    EXPECT_DOUBLE_EQ(MontageSegmentStartUVE(montage, 1U), 1.0);
    ASSERT_EQ(montage.sections.size(), 1U) << "the first clip brings a Default section";
    EXPECT_EQ(montage.sections.front().name, "Default");

    auto at = MapMontageTimeUVE(montage, 0.25);
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(at->segment, 0U);
    EXPECT_DOUBLE_EQ(at->clipTime, 0.25);
    at = MapMontageTimeUVE(montage, 1.75);   // second loop of the 0.5 s clip
    ASSERT_TRUE(at.has_value());
    EXPECT_EQ(at->segment, 1U);
    EXPECT_NEAR(at->clipTime, 0.25, 1e-9);
    at = MapMontageTimeUVE(montage, 2.0);
    ASSERT_TRUE(at.has_value());
    EXPECT_NEAR(at->clipTime, 0.5, 1e-9) << "the very end shows the clip's last pose";

    montage.segments[0].rate = 2.0;   // the first clip now takes half the time
    EXPECT_DOUBLE_EQ(MontageLengthUVE(montage), 1.5);
    ASSERT_TRUE(MoveMontageSegmentUVE(montage, 1U, 0U));
    EXPECT_EQ(montage.segments[0].clip.value, 22U);
    ASSERT_TRUE(RemoveMontageSegmentUVE(montage, 0U));
    EXPECT_DOUBLE_EQ(MontageLengthUVE(montage), 0.5);
}

TEST(AnimationMontageAssetUVETest, SectionsLinkRenameAndRemove) {
    AnimationMontageAssetUVE montage = MakeAttackUVE();
    ASSERT_TRUE(RenameMontageSectionUVE(montage, "Default", "Start"));
    const std::string attack1 = AddMontageSectionUVE(montage, "Attack1", 0.5);
    const std::string attack2 = AddMontageSectionUVE(montage, "Attack2", 1.0);
    EXPECT_EQ(attack1, "Attack1");
    EXPECT_EQ(FindMontageSectionUVE(montage, "Start")->next, "Attack1") << "a new section continues the one it cuts";
    EXPECT_EQ(FindMontageSectionUVE(montage, "Attack1")->next, "Attack2");
    EXPECT_EQ(AddMontageSectionUVE(montage, "Attack1", 1.5), "Attack1 2") << "names stay unique";
    EXPECT_EQ(MontageSectionAtUVE(montage, 0.75)->name, "Attack1");
    EXPECT_DOUBLE_EQ(MontageSectionEndUVE(montage, "Attack1"), 1.0);

    ASSERT_TRUE(SetMontageSectionNextUVE(montage, "Attack2", "Attack2"));   // loop
    EXPECT_FALSE(SetMontageSectionNextUVE(montage, "Attack2", "Nowhere"));
    ASSERT_TRUE(RenameMontageSectionUVE(montage, "Attack2", "Combo"));
    EXPECT_EQ(FindMontageSectionUVE(montage, "Attack1")->next, "Combo");
    EXPECT_EQ(FindMontageSectionUVE(montage, "Combo")->next, "Combo");
    EXPECT_FALSE(RenameMontageSectionUVE(montage, "Combo", "Start")) << "a taken name is refused";

    ASSERT_TRUE(RemoveMontageSectionUVE(montage, "Attack1"));
    EXPECT_EQ(FindMontageSectionUVE(montage, "Start")->next, "Combo") << "what led to it now leads where it led";
}

TEST(AnimationMontageAssetUVETest, CurvesAreLinearAndClamped) {
    AnimationMontageAssetUVE montage = MakeAttackUVE();
    SetMontageCurveKeyUVE(montage, "Damage", 0.0, 0.0F);
    SetMontageCurveKeyUVE(montage, "Damage", 1.0, 10.0F);
    SetMontageCurveKeyUVE(montage, "Damage", 1.0, 20.0F);   // replaces
    EXPECT_FLOAT_EQ(EvaluateMontageCurveUVE(montage, "Damage", 0.5), 10.0F);
    EXPECT_FLOAT_EQ(EvaluateMontageCurveUVE(montage, "Damage", 5.0), 20.0F);
    EXPECT_FLOAT_EQ(EvaluateMontageCurveUVE(montage, "Missing", 0.5), 0.0F);
}

TEST(AnimationMontageAssetUVETest, SaveThenLoadRoundTrips) {
    const std::filesystem::path path = "uve_animation_montage_round_trip.uvmontage";
    std::filesystem::remove(path);
    AnimationMontageAssetUVE montage = MakeAttackUVE();
    static_cast<void>(AddMontageSectionUVE(montage, "Attack", 1.0));
    ASSERT_TRUE(SetMontageSectionNextUVE(montage, "Attack", "Attack"));
    AddMontageNotifyUVE(montage, "hit", 1.2, 0U);
    AddMontageNotifyStateUVE(montage, "hit_window", 1.1, 1.4, 1U);
    SetMontageCurveKeyUVE(montage, "Speed", 0.5, 2.0F);
    montage.previewEntity = AssetGuidUVE{99U};
    montage.blendInSeconds = 0.1;
    montage.autoBlendOut = false;
    montage.rootMotion = MontageRootMotionUVE::InPlace;
    montage.slot = "UpperBody";
    ASSERT_TRUE(SaveAnimationMontageAssetUVE(montage, path));

    AnimationMontageAssetUVE loaded;
    ASSERT_TRUE(LoadAnimationMontageAssetUVE(path, loaded));
    EXPECT_EQ(loaded, montage);
    EXPECT_EQ(loaded.notifyTracks, 2U);

    AnimationMontageAssetUVE broken = montage;
    broken.sections.push_back(broken.sections.front());   // a duplicate name
    EXPECT_FALSE(SaveAnimationMontageAssetUVE(broken, path));
    std::filesystem::remove(path);
}

} // namespace UVE::Asset::Tests
