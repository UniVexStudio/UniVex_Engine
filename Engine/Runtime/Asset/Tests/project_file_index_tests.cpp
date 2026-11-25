// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_file_index.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"

namespace UVE::Asset::Tests {
namespace {

void WriteFixtureFile(const std::filesystem::path& path, const std::string_view contents) {
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream file(path, std::ios::binary);
    ASSERT_TRUE(file.is_open());
    file << contents;
    ASSERT_TRUE(file.good());
}

[[nodiscard]] std::vector<std::string> SnapshotPaths(const ProjectFileSnapshot& snapshot) {
    std::vector<std::string> paths;
    paths.reserve(snapshot.entries.size());
    for (const ProjectFileEntry& entry : snapshot.entries) {
        paths.push_back(entry.relativePath.generic_string());
    }
    return paths;
}

TEST(ProjectFileIndexTest, Refresh_MissingRootPublishesValidEmptySnapshot) {
    const std::filesystem::path root = "uve_project_file_index_tests_missing";
    std::filesystem::remove_all(root);

    AssetDatabase assetDatabase;
    ProjectFileIndex index(root);

    ASSERT_TRUE(index.Refresh(assetDatabase));
    const ProjectFileSnapshot snapshot = index.GetSnapshot();
    EXPECT_EQ(snapshot.contentRoot, root.lexically_normal());
    EXPECT_FALSE(snapshot.contentRootExists);
    EXPECT_TRUE(snapshot.entries.empty());
    EXPECT_EQ(snapshot.refreshGeneration, 1U);
}

TEST(ProjectFileIndexTest, GetSnapshot_UsesCachedTreeUntilExplicitRefresh) {
    const std::filesystem::path root = "uve_project_file_index_tests_cached";
    std::filesystem::remove_all(root);
    WriteFixtureFile(root / "first.txt", "first");

    AssetDatabase assetDatabase;
    ProjectFileIndex index(root);
    ASSERT_TRUE(index.Refresh(assetDatabase));
    const ProjectFileSnapshot firstSnapshot = index.GetSnapshot();
    ASSERT_EQ(SnapshotPaths(firstSnapshot), std::vector<std::string>({"first.txt"}));

    WriteFixtureFile(root / "second.txt", "second");
    const ProjectFileSnapshot cachedSnapshot = index.GetSnapshot();
    EXPECT_EQ(cachedSnapshot.refreshGeneration, firstSnapshot.refreshGeneration);
    EXPECT_EQ(SnapshotPaths(cachedSnapshot), SnapshotPaths(firstSnapshot));

    ASSERT_TRUE(index.Refresh(assetDatabase));
    const ProjectFileSnapshot refreshedSnapshot = index.GetSnapshot();
    EXPECT_EQ(refreshedSnapshot.refreshGeneration, firstSnapshot.refreshGeneration + 1U);
    EXPECT_EQ(SnapshotPaths(refreshedSnapshot), std::vector<std::string>({"first.txt", "second.txt"}));

    std::filesystem::remove_all(root);
}

TEST(ProjectFileIndexTest, Refresh_NormalizesRelativeTrailingSeparatorBeforeRootBoundaryComparison) {
    const std::filesystem::path root = "uve_project_file_index_tests_trailing_separator";
    const std::filesystem::path configuredRoot = root.generic_string() + "/";
    std::filesystem::remove_all(root);
    WriteFixtureFile(root / "nested" / "asset.txt", "asset");

    AssetDatabase assetDatabase;
    ProjectFileIndex index(configuredRoot);

    ASSERT_TRUE(index.Refresh(assetDatabase));
    const ProjectFileSnapshot snapshot = index.GetSnapshot();
    EXPECT_EQ(snapshot.contentRoot, root);
    EXPECT_TRUE(snapshot.contentRootExists);
    EXPECT_EQ(SnapshotPaths(snapshot), std::vector<std::string>({"nested", "nested/asset.txt"}));

    std::filesystem::remove_all(root);
}

TEST(ProjectFileIndexTest, Refresh_BuildsDeterministicFoldersFirstTreeAndCorrelatesInRootRecords) {
    const std::filesystem::path root = "uve_project_file_index_tests_tree";
    const std::filesystem::path outside = "uve_project_file_index_tests_outside.txt";
    std::filesystem::remove_all(root);
    std::filesystem::remove(outside);
    WriteFixtureFile(root / "BFolder" / "other.txt", "other");
    WriteFixtureFile(root / "aFolder" / "registered.txt", "registered");
    WriteFixtureFile(root / "root.txt", "root");
    WriteFixtureFile(outside, "outside");

    AssetDatabase assetDatabase;
    const AssetGuid registeredGuid = assetDatabase.Register(root / "aFolder" / "registered.txt");
    const AssetGuid outsideGuid = assetDatabase.Register(outside);
    ASSERT_NE(registeredGuid, kInvalidAssetGuid);
    ASSERT_NE(outsideGuid, kInvalidAssetGuid);

    ProjectFileIndex index(root);
    ASSERT_TRUE(index.Refresh(assetDatabase));
    const ProjectFileSnapshot snapshot = index.GetSnapshot();
    EXPECT_TRUE(snapshot.contentRootExists);
    EXPECT_EQ(SnapshotPaths(snapshot),
              std::vector<std::string>({"BFolder", "aFolder", "BFolder/other.txt", "aFolder/registered.txt", "root.txt"}));

    const auto registeredIt = std::find_if(snapshot.entries.begin(), snapshot.entries.end(), [](const ProjectFileEntry& entry) {
        return entry.relativePath.generic_string() == "aFolder/registered.txt";
    });
    ASSERT_NE(registeredIt, snapshot.entries.end());
    ASSERT_TRUE(registeredIt->registeredAssetGuid.has_value());
    EXPECT_EQ(*registeredIt->registeredAssetGuid, registeredGuid);
    EXPECT_TRUE(std::none_of(snapshot.entries.begin(), snapshot.entries.end(), [outsideGuid](const ProjectFileEntry& entry) {
        return entry.registeredAssetGuid == outsideGuid;
    }));

    std::filesystem::remove_all(root);
    std::filesystem::remove(outside);
}

TEST(ProjectFileIndexTest, Refresh_SkipsAllSymlinksAndRetainsLastGoodSnapshotWhenRootBecomesSymlink) {
    const std::filesystem::path root = "uve_project_file_index_tests_symlink_root";
    const std::filesystem::path outside = "uve_project_file_index_tests_symlink_outside";
    std::filesystem::remove_all(root);
    std::filesystem::remove_all(outside);
    WriteFixtureFile(root / "safe.txt", "safe");
    WriteFixtureFile(outside / "outside.txt", "outside");

    std::error_code errorCode;
    std::filesystem::create_directory_symlink(outside, root / "outside_link", errorCode);
    if (errorCode) {
        GTEST_SKIP() << "Host does not permit test symlink creation: " << errorCode.message();
    }
    errorCode.clear();
    std::filesystem::create_directory_symlink(root, root / "cycle_link", errorCode);
    if (errorCode) {
        GTEST_SKIP() << "Host does not permit cycle symlink creation: " << errorCode.message();
    }

    AssetDatabase assetDatabase;
    ProjectFileIndex index(root);
    ASSERT_TRUE(index.Refresh(assetDatabase));
    const ProjectFileSnapshot goodSnapshot = index.GetSnapshot();
    EXPECT_EQ(SnapshotPaths(goodSnapshot), std::vector<std::string>({"safe.txt"}));

    std::filesystem::remove_all(root);
    errorCode.clear();
    std::filesystem::create_directory_symlink(outside, root, errorCode);
    if (errorCode) {
        GTEST_SKIP() << "Host does not permit root symlink creation: " << errorCode.message();
    }

    EXPECT_FALSE(index.Refresh(assetDatabase));
    const ProjectFileSnapshot retainedSnapshot = index.GetSnapshot();
    EXPECT_EQ(retainedSnapshot.refreshGeneration, goodSnapshot.refreshGeneration);
    EXPECT_EQ(SnapshotPaths(retainedSnapshot), SnapshotPaths(goodSnapshot));

    std::filesystem::remove(root, errorCode);
    std::filesystem::remove_all(outside);
}

} // namespace
} // namespace UVE::Asset::Tests
