// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "application_runtime.h"

#include <cstdlib>
#include <filesystem>
#include <string>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

namespace UVE::Platform::Tests {
namespace {

class ApplicationRuntimeTest : public ::testing::Test {
protected:
    void SetUp() override {
        projectRoot = ::UVE::Tests::MakeTestCaseDirectory("project");
        dataRoot = ::UVE::Tests::MakeTestCaseDirectory("user-data");
    }

    std::filesystem::path projectRoot;
    std::filesystem::path dataRoot;
};

TEST_F(ApplicationRuntimeTest, ResolvePortableUserDataDirectory_UsesProjectSiblingAndCreatesIt) {
    EditorProjectPackage package;
    package.projectId = "portable.project";
    package.applicationSettings.userDataDirectoryName = "portable-data";
    package.applicationSettings.portableUserData = true;

    const ApplicationUserDataDirectoryResult result =
        ResolveApplicationUserDataDirectory(package, projectRoot);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.directory, std::filesystem::absolute(projectRoot) / "portable-data");
    EXPECT_TRUE(std::filesystem::is_directory(result.directory));
}

TEST_F(ApplicationRuntimeTest, ResolveUserDataDirectory_UsesSanitizedFallbackName) {
    EditorProjectPackage package;
    package.projectId = "org.example.project";
    package.productMetadata.shortName = "Space Quest";
    package.applicationSettings.portableUserData = true;

    const ApplicationUserDataDirectoryResult result =
        ResolveApplicationUserDataDirectory(package, projectRoot);

    ASSERT_TRUE(result.success) << result.message;
    EXPECT_EQ(result.directory.filename(), "Space_Quest");
    EXPECT_TRUE(std::filesystem::is_directory(result.directory));
}

TEST_F(ApplicationRuntimeTest, ResolveUserDataDirectory_RejectsUnsafeExplicitName) {
    EditorProjectPackage package;
    package.projectId = "unsafe-project";
    package.applicationSettings.userDataDirectoryName = "../escape";
    package.applicationSettings.portableUserData = true;

    const ApplicationUserDataDirectoryResult result =
        ResolveApplicationUserDataDirectory(package, projectRoot);

    EXPECT_FALSE(result.success);
    EXPECT_FALSE(std::filesystem::exists(projectRoot.parent_path() / "escape"));
}

#if !defined(__EMSCRIPTEN__)
TEST_F(ApplicationRuntimeTest, SingleInstanceLock_RejectsDuplicateAndReleasesOnDestruction) {
    const std::string identifier = "com.example.unique-instance-lock-test";
    {
        SingleInstanceLock first;
        const SingleInstanceLockResult firstResult = first.Acquire(identifier, dataRoot);
        ASSERT_TRUE(firstResult.IsAcquired()) << firstResult.message;

        SingleInstanceLock duplicate;
        const SingleInstanceLockResult duplicateResult = duplicate.Acquire(identifier, dataRoot);
        EXPECT_EQ(duplicateResult.code, SingleInstanceLockCode::AlreadyRunning);

        SingleInstanceLock anotherApplication;
        EXPECT_TRUE(anotherApplication.Acquire("com.example.other-app", dataRoot).IsAcquired());
    }

    SingleInstanceLock afterRelease;
    const SingleInstanceLockResult afterReleaseResult = afterRelease.Acquire(identifier, dataRoot);
    EXPECT_TRUE(afterReleaseResult.IsAcquired()) << afterReleaseResult.message;
}
#endif

} // namespace
} // namespace UVE::Platform::Tests
