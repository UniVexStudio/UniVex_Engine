// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/platform/application_runtime_uve.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

namespace UVE::Platform::Tests {
namespace {

class ApplicationRuntimeUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        projectRoot = ::UVE::Tests::MakeTestCaseDirectoryUVE("project");
        dataRoot = ::UVE::Tests::MakeTestCaseDirectoryUVE("user-data");
    }

    std::filesystem::path projectRoot;
    std::filesystem::path dataRoot;
};

TEST_F(ApplicationRuntimeUVETest, ResolvePortableUserDataDirectory_UsesProjectSiblingAndCreatesIt) {
    EditorProjectPackageUVE package;
    package.projectId = "portable.project";
    package.applicationSettings.userDataDirectoryName = "portable-data";
    package.applicationSettings.portableUserData = true;

    const ApplicationUserDataDirectoryResultUVE result =
        ResolveApplicationUserDataDirectoryUVE(package, projectRoot);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.directory, std::filesystem::absolute(projectRoot) / "portable-data");
    EXPECT_TRUE(std::filesystem::is_directory(result.directory));
}

TEST_F(ApplicationRuntimeUVETest, ResolveUserDataDirectory_UsesSanitizedFallbackName) {
    EditorProjectPackageUVE package;
    package.projectId = "org.example.project";
    package.productMetadata.shortName = "Space Quest";
    package.applicationSettings.portableUserData = true;

    const ApplicationUserDataDirectoryResultUVE result =
        ResolveApplicationUserDataDirectoryUVE(package, projectRoot);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.directory.filename(), "Space_Quest");
    EXPECT_TRUE(std::filesystem::is_directory(result.directory));
}

TEST_F(ApplicationRuntimeUVETest, ResolveUserDataDirectory_RejectsUnsafeExplicitName) {
    EditorProjectPackageUVE package;
    package.projectId = "unsafe-project";
    package.applicationSettings.userDataDirectoryName = "../escape";
    package.applicationSettings.portableUserData = true;

    const ApplicationUserDataDirectoryResultUVE result =
        ResolveApplicationUserDataDirectoryUVE(package, projectRoot);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(std::filesystem::exists(projectRoot.parent_path() / "escape"));
}

#if !defined(__EMSCRIPTEN__)
TEST_F(ApplicationRuntimeUVETest, SingleInstanceLock_RejectsDuplicateAndReleasesOnDestruction) {
    const std::string identifier = "com.example.unique-instance-lock-test";
    {
        SingleInstanceLockUVE first;
        const SingleInstanceLockResultUVE firstResult = first.AcquireUVE(identifier, dataRoot);
        ASSERT_TRUE(firstResult.IsAcquiredUVE()) << firstResult.message;

        SingleInstanceLockUVE duplicate;
        const SingleInstanceLockResultUVE duplicateResult = duplicate.AcquireUVE(identifier, dataRoot);
        EXPECT_EQ(duplicateResult.code, SingleInstanceLockCodeUVE::AlreadyRunning);

        SingleInstanceLockUVE anotherApplication;
        EXPECT_TRUE(anotherApplication.AcquireUVE("com.example.other-app", dataRoot).IsAcquiredUVE());
    }

    SingleInstanceLockUVE afterRelease;
    const SingleInstanceLockResultUVE afterReleaseResult = afterRelease.AcquireUVE(identifier, dataRoot);
    EXPECT_TRUE(afterReleaseResult.IsAcquiredUVE()) << afterReleaseResult.message;
}
#endif

} // namespace
} // namespace UVE::Platform::Tests
