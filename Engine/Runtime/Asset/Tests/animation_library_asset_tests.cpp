// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_library_asset.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "animation_clip_asset.h"
#include "uve_file_envelope.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPath(const char* const name) {
    return ::UVE::Tests::ScratchRoot() / name;
}

AnimationLibraryAsset MakeValidLibrary() {
    AnimationLibraryAsset library;
    library.libraryId = "hero_moveset";
    library.entries = {
        AnimationLibraryEntry{AssetGuid{7U}, "Idle"},
        AnimationLibraryEntry{AssetGuid{42U}, "Walk"},
        AnimationLibraryEntry{AssetGuid{9001U}, "Jump"},
    };
    return library;
}

} // namespace

TEST(AnimationLibraryAssetTest, ValidationAcceptsEmptyAndPopulated) {
    AnimationLibraryAsset empty;
    empty.libraryId = "fresh";
    EXPECT_TRUE(IsAnimationLibraryAssetValid(empty));
    EXPECT_TRUE(IsAnimationLibraryAssetValid(MakeValidLibrary()));
}

TEST(AnimationLibraryAssetTest, SaveThenLoad_RoundTripsEntriesVerbatim) {
    const std::filesystem::path path = TestPath("uve_animation_library_round_trip.uvanimlib");
    std::filesystem::remove(path);
    const AnimationLibraryAsset original = MakeValidLibrary();
    ASSERT_TRUE(SaveAnimationLibraryAsset(original, path));
    AnimationLibraryAsset reloaded;
    ASSERT_TRUE(LoadAnimationLibraryAsset(path, reloaded));
    EXPECT_EQ(reloaded, original);
    std::filesystem::remove(path);
}

TEST(AnimationLibraryAssetTest, SaveThenLoad_RoundTripsEmptyLibrary) {
    const std::filesystem::path path = TestPath("uve_animation_library_empty.uvanimlib");
    std::filesystem::remove(path);
    AnimationLibraryAsset empty;
    empty.libraryId = "fresh";
    ASSERT_TRUE(SaveAnimationLibraryAsset(empty, path));
    AnimationLibraryAsset reloaded;
    ASSERT_TRUE(LoadAnimationLibraryAsset(path, reloaded));
    EXPECT_EQ(reloaded, empty);
    std::filesystem::remove(path);
}

TEST(AnimationLibraryAssetTest, SaveRefuses_DuplicateClipGuid) {
    AnimationLibraryAsset library = MakeValidLibrary();
    library.entries.push_back(AnimationLibraryEntry{AssetGuid{42U}, "Walk Again"});
    EXPECT_FALSE(IsAnimationLibraryAssetValid(library));
    EXPECT_FALSE(SaveAnimationLibraryAsset(library, TestPath("uve_animation_library_dup.uvanimlib")));
}

TEST(AnimationLibraryAssetTest, SaveRefuses_InvalidClipGuidAndBadNames) {
    AnimationLibraryAsset zeroGuid = MakeValidLibrary();
    zeroGuid.entries[0].clip = kInvalidAssetGuid;
    EXPECT_FALSE(IsAnimationLibraryAssetValid(zeroGuid));

    AnimationLibraryAsset emptyName = MakeValidLibrary();
    emptyName.entries[1].name.clear();
    EXPECT_FALSE(IsAnimationLibraryAssetValid(emptyName));

    AnimationLibraryAsset emptyId = MakeValidLibrary();
    emptyId.libraryId.clear();
    EXPECT_FALSE(IsAnimationLibraryAssetValid(emptyId));

    AnimationLibraryAsset longId = MakeValidLibrary();
    longId.libraryId = std::string(kMaximumAnimationLibraryIdentifierBytes + 1U, 'x');
    EXPECT_FALSE(IsAnimationLibraryAssetValid(longId));
    EXPECT_FALSE(SaveAnimationLibraryAsset(longId, TestPath("uve_animation_library_long.uvanimlib")));
}

TEST(AnimationLibraryAssetTest, SaveRefuses_OverCapEntries) {
    AnimationLibraryAsset library;
    library.libraryId = "too_many";
    for (std::size_t i = 0U; i <= kMaximumAnimationLibraryEntries; ++i) {
        library.entries.push_back(
            AnimationLibraryEntry{AssetGuid{static_cast<std::uint64_t>(i + 1U)}, "Clip"});
    }
    EXPECT_FALSE(IsAnimationLibraryAssetValid(library));
    EXPECT_FALSE(SaveAnimationLibraryAsset(library, TestPath("uve_animation_library_cap.uvanimlib")));
}

TEST(AnimationLibraryAssetTest, LoadRejects_WrongKindEnvelope) {
    // A clip envelope is structurally a valid .uve file but the wrong kind — and saving it
    // proves the Animation path is undisturbed by the new AnimationLibrary kind beside it.
    const std::filesystem::path path = TestPath("uve_animation_library_wrong_kind.uvanim");
    std::filesystem::remove(path);
    AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {AnimationAssetSample{0.0, AnimationAssetPose{}}};
    ASSERT_TRUE(SaveAnimationClipAsset(clip, path));

    AnimationLibraryAsset library;
    EXPECT_FALSE(LoadAnimationLibraryAsset(path, library));
    std::filesystem::remove(path);
}

TEST(AnimationLibraryAssetTest, LoadRejects_MalformedPayload) {
    const std::filesystem::path garbage = TestPath("uve_animation_library_garbage.uvanimlib");
    std::filesystem::remove(garbage);
    {
        std::ofstream stream(garbage, std::ios::binary);
        stream << "not a uve file";
    }
    AnimationLibraryAsset library;
    EXPECT_FALSE(LoadAnimationLibraryAsset(garbage, library));

    // Right kind, wrong bytes: proves AnimationLibrary flows through the envelope bound (a stale
    // Skeleton-capped bound would reject the kind before the payload is even inspected).
    const std::filesystem::path badJson = TestPath("uve_animation_library_bad_json.uvanimlib");
    std::filesystem::remove(badJson);
    const std::string payload = "{\"schema\":\"uve-animation-library-v1\"}"; // no libraryId/entries
    const auto* const bytes = reinterpret_cast<const std::byte*>(payload.data());
    ASSERT_TRUE(WriteUveFile(badJson, AssetKind::AnimationLibrary,
                                 std::vector<std::byte>(bytes, bytes + payload.size())));
    EXPECT_FALSE(LoadAnimationLibraryAsset(badJson, library));

    std::filesystem::remove(garbage);
    std::filesystem::remove(badJson);
}

} // namespace UVE::Asset::Tests
