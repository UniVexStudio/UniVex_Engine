// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/pack/project_packager_uve.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "uve/platform/editor_project_package_uve.h"

namespace UVE::Pack::Tests {
namespace {

[[nodiscard]] std::filesystem::path MakeUniqueTestDirectoryUVE(const std::string& label) {
    static std::atomic<unsigned int> nextId{0U};
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                            ("uve_project_packager_test_" + label + "_" +
                                             std::to_string(nextId.fetch_add(1U)));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

class ProjectPackagerUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        projectRoot = MakeUniqueTestDirectoryUVE("root");
        outputDirectory = MakeUniqueTestDirectoryUVE("output");
        std::filesystem::remove_all(outputDirectory); // PackUVE itself must create it

        std::filesystem::create_directories(projectRoot / "content" / "scenes");
        {
            std::ofstream scene(projectRoot / "content" / "scenes" / "main.uvescene");
            scene << "{}";
        }
        {
            std::ofstream texture(projectRoot / "content" / "hero.uvetex", std::ios::binary);
            texture << "fake-texture-bytes";
        }

        runtimeExecutablePath = projectRoot / "fake_uve_runtime";
        {
            std::ofstream runtime(runtimeExecutablePath, std::ios::binary);
            runtime << "#!/bin/sh\necho fake runtime\n";
        }
        std::filesystem::permissions(runtimeExecutablePath,
                                     std::filesystem::perms::owner_all | std::filesystem::perms::group_read |
                                         std::filesystem::perms::group_exec | std::filesystem::perms::others_read |
                                         std::filesystem::perms::others_exec);

        package.revision = 1U;
        package.projectId = "packager-test-project";
        package.displayName = "Packager Test Project";
        package.engineVersion = {0U, 1U, 0U, 1U};
        package.contentRoot = "content";
        package.assetDatabasePath = ".uveassetdb";
        package.settingsPath = ".uvesettings";
        package.startupScenePath = "scenes/main.uvescene";
    }

    void TearDown() override {
        std::filesystem::remove_all(projectRoot);
        std::filesystem::remove_all(outputDirectory);
    }

    [[nodiscard]] std::filesystem::path projectFile() const { return projectRoot / "project.uveditor"; }

    [[nodiscard]] ProjectPackOptionsUVE MakeOptionsUVE() const {
        ProjectPackOptionsUVE options;
        options.projectFile = projectFile();
        options.runtimeExecutablePath = runtimeExecutablePath;
        options.outputDirectory = outputDirectory;
        return options;
    }

    std::filesystem::path projectRoot;
    std::filesystem::path outputDirectory;
    std::filesystem::path runtimeExecutablePath;
    Platform::EditorProjectPackageUVE package;
};

TEST_F(ProjectPackagerUVETest, PackUVE_CopiesRuntimeManifestAndContentIntoOneFolder) {
    ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());

    const ProjectPackResultUVE result = ProjectPackagerUVE::PackUVE(MakeOptionsUVE());

    ASSERT_TRUE(result.IsSuccessUVE()) << result.message;
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "fake_uve_runtime"));
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "project.uveditor"));
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "content" / "scenes" / "main.uvescene"));
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "content" / "hero.uvetex"));

    // The copied .uveditor's relative paths must still resolve unchanged against the copy.
    const Platform::EditorProjectPackageLoadResultUVE reloaded =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(outputDirectory / "project.uveditor");
    ASSERT_TRUE(reloaded.IsAcceptedUVE());
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / reloaded.package->contentRoot /
                                                 reloaded.package->startupScenePath));
}

TEST_F(ProjectPackagerUVETest, PackUVE_PreservesRuntimeExecutablePermissionBits) {
    ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());

    ASSERT_TRUE(ProjectPackagerUVE::PackUVE(MakeOptionsUVE()).IsSuccessUVE());

    const std::filesystem::perms copiedPermissions =
        std::filesystem::status(outputDirectory / "fake_uve_runtime").permissions();
    EXPECT_NE((copiedPermissions & std::filesystem::perms::owner_exec), std::filesystem::perms::none);
}

TEST_F(ProjectPackagerUVETest, PackUVE_RejectsProjectWithNoStartupSceneConfigured) {
    package.startupScenePath.clear();
    ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());

    const ProjectPackResultUVE result = ProjectPackagerUVE::PackUVE(MakeOptionsUVE());

    EXPECT_FALSE(result.IsSuccessUVE());
    EXPECT_EQ(result.code, ProjectPackCodeUVE::NoStartupSceneConfigured);
    EXPECT_FALSE(std::filesystem::exists(outputDirectory));
}

TEST_F(ProjectPackagerUVETest, PackUVE_RejectsMissingRuntimeExecutable) {
    ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());
    ProjectPackOptionsUVE options = MakeOptionsUVE();
    options.runtimeExecutablePath = projectRoot / "no_such_runtime";

    const ProjectPackResultUVE result = ProjectPackagerUVE::PackUVE(options);

    EXPECT_FALSE(result.IsSuccessUVE());
    EXPECT_EQ(result.code, ProjectPackCodeUVE::RuntimeExecutableNotFound);
}

TEST_F(ProjectPackagerUVETest, PackUVE_RejectsNonEmptyExistingOutputDirectory) {
    ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());
    std::filesystem::create_directories(outputDirectory);
    std::ofstream(outputDirectory / "stray.txt") << "pre-existing";

    const ProjectPackResultUVE result = ProjectPackagerUVE::PackUVE(MakeOptionsUVE());

    EXPECT_FALSE(result.IsSuccessUVE());
    EXPECT_EQ(result.code, ProjectPackCodeUVE::OutputDirectoryNotEmpty);
}

} // namespace
} // namespace UVE::Pack::Tests
