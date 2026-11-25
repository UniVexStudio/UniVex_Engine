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

inline constexpr Core::Version kEngine{1U, 4U, 0U, 0U};

[[nodiscard]] ProjectRegistryEntry Entry(const char* name, const std::int64_t opened,
                                               const Platform::EditorProjectVersion version, const bool favorite) {
    ProjectRegistryEntry entry;
    entry.projectFile = std::filesystem::path("/projects") / name / (std::string(name) + ".uvproject");
    entry.displayName = name;
    entry.lastOpenedUnixSeconds = opened;
    entry.engineVersion = version;
    entry.favorite = favorite;
    return entry;
}

[[nodiscard]] bool NeverMissing(const ProjectRegistryEntry& /*entry*/) { return false; }

TEST(ProjectListTest, FavoritesFirstThenMostRecent) {
    const std::vector<ProjectRegistryEntry> entries{Entry("Alpha", 10, {1U, 4U, 0U, 0U}, false),
                                                       Entry("Beta", 30, {1U, 4U, 0U, 0U}, false),
                                                       Entry("Gamma", 20, {1U, 4U, 0U, 0U}, true)};
    const std::vector<ProjectListRow> rows =
        BuildProjectList(entries, "", ProjectListSort::RecentlyOpened, kEngine, NeverMissing);
    ASSERT_EQ(rows.size(), 3U);
    EXPECT_EQ(rows[0].entry.displayName, "Gamma");
    EXPECT_EQ(rows[1].entry.displayName, "Beta");
    EXPECT_EQ(rows[2].entry.displayName, "Alpha");
}

TEST(ProjectListTest, SortsByNameIgnoringCase) {
    const std::vector<ProjectRegistryEntry> entries{Entry("beta", 10, {1U, 4U, 0U, 0U}, false),
                                                       Entry("Alpha", 30, {1U, 4U, 0U, 0U}, false)};
    const std::vector<ProjectListRow> rows =
        BuildProjectList(entries, "", ProjectListSort::Name, kEngine, NeverMissing);
    ASSERT_EQ(rows.size(), 2U);
    EXPECT_EQ(rows[0].entry.displayName, "Alpha");
}

TEST(ProjectListTest, QueryMatchesNameOrPathIgnoringCase) {
    const std::vector<ProjectRegistryEntry> entries{Entry("Racer", 10, {1U, 4U, 0U, 0U}, false),
                                                       Entry("Puzzle", 20, {1U, 4U, 0U, 0U}, false)};
    EXPECT_EQ(BuildProjectList(entries, "RAC", ProjectListSort::Name, kEngine, NeverMissing).size(), 1U);
    EXPECT_EQ(BuildProjectList(entries, "/projects/puzzle", ProjectListSort::Name, kEngine, NeverMissing)
                  .size(),
              1U);
    EXPECT_TRUE(BuildProjectList(entries, "none", ProjectListSort::Name, kEngine, NeverMissing).empty());
}

TEST(ProjectListTest, StatusFollowsCompatibilityAndMissingFiles) {
    const std::vector<ProjectRegistryEntry> entries{
        Entry("Ready", 50, {1U, 4U, 0U, 0U}, false), Entry("Old", 40, {1U, 2U, 0U, 0U}, false),
        Entry("Newer", 30, {1U, 5U, 0U, 0U}, false), Entry("Future", 20, {2U, 0U, 0U, 0U}, false),
        Entry("Gone", 10, {1U, 4U, 0U, 0U}, false)};
    const auto missing = [](const ProjectRegistryEntry& entry) { return entry.displayName == "Gone"; };
    const std::vector<ProjectListRow> rows =
        BuildProjectList(entries, "", ProjectListSort::RecentlyOpened, kEngine, missing);
    ASSERT_EQ(rows.size(), 5U);
    EXPECT_EQ(rows[0].status, ProjectListStatus::Ready);
    EXPECT_EQ(rows[1].status, ProjectListStatus::NeedsUpgrade);
    EXPECT_EQ(rows[2].status, ProjectListStatus::OpensWithWarning);
    EXPECT_EQ(rows[3].status, ProjectListStatus::Incompatible);
    EXPECT_EQ(rows[4].status, ProjectListStatus::Missing);
    EXPECT_NE(rows[0].badge.find("1.4.0"), std::string::npos);
}

TEST(LauncherSettingsTest, RoundTripsAndFallsBackToDefaults) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("launcher");
    const std::filesystem::path file = root / kLauncherSettingsFileName;

    const LauncherSettings defaults = LoadLauncherSettings(file);
    EXPECT_EQ(defaults.defaultProjectLocation.filename(), kDefaultProjectsDirectoryName);

    LauncherSettings settings;
    settings.defaultProjectLocation = root / "Games";
    ASSERT_TRUE(SaveLauncherSettings(file, settings));
    EXPECT_EQ(LoadLauncherSettings(file).defaultProjectLocation, root / "Games");

    std::ofstream(file, std::ios::trunc) << "not json";
    EXPECT_EQ(LoadLauncherSettings(file).defaultProjectLocation.filename(), kDefaultProjectsDirectoryName);
    std::filesystem::remove_all(root);
}

TEST(EngineInstallTest, FindsEditorBesideTheLauncher) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("install");
    const Core::Version version{0U, 1U, 0U, 1U};
    EXPECT_FALSE(ResolveEngineInstall(root, version).IsFound());

    const std::filesystem::path editor =
        root / (std::string(kEditorExecutableStem) + std::string(Platform::kExecutableSuffix));
    std::ofstream(editor) << "editor";
    const EngineInstallResult found = ResolveEngineInstall(root, version);
    ASSERT_TRUE(found.IsFound()) << found.message;
    EXPECT_EQ(found.install->editorExecutable, std::filesystem::canonical(editor));
    EXPECT_EQ(found.install->templatesDirectory.filename(), kProjectTemplatesDirectoryName);
    EXPECT_EQ(found.install->version, version);
    std::filesystem::remove_all(root);
}

TEST(EngineInstallTest, EditorArgumentsOpenTheProject) {
    EXPECT_EQ(MakeEditorLaunchArguments("/p/Game.uvproject"),
              (std::vector<std::string>{std::string(kEditorProjectOption), "/p/Game.uvproject"}));
    EXPECT_EQ(MakeEditorLaunchArguments("/p/Game.uvproject", "/r/projects.uvregistry"),
              (std::vector<std::string>{std::string(kEditorProjectOption), "/p/Game.uvproject",
                                        std::string(kEditorProjectRegistryOption), "/r/projects.uvregistry"}));
}

#if !defined(_WIN32)
TEST(ProcessLauncherTest, LaunchesAndReportsMissingPrograms) {
    const std::filesystem::path root = ::UVE::Tests::MakeTestCaseDirectory("launch");
    const std::filesystem::path marker = root / "ran";
    const Platform::ProcessLaunchResult launched =
        Platform::LaunchDetachedProcess({"touch", {marker.string()}, {}});
    ASSERT_TRUE(launched.launched) << launched.message;
    // The program is detached; give it a bounded moment to run.
    for (int attempt = 0; attempt < 200 && !std::filesystem::exists(marker); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(std::filesystem::exists(marker));

    EXPECT_FALSE(Platform::LaunchDetachedProcess({root / "does-not-exist", {}, {}}).launched);
    EXPECT_FALSE(Platform::RevealInFileManager(root / "missing-folder").launched);
    std::filesystem::remove_all(root);
}
#endif

} // namespace
} // namespace UVE::Pack::Tests
