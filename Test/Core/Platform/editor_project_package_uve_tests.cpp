#include "uve/platform/editor_project_package_uve.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "Support/test_scratch_uve.h"

namespace UVE::Platform::Tests {
namespace {

class EditorProjectPackageUVETest : public ::testing::Test {
protected:
    void SetUp() override { packagePath = ::UVE::Tests::MakeTestCaseDirectoryUVE() / "package.uvproject"; }

    [[nodiscard]] EditorProjectPackageUVE MakePackage() const {
        EditorProjectPackageUVE package;
        package.revision = 4U;
        package.projectId = "univex-demo-01";
        package.displayName = "UniVex Demo";
        package.engineVersion = {0U, 1U, 0U, 42U};
        package.productMetadata.name = "Astro Build";
        package.productMetadata.shortName = "astro";
        package.productMetadata.description = "A compact sample project.";
        package.productMetadata.version = "2.3.1-beta";
        package.productMetadata.buildNumber = 42U;
        package.applicationSettings.publisherName = "UniVex Studios";
        package.applicationSettings.copyrightLine = "Copyright (c) 2026 UniVex Studios.";
        package.applicationSettings.applicationIdentifiersByTarget = {
            {"linux", "com.example.astro"}, {"windows", "com.example.astro"}};
        package.applicationSettings.iconPathsByTarget["linux"] = {{64U, "icons/linux-64.png"},
                                                                    {128U, "icons/linux-128.png"}};
        package.applicationSettings.splashImagePath = "branding/splash.uvtex";
        package.applicationSettings.splashBackgroundColor = {0.05F, 0.1F, 0.2F, 1.0F};
        package.applicationSettings.splashFadeSeconds = 0.5;
        package.applicationSettings.splashMinimumDisplaySeconds = 2.0;
        package.applicationSettings.splashSkippable = false;
        package.applicationSettings.skipSplashInEditorPlayMode = true;
        package.applicationSettings.quitOnLastWindowClosed = false;
        package.applicationSettings.enforceSingleInstance = true;
        package.applicationSettings.userDataDirectoryName = "astro";
        package.applicationSettings.portableUserData = true;
        package.applicationSettings.crashHandlerEnabled = false;
        package.applicationSettings.crashDumpDirectory = "reports/crashes";
        package.applicationSettings.symbolUploadEndpoint = "https://symbols.example.com/upload";
        package.applicationSettings.windowWidth = 1920U;
        package.applicationSettings.windowHeight = 1080U;
        package.applicationSettings.windowMode = WindowModeUVE::ExclusiveFullscreen;
        package.applicationSettings.windowResizable = false;
        package.applicationSettings.windowBorderless = true;
        package.applicationSettings.windowAlwaysOnTop = true;
        package.applicationSettings.windowTransparent = true;
        package.applicationSettings.minimumWindowWidth = 640U;
        package.applicationSettings.minimumWindowHeight = 360U;
        package.applicationSettings.maximumWindowWidth = 2560U;
        package.applicationSettings.maximumWindowHeight = 1440U;
        package.applicationSettings.initialWindowPositionSpecified = true;
        package.applicationSettings.initialWindowPositionX = 40;
        package.applicationSettings.initialWindowPositionY = 80;
        package.applicationSettings.initialMonitorName = "Display-1";
        package.applicationSettings.highDpiAware = false;
        package.applicationSettings.perMonitorScaling = false;
        package.applicationSettings.contentScaleOverride = 1.25;
        package.applicationSettings.stretchMode = StretchModeUVE::Viewport;
        package.applicationSettings.aspectPolicy = AspectPolicyUVE::KeepWidth;
        package.applicationSettings.integerOnlyScaling = true;
        package.applicationSettings.orientation = DisplayOrientationUVE::LandscapeLeft;
        package.applicationSettings.allowedOrientations = {DisplayOrientationUVE::LandscapeLeft,
                                                            DisplayOrientationUVE::Portrait};
        package.applicationSettings.vsyncMode = VSyncModeUVE::Mailbox;
        package.applicationSettings.focusedFrameRateCap = 144U;
        package.applicationSettings.unfocusedFrameRateCap = 30U;
        package.applicationSettings.allowDisplaySleep = false;
        package.applicationSettings.cursorImagePath = "ui/cursor.png";
        package.applicationSettings.cursorHotspotX = 3U;
        package.applicationSettings.cursorHotspotY = 5U;
        package.applicationSettings.cursorVisible = false;
        package.applicationSettings.cursorConfinedToWindow = true;
        package.applicationSettings.windowTitleFormat = "{projectName} ({sceneName})";
        package.applicationSettings.appendSceneNameInEditorPlayMode = true;
        package.contentRoot = "assets";
        package.assetDatabasePath = ".uvassetdb";
        package.settingsPath = ".uvsettings";
        return package;
    }

    std::filesystem::path packagePath;
};

TEST_F(EditorProjectPackageUVETest, SaveAndLoad_RoundTripsPortableDescriptor) {
    const EditorProjectPackageUVE expected = MakePackage();

    const EditorProjectPackageResultUVE saveResult =
        EditorProjectPackageCodecUVE::SaveUVE(packagePath, expected);

    ASSERT_TRUE(saveResult.IsAcceptedUVE()) << saveResult.message;
    const EditorProjectPackageLoadResultUVE loadResult =
        EditorProjectPackageCodecUVE::LoadUVE(packagePath);

    ASSERT_TRUE(loadResult.IsAcceptedUVE()) << loadResult.result.message;
    EXPECT_EQ(*loadResult.package, expected);
}

TEST_F(EditorProjectPackageUVETest, Validate_RejectsUnboundedIdentityAndTraversalPaths) {
    EditorProjectPackageUVE package = MakePackage();
    package.projectId = "demo/project";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.contentRoot = "../assets";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPath);

    package = MakePackage();
    package.settingsPath = "/tmp/settings.json";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPath);
}

TEST_F(EditorProjectPackageUVETest, Validate_RejectsInvalidProductMetadata) {
    EditorProjectPackageUVE package = MakePackage();
    package.productMetadata.name = std::string(kMaximumEditorProductNameBytesUVE + 1U, 'x');
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.productMetadata.shortName = std::string(kMaximumEditorProductShortNameBytesUVE + 1U, 'x');
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.productMetadata.description.assign(16U, 'x');
    package.productMetadata.description[3U] = static_cast<char>(0);
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.productMetadata.version.clear();
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);
}

TEST_F(EditorProjectPackageUVETest, Validate_RejectsInvalidApplicationSettings) {
    EditorProjectPackageUVE package = MakePackage();
    package.applicationSettings.applicationIdentifiersByTarget["linux"] = "com..astro";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.applicationIdentifiersByTarget["desktop"] = "com.example.astro";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.iconPathsByTarget["windows"][256U] = "../icon.png";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPath);

    package = MakePackage();
    package.applicationSettings.splashFadeSeconds = 61.0;
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.userDataDirectoryName = "../escape";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.symbolUploadEndpoint = "http://symbols.example.com";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);
}

TEST_F(EditorProjectPackageUVETest, Validate_RejectsInvalidWindowAndCursorPolicies) {
    EditorProjectPackageUVE package = MakePackage();
    package.applicationSettings.windowWidth = 0U;
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.minimumWindowWidth = 3000U;
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.cursorImagePath = "../cursor.png";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPath);

    package = MakePackage();
    package.applicationSettings.orientation = DisplayOrientationUVE::LandscapeRight;
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.focusedFrameRateCap = 1001U;
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.contentScaleOverride = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code,
              EditorProjectPackageCodeUVE::InvalidPackage);
}

TEST_F(EditorProjectPackageUVETest, Save_RejectsWrongExtensionWithoutCreatingFile) {
    EditorProjectPackageUVE package = MakePackage();
    std::filesystem::path wrongPath = packagePath;
    wrongPath.replace_extension(".json");

    const EditorProjectPackageResultUVE result =
        EditorProjectPackageCodecUVE::SaveUVE(wrongPath, package);

    EXPECT_EQ(result.code, EditorProjectPackageCodeUVE::InvalidPath);
    EXPECT_FALSE(std::filesystem::exists(wrongPath));
}

TEST_F(EditorProjectPackageUVETest, Load_MalformedOrWrongFormatFailsClosed) {
    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << "{\"format\":\"other\"}\n";
    }
    EXPECT_EQ(EditorProjectPackageCodecUVE::LoadUVE(packagePath).result.code,
              EditorProjectPackageCodeUVE::ParseFailed);

    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << "not-json\n";
    }
    EXPECT_EQ(EditorProjectPackageCodecUVE::LoadUVE(packagePath).result.code,
              EditorProjectPackageCodeUVE::ParseFailed);
}

TEST_F(EditorProjectPackageUVETest, ApplyUpdate_RequiresExpectedRevisionAndMatchingIdentity) {
    const EditorProjectPackageUVE initial = MakePackage();
    ASSERT_TRUE(EditorProjectPackageCodecUVE::SaveUVE(packagePath, initial).IsAcceptedUVE());

    EditorProjectPackageUVE replacement = initial;
    replacement.revision = 5U;
    replacement.displayName = "Updated Demo";

    const EditorProjectPackageResultUVE staleResult =
        EditorProjectPackageCodecUVE::ApplyUpdateUVE(packagePath, 3U, replacement);
    EXPECT_EQ(staleResult.code, EditorProjectPackageCodeUVE::RevisionConflict);
    ASSERT_TRUE(EditorProjectPackageCodecUVE::LoadUVE(packagePath).package.has_value());
    EXPECT_EQ(EditorProjectPackageCodecUVE::LoadUVE(packagePath).package->revision, 4U);

    const EditorProjectPackageResultUVE updateResult =
        EditorProjectPackageCodecUVE::ApplyUpdateUVE(packagePath, 4U, replacement);
    ASSERT_TRUE(updateResult.IsAcceptedUVE()) << updateResult.message;
    EXPECT_EQ(EditorProjectPackageCodecUVE::LoadUVE(packagePath).package->displayName, "Updated Demo");

    EditorProjectPackageUVE differentProject = replacement;
    differentProject.revision = 6U;
    differentProject.projectId = "other-project";
    const EditorProjectPackageResultUVE identityResult =
        EditorProjectPackageCodecUVE::ApplyUpdateUVE(packagePath, 5U, differentProject);
    EXPECT_EQ(identityResult.code, EditorProjectPackageCodeUVE::ProjectIdentityConflict);
    EXPECT_EQ(EditorProjectPackageCodecUVE::LoadUVE(packagePath).package->revision, 5U);
}

TEST_F(EditorProjectPackageUVETest, ApplyUpdate_RejectsNonNewerReplacementWithoutMutation) {
    const EditorProjectPackageUVE initial = MakePackage();
    ASSERT_TRUE(EditorProjectPackageCodecUVE::SaveUVE(packagePath, initial).IsAcceptedUVE());

    EditorProjectPackageUVE replacement = initial;
    replacement.displayName = "Stale";
    const EditorProjectPackageResultUVE result =
        EditorProjectPackageCodecUVE::ApplyUpdateUVE(packagePath, initial.revision, replacement);

    EXPECT_EQ(result.code, EditorProjectPackageCodeUVE::RevisionConflict);
    const auto loaded = EditorProjectPackageCodecUVE::LoadUVE(packagePath);
    ASSERT_TRUE(loaded.IsAcceptedUVE());
    EXPECT_EQ(loaded.package->displayName, initial.displayName);
}

TEST_F(EditorProjectPackageUVETest, SaveAndLoad_RoundTripsStartupScenePath) {
    EditorProjectPackageUVE expected = MakePackage();
    expected.startupScenePath = "scenes/main.uvscene";

    ASSERT_TRUE(EditorProjectPackageCodecUVE::SaveUVE(packagePath, expected).IsAcceptedUVE());
    const EditorProjectPackageLoadResultUVE loadResult = EditorProjectPackageCodecUVE::LoadUVE(packagePath);

    ASSERT_TRUE(loadResult.IsAcceptedUVE()) << loadResult.result.message;
    EXPECT_EQ(loadResult.package->startupScenePath, "scenes/main.uvscene");
}

TEST_F(EditorProjectPackageUVETest, Load_DefaultsMissingStartupScenePathToEmptyForOlderFiles) {
    // Simulates an older .uvproject file written before productMetadata and startupScenePath
    // existed - neither key is present, not merely set to an empty value.
    const nlohmann::json legacyJson{{"format", "uvproject"},
                                    {"schemaVersion", kCurrentEditorProjectSchemaVersionUVE},
                                    {"revision", 1U},
                                    {"projectId", "legacy-project"},
                                    {"displayName", "Legacy"},
                                    {"engineVersion", {{"major", 0U}, {"minor", 1U}, {"patch", 0U}, {"build", 1U}}},
                                    {"contentRoot", "assets"},
                                    {"assetDatabasePath", ".uvassetdb"},
                                    {"settingsPath", ".uvsettings"}};
    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << legacyJson.dump();
    }

    const EditorProjectPackageLoadResultUVE loadResult = EditorProjectPackageCodecUVE::LoadUVE(packagePath);

    ASSERT_TRUE(loadResult.IsAcceptedUVE()) << loadResult.result.message;
    EXPECT_TRUE(loadResult.package->startupScenePath.empty());
    EXPECT_TRUE(loadResult.package->productMetadata.name.empty());
    EXPECT_TRUE(loadResult.package->productMetadata.shortName.empty());
    EXPECT_TRUE(loadResult.package->productMetadata.description.empty());
    EXPECT_EQ(loadResult.package->productMetadata.version, "1.0.0");
    EXPECT_EQ(loadResult.package->productMetadata.buildNumber, 0U);
    EXPECT_TRUE(loadResult.package->applicationSettings.publisherName.empty());
    EXPECT_TRUE(loadResult.package->applicationSettings.applicationIdentifiersByTarget.empty());
    EXPECT_TRUE(loadResult.package->applicationSettings.iconPathsByTarget.empty());
    EXPECT_TRUE(loadResult.package->applicationSettings.quitOnLastWindowClosed);
    EXPECT_FALSE(loadResult.package->applicationSettings.enforceSingleInstance);
    EXPECT_TRUE(loadResult.package->applicationSettings.crashHandlerEnabled);
    EXPECT_EQ(loadResult.package->applicationSettings.crashDumpDirectory, "crash-dumps");
}

TEST_F(EditorProjectPackageUVETest, Validate_RejectsTraversalStartupScenePathButAllowsEmpty) {
    EditorProjectPackageUVE package = MakePackage();
    EXPECT_TRUE(EditorProjectPackageCodecUVE::ValidateUVE(package).IsAcceptedUVE());

    package.startupScenePath = "../outside.uvscene";
    EXPECT_EQ(EditorProjectPackageCodecUVE::ValidateUVE(package).code, EditorProjectPackageCodeUVE::InvalidPath);
}

} // namespace
} // namespace UVE::Platform::Tests
