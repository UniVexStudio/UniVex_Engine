// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <filesystem>

#include <gtest/gtest.h>

#include "animation_library_asset.h"
#include "montage_library_asset.h"

namespace UVE::Asset {

TEST(MontageLibraryAssetUVETest, SaveThenLoadRoundTrips) {
    const std::filesystem::path path = "uve_montage_library_round_trip.uvmontagelib";
    std::filesystem::remove(path);
    MontageLibraryAssetUVE library;
    library.libraryId = "Combat";
    library.entries = {MontageLibraryEntryUVE{AssetGuidUVE{11U}, "Attack"}, MontageLibraryEntryUVE{AssetGuidUVE{12U}, "Dodge"}};
    ASSERT_TRUE(SaveMontageLibraryAssetUVE(library, path));
    MontageLibraryAssetUVE loaded;
    ASSERT_TRUE(LoadMontageLibraryAssetUVE(path, loaded));
    EXPECT_EQ(loaded, library);
    AnimationLibraryAssetUVE notAnimations;
    EXPECT_FALSE(LoadAnimationLibraryAssetUVE(path, notAnimations)) << "a montage library is not an animation library";
    std::filesystem::remove(path);
}

TEST(MontageLibraryAssetUVETest, RefusesDuplicatesAndBlankNames) {
    MontageLibraryAssetUVE library;
    library.libraryId = "Combat";
    library.entries = {MontageLibraryEntryUVE{AssetGuidUVE{11U}, "Attack"}, MontageLibraryEntryUVE{AssetGuidUVE{11U}, "Again"}};
    EXPECT_FALSE(IsMontageLibraryAssetValidUVE(library));
    library.entries = {MontageLibraryEntryUVE{AssetGuidUVE{11U}, ""}};
    EXPECT_FALSE(IsMontageLibraryAssetValidUVE(library));
    library.entries.clear();
    EXPECT_TRUE(IsMontageLibraryAssetValidUVE(library)) << "an empty library is a new one";
}

} // namespace UVE::Asset
