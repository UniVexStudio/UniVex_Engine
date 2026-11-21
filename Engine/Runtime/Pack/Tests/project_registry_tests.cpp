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

inline constexpr Platform::EditorProjectVersionUVE kVersionUVE{1U, 2U, 3U, 4U};

class ProjectRegistryUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        root = ::UVE::Tests::MakeTestCaseDirectoryUVE("registry");
        registryFile = root / "tools" / kProjectRegistryFileNameUVE;
    }

    void TearDown() override { std::filesystem::remove_all(root); }

    [[nodiscard]] std::filesystem::path MakeProjectFileUVE(const std::string& name) const {
        const std::filesystem::path file = root / name / (name + ".uvproject");
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file) << "{}";
        return file;
    }

    std::filesystem::path root;
    std::filesystem::path registryFile;
};

TEST_F(ProjectRegistryUVETest, MissingFileIsAnEmptyRegistry) {
    ProjectRegistryUVE registry;
    EXPECT_TRUE(registry.LoadUVE(registryFile).IsAppliedUVE());
    EXPECT_TRUE(registry.EntriesUVE().empty());
    EXPECT_EQ(registry.RecentLimitUVE(), kDefaultRecentProjectLimitUVE);
}

TEST_F(ProjectRegistryUVETest, RecordOpenedKeepsMostRecentFirstWithoutDuplicates) {
    ProjectRegistryUVE registry;
    const std::filesystem::path first = MakeProjectFileUVE("First");
    const std::filesystem::path second = MakeProjectFileUVE("Second");
    registry.RecordOpenedUVE(first, "First", kVersionUVE, 10);
    registry.RecordOpenedUVE(second, "Second", kVersionUVE, 20);
    registry.RecordOpenedUVE(first, "First Renamed", kVersionUVE, 30);

    ASSERT_EQ(registry.EntriesUVE().size(), 2U);
    EXPECT_EQ(registry.EntriesUVE()[0].projectFile, ProjectRegistryUVE::NormalizeProjectFileUVE(first));
    EXPECT_EQ(registry.EntriesUVE()[0].displayName, "First Renamed");
    EXPECT_EQ(registry.EntriesUVE()[0].lastOpenedUnixSeconds, 30);
    EXPECT_EQ(registry.EntriesUVE()[1].displayName, "Second");
}

TEST_F(ProjectRegistryUVETest, SaveAndLoadRoundTrip) {
    ProjectRegistryUVE registry;
    const std::filesystem::path project = MakeProjectFileUVE("Game");
    registry.RecordOpenedUVE(project, "Game", kVersionUVE, 42);
    ASSERT_TRUE(registry.SetFavoriteUVE(project, true));
    registry.SetRecentLimitUVE(7U);
    ASSERT_TRUE(registry.SaveUVE(registryFile).IsAppliedUVE());

    ProjectRegistryUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(registryFile).IsAppliedUVE());
    EXPECT_EQ(reloaded.EntriesUVE(), registry.EntriesUVE());
    EXPECT_EQ(reloaded.RecentLimitUVE(), 7U);
}

TEST_F(ProjectRegistryUVETest, LimitTrimsOldestButNeverFavorites) {
    ProjectRegistryUVE registry;
    const std::filesystem::path favorite = MakeProjectFileUVE("Favorite");
    registry.RecordOpenedUVE(favorite, "Favorite", kVersionUVE, 1);
    ASSERT_TRUE(registry.SetFavoriteUVE(favorite, true));
    registry.RecordOpenedUVE(MakeProjectFileUVE("A"), "A", kVersionUVE, 2);
    registry.RecordOpenedUVE(MakeProjectFileUVE("B"), "B", kVersionUVE, 3);
    registry.RecordOpenedUVE(MakeProjectFileUVE("C"), "C", kVersionUVE, 4);

    registry.SetRecentLimitUVE(2U);

    ASSERT_EQ(registry.EntriesUVE().size(), 3U);
    EXPECT_EQ(registry.EntriesUVE()[0].displayName, "C");
    EXPECT_EQ(registry.EntriesUVE()[1].displayName, "B");
    EXPECT_EQ(registry.EntriesUVE()[2].displayName, "Favorite");
}

TEST_F(ProjectRegistryUVETest, LimitIsClampedToItsBounds) {
    ProjectRegistryUVE registry;
    registry.SetRecentLimitUVE(0U);
    EXPECT_EQ(registry.RecentLimitUVE(), kMinimumRecentProjectLimitUVE);
    registry.SetRecentLimitUVE(kMaximumRecentProjectLimitUVE + 1U);
    EXPECT_EQ(registry.RecentLimitUVE(), kMaximumRecentProjectLimitUVE);
}

TEST_F(ProjectRegistryUVETest, MissingProjectsAreReportedNotDropped) {
    ProjectRegistryUVE registry;
    const std::filesystem::path project = MakeProjectFileUVE("Gone");
    registry.RecordOpenedUVE(project, "Gone", kVersionUVE, 1);
    std::filesystem::remove_all(project.parent_path());
    ASSERT_TRUE(registry.SaveUVE(registryFile).IsAppliedUVE());

    ProjectRegistryUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(registryFile).IsAppliedUVE());
    ASSERT_EQ(reloaded.EntriesUVE().size(), 1U);
    EXPECT_TRUE(ProjectRegistryUVE::IsMissingUVE(reloaded.EntriesUVE().front()));
    EXPECT_TRUE(reloaded.RemoveUVE(project));
    EXPECT_TRUE(reloaded.EntriesUVE().empty());
    EXPECT_FALSE(reloaded.RemoveUVE(project));
}

TEST_F(ProjectRegistryUVETest, DamagedRegistryIsReportedAndNotOverwritten) {
    std::filesystem::create_directories(registryFile.parent_path());
    std::ofstream(registryFile) << "{ damaged";

    Platform::EditorProjectPackageUVE package;
    package.displayName = "Game";
    const ProjectRegistryResultUVE result = RecordProjectOpenedUVE(registryFile, MakeProjectFileUVE("Game"), package);

    EXPECT_EQ(result.code, ProjectRegistryCodeUVE::ParseFailed);
    std::ifstream input(registryFile);
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(text, "{ damaged");
}

TEST_F(ProjectRegistryUVETest, RecordProjectOpenedAddsToTheFile) {
    Platform::EditorProjectPackageUVE package;
    package.displayName = "Game";
    package.engineVersion = kVersionUVE;
    const std::filesystem::path project = MakeProjectFileUVE("Game");

    ASSERT_TRUE(RecordProjectOpenedUVE(registryFile, project, package).IsAppliedUVE());

    ProjectRegistryUVE registry;
    ASSERT_TRUE(registry.LoadUVE(registryFile).IsAppliedUVE());
    const ProjectRegistryEntryUVE* const entry = registry.FindUVE(project);
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->displayName, "Game");
    EXPECT_EQ(entry->engineVersion, kVersionUVE);
    EXPECT_GT(entry->lastOpenedUnixSeconds, 0);
}

} // namespace
} // namespace UVE::Pack::Tests
