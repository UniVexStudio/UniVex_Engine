// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_packager.h"

#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "editor_project_package.h"

namespace UVE::Pack::Tests {
namespace {

class ProjectPackagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        projectRoot = ::UVE::Tests::MakeTestCaseDirectory("root");
        // Deliberately NOT created: Pack itself must create its output directory.
        outputDirectory = ::UVE::Tests::ScratchPath("packager_output");

        std::filesystem::create_directories(projectRoot / "content" / "scenes");
        std::filesystem::create_directories(projectRoot / "content" / "icons");
        {
            std::ofstream scene(projectRoot / "content" / "scenes" / "main.uvscene");
            scene << "{}";
        }
        {
            std::ofstream icon(projectRoot / "content" / "icons" / "linux-64.png", std::ios::binary);
            icon << "fake-icon-bytes";
        }
        {
            std::ofstream texture(projectRoot / "content" / "hero.uvtex", std::ios::binary);
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
        package.productMetadata.name = "Sky Runner";
        package.productMetadata.shortName = "sky-runner";
        package.productMetadata.description = "A packaged sample.";
        package.productMetadata.version = "2.4.1-beta";
        package.productMetadata.buildNumber = 17U;
        package.applicationSettings.publisherName = "Studio Labs";
        package.applicationSettings.copyrightLine = "Copyright (c) 2026 Studio Labs.";
        package.applicationSettings.applicationIdentifiersByTarget["linux"] = "com.example.skyrunner";
        package.applicationSettings.iconPathsByTarget["linux"] = {{64U, "icons/linux-64.png"}};
        package.contentRoot = "content";
        package.assetDatabasePath = ".uvassetdb";
        package.settingsPath = ".uvsettings";
        package.startupScenePath = "scenes/main.uvscene";
    }

    void TearDown() override {
        std::filesystem::remove_all(projectRoot);
        std::filesystem::remove_all(outputDirectory);
    }

    [[nodiscard]] std::filesystem::path projectFile() const { return projectRoot / "project.uvproject"; }

    [[nodiscard]] ProjectPackOptions MakeOptions() const {
        ProjectPackOptions options;
        options.projectFile = projectFile();
        options.runtimeExecutablePath = runtimeExecutablePath;
        options.outputDirectory = outputDirectory;
        return options;
    }

    std::filesystem::path projectRoot;
    std::filesystem::path outputDirectory;
    std::filesystem::path runtimeExecutablePath;
    Platform::EditorProjectPackage package;
};

TEST_F(ProjectPackagerTest, Pack_CopiesRuntimeManifestAndContentIntoOneFolder) {
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    const ProjectPackResult result = ProjectPackager::Pack(MakeOptions());

    ASSERT_TRUE(result.IsSuccess()) << result.message;
    EXPECT_NE(result.message.find("Sky Runner"), std::string::npos);
    EXPECT_NE(result.message.find("v2.4.1-beta (build 17)"), std::string::npos);
    EXPECT_NE(result.message.find("publisher: Studio Labs"), std::string::npos);
    EXPECT_NE(result.message.find("Copyright (c) 2026 Studio Labs."), std::string::npos);
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "fake_uve_runtime"));
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "project.uvproject"));
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "content" / "scenes" / "main.uvscene"));
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "content" / "hero.uvtex"));
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / "content" / "icons" / "linux-64.png"));

    // The copied .uvproject's relative paths must still resolve unchanged against the copy.
    const Platform::EditorProjectPackageLoadResult reloaded =
        Platform::EditorProjectPackageCodec::Load(outputDirectory / "project.uvproject");
    ASSERT_TRUE(reloaded.IsAccepted());
    EXPECT_EQ(reloaded.package->productMetadata, package.productMetadata);
    EXPECT_EQ(reloaded.package->applicationSettings, package.applicationSettings);
    EXPECT_TRUE(std::filesystem::is_regular_file(outputDirectory / reloaded.package->contentRoot /
                                                 reloaded.package->startupScenePath));
}

TEST_F(ProjectPackagerTest, Pack_RejectsMissingReferencedApplicationIcon) {
    package.applicationSettings.iconPathsByTarget["windows"] = {{256U, "icons/missing.png"}};
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    const ProjectPackResult result = ProjectPackager::Pack(MakeOptions());

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.code, ProjectPackCode::ApplicationResourceNotFound);
    EXPECT_FALSE(std::filesystem::exists(outputDirectory));
}

TEST_F(ProjectPackagerTest, Pack_PreservesRuntimeExecutablePermissionBits) {
#if defined(_WIN32)
    GTEST_SKIP() << "POSIX execute permissions are not represented on Windows filesystem";
#else
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    ASSERT_TRUE(ProjectPackager::Pack(MakeOptions()).IsSuccess());

    const std::filesystem::perms copiedPermissions =
        std::filesystem::status(outputDirectory / "fake_uve_runtime").permissions();
    EXPECT_NE((copiedPermissions & std::filesystem::perms::owner_exec), std::filesystem::perms::none);
#endif
}

TEST_F(ProjectPackagerTest, Pack_RejectsProjectWithNoStartupSceneConfigured) {
    package.startupScenePath.clear();
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    const ProjectPackResult result = ProjectPackager::Pack(MakeOptions());

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.code, ProjectPackCode::NoStartupSceneConfigured);
    EXPECT_FALSE(std::filesystem::exists(outputDirectory));
}

TEST_F(ProjectPackagerTest, Pack_RejectsMissingRuntimeExecutable) {
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());
    ProjectPackOptions options = MakeOptions();
    options.runtimeExecutablePath = projectRoot / "no_such_runtime";

    const ProjectPackResult result = ProjectPackager::Pack(options);

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.code, ProjectPackCode::RuntimeExecutableNotFound);
}

TEST_F(ProjectPackagerTest, Pack_RejectsNonEmptyExistingOutputDirectory) {
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());
    std::filesystem::create_directories(outputDirectory);
    std::ofstream(outputDirectory / "stray.txt") << "pre-existing";

    const ProjectPackResult result = ProjectPackager::Pack(MakeOptions());

    EXPECT_FALSE(result.IsSuccess());
    EXPECT_EQ(result.code, ProjectPackCode::OutputDirectoryNotEmpty);
}

} // namespace
} // namespace UVE::Pack::Tests
