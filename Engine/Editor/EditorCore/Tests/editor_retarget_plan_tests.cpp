// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "retarget_test_rig.h"
#include "Support/test_scratch.h"
#include "animation_clip_asset.h"
#include "editor_retarget_plan.h"
#include "model_animations.h"

namespace UVE::Editor {
namespace {

namespace fs = std::filesystem;
using namespace Retarget::TestRig;
using Retarget::JointStatus;
using Retarget::RetargetAnimationState;

struct PlanFixture {
    fs::path dir = Tests::MakeTestCaseDirectory();
    TPoseRig rig;
    fs::path model = dir / "hero.uvmodel";
    std::vector<fs::path> animations;

    PlanFixture() {
        EXPECT_TRUE(Asset::SaveMeshAsset(MakeMesh(rig), model));
        Asset::AnimationClipAsset fresh = MakeClip(rig, "idle");
        Asset::AnimationClipAsset noSkeleton = MakeClip(rig, "old");
        noSkeleton.rest.clear();
        Asset::AnimationClipAsset done = MakeClip(rig, "done");
        done.conformed = true;
        for (const auto& [name, clip] : {std::pair{"idle", fresh}, std::pair{"old", noSkeleton}, std::pair{"done", done}}) {
            const fs::path path = dir / (std::string{name} + ".uvanim");
            EXPECT_TRUE(Asset::SaveAnimationClipAsset(clip, path));
            animations.push_back(path);
        }
    }
};

TEST(RetargetPlanTest, TheHumanoidsBonesAreGradedAgainstTheCharacter) {
    const PlanFixture fixture;
    const RetargetPlan plan = PlanRetarget(fixture.model, fixture.animations);
    ASSERT_TRUE(plan.modelReadable) << plan.modelError;
    EXPECT_FALSE(plan.modelAlreadyConformed);
    EXPECT_NEAR(plan.heightScale, 1.1F, 1e-3F);
    ASSERT_EQ(plan.joints.size(), Retarget::GetHumanoidReference().skeleton.bones.size());
    EXPECT_EQ(plan.joints[0].depth, 0);
    EXPECT_GT(plan.joints[1].depth, 0) << "the hips sit under the root";
    // The auto-rigger's character has the main bones (green) and lacks the humanoid's helpers (grey).
    EXPECT_GT(plan.Count(JointStatus::Good), 20U);
    EXPECT_GT(plan.Count(JointStatus::Missing), 100U);
    EXPECT_EQ(plan.Count(JointStatus::Broken), 0U);
    std::size_t rows = 0U;
    for (const RetargetJointRow& joint : plan.joints) {
        ++rows;
        if (joint.status == JointStatus::Good) {
            EXPECT_FALSE(joint.characterName.empty());
        } else if (joint.status == JointStatus::Missing) {
            EXPECT_TRUE(joint.characterName.empty());
        }
    }
    EXPECT_EQ(rows, plan.joints.size());
    EXPECT_EQ(plan.keptBones, (std::vector<std::string>{"HeadTop_End"}));
    EXPECT_STREQ(GetRetargetStatusLabel(JointStatus::Missing), "missing");
}

TEST(RetargetPlanTest, EachAnimationSaysWhetherItWillBeConformedAndWhyNot) {
    const PlanFixture fixture;
    const RetargetPlan plan = PlanRetarget(fixture.model, fixture.animations);
    ASSERT_EQ(plan.animations.size(), 3U);
    EXPECT_EQ(plan.animations[0].state, RetargetAnimationState::Ready);
    EXPECT_EQ(plan.animations[1].state, RetargetAnimationState::NoSkeleton);
    EXPECT_NE(plan.animations[1].note.find("import the FBX again"), std::string::npos);
    EXPECT_EQ(plan.animations[2].state, RetargetAnimationState::AlreadyConformed);
    EXPECT_EQ(plan.ReadyAnimations(), 1U);
    EXPECT_TRUE(plan.CanGenerate());
}

TEST(RetargetPlanTest, ACharacterThatCannotBeReadCannotBeGenerated) {
    const PlanFixture fixture;
    const RetargetPlan missing = PlanRetarget(fixture.dir / "nobody.uvmodel", fixture.animations);
    EXPECT_FALSE(missing.modelReadable);
    EXPECT_NE(missing.modelError.find("nobody.uvmodel"), std::string::npos);
    EXPECT_FALSE(missing.CanGenerate());
    EXPECT_TRUE(missing.joints.empty());

    // A static mesh has no skeleton to conform.
    Asset::MeshAsset plain = MakeMesh(fixture.rig);
    plain.joints.clear();
    plain.skinningInfluences.clear();
    const fs::path prop = fixture.dir / "prop.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAsset(plain, prop));
    const RetargetPlan staticPlan = PlanRetarget(prop, fixture.animations);
    EXPECT_FALSE(staticPlan.modelReadable);
    EXPECT_FALSE(staticPlan.modelError.empty());
}

TEST(RetargetPlanTest, AConformedCharacterIsRecognisedAndNeedsNothingWithoutAnimations) {
    const PlanFixture fixture;
    Retarget::RetargetFilesRequest request;
    request.model = fixture.model;
    request.backupRoot = fixture.dir / ".backup";
    ASSERT_TRUE(Retarget::RetargetFiles(request, Retarget::GetHumanoidReference()).ok);
    const RetargetPlan plan = PlanRetarget(fixture.model, {});
    ASSERT_TRUE(plan.modelReadable) << plan.modelError;
    EXPECT_TRUE(plan.modelAlreadyConformed);
    EXPECT_EQ(plan.Count(JointStatus::Missing), 0U);
    EXPECT_EQ(plan.Count(JointStatus::Good), plan.joints.size());
    EXPECT_FALSE(plan.CanGenerate()) << "nothing to conform";
}

TEST(RetargetPlanTest, EmbeddedAnimationsAreDiscoveredEvenWhenCharacterIsAlreadyConformed) {
    const PlanFixture fixture;
    Retarget::RetargetFilesRequest request;
    request.model = fixture.model;
    request.backupRoot = fixture.dir / ".backup";
    request.useBrain = false;
    ASSERT_TRUE(Retarget::RetargetFiles(request, Retarget::GetHumanoidReference()).ok);

    // Embed 10 un-conformed takes into the already-conformed .uvmodel.
    std::vector<Asset::AnimationClipAsset> takes;
    for (std::size_t i = 0U; i < 10U; ++i) {
        takes.push_back(MakeClip(fixture.rig, "Take_" + std::to_string(i)));
    }
    ASSERT_TRUE(Asset::SaveModelAnimations(fixture.model, takes, 7U));

    const RetargetPlan plan = PlanRetarget(fixture.model, {});
    ASSERT_TRUE(plan.modelReadable) << plan.modelError;
    EXPECT_TRUE(plan.modelAlreadyConformed);
    ASSERT_EQ(plan.animations.size(), 10U);
    EXPECT_EQ(plan.ReadyAnimations(), 10U);
    EXPECT_TRUE(plan.CanGenerate()) << "embedded animations still need conforming";

    // Animation-only plan with all clips already conformed cannot generate.
    const RetargetPlan allDoneAnimOnly = PlanRetarget({}, std::span<const fs::path>(&fixture.animations[2], 1U));
    EXPECT_EQ(allDoneAnimOnly.ReadyAnimations(), 0U);
    EXPECT_FALSE(allDoneAnimOnly.CanGenerate());

    // Pre-set cancel flag exits immediately without scanning animations.
    std::atomic<bool> cancel{true};
    const RetargetPlan cancelled = PlanRetarget(fixture.model, {}, {}, &cancel);
    EXPECT_TRUE(cancelled.animations.empty());
}

} // namespace
} // namespace UVE::Editor
