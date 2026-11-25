// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <filesystem>

#include <gtest/gtest.h>

#include "animation_library_asset.h"
#include "montage_library_asset.h"

namespace UVE::Asset {

TEST(MontageLibraryAssetTest, SaveThenLoadRoundTrips) {
    const std::filesystem::path path = "uve_montage_library_round_trip.uvmontagelib";
    std::filesystem::remove(path);
    MontageLibraryAsset library;
    library.libraryId = "Combat";
    library.entries = {MontageLibraryEntry{AssetGuid{11U}, "Attack"}, MontageLibraryEntry{AssetGuid{12U}, "Dodge"}};
    ASSERT_TRUE(SaveMontageLibraryAsset(library, path));
    MontageLibraryAsset loaded;
    ASSERT_TRUE(LoadMontageLibraryAsset(path, loaded));
    EXPECT_EQ(loaded, library);
    AnimationLibraryAsset notAnimations;
    EXPECT_FALSE(LoadAnimationLibraryAsset(path, notAnimations)) << "a motion montage library is not an animation library";
    std::filesystem::remove(path);
}

TEST(MontageLibraryAssetTest, RefusesDuplicatesAndBlankNames) {
    MontageLibraryAsset library;
    library.libraryId = "Combat";
    library.entries = {MontageLibraryEntry{AssetGuid{11U}, "Attack"}, MontageLibraryEntry{AssetGuid{11U}, "Again"}};
    EXPECT_FALSE(IsMontageLibraryAssetValid(library));
    library.entries = {MontageLibraryEntry{AssetGuid{11U}, ""}};
    EXPECT_FALSE(IsMontageLibraryAssetValid(library));
    library.entries.clear();
    EXPECT_TRUE(IsMontageLibraryAssetValid(library)) << "an empty library is a new one";
}

} // namespace UVE::Asset
