#include "editor_project_package.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "Support/test_scratch.h"

namespace UVE::Platform::Tests {
namespace {

class EditorProjectPackageTest : public ::testing::Test {
protected:
    void SetUp() override { packagePath = ::UVE::Tests::MakeTestCaseDirectory() / "package.uvproject"; }

    [[nodiscard]] EditorProjectPackage MakePackage() const {
        EditorProjectPackage package;
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
        package.applicationSettings.windowMode = WindowMode::ExclusiveFullscreen;
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
        package.applicationSettings.stretchMode = StretchMode::Viewport;
        package.applicationSettings.aspectPolicy = AspectPolicy::KeepWidth;
        package.applicationSettings.integerOnlyScaling = true;
        package.applicationSettings.orientation = DisplayOrientation::LandscapeLeft;
        package.applicationSettings.allowedOrientations = {DisplayOrientation::LandscapeLeft,
                                                            DisplayOrientation::Portrait};
        package.applicationSettings.vsyncMode = VSyncMode::Mailbox;
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

TEST_F(EditorProjectPackageTest, SaveAndLoad_RoundTripsPortableDescriptor) {
    const EditorProjectPackage expected = MakePackage();

    const EditorProjectPackageResult saveResult =
        EditorProjectPackageCodec::Save(packagePath, expected);

    ASSERT_TRUE(saveResult.IsAccepted()) << saveResult.message;
    const EditorProjectPackageLoadResult loadResult =
        EditorProjectPackageCodec::Load(packagePath);

    ASSERT_TRUE(loadResult.IsAccepted()) << loadResult.result.message;
    EXPECT_EQ(*loadResult.package, expected);
}

TEST_F(EditorProjectPackageTest, Validate_RejectsUnboundedIdentityAndTraversalPaths) {
    EditorProjectPackage package = MakePackage();
    package.projectId = "demo/project";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.contentRoot = "../assets";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPath);

    package = MakePackage();
    package.settingsPath = "/tmp/settings.json";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPath);
}

TEST_F(EditorProjectPackageTest, Validate_RejectsInvalidProductMetadata) {
    EditorProjectPackage package = MakePackage();
    package.productMetadata.name = std::string(kMaximumEditorProductNameBytes + 1U, 'x');
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.productMetadata.shortName = std::string(kMaximumEditorProductShortNameBytes + 1U, 'x');
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.productMetadata.description.assign(16U, 'x');
    package.productMetadata.description[3U] = static_cast<char>(0);
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.productMetadata.version.clear();
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);
}

TEST_F(EditorProjectPackageTest, Validate_RejectsInvalidApplicationSettings) {
    EditorProjectPackage package = MakePackage();
    package.applicationSettings.applicationIdentifiersByTarget["linux"] = "com..astro";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.applicationIdentifiersByTarget["desktop"] = "com.example.astro";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.iconPathsByTarget["windows"][256U] = "../icon.png";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPath);

    package = MakePackage();
    package.applicationSettings.splashFadeSeconds = 61.0;
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.userDataDirectoryName = "../escape";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.symbolUploadEndpoint = "http://symbols.example.com";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);
}

TEST_F(EditorProjectPackageTest, Validate_RejectsInvalidWindowAndCursorPolicies) {
    EditorProjectPackage package = MakePackage();
    package.applicationSettings.windowWidth = 0U;
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.minimumWindowWidth = 3000U;
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.cursorImagePath = "../cursor.png";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPath);

    package = MakePackage();
    package.applicationSettings.orientation = DisplayOrientation::LandscapeRight;
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.focusedFrameRateCap = 1001U;
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);

    package = MakePackage();
    package.applicationSettings.contentScaleOverride = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code,
              EditorProjectPackageCode::InvalidPackage);
}

TEST_F(EditorProjectPackageTest, Save_RejectsWrongExtensionWithoutCreatingFile) {
    EditorProjectPackage package = MakePackage();
    std::filesystem::path wrongPath = packagePath;
    wrongPath.replace_extension(".json");

    const EditorProjectPackageResult result =
        EditorProjectPackageCodec::Save(wrongPath, package);

    EXPECT_EQ(result.code, EditorProjectPackageCode::InvalidPath);
    EXPECT_FALSE(std::filesystem::exists(wrongPath));
}

TEST_F(EditorProjectPackageTest, Load_MalformedOrWrongFormatFailsClosed) {
    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << "{\"format\":\"other\"}\n";
    }
    EXPECT_EQ(EditorProjectPackageCodec::Load(packagePath).result.code,
              EditorProjectPackageCode::ParseFailed);

    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << "not-json\n";
    }
    EXPECT_EQ(EditorProjectPackageCodec::Load(packagePath).result.code,
              EditorProjectPackageCode::ParseFailed);
}

TEST_F(EditorProjectPackageTest, ApplyUpdate_RequiresExpectedRevisionAndMatchingIdentity) {
    const EditorProjectPackage initial = MakePackage();
    ASSERT_TRUE(EditorProjectPackageCodec::Save(packagePath, initial).IsAccepted());

    EditorProjectPackage replacement = initial;
    replacement.revision = 5U;
    replacement.displayName = "Updated Demo";

    const EditorProjectPackageResult staleResult =
        EditorProjectPackageCodec::ApplyUpdate(packagePath, 3U, replacement);
    EXPECT_EQ(staleResult.code, EditorProjectPackageCode::RevisionConflict);
    ASSERT_TRUE(EditorProjectPackageCodec::Load(packagePath).package.has_value());
    EXPECT_EQ(EditorProjectPackageCodec::Load(packagePath).package->revision, 4U);

    const EditorProjectPackageResult updateResult =
        EditorProjectPackageCodec::ApplyUpdate(packagePath, 4U, replacement);
    ASSERT_TRUE(updateResult.IsAccepted()) << updateResult.message;
    EXPECT_EQ(EditorProjectPackageCodec::Load(packagePath).package->displayName, "Updated Demo");

    EditorProjectPackage differentProject = replacement;
    differentProject.revision = 6U;
    differentProject.projectId = "other-project";
    const EditorProjectPackageResult identityResult =
        EditorProjectPackageCodec::ApplyUpdate(packagePath, 5U, differentProject);
    EXPECT_EQ(identityResult.code, EditorProjectPackageCode::ProjectIdentityConflict);
    EXPECT_EQ(EditorProjectPackageCodec::Load(packagePath).package->revision, 5U);
}

TEST_F(EditorProjectPackageTest, ApplyUpdate_RejectsNonNewerReplacementWithoutMutation) {
    const EditorProjectPackage initial = MakePackage();
    ASSERT_TRUE(EditorProjectPackageCodec::Save(packagePath, initial).IsAccepted());

    EditorProjectPackage replacement = initial;
    replacement.displayName = "Stale";
    const EditorProjectPackageResult result =
        EditorProjectPackageCodec::ApplyUpdate(packagePath, initial.revision, replacement);

    EXPECT_EQ(result.code, EditorProjectPackageCode::RevisionConflict);
    const auto loaded = EditorProjectPackageCodec::Load(packagePath);
    ASSERT_TRUE(loaded.IsAccepted());
    EXPECT_EQ(loaded.package->displayName, initial.displayName);
}

TEST_F(EditorProjectPackageTest, SaveAndLoad_RoundTripsStartupScenePath) {
    EditorProjectPackage expected = MakePackage();
    expected.startupScenePath = "scenes/main.uvscene";

    ASSERT_TRUE(EditorProjectPackageCodec::Save(packagePath, expected).IsAccepted());
    const EditorProjectPackageLoadResult loadResult = EditorProjectPackageCodec::Load(packagePath);

    ASSERT_TRUE(loadResult.IsAccepted()) << loadResult.result.message;
    EXPECT_EQ(loadResult.package->startupScenePath, "scenes/main.uvscene");
}

TEST_F(EditorProjectPackageTest, Load_DefaultsMissingStartupScenePathToEmptyForOlderFiles) {
    // Simulates an older .uvproject file written before productMetadata and startupScenePath
    // existed - neither key is present, not merely set to an empty value.
    const nlohmann::json legacyJson{{"format", "uvproject"},
                                    {"schemaVersion", kCurrentEditorProjectSchemaVersion},
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

    const EditorProjectPackageLoadResult loadResult = EditorProjectPackageCodec::Load(packagePath);

    ASSERT_TRUE(loadResult.IsAccepted()) << loadResult.result.message;
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

TEST_F(EditorProjectPackageTest, Validate_RejectsTraversalStartupScenePathButAllowsEmpty) {
    EditorProjectPackage package = MakePackage();
    EXPECT_TRUE(EditorProjectPackageCodec::Validate(package).IsAccepted());

    package.startupScenePath = "../outside.uvscene";
    EXPECT_EQ(EditorProjectPackageCodec::Validate(package).code, EditorProjectPackageCode::InvalidPath);
}

TEST_F(EditorProjectPackageTest, SaveAndLoad_RoundTripsAllRendererProfiles) {
    for (const RendererProfile profile :
         {RendererProfile::Compatibility, RendererProfile::Mobile, RendererProfile::ForwardPlus}) {
        EditorProjectPackage expected = MakePackage();
        expected.rendererProfile = profile;

        ASSERT_TRUE(EditorProjectPackageCodec::Save(packagePath, expected).IsAccepted());
        const EditorProjectPackageLoadResult loadResult = EditorProjectPackageCodec::Load(packagePath);

        ASSERT_TRUE(loadResult.IsAccepted()) << loadResult.result.message;
        EXPECT_EQ(loadResult.package->rendererProfile, profile);
        EXPECT_EQ(ParseRendererProfile(GetRendererProfileName(profile)), profile);
    }
}

TEST_F(EditorProjectPackageTest, Load_DefaultsMissingRendererProfileAndRejectsUnknownProfile) {
    const nlohmann::json legacyJson{{"format", "uvproject"},
                                    {"schemaVersion", kCurrentEditorProjectSchemaVersion},
                                    {"revision", 1U},
                                    {"projectId", "legacy-renderer-default"},
                                    {"displayName", "Legacy"},
                                    {"engineVersion", {{"major", 0U}, {"minor", 1U}, {"patch", 0U}, {"build", 1U}}},
                                    {"contentRoot", "assets"},
                                    {"assetDatabasePath", ".uvassetdb"},
                                    {"settingsPath", ".uvsettings"}};
    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << legacyJson.dump();
    }
    const EditorProjectPackageLoadResult legacyLoad = EditorProjectPackageCodec::Load(packagePath);
    ASSERT_TRUE(legacyLoad.IsAccepted()) << legacyLoad.result.message;
    EXPECT_EQ(legacyLoad.package->rendererProfile, DefaultRendererProfileForHost());

    nlohmann::json invalidJson = legacyJson;
    invalidJson["rendererProfile"] = "ultra_raytrace";
    {
        std::ofstream output(packagePath, std::ios::binary | std::ios::trunc);
        output << invalidJson.dump();
    }
    EXPECT_EQ(EditorProjectPackageCodec::Load(packagePath).result.code,
              EditorProjectPackageCode::ParseFailed);
}

} // namespace
} // namespace UVE::Platform::Tests
