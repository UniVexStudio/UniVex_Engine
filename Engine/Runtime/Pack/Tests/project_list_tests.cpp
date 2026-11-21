// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_list.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "engine_install.h"
#include "project_template.h"
#include "process_launcher.h"

namespace UVE::Pack::Tests {
namespace {

inline constexpr Core::VersionUVE kEngineUVE{1U, 4U, 0U, 0U};

[[nodiscard]] ProjectRegistryEntryUVE EntryUVE(const char* name, const std::int64_t opened,
                                               const Platform::EditorProjectVersionUVE version, const bool favorite) {
    ProjectRegistryEntryUVE entry;
    entry.projectFile = std::filesystem::path("/projects") / name / (std::string(name) + ".uvproject");
    entry.displayName = name;
    entry.lastOpenedUnixSeconds = opened;
    entry.engineVersion = version;
    entry.favorite = favorite;
    return entry;
}

[[nodiscard]] bool NeverMissingUVE(const ProjectRegistryEntryUVE& /*entry*/) { return false; }

TEST(ProjectListUVETest, FavoritesFirstThenMostRecent) {
    const std::vector<ProjectRegistryEntryUVE> entries{EntryUVE("Alpha", 10, {1U, 4U, 0U, 0U}, false),
                                                       EntryUVE("Beta", 30, {1U, 4U, 0U, 0U}, false),
                                                       EntryUVE("Gamma", 20, {1U, 4U, 0U, 0U}, true)};
    const std::vector<ProjectListRowUVE> rows =
        BuildProjectListUVE(entries, "", ProjectListSortUVE::RecentlyOpened, kEngineUVE, NeverMissingUVE);
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].entry.displayName, "Gamma");
    EXPECT_EQ(rows[1].entry.displayName, "Beta");
    EXPECT_EQ(rows[2].entry.displayName, "Alpha");
}

TEST(ProjectListUVETest, SortsByNameIgnoringCase) {
    const std::vector<ProjectRegistryEntryUVE> entries{EntryUVE("beta", 10, {1U, 4U, 0U, 0U}, false),
                                                       EntryUVE("Alpha", 30, {1U, 4U, 0U, 0U}, false)};
    const std::vector<ProjectListRowUVE> rows =
        BuildProjectListUVE(entries, "", ProjectListSortUVE::Name, kEngineUVE, NeverMissingUVE);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].entry.displayName, "Alpha");
}

TEST(ProjectListUVETest, QueryMatchesNameOrPathIgnoringCase) {
    const std::vector<ProjectRegistryEntryUVE> entries{EntryUVE("Racer", 10, {1U, 4U, 0U, 0U}, false),
                                                       EntryUVE("Puzzle", 20, {1U, 4U, 0U, 0U}, false)};
    EXPECT_EQ(BuildProjectListUVE(entries, "RAC", ProjectListSortUVE::Name, kEngineUVE, NeverMissingUVE).size(), 1U);
    EXPECT_EQ(BuildProjectListUVE(entries, "/projects/puzzle", ProjectListSortUVE::Name, kEngineUVE, NeverMissingUVE)
                  .size(),
              1U);
    EXPECT_TRUE(BuildProjectListUVE(entries, "none", ProjectListSortUVE::Name, kEngineUVE, NeverMissingUVE).empty());
}

TEST(ProjectListUVETest, StatusFollowsCompatibilityAndMissingFiles) {
    const std::vector<ProjectRegistryEntryUVE> entries{
        EntryUVE("Ready", 50, {1U, 4U, 0U, 0U}, false), EntryUVE("Old", 40, {1U, 2U, 0U, 0U}, false),
        EntryUVE("Newer", 30, {1U, 5U, 0U, 0U}, false), EntryUVE("Future", 20, {2U, 0U, 0U, 0U}, false),
        EntryUVE("Gone", 10, {1U, 4U, 0U, 0U}, false)};
    const auto missing = [](const ProjectRegistryEntryUVE& entry) { return entry.displayName == "Gone"; };
    const std::vector<ProjectListRowUVE> rows =
        BuildProjectListUVE(entries, "", ProjectListSortUVE::RecentlyOpened, kEngineUVE, missing);
    ASSERT_EQ(rows.size(), 5U);
    EXPECT_EQ(rows[0].status, ProjectListStatusUVE::Ready);
    EXPECT_EQ(rows[1].status, ProjectListStatusUVE::NeedsUpgrade);
    EXPECT_EQ(rows[2].status, ProjectListStatusUVE::OpensWithWarning);
    EXPECT_EQ(rows[3].status, ProjectListStatusUVE::Incompatible);
    EXPECT_EQ(rows[4].status, ProjectListStatusUVE::Missing);
    EXPECT_NE(rows[0].badge.find("1.4.0"), std::string::npos);
}

TEST(LauncherSettingsUVETest, RoundTripsAndFallsBackToDefaults) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("launcher");
    const std::filesystem::path file = root / kLauncherSettingsFileNameUVE;

    const LauncherSettingsUVE defaults = LoadLauncherSettingsUVE(file);
    EXPECT_EQ(defaults.defaultProjectLocation.filename(), kDefaultProjectsDirectoryNameUVE);

    LauncherSettingsUVE settings;
    settings.defaultProjectLocation = root / "Games";
    ASSERT_TRUE(SaveLauncherSettingsUVE(file, settings));
    EXPECT_EQ(LoadLauncherSettingsUVE(file).defaultProjectLocation, root / "Games");

    std::ofstream(file, std::ios::trunc) << "not json";
    EXPECT_EQ(LoadLauncherSettingsUVE(file).defaultProjectLocation.filename(), kDefaultProjectsDirectoryNameUVE);
    std::filesystem::remove_all(root);
}

TEST(EngineInstallUVETest, FindsEditorBesideTheLauncher) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("install");
    const Core::VersionUVE version{0U, 1U, 0U, 1U};
    EXPECT_FALSE(ResolveEngineInstallUVE(root, version).IsFoundUVE());

    const std::filesystem::path editor =
        root / (std::string(kEditorExecutableStemUVE) + std::string(Platform::kExecutableSuffixUVE));
    std::ofstream(editor) << "editor";
    const EngineInstallResultUVE found = ResolveEngineInstallUVE(root, version);
    ASSERT_TRUE(found.IsFoundUVE()) << found.message;
    EXPECT_EQ(found.install->editorExecutable, std::filesystem::canonical(editor));
    EXPECT_EQ(found.install->templatesDirectory.filename(), kProjectTemplatesDirectoryNameUVE);
    EXPECT_EQ(found.install->version, version);
    std::filesystem::remove_all(root);
}

TEST(EngineInstallUVETest, EditorArgumentsOpenTheProject) {
    EXPECT_EQ(MakeEditorLaunchArgumentsUVE("/p/Game.uvproject"),
              (std::vector<std::string>{std::string(kEditorProjectOptionUVE), "/p/Game.uvproject"}));
    EXPECT_EQ(MakeEditorLaunchArgumentsUVE("/p/Game.uvproject", "/r/projects.uvregistry"),
              (std::vector<std::string>{std::string(kEditorProjectOptionUVE), "/p/Game.uvproject",
                                        std::string(kEditorProjectRegistryOptionUVE), "/r/projects.uvregistry"}));
}

#if !defined(_WIN32)
TEST(ProcessLauncherUVETest, LaunchesAndReportsMissingPrograms) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectoryUVE("launch");
    const std::filesystem::path marker = root / "ran";
    const Platform::ProcessLaunchResultUVE launched =
        Platform::LaunchDetachedProcessUVE({"touch", {marker.string()}, {}});
    ASSERT_TRUE(launched.launched) << launched.message;
    // The program is detached; give it a bounded moment to run.
    for (int attempt = 0; attempt < 200 && !std::filesystem::exists(marker); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(std::filesystem::exists(marker));

    EXPECT_FALSE(Platform::LaunchDetachedProcessUVE({root / "does-not-exist", {}, {}}).launched);
    EXPECT_FALSE(Platform::RevealInFileManagerUVE(root / "missing-folder").launched);
    std::filesystem::remove_all(root);
}
#endif

} // namespace
} // namespace UVE::Pack::Tests
