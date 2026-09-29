// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Retarget/retarget_test_rig_uve.h"
#include "Support/test_scratch_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/retarget/retarget_files_uve.h"

namespace UVE::Retarget {
namespace {

namespace fs = std::filesystem;
using namespace TestRigUVE;

[[nodiscard]] std::string ReadAllUVE(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct FilesFixtureUVE {
    fs::path dir = Tests::MakeTestCaseDirectoryUVE();
    TPoseRigUVE rig;
    RetargetFilesRequestUVE request;

    FilesFixtureUVE() {
        request.model = dir / "hero.uvmodel";
        request.backupRoot = dir / ".retarget-backup";
        EXPECT_TRUE(Asset::SaveMeshAssetUVE(MakeMeshUVE(rig), request.model));
        for (const char* name : {"idle", "walk"}) {
            const fs::path path = dir / (std::string{name} + ".uvanim");
            EXPECT_TRUE(Asset::SaveAnimationClipAssetUVE(MakeClipUVE(rig, name), path));
            request.animations.push_back(path);
        }
    }
};

TEST(RetargetFilesUVETest, RetargetFilesUVE_ConformsTheModelAndEveryAnimationInPlace) {
    FilesFixtureUVE fixture;
    std::vector<std::string> steps;
    const RetargetFilesResultUVE result = RetargetFilesUVE(fixture.request, GetHumanoidReferenceUVE(),
                                                           [&steps](std::size_t, std::size_t, const std::string& what) { steps.push_back(what); });
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 3U);
    for (const RetargetFileReportUVE& file : result.files) {
        EXPECT_TRUE(file.changed) << file.file << ": " << file.note;
    }
    EXPECT_FALSE(steps.empty());
    EXPECT_NEAR(result.heightScale, 1.1F, 1e-3F);

    // No new files beside them: the two animations, the model and the backup folder.
    std::size_t entries = 0U;
    for ([[maybe_unused]] const fs::directory_entry& entry : fs::directory_iterator(fixture.dir)) {
        ++entries;
    }
    EXPECT_EQ(entries, 4U);

    Asset::MeshAssetUVE mesh;
    ASSERT_TRUE(Asset::LoadMeshAssetUVE(fixture.request.model, mesh));
    ASSERT_TRUE(Asset::IsMeshSkinningDataValidUVE(mesh));
    EXPECT_EQ(mesh.joints.size(), GetHumanoidReferenceUVE().skeleton.bones.size() + 1U);
    EXPECT_EQ(mesh.joints[1].name, "Hips");

    for (const fs::path& path : fixture.request.animations) {
        Asset::AnimationClipAssetUVE clip;
        ASSERT_TRUE(Asset::LoadAnimationClipAssetUVE(path, clip));
        EXPECT_TRUE(clip.conformed);
        EXPECT_EQ(clip.bones.size(), mesh.joints.size());
        EXPECT_EQ(clip.rest.size(), mesh.joints.size());
        EXPECT_EQ(clip.bones[1].bone, "Hips");
        EXPECT_EQ(clip.clipId, path.stem().string());
    }
}

TEST(RetargetFilesUVETest, RestoreRetargetBackupUVE_PutsTheOriginalsBackByteForByte) {
    FilesFixtureUVE fixture;
    const std::string modelBefore = ReadAllUVE(fixture.request.model);
    const std::string idleBefore = ReadAllUVE(fixture.request.animations[0]);
    const RetargetFilesResultUVE result = RetargetFilesUVE(fixture.request, GetHumanoidReferenceUVE());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_NE(ReadAllUVE(fixture.request.model), modelBefore);
    ASSERT_TRUE(fs::is_directory(result.backupDir));
    EXPECT_EQ(result.backupDir.parent_path(), fixture.request.backupRoot);

    std::string error;
    ASSERT_TRUE(RestoreRetargetBackupUVE(result.backupDir, &error)) << error;
    EXPECT_EQ(ReadAllUVE(fixture.request.model), modelBefore);
    EXPECT_EQ(ReadAllUVE(fixture.request.animations[0]), idleBefore);
    EXPECT_FALSE(RestoreRetargetBackupUVE(fixture.dir / "nothing-here", &error));
}

TEST(RetargetFilesUVETest, RetargetFilesUVE_LeavesAnimationsItCannotConformAloneWithAReason) {
    FilesFixtureUVE fixture;
    // "walk" carries no skeleton (an older clip); a third is already conformed.
    Asset::AnimationClipAssetUVE noSkeleton = MakeClipUVE(fixture.rig, "walk");
    noSkeleton.rest.clear();
    ASSERT_TRUE(Asset::SaveAnimationClipAssetUVE(noSkeleton, fixture.request.animations[1]));
    const std::string walkBefore = ReadAllUVE(fixture.request.animations[1]);
    Asset::AnimationClipAssetUVE done = MakeClipUVE(fixture.rig, "run");
    done.conformed = true;
    const fs::path runPath = fixture.dir / "run.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAssetUVE(done, runPath));
    fixture.request.animations.push_back(runPath);

    const RetargetFilesResultUVE result = RetargetFilesUVE(fixture.request, GetHumanoidReferenceUVE());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 4U);
    EXPECT_TRUE(result.files[1].changed);
    EXPECT_FALSE(result.files[2].changed);
    EXPECT_NE(result.files[2].note.find("no skeleton"), std::string::npos) << result.files[2].note;
    EXPECT_FALSE(result.files[3].changed);
    EXPECT_NE(result.files[3].note.find("already conformed"), std::string::npos) << result.files[3].note;
    EXPECT_EQ(ReadAllUVE(fixture.request.animations[1]), walkBefore);
}

TEST(RetargetFilesUVETest, RetargetFilesUVE_ChangesNothingWhenTheCharacterCannotBeConformed) {
    FilesFixtureUVE fixture;
    const std::string idleBefore = ReadAllUVE(fixture.request.animations[0]);
    fixture.request.model = fixture.dir / "missing.uvmodel";
    const RetargetFilesResultUVE result = RetargetFilesUVE(fixture.request, GetHumanoidReferenceUVE());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("missing.uvmodel"), std::string::npos) << result.error;
    EXPECT_EQ(ReadAllUVE(fixture.request.animations[0]), idleBefore);
    EXPECT_FALSE(fs::exists(fixture.request.backupRoot));

    // A static mesh has no skeleton to conform.
    Asset::MeshAssetUVE plain = MakeMeshUVE(fixture.rig);
    plain.joints.clear();
    plain.skinningInfluences.clear();
    fixture.request.model = fixture.dir / "prop.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAssetUVE(plain, fixture.request.model));
    EXPECT_FALSE(RetargetFilesUVE(fixture.request, GetHumanoidReferenceUVE()).ok);
    EXPECT_EQ(ReadAllUVE(fixture.request.animations[0]), idleBefore);
}

} // namespace
} // namespace UVE::Retarget
