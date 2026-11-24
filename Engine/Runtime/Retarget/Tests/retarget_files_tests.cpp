// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "retarget_test_rig.h"
#include "Support/test_scratch.h"
#include "animation_clip_asset.h"
#include "model_animations.h"
#include "retarget_files.h"
#include "skeleton_asset.h"

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
    ASSERT_EQ(result.files.size(), 4U) << "the model, its skeleton and the two animations";
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
    EXPECT_EQ(entries, 5U) << "the two animations, the model, its skeleton (resting in the A-pose) and the backup folder";

    Asset::MeshAssetUVE mesh;
    ASSERT_TRUE(Asset::LoadMeshAssetUVE(fixture.request.model, mesh));
    ASSERT_TRUE(Asset::IsMeshSkinningDataValidUVE(mesh));
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    EXPECT_EQ(mesh.joints.size(),
              static_cast<std::size_t>(std::ranges::count_if(reference.info, [](const HumanoidBoneInfoUVE& info) { return !info.optional; })) + 1U);
    // The character keeps its own rest pose: not one vertex moved.
    const Asset::MeshAssetUVE original = MakeMeshUVE(fixture.rig);
    ASSERT_EQ(mesh.vertices.size(), original.vertices.size());
    for (std::size_t vertex = 0U; vertex < mesh.vertices.size(); ++vertex) {
        EXPECT_EQ(mesh.vertices[vertex].position.x, original.vertices[vertex].position.x);
        EXPECT_EQ(mesh.vertices[vertex].position.y, original.vertices[vertex].position.y);
        EXPECT_EQ(mesh.vertices[vertex].position.z, original.vertices[vertex].position.z);
    }
    EXPECT_EQ(result.meshDistortion.maximumEdgeChange, 0.0F);
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

TEST(RetargetFilesUVETest, RetargetFilesUVE_ConformsTheMeshNodesPublishedBesideTheCharacterToo) {
    FilesFixtureUVE fixture;
    // A character imported from a file with several mesh nodes has a model per node beside it.
    const fs::path body = fixture.dir / "hero.part-Body.uvmodel";
    const fs::path joints = fixture.dir / "hero.part-Joints.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAssetUVE(MakeMeshUVE(fixture.rig), body));
    ASSERT_TRUE(Asset::SaveMeshAssetUVE(MakeMeshUVE(fixture.rig), joints));
    const RetargetFilesResultUVE result = RetargetFilesUVE(fixture.request, GetHumanoidReferenceUVE(), {});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 6U) << "the model, its skeleton, its two mesh nodes and the two animations";
    Asset::MeshAssetUVE model;
    ASSERT_TRUE(Asset::LoadMeshAssetUVE(fixture.request.model, model));
    for (const fs::path& path : {body, joints}) {
        Asset::MeshAssetUVE part;
        ASSERT_TRUE(Asset::LoadMeshAssetUVE(path, part));
        ASSERT_TRUE(Asset::IsMeshSkinningDataValidUVE(part));
        ASSERT_EQ(part.joints.size(), model.joints.size()) << "a mesh node binds to the same conformed skeleton";
        EXPECT_EQ(part.joints[1].name, "Hips");
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
    ASSERT_EQ(result.files.size(), 5U);
    EXPECT_TRUE(result.files[1].changed) << "the skeleton";
    EXPECT_TRUE(result.files[2].changed);
    EXPECT_FALSE(result.files[3].changed);
    EXPECT_NE(result.files[3].note.find("no skeleton"), std::string::npos) << result.files[3].note;
    EXPECT_FALSE(result.files[4].changed);
    EXPECT_NE(result.files[4].note.find("already conformed"), std::string::npos) << result.files[4].note;
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

TEST(RetargetFilesUVETest, RetargetFilesUVE_ConformsFiftyEmbeddedAnimationsInOneWriteAndRestoresOnUndo) {
    const fs::path dir = Tests::MakeTestCaseDirectoryUVE();
    const TPoseRigUVE rig;
    const fs::path model = dir / "hero.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAssetUVE(MakeMeshUVE(rig), model));

    constexpr std::size_t kTakeCount = 50U;
    std::vector<Asset::AnimationClipAssetUVE> embeddedClips;
    embeddedClips.reserve(kTakeCount);
    RetargetFilesRequestUVE request;
    request.model = model;
    request.backupRoot = dir / ".retarget-backup";
    request.useBrain = false;
    for (std::size_t i = 0U; i < kTakeCount; ++i) {
        const std::string name = "Anim_" + std::to_string(i);
        embeddedClips.push_back(MakeClipUVE(rig, name));
        request.animations.push_back(Asset::MakeEmbeddedClipPathUVE(model, name));
    }
    constexpr std::uint64_t kStamp = 0x123456789ABCDEF0ULL;
    ASSERT_TRUE(Asset::SaveModelAnimationsUVE(model, embeddedClips, kStamp));
    const std::string modelBefore = ReadAllUVE(model);

    const RetargetFilesResultUVE result = RetargetFilesUVE(request, GetHumanoidReferenceUVE());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 2U + kTakeCount);
    for (const RetargetFileReportUVE& entry : result.files) {
        EXPECT_TRUE(entry.changed) << entry.file << ": " << entry.note;
    }

    // All 50 embedded takes are now conformed with humanoid bones, and the source stamp is preserved.
    std::vector<Asset::AnimationClipAssetUVE> afterTakes;
    std::uint64_t afterStamp = 0U;
    ASSERT_TRUE(Asset::LoadAllModelAnimationsUVE(model, afterTakes, &afterStamp));
    ASSERT_EQ(afterTakes.size(), kTakeCount);
    EXPECT_EQ(afterStamp, kStamp);
    for (std::size_t i = 0U; i < kTakeCount; ++i) {
        EXPECT_EQ(afterTakes[i].clipId, "Anim_" + std::to_string(i));
        EXPECT_TRUE(afterTakes[i].conformed);
        EXPECT_FALSE(afterTakes[i].bones.empty());
        EXPECT_EQ(afterTakes[i].bones[1].bone, "Hips");
    }

    // Sibling .uvskel was created in the A-pose.
    fs::path skelPath = model;
    skelPath.replace_extension(".uvskel");
    Asset::SkeletonAssetUVE skel;
    ASSERT_TRUE(Asset::LoadSkeletonAssetUVE(skelPath, skel));
    EXPECT_FALSE(skel.joints.empty());

    // Undo restores the .uvmodel (including all 50 pre-retarget takes) byte-for-byte and removes .uvskel.
    std::string restoreError;
    ASSERT_TRUE(RestoreRetargetBackupUVE(result.backupDir, &restoreError)) << restoreError;
    EXPECT_EQ(ReadAllUVE(model), modelBefore);
    EXPECT_FALSE(fs::exists(skelPath));
}

TEST(RetargetFilesUVETest, RetargetFilesUVE_ConformsEmbeddedTakesOnlyOnAlreadyConformedModelAndBacksUpModel) {
    const fs::path dir = Tests::MakeTestCaseDirectoryUVE();
    const TPoseRigUVE rig;
    const fs::path model = dir / "hero.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAssetUVE(MakeMeshUVE(rig), model));

    // Step 1: conform the character mesh alone first.
    RetargetFilesRequestUVE firstReq;
    firstReq.model = model;
    firstReq.backupRoot = dir / ".backup-first";
    firstReq.useBrain = false;
    ASSERT_TRUE(RetargetFilesUVE(firstReq, GetHumanoidReferenceUVE()).ok);

    // Step 2: embed 10 un-conformed takes into the already-conformed .uvmodel.
    std::vector<Asset::AnimationClipAssetUVE> rawTakes;
    RetargetFilesRequestUVE takesReq;
    takesReq.backupRoot = dir / ".backup-takes";
    takesReq.useBrain = false;
    for (std::size_t i = 0U; i < 10U; ++i) {
        const std::string name = "Take_" + std::to_string(i);
        rawTakes.push_back(MakeClipUVE(rig, name));
        takesReq.animations.push_back(Asset::MakeEmbeddedClipPathUVE(model, name));
    }
    ASSERT_TRUE(Asset::SaveModelAnimationsUVE(model, rawTakes, 42U));
    const std::string modelBeforeTakes = ReadAllUVE(model);

    // Step 3: conform ONLY the embedded takes (request.model is empty!).
    const RetargetFilesResultUVE result = RetargetFilesUVE(takesReq, GetHumanoidReferenceUVE());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 10U);
    for (const RetargetFileReportUVE& entry : result.files) {
        EXPECT_TRUE(entry.changed) << entry.file << ": " << entry.note;
    }
    ASSERT_FALSE(result.backupDir.empty());

    std::vector<Asset::AnimationClipAssetUVE> afterTakes;
    ASSERT_TRUE(Asset::LoadAllModelAnimationsUVE(model, afterTakes));
    ASSERT_EQ(afterTakes.size(), 10U);
    for (const Asset::AnimationClipAssetUVE& clip : afterTakes) {
        EXPECT_TRUE(clip.conformed);
    }

    // Undo restores the real backing .uvmodel file byte-for-byte.
    std::string restoreError;
    ASSERT_TRUE(RestoreRetargetBackupUVE(result.backupDir, &restoreError)) << restoreError;
    EXPECT_EQ(ReadAllUVE(model), modelBeforeTakes);
}

TEST(RetargetFilesUVETest, RetargetFilesUVE_StreamsLargeBatchOfLooseAnimationsWithRigCacheAndUndo) {
    const fs::path dir = Tests::MakeTestCaseDirectoryUVE();
    const TPoseRigUVE rig;
    constexpr std::size_t kClipCount = 120U;
    RetargetFilesRequestUVE request;
    request.backupRoot = dir / ".retarget-backup";
    request.useBrain = false;
    std::vector<std::string> originals;
    originals.reserve(kClipCount);
    for (std::size_t i = 0U; i < kClipCount; ++i) {
        const fs::path clipPath = dir / ("anim_" + std::to_string(i) + ".uvanim");
        ASSERT_TRUE(Asset::SaveAnimationClipAssetUVE(MakeClipUVE(rig, "anim_" + std::to_string(i)), clipPath));
        if (i == 0U || i == kClipCount - 1U) {
            originals.push_back(ReadAllUVE(clipPath));
        }
        request.animations.push_back(clipPath);
    }

    const RetargetFilesResultUVE result = RetargetFilesUVE(request, GetHumanoidReferenceUVE());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), kClipCount);
    for (std::size_t i = 0U; i < kClipCount; ++i) {
        EXPECT_TRUE(result.files[i].changed);
        const Asset::AnimationClipSummaryUVE summary = Asset::InspectAnimationClipFileSummaryUVE(request.animations[i]);
        EXPECT_TRUE(summary.valid);
        EXPECT_TRUE(summary.conformed);
    }

    std::string restoreError;
    ASSERT_TRUE(RestoreRetargetBackupUVE(result.backupDir, &restoreError)) << restoreError;
    EXPECT_EQ(ReadAllUVE(request.animations.front()), originals.front());
    EXPECT_EQ(ReadAllUVE(request.animations.back()), originals.back());
}

} // namespace
} // namespace UVE::Retarget
