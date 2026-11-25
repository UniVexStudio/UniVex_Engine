// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "legacy_extension_migration.h"

namespace UVE::Asset::Tests {
namespace {

void Write(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

[[nodiscard]] std::string Read(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

TEST(LegacyExtensionMigrationTest, MapsEveryOldNameToItsNewOne) {
    EXPECT_EQ(GetRenamedExtension(".uvescene"), ".uvscene");
    EXPECT_EQ(GetRenamedExtension(".UVEMODEL"), ".uvmodel");
    EXPECT_EQ(GetRenamedExtension(".uveentity"), ".uventity");
    EXPECT_EQ(GetRenamedExtension(".uveshadercache"), ".uvshadercache");
    EXPECT_EQ(GetRenamedExtension(".uveditor"), ".uvproject");
    EXPECT_TRUE(GetRenamedExtension(".uvscene").empty());
    EXPECT_TRUE(GetRenamedExtension(".png").empty());
}

TEST(LegacyExtensionMigrationTest, RenamesContentAndNeverOverwrites) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("content");
    Write(root / "Hero.uveentity", "hero");
    Write(root / "Maps/Level.uvescene", "level");
    Write(root / "Maps/Rock.png", "png");
    Write(root / "Both.uvemodel", "old");
    Write(root / "Both.uvmodel", "new");

    const LegacyMigrationReport report = MigrateLegacyContent(root);
    EXPECT_EQ(report.renamed, 2U);
    EXPECT_EQ(report.skipped, 1U);
    EXPECT_EQ(Read(root / "Hero.uventity"), "hero");
    EXPECT_EQ(Read(root / "Maps/Level.uvscene"), "level");
    EXPECT_TRUE(std::filesystem::exists(root / "Maps/Rock.png"));
    EXPECT_EQ(Read(root / "Both.uvmodel"), "new");      // the newer file wins
    EXPECT_TRUE(std::filesystem::exists(root / "Both.uvemodel")); // and the old one is kept, not lost
    EXPECT_EQ(MigrateLegacyContent(root).renamed, 0U);  // idempotent
    EXPECT_EQ(MigrateLegacyContent(root / "missing").renamed, 0U);
}

TEST(LegacyExtensionMigrationTest, RenamesASingleFileOnlyWhenTheNewOneIsMissing) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("files");
    Write(root / "project.uvesettings", "{}");
    EXPECT_TRUE(MigrateLegacyFile(root / "project.uvsettings"));
    EXPECT_TRUE(std::filesystem::exists(root / "project.uvsettings"));
    EXPECT_FALSE(MigrateLegacyFile(root / "project.uvsettings"));
    EXPECT_FALSE(MigrateLegacyFile(root / "notes.txt"));
}

TEST(LegacyExtensionMigrationTest, RewritesPathsInsideTheRegistry) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("registry");
    const std::filesystem::path registry = root / ".uvassetdb";
    Write(registry, R"({"a":"assets/Hero.uveentity","b":"x.UVESHADERCACHE","c":"keep.uvescenery","d":"y.uvscene"})");
    EXPECT_TRUE(RewriteLegacyExtensionsInTextFile(registry));
    EXPECT_EQ(Read(registry),
              R"({"a":"assets/Hero.uventity","b":"x.uvshadercache","c":"keep.uvescenery","d":"y.uvscene"})");
    EXPECT_FALSE(RewriteLegacyExtensionsInTextFile(registry));
}

} // namespace
} // namespace UVE::Asset::Tests
