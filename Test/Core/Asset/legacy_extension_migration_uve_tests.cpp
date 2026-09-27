// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/legacy_extension_migration_uve.h"

namespace UVE::Asset::Tests {
namespace {

void WriteUVE(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

[[nodiscard]] std::string ReadUVE(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

TEST(LegacyExtensionMigrationUVETest, MapsEveryOldNameToItsNewOne) {
    EXPECT_EQ(GetRenamedExtensionUVE(".uvescene"), ".uvscene");
    EXPECT_EQ(GetRenamedExtensionUVE(".UVEMODEL"), ".uvmodel");
    EXPECT_EQ(GetRenamedExtensionUVE(".uveentity"), ".uventity");
    EXPECT_EQ(GetRenamedExtensionUVE(".uveshadercache"), ".uvshadercache");
    EXPECT_EQ(GetRenamedExtensionUVE(".uveditor"), ".uvproject");
    EXPECT_TRUE(GetRenamedExtensionUVE(".uvscene").empty());
    EXPECT_TRUE(GetRenamedExtensionUVE(".png").empty());
}

TEST(LegacyExtensionMigrationUVETest, RenamesContentAndNeverOverwrites) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("content");
    WriteUVE(root / "Hero.uveentity", "hero");
    WriteUVE(root / "Maps/Level.uvescene", "level");
    WriteUVE(root / "Maps/Rock.png", "png");
    WriteUVE(root / "Both.uvemodel", "old");
    WriteUVE(root / "Both.uvmodel", "new");

    const LegacyMigrationReportUVE report = MigrateLegacyContentUVE(root);
    EXPECT_EQ(report.renamed, 2U);
    EXPECT_EQ(report.skipped, 1U);
    EXPECT_EQ(ReadUVE(root / "Hero.uventity"), "hero");
    EXPECT_EQ(ReadUVE(root / "Maps/Level.uvscene"), "level");
    EXPECT_TRUE(std::filesystem::exists(root / "Maps/Rock.png"));
    EXPECT_EQ(ReadUVE(root / "Both.uvmodel"), "new");      // the newer file wins
    EXPECT_TRUE(std::filesystem::exists(root / "Both.uvemodel")); // and the old one is kept, not lost
    EXPECT_EQ(MigrateLegacyContentUVE(root).renamed, 0U);  // idempotent
    EXPECT_EQ(MigrateLegacyContentUVE(root / "missing").renamed, 0U);
}

TEST(LegacyExtensionMigrationUVETest, RenamesASingleFileOnlyWhenTheNewOneIsMissing) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("files");
    WriteUVE(root / "project.uvesettings", "{}");
    EXPECT_TRUE(MigrateLegacyFileUVE(root / "project.uvsettings"));
    EXPECT_TRUE(std::filesystem::exists(root / "project.uvsettings"));
    EXPECT_FALSE(MigrateLegacyFileUVE(root / "project.uvsettings"));
    EXPECT_FALSE(MigrateLegacyFileUVE(root / "notes.txt"));
}

TEST(LegacyExtensionMigrationUVETest, RewritesPathsInsideTheRegistry) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("registry");
    const std::filesystem::path registry = root / ".uvassetdb";
    WriteUVE(registry, R"({"a":"assets/Hero.uveentity","b":"x.UVESHADERCACHE","c":"keep.uvescenery","d":"y.uvscene"})");
    EXPECT_TRUE(RewriteLegacyExtensionsInTextFileUVE(registry));
    EXPECT_EQ(ReadUVE(registry),
              R"({"a":"assets/Hero.uventity","b":"x.uvshadercache","c":"keep.uvescenery","d":"y.uvscene"})");
    EXPECT_FALSE(RewriteLegacyExtensionsInTextFileUVE(registry));
}

} // namespace
} // namespace UVE::Asset::Tests
