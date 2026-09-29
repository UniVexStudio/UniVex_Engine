// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Retarget/retarget_test_rig_uve.h"
#include "Support/test_scratch_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/editor/editor_retarget_plan_uve.h"

namespace UVE::Editor {
namespace {

namespace fs = std::filesystem;
using namespace Retarget::TestRigUVE;
using Retarget::JointStatusUVE;
using Retarget::RetargetAnimationStateUVE;

struct PlanFixtureUVE {
    fs::path dir = Tests::MakeTestCaseDirectoryUVE();
    TPoseRigUVE rig;
    fs::path model = dir / "hero.uvmodel";
    std::vector<fs::path> animations;

    PlanFixtureUVE() {
        EXPECT_TRUE(Asset::SaveMeshAssetUVE(MakeMeshUVE(rig), model));
        Asset::AnimationClipAssetUVE fresh = MakeClipUVE(rig, "idle");
        Asset::AnimationClipAssetUVE noSkeleton = MakeClipUVE(rig, "old");
        noSkeleton.rest.clear();
        Asset::AnimationClipAssetUVE done = MakeClipUVE(rig, "done");
        done.conformed = true;
        for (const auto& [name, clip] : {std::pair{"idle", fresh}, std::pair{"old", noSkeleton}, std::pair{"done", done}}) {
            const fs::path path = dir / (std::string{name} + ".uvanim");
            EXPECT_TRUE(Asset::SaveAnimationClipAssetUVE(clip, path));
            animations.push_back(path);
        }
    }
};

TEST(RetargetPlanUVETest, TheHumanoidsBonesAreGradedAgainstTheCharacter) {
    const PlanFixtureUVE fixture;
    const RetargetPlanUVE plan = PlanRetargetUVE(fixture.model, fixture.animations);
    ASSERT_TRUE(plan.modelReadable) << plan.modelError;
    EXPECT_FALSE(plan.modelAlreadyConformed);
    EXPECT_NEAR(plan.heightScale, 1.1F, 1e-3F);
    ASSERT_EQ(plan.joints.size(), Retarget::GetHumanoidReferenceUVE().skeleton.bones.size());
    EXPECT_EQ(plan.joints[0].depth, 0);
    EXPECT_GT(plan.joints[1].depth, 0) << "the hips sit under the root";
    // The auto-rigger's character has the main bones (green) and lacks the humanoid's helpers (grey).
    EXPECT_GT(plan.CountUVE(JointStatusUVE::Good), 20U);
    EXPECT_GT(plan.CountUVE(JointStatusUVE::Missing), 100U);
    EXPECT_EQ(plan.CountUVE(JointStatusUVE::Broken), 0U);
    std::size_t rows = 0U;
    for (const RetargetJointRowUVE& joint : plan.joints) {
        ++rows;
        if (joint.status == JointStatusUVE::Good) {
            EXPECT_FALSE(joint.characterName.empty());
        } else if (joint.status == JointStatusUVE::Missing) {
            EXPECT_TRUE(joint.characterName.empty());
        }
    }
    EXPECT_EQ(rows, plan.joints.size());
    EXPECT_EQ(plan.keptBones, (std::vector<std::string>{"HeadTop_End"}));
    EXPECT_STREQ(GetRetargetStatusLabelUVE(JointStatusUVE::Missing), "missing");
}

TEST(RetargetPlanUVETest, EachAnimationSaysWhetherItWillBeConformedAndWhyNot) {
    const PlanFixtureUVE fixture;
    const RetargetPlanUVE plan = PlanRetargetUVE(fixture.model, fixture.animations);
    ASSERT_EQ(plan.animations.size(), 3U);
    EXPECT_EQ(plan.animations[0].state, RetargetAnimationStateUVE::Ready);
    EXPECT_EQ(plan.animations[1].state, RetargetAnimationStateUVE::NoSkeleton);
    EXPECT_NE(plan.animations[1].note.find("import the FBX again"), std::string::npos);
    EXPECT_EQ(plan.animations[2].state, RetargetAnimationStateUVE::AlreadyConformed);
    EXPECT_EQ(plan.ReadyAnimationsUVE(), 1U);
    EXPECT_TRUE(plan.CanGenerateUVE());
}

TEST(RetargetPlanUVETest, ACharacterThatCannotBeReadCannotBeGenerated) {
    const PlanFixtureUVE fixture;
    const RetargetPlanUVE missing = PlanRetargetUVE(fixture.dir / "nobody.uvmodel", fixture.animations);
    EXPECT_FALSE(missing.modelReadable);
    EXPECT_NE(missing.modelError.find("nobody.uvmodel"), std::string::npos);
    EXPECT_FALSE(missing.CanGenerateUVE());
    EXPECT_TRUE(missing.joints.empty());

    // A static mesh has no skeleton to conform.
    Asset::MeshAssetUVE plain = MakeMeshUVE(fixture.rig);
    plain.joints.clear();
    plain.skinningInfluences.clear();
    const fs::path prop = fixture.dir / "prop.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAssetUVE(plain, prop));
    const RetargetPlanUVE staticPlan = PlanRetargetUVE(prop, fixture.animations);
    EXPECT_FALSE(staticPlan.modelReadable);
    EXPECT_FALSE(staticPlan.modelError.empty());
}

TEST(RetargetPlanUVETest, AConformedCharacterIsRecognisedAndNeedsNothingWithoutAnimations) {
    const PlanFixtureUVE fixture;
    Retarget::RetargetFilesRequestUVE request;
    request.model = fixture.model;
    request.backupRoot = fixture.dir / ".backup";
    ASSERT_TRUE(Retarget::RetargetFilesUVE(request, Retarget::GetHumanoidReferenceUVE()).ok);
    const RetargetPlanUVE plan = PlanRetargetUVE(fixture.model, {});
    ASSERT_TRUE(plan.modelReadable) << plan.modelError;
    EXPECT_TRUE(plan.modelAlreadyConformed);
    EXPECT_EQ(plan.CountUVE(JointStatusUVE::Missing), 0U);
    EXPECT_EQ(plan.CountUVE(JointStatusUVE::Good), plan.joints.size());
    EXPECT_FALSE(plan.CanGenerateUVE()) << "nothing to conform";
}

} // namespace
} // namespace UVE::Editor
