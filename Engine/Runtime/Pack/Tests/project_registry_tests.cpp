// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_registry.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

namespace UVE::Pack::Tests {
namespace {

inline constexpr Platform::EditorProjectVersion kVersion{1U, 2U, 3U, 4U};

class ProjectRegistryTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = ::UVE::Tests::MakeTestCaseDirectory("registry");
        registryFile = root / "tools" / kProjectRegistryFileName;
    }

    void TearDown() override { std::filesystem::remove_all(root); }

    [[nodiscard]] std::filesystem::path MakeProjectFile(const std::string& name) const {
        const std::filesystem::path file = root / name / (name + ".uvproject");
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << "{}";
        return file;
    }

    std::filesystem::path root;
    std::filesystem::path registryFile;
};

TEST_F(ProjectRegistryTest, MissingFileIsAnEmptyRegistry) {
    ProjectRegistry registry;
    EXPECT_TRUE(registry.Load(registryFile).IsApplied());
    EXPECT_TRUE(registry.Entries().empty());
    EXPECT_EQ(registry.RecentLimit(), kDefaultRecentProjectLimit);
}

TEST_F(ProjectRegistryTest, RecordOpenedKeepsMostRecentFirstWithoutDuplicates) {
    ProjectRegistry registry;
    const std::filesystem::path first = MakeProjectFile("First");
    const std::filesystem::path second = MakeProjectFile("Second");
    registry.RecordOpened(first, "First", kVersion, 10);
    registry.RecordOpened(second, "Second", kVersion, 20);
    registry.RecordOpened(first, "First Renamed", kVersion, 30);

    ASSERT_EQ(registry.Entries().size(), 2U);
    EXPECT_EQ(registry.Entries()[0].projectFile, ProjectRegistry::NormalizeProjectFile(first));
    EXPECT_EQ(registry.Entries()[0].displayName, "First Renamed");
    EXPECT_EQ(registry.Entries()[0].lastOpenedUnixSeconds, 30);
    EXPECT_EQ(registry.Entries()[1].displayName, "Second");
}

TEST_F(ProjectRegistryTest, SaveAndLoadRoundTrip) {
    ProjectRegistry registry;
    const std::filesystem::path project = MakeProjectFile("Game");
    registry.RecordOpened(project, "Game", kVersion, 42);
    ASSERT_TRUE(registry.SetFavorite(project, true));
    registry.SetRecentLimit(7U);
    ASSERT_TRUE(registry.Save(registryFile).IsApplied());

    ProjectRegistry reloaded;
    ASSERT_TRUE(reloaded.Load(registryFile).IsApplied());
    EXPECT_EQ(reloaded.Entries(), registry.Entries());
    EXPECT_EQ(reloaded.RecentLimit(), 7U);
}

TEST_F(ProjectRegistryTest, LimitTrimsOldestButNeverFavorites) {
    ProjectRegistry registry;
    const std::filesystem::path favorite = MakeProjectFile("Favorite");
    registry.RecordOpened(favorite, "Favorite", kVersion, 1);
    ASSERT_TRUE(registry.SetFavorite(favorite, true));
    registry.RecordOpened(MakeProjectFile("A"), "A", kVersion, 2);
    registry.RecordOpened(MakeProjectFile("B"), "B", kVersion, 3);
    registry.RecordOpened(MakeProjectFile("C"), "C", kVersion, 4);

    registry.SetRecentLimit(2U);

    ASSERT_EQ(registry.Entries().size(), 3U);
    EXPECT_EQ(registry.Entries()[0].displayName, "C");
    EXPECT_EQ(registry.Entries()[1].displayName, "B");
    EXPECT_EQ(registry.Entries()[2].displayName, "Favorite");
}

TEST_F(ProjectRegistryTest, LimitIsClampedToItsBounds) {
    ProjectRegistry registry;
    registry.SetRecentLimit(0U);
    EXPECT_EQ(registry.RecentLimit(), kMinimumRecentProjectLimit);
    registry.SetRecentLimit(kMaximumRecentProjectLimit + 1U);
    EXPECT_EQ(registry.RecentLimit(), kMaximumRecentProjectLimit);
}

TEST_F(ProjectRegistryTest, MissingProjectsAreReportedNotDropped) {
    ProjectRegistry registry;
    const std::filesystem::path project = MakeProjectFile("Gone");
    registry.RecordOpened(project, "Gone", kVersion, 1);
    std::filesystem::remove_all(project.parent_path());
    ASSERT_TRUE(registry.Save(registryFile).IsApplied());

    ProjectRegistry reloaded;
    ASSERT_TRUE(reloaded.Load(registryFile).IsApplied());
    ASSERT_EQ(reloaded.Entries().size(), 1U);
    EXPECT_TRUE(ProjectRegistry::IsMissing(reloaded.Entries().front()));
    EXPECT_TRUE(reloaded.Remove(project));
    EXPECT_TRUE(reloaded.Entries().empty());
    EXPECT_FALSE(reloaded.Remove(project));
}

TEST_F(ProjectRegistryTest, DamagedRegistryIsReportedAndNotOverwritten) {
    std::filesystem::create_directories(registryFile.parent_path());
    std::ofstream(registryFile) << "{ damaged";

    Platform::EditorProjectPackage package;
    package.displayName = "Game";
    const ProjectRegistryResult result = RecordProjectOpened(registryFile, MakeProjectFile("Game"), package);

    EXPECT_EQ(result.code, ProjectRegistryCode::ParseFailed);
    std::ifstream input(registryFile);
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(text, "{ damaged");
}

TEST_F(ProjectRegistryTest, RecordProjectOpenedAddsToTheFile) {
    Platform::EditorProjectPackage package;
    package.displayName = "Game";
    package.engineVersion = kVersion;
    const std::filesystem::path project = MakeProjectFile("Game");

    ASSERT_TRUE(RecordProjectOpened(registryFile, project, package).IsApplied());

    ProjectRegistry registry;
    ASSERT_TRUE(registry.Load(registryFile).IsApplied());
    const ProjectRegistryEntry* const entry = registry.Find(project);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->displayName, "Game");
    EXPECT_EQ(entry->engineVersion, kVersion);
    EXPECT_GT(entry->lastOpenedUnixSeconds, 0);
}

} // namespace
} // namespace UVE::Pack::Tests
