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
using namespace TestRig;

[[nodiscard]] std::string ReadAll(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct FilesFixture {
    fs::path dir = Tests::MakeTestCaseDirectory();
    TPoseRig rig;
    RetargetFilesRequest request;

    FilesFixture() {
        request.model = dir / "hero.uvmodel";
        request.backupRoot = dir / ".retarget-backup";
        EXPECT_TRUE(Asset::SaveMeshAsset(MakeMesh(rig), request.model));
        for (const char* name : {"idle", "walk"}) {
            const fs::path path = dir / (std::string{name} + ".uvanim");
            EXPECT_TRUE(Asset::SaveAnimationClipAsset(MakeClip(rig, name), path));
            request.animations.push_back(path);
        }
    }
};

TEST(RetargetFilesTest, RetargetFiles_ConformsTheModelAndEveryAnimationInPlace) {
    FilesFixture fixture;
    std::vector<std::string> steps;
    const RetargetFilesResult result = RetargetFiles(fixture.request, GetHumanoidReference(),
                                                           [&steps](std::size_t, std::size_t, const std::string& what) { steps.push_back(what); });
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 4U) << "the model, its skeleton and the two animations";
    for (const RetargetFileReport& file : result.files) {
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

    Asset::MeshAsset mesh;
    ASSERT_TRUE(Asset::LoadMeshAsset(fixture.request.model, mesh));
    ASSERT_TRUE(Asset::IsMeshSkinningDataValid(mesh));
    const HumanoidReference& reference = GetHumanoidReference();
    EXPECT_EQ(mesh.joints.size(),
              static_cast<std::size_t>(std::ranges::count_if(reference.info, [](const HumanoidBoneInfo& info) { return !info.optional; })) + 1U);
    // The character keeps its own rest pose: not one vertex moved.
    const Asset::MeshAsset original = MakeMesh(fixture.rig);
    ASSERT_EQ(mesh.vertices.size(), original.vertices.size());
    for (std::size_t vertex = 0U; vertex < mesh.vertices.size(); ++vertex) {
        EXPECT_EQ(mesh.vertices[vertex].position.x, original.vertices[vertex].position.x);
        EXPECT_EQ(mesh.vertices[vertex].position.y, original.vertices[vertex].position.y);
        EXPECT_EQ(mesh.vertices[vertex].position.z, original.vertices[vertex].position.z);
    }
    EXPECT_EQ(result.meshDistortion.maximumEdgeChange, 0.0F);
    EXPECT_EQ(mesh.joints[1].name, "Hips");

    for (const fs::path& path : fixture.request.animations) {
        Asset::AnimationClipAsset clip;
        ASSERT_TRUE(Asset::LoadAnimationClipAsset(path, clip));
        EXPECT_TRUE(clip.conformed);
        EXPECT_EQ(clip.bones.size(), mesh.joints.size());
        EXPECT_EQ(clip.rest.size(), mesh.joints.size());
        EXPECT_EQ(clip.bones[1].bone, "Hips");
        EXPECT_EQ(clip.clipId, path.stem().string());
    }
}

TEST(RetargetFilesTest, RetargetFiles_ConformsTheMeshNodesPublishedBesideTheCharacterToo) {
    FilesFixture fixture;
    // A character imported from a file with several mesh nodes has a model per node beside it.
    const fs::path body = fixture.dir / "hero.part-Body.uvmodel";
    const fs::path joints = fixture.dir / "hero.part-Joints.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAsset(MakeMesh(fixture.rig), body));
    ASSERT_TRUE(Asset::SaveMeshAsset(MakeMesh(fixture.rig), joints));
    const RetargetFilesResult result = RetargetFiles(fixture.request, GetHumanoidReference(), {});
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 6U) << "the model, its skeleton, its two mesh nodes and the two animations";
    Asset::MeshAsset model;
    ASSERT_TRUE(Asset::LoadMeshAsset(fixture.request.model, model));
    for (const fs::path& path : {body, joints}) {
        Asset::MeshAsset part;
        ASSERT_TRUE(Asset::LoadMeshAsset(path, part));
        ASSERT_TRUE(Asset::IsMeshSkinningDataValid(part));
        ASSERT_EQ(part.joints.size(), model.joints.size()) << "a mesh node binds to the same conformed skeleton";
        EXPECT_EQ(part.joints[1].name, "Hips");
    }
}

TEST(RetargetFilesTest, RestoreRetargetBackup_PutsTheOriginalsBackByteForByte) {
    FilesFixture fixture;
    const std::string modelBefore = ReadAll(fixture.request.model);
    const std::string idleBefore = ReadAll(fixture.request.animations[0]);
    const RetargetFilesResult result = RetargetFiles(fixture.request, GetHumanoidReference());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_NE(ReadAll(fixture.request.model), modelBefore);
    ASSERT_TRUE(fs::is_directory(result.backupDir));
    EXPECT_EQ(result.backupDir.parent_path(), fixture.request.backupRoot);

    std::string error;
    ASSERT_TRUE(RestoreRetargetBackup(result.backupDir, &error)) << error;
    EXPECT_EQ(ReadAll(fixture.request.model), modelBefore);
    EXPECT_EQ(ReadAll(fixture.request.animations[0]), idleBefore);
    EXPECT_FALSE(RestoreRetargetBackup(fixture.dir / "nothing-here", &error));
}

TEST(RetargetFilesTest, RetargetFiles_LeavesAnimationsItCannotConformAloneWithAReason) {
    FilesFixture fixture;
    // "walk" carries no skeleton (an older clip); a third is already conformed.
    Asset::AnimationClipAsset noSkeleton = MakeClip(fixture.rig, "walk");
    noSkeleton.rest.clear();
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(noSkeleton, fixture.request.animations[1]));
    const std::string walkBefore = ReadAll(fixture.request.animations[1]);
    Asset::AnimationClipAsset done = MakeClip(fixture.rig, "run");
    done.conformed = true;
    const fs::path runPath = fixture.dir / "run.uvanim";
    ASSERT_TRUE(Asset::SaveAnimationClipAsset(done, runPath));
    fixture.request.animations.push_back(runPath);

    const RetargetFilesResult result = RetargetFiles(fixture.request, GetHumanoidReference());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 5U);
    EXPECT_TRUE(result.files[1].changed) << "the skeleton";
    EXPECT_TRUE(result.files[2].changed);
    EXPECT_FALSE(result.files[3].changed);
    EXPECT_NE(result.files[3].note.find("no skeleton"), std::string::npos) << result.files[3].note;
    EXPECT_FALSE(result.files[4].changed);
    EXPECT_NE(result.files[4].note.find("already conformed"), std::string::npos) << result.files[4].note;
    EXPECT_EQ(ReadAll(fixture.request.animations[1]), walkBefore);
}

TEST(RetargetFilesTest, RetargetFiles_ChangesNothingWhenTheCharacterCannotBeConformed) {
    FilesFixture fixture;
    const std::string idleBefore = ReadAll(fixture.request.animations[0]);
    fixture.request.model = fixture.dir / "missing.uvmodel";
    const RetargetFilesResult result = RetargetFiles(fixture.request, GetHumanoidReference());
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.error.find("missing.uvmodel"), std::string::npos) << result.error;
    EXPECT_EQ(ReadAll(fixture.request.animations[0]), idleBefore);
    EXPECT_FALSE(fs::exists(fixture.request.backupRoot));

    // A static mesh has no skeleton to conform.
    Asset::MeshAsset plain = MakeMesh(fixture.rig);
    plain.joints.clear();
    plain.skinningInfluences.clear();
    fixture.request.model = fixture.dir / "prop.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAsset(plain, fixture.request.model));
    EXPECT_FALSE(RetargetFiles(fixture.request, GetHumanoidReference()).ok);
    EXPECT_EQ(ReadAll(fixture.request.animations[0]), idleBefore);
}

TEST(RetargetFilesTest, RetargetFiles_ConformsFiftyEmbeddedAnimationsInOneWriteAndRestoresOnUndo) {
    const fs::path dir = Tests::MakeTestCaseDirectory();
    const TPoseRig rig;
    const fs::path model = dir / "hero.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAsset(MakeMesh(rig), model));

    constexpr std::size_t kTakeCount = 50U;
    std::vector<Asset::AnimationClipAsset> embeddedClips;
    embeddedClips.reserve(kTakeCount);
    RetargetFilesRequest request;
    request.model = model;
    request.backupRoot = dir / ".retarget-backup";
    request.useBrain = false;
    for (std::size_t i = 0U; i < kTakeCount; ++i) {
        const std::string name = "Anim_" + std::to_string(i);
        embeddedClips.push_back(MakeClip(rig, name));
        request.animations.push_back(Asset::MakeEmbeddedClipPath(model, name));
    }
    constexpr std::uint64_t kStamp = 0x123456789ABCDEF0ULL;
    ASSERT_TRUE(Asset::SaveModelAnimations(model, embeddedClips, kStamp));
    const std::string modelBefore = ReadAll(model);

    const RetargetFilesResult result = RetargetFiles(request, GetHumanoidReference());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 2U + kTakeCount);
    for (const RetargetFileReport& entry : result.files) {
        EXPECT_TRUE(entry.changed) << entry.file << ": " << entry.note;
    }

    // All 50 embedded takes are now conformed with humanoid bones, and the source stamp is preserved.
    std::vector<Asset::AnimationClipAsset> afterTakes;
    std::uint64_t afterStamp = 0U;
    ASSERT_TRUE(Asset::LoadAllModelAnimations(model, afterTakes, &afterStamp));
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
    Asset::SkeletonAsset skel;
    ASSERT_TRUE(Asset::LoadSkeletonAsset(skelPath, skel));
    EXPECT_FALSE(skel.joints.empty());

    // Undo restores the .uvmodel (including all 50 pre-retarget takes) byte-for-byte and removes .uvskel.
    std::string restoreError;
    ASSERT_TRUE(RestoreRetargetBackup(result.backupDir, &restoreError)) << restoreError;
    EXPECT_EQ(ReadAll(model), modelBefore);
    EXPECT_FALSE(fs::exists(skelPath));
}

TEST(RetargetFilesTest, RetargetFiles_ConformsEmbeddedTakesOnlyOnAlreadyConformedModelAndBacksUpModel) {
    const fs::path dir = Tests::MakeTestCaseDirectory();
    const TPoseRig rig;
    const fs::path model = dir / "hero.uvmodel";
    ASSERT_TRUE(Asset::SaveMeshAsset(MakeMesh(rig), model));

    // Step 1: conform the character mesh alone first.
    RetargetFilesRequest firstReq;
    firstReq.model = model;
    firstReq.backupRoot = dir / ".backup-first";
    firstReq.useBrain = false;
    ASSERT_TRUE(RetargetFiles(firstReq, GetHumanoidReference()).ok);

    // Step 2: embed 10 un-conformed takes into the already-conformed .uvmodel.
    std::vector<Asset::AnimationClipAsset> rawTakes;
    RetargetFilesRequest takesReq;
    takesReq.backupRoot = dir / ".backup-takes";
    takesReq.useBrain = false;
    for (std::size_t i = 0U; i < 10U; ++i) {
        const std::string name = "Take_" + std::to_string(i);
        rawTakes.push_back(MakeClip(rig, name));
        takesReq.animations.push_back(Asset::MakeEmbeddedClipPath(model, name));
    }
    ASSERT_TRUE(Asset::SaveModelAnimations(model, rawTakes, 42U));
    const std::string modelBeforeTakes = ReadAll(model);

    // Step 3: conform ONLY the embedded takes (request.model is empty!).
    const RetargetFilesResult result = RetargetFiles(takesReq, GetHumanoidReference());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), 10U);
    for (const RetargetFileReport& entry : result.files) {
        EXPECT_TRUE(entry.changed) << entry.file << ": " << entry.note;
    }
    ASSERT_FALSE(result.backupDir.empty());

    std::vector<Asset::AnimationClipAsset> afterTakes;
    ASSERT_TRUE(Asset::LoadAllModelAnimations(model, afterTakes));
    ASSERT_EQ(afterTakes.size(), 10U);
    for (const Asset::AnimationClipAsset& clip : afterTakes) {
        EXPECT_TRUE(clip.conformed);
    }

    // Undo restores the real backing .uvmodel file byte-for-byte.
    std::string restoreError;
    ASSERT_TRUE(RestoreRetargetBackup(result.backupDir, &restoreError)) << restoreError;
    EXPECT_EQ(ReadAll(model), modelBeforeTakes);
}

TEST(RetargetFilesTest, RetargetFiles_StreamsLargeBatchOfLooseAnimationsWithRigCacheAndUndo) {
    const fs::path dir = Tests::MakeTestCaseDirectory();
    const TPoseRig rig;
    constexpr std::size_t kClipCount = 120U;
    RetargetFilesRequest request;
    request.backupRoot = dir / ".retarget-backup";
    request.useBrain = false;
    std::vector<std::string> originals;
    originals.reserve(kClipCount);
    for (std::size_t i = 0U; i < kClipCount; ++i) {
        const fs::path clipPath = dir / ("anim_" + std::to_string(i) + ".uvanim");
        ASSERT_TRUE(Asset::SaveAnimationClipAsset(MakeClip(rig, "anim_" + std::to_string(i)), clipPath));
        if (i == 0U || i == kClipCount - 1U) {
            originals.push_back(ReadAll(clipPath));
        }
        request.animations.push_back(clipPath);
    }

    const RetargetFilesResult result = RetargetFiles(request, GetHumanoidReference());
    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.files.size(), kClipCount);
    for (std::size_t i = 0U; i < kClipCount; ++i) {
        EXPECT_TRUE(result.files[i].changed);
        const Asset::AnimationClipSummary summary = Asset::InspectAnimationClipFileSummary(request.animations[i]);
        EXPECT_TRUE(summary.valid);
        EXPECT_TRUE(summary.conformed);
    }

    std::string restoreError;
    ASSERT_TRUE(RestoreRetargetBackup(result.backupDir, &restoreError)) << restoreError;
    EXPECT_EQ(ReadAll(request.animations.front()), originals.front());
    EXPECT_EQ(ReadAll(request.animations.back()), originals.back());
}

} // namespace
} // namespace UVE::Retarget
