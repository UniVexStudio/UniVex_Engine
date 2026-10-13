// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/animation_library_asset_uve.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPathUVE(const char* const name) {
    return ::UVE::Tests::ScratchRootUVE() / name;
}

AnimationLibraryAssetUVE MakeValidLibraryUVE() {
    AnimationLibraryAssetUVE library;
    library.libraryId = "hero_moveset";
    library.entries = {
        AnimationLibraryEntryUVE{AssetGuidUVE{7U}, "Idle"},
        AnimationLibraryEntryUVE{AssetGuidUVE{42U}, "Walk"},
        AnimationLibraryEntryUVE{AssetGuidUVE{9001U}, "Jump"},
    };
    return library;
}

} // namespace

TEST(AnimationLibraryAssetUVETest, ValidationAcceptsEmptyAndPopulated) {
    AnimationLibraryAssetUVE empty;
    empty.libraryId = "fresh";
    EXPECT_TRUE(IsAnimationLibraryAssetValidUVE(empty));
    EXPECT_TRUE(IsAnimationLibraryAssetValidUVE(MakeValidLibraryUVE()));
}

TEST(AnimationLibraryAssetUVETest, SaveThenLoad_RoundTripsEntriesVerbatim) {
    const std::filesystem::path path = TestPathUVE("uve_animation_library_round_trip.uvanimlib");
    std::filesystem::remove(path);
    const AnimationLibraryAssetUVE original = MakeValidLibraryUVE();
    ASSERT_TRUE(SaveAnimationLibraryAssetUVE(original, path));
    AnimationLibraryAssetUVE reloaded;
    ASSERT_TRUE(LoadAnimationLibraryAssetUVE(path, reloaded));
    EXPECT_EQ(reloaded, original);
    std::filesystem::remove(path);
}

TEST(AnimationLibraryAssetUVETest, SaveThenLoad_RoundTripsEmptyLibrary) {
    const std::filesystem::path path = TestPathUVE("uve_animation_library_empty.uvanimlib");
    std::filesystem::remove(path);
    AnimationLibraryAssetUVE empty;
    empty.libraryId = "fresh";
    ASSERT_TRUE(SaveAnimationLibraryAssetUVE(empty, path));
    AnimationLibraryAssetUVE reloaded;
    ASSERT_TRUE(LoadAnimationLibraryAssetUVE(path, reloaded));
    EXPECT_EQ(reloaded, empty);
    std::filesystem::remove(path);
}

TEST(AnimationLibraryAssetUVETest, SaveRefuses_DuplicateClipGuid) {
    AnimationLibraryAssetUVE library = MakeValidLibraryUVE();
    library.entries.push_back(AnimationLibraryEntryUVE{AssetGuidUVE{42U}, "Walk Again"});
    EXPECT_FALSE(IsAnimationLibraryAssetValidUVE(library));
    EXPECT_FALSE(SaveAnimationLibraryAssetUVE(library, TestPathUVE("uve_animation_library_dup.uvanimlib")));
}

TEST(AnimationLibraryAssetUVETest, SaveRefuses_InvalidClipGuidAndBadNames) {
    AnimationLibraryAssetUVE zeroGuid = MakeValidLibraryUVE();
    zeroGuid.entries[0].clip = kInvalidAssetGuidUVE;
    EXPECT_FALSE(IsAnimationLibraryAssetValidUVE(zeroGuid));

    AnimationLibraryAssetUVE emptyName = MakeValidLibraryUVE();
    emptyName.entries[1].name.clear();
    EXPECT_FALSE(IsAnimationLibraryAssetValidUVE(emptyName));

    AnimationLibraryAssetUVE emptyId = MakeValidLibraryUVE();
    emptyId.libraryId.clear();
    EXPECT_FALSE(IsAnimationLibraryAssetValidUVE(emptyId));

    AnimationLibraryAssetUVE longId = MakeValidLibraryUVE();
    longId.libraryId = std::string(kMaximumAnimationLibraryIdentifierBytesUVE + 1U, 'x');
    EXPECT_FALSE(IsAnimationLibraryAssetValidUVE(longId));
    EXPECT_FALSE(SaveAnimationLibraryAssetUVE(longId, TestPathUVE("uve_animation_library_long.uvanimlib")));
}

TEST(AnimationLibraryAssetUVETest, SaveRefuses_OverCapEntries) {
    AnimationLibraryAssetUVE library;
    library.libraryId = "too_many";
    for (std::size_t i = 0U; i <= kMaximumAnimationLibraryEntriesUVE; ++i) {
        library.entries.push_back(
            AnimationLibraryEntryUVE{AssetGuidUVE{static_cast<std::uint64_t>(i + 1U)}, "Clip"});
    }
    EXPECT_FALSE(IsAnimationLibraryAssetValidUVE(library));
    EXPECT_FALSE(SaveAnimationLibraryAssetUVE(library, TestPathUVE("uve_animation_library_cap.uvanimlib")));
}

TEST(AnimationLibraryAssetUVETest, LoadRejects_WrongKindEnvelope) {
    // A clip envelope is structurally a valid .uve file but the wrong kind — and saving it
    // proves the Animation path is undisturbed by the new AnimationLibrary kind beside it.
    const std::filesystem::path path = TestPathUVE("uve_animation_library_wrong_kind.uvanim");
    std::filesystem::remove(path);
    AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {AnimationAssetSampleUVE{0.0, AnimationAssetPoseUVE{}}};
    ASSERT_TRUE(SaveAnimationClipAssetUVE(clip, path));

    AnimationLibraryAssetUVE library;
    EXPECT_FALSE(LoadAnimationLibraryAssetUVE(path, library));
    std::filesystem::remove(path);
}

TEST(AnimationLibraryAssetUVETest, LoadRejects_MalformedPayload) {
    const std::filesystem::path garbage = TestPathUVE("uve_animation_library_garbage.uvanimlib");
    std::filesystem::remove(garbage);
    {
        std::ofstream stream(garbage, std::ios::binary);
        stream << "not a uve file";
    }
    AnimationLibraryAssetUVE library;
    EXPECT_FALSE(LoadAnimationLibraryAssetUVE(garbage, library));

    // Right kind, wrong bytes: proves AnimationLibrary flows through the envelope bound (a stale
    // Skeleton-capped bound would reject the kind before the payload is even inspected).
    const std::filesystem::path badJson = TestPathUVE("uve_animation_library_bad_json.uvanimlib");
    std::filesystem::remove(badJson);
    const std::string payload = "{\"schema\":\"uve-animation-library-v1\"}"; // no libraryId/entries
    const auto* const bytes = reinterpret_cast<const std::byte*>(payload.data());
    ASSERT_TRUE(WriteUveFileUVE(badJson, AssetKindUVE::AnimationLibrary,
                                 std::vector<std::byte>(bytes, bytes + payload.size())));
    EXPECT_FALSE(LoadAnimationLibraryAssetUVE(badJson, library));

    std::filesystem::remove(garbage);
    std::filesystem::remove(badJson);
}

} // namespace UVE::Asset::Tests
