// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/pack/project_launcher_uve.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/core/engine_core_uve.h"
#include "uve/platform/application_runtime_uve.h"
#include "uve/platform/editor_project_package_uve.h"
#include "uve/component/camera_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/scene_serializer_uve.h"

namespace UVE::Pack::Tests {
namespace {

[[nodiscard]] Core::EngineConfigUVE MakeHeadlessConfigUVE(const std::filesystem::path& scratchDirectory) {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.enableConsoleLogging = false;
    config.logFilePath = scratchDirectory / "uve_project_launcher_tests.log";
    config.settingsFilePath = scratchDirectory / "uve_project_launcher_tests.uvsettings";
    config.assetDatabaseFilePath = scratchDirectory / "uve_project_launcher_tests.uvassetdb";
    config.threadPoolWorkerCount = 2;
    return config;
}

void WriteSample64PxPngIconUVE(const std::filesystem::path& path) {
    static constexpr std::array<unsigned char, 158U> kPngBytes{
        0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
        0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x08, 0x06, 0x00, 0x00, 0x00, 0xAA, 0x69, 0x71,
        0xDE, 0x00, 0x00, 0x00, 0x65, 0x49, 0x44, 0x41, 0x54, 0x78, 0xDA, 0xED, 0xD0, 0x41, 0x11,
        0x00, 0x00, 0x04, 0x00, 0x30, 0x51, 0x44, 0x15, 0x55, 0x13, 0x72, 0x38, 0x7B, 0xAC, 0xC0, 0x22,
        0xAB, 0xE7, 0xB3, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
        0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20,
        0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08,
        0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02,
        0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0xEE, 0x5B, 0x88, 0x1F, 0xF2, 0x4A, 0x36,
        0x5D, 0x80, 0x2C, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(kPngBytes.data()), static_cast<std::streamsize>(kPngBytes.size()));
}

class ProjectLauncherUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        projectRoot = ::UVE::Tests::MakeTestCaseDirectoryUVE("root");
        std::filesystem::create_directories(projectRoot / "content" / "scenes");
    }

    void TearDown() override { std::filesystem::remove_all(projectRoot); }

    // Authors one entity with a real camera + world transform into a fresh in-memory entity
    // manager, saves it as the project's own scene file, then writes a matching .uvproject package
    // naming it as the startup scene - the same shape ProjectPackagerUVE expects a real authored
    // project to already have on disk.
    void WriteProjectWithCameraSceneUVE(Core::EngineCoreUVE& authoringEngine) {
        Core::EngineServicesUVE& services = authoringEngine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
        services.GetSceneGraphUVE().AttachTransformUVE(entityManager, camera, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera);

        Scene::SceneSerializerUVE serializer;
        ASSERT_TRUE(serializer.SaveUVE(entityManager, {camera}, projectRoot / "content" / "scenes" / "main.uvscene",
                                       Scene::SceneAssetTypeUVE::Scene));

        Platform::EditorProjectPackageUVE package;
        package.revision = 1U;
        package.projectId = "launcher-test-project";
        package.displayName = "Launcher Test Project";
        package.productMetadata.name = "Launcher Test Product";
        package.productMetadata.shortName = "launcher-test-product";
        package.applicationSettings.publisherName = "UniVex Test Studio";
        package.applicationSettings.copyrightLine = "Copyright (c) UniVex Test Studio.";
        package.engineVersion = {0U, 1U, 0U, 1U};
        package.contentRoot = "content";
        package.assetDatabasePath = ".uvassetdb";
        package.settingsPath = ".uvsettings";
        package.startupScenePath = "scenes/main.uvscene";
        std::filesystem::create_directories(projectRoot / "content" / "branding");
        {
            std::ofstream splash(projectRoot / "content" / "branding" / "splash.uvtex", std::ios::binary);
            splash << "placeholder splash asset";
        }
        package.applicationSettings.splashImagePath = "branding/splash.uvtex";
        std::filesystem::create_directories(projectRoot / "content" / "icons");
        WriteSample64PxPngIconUVE(projectRoot / "content" / "icons" / "icon-64.png");
        package.applicationSettings.iconPathsByTarget[std::string{Platform::GetApplicationTargetUVE()}] = {
            {64U, "icons/icon-64.png"}};
        package.applicationSettings.applicationIdentifiersByTarget[
            std::string{Platform::GetApplicationTargetUVE()}] = "com.example.launcher";
        package.applicationSettings.userDataDirectoryName = "runtime-data";
        package.applicationSettings.portableUserData = true;
        package.applicationSettings.quitOnLastWindowClosed = false;
        package.applicationSettings.enforceSingleInstance = true;
        package.applicationSettings.windowWidth = 1600U;
        package.applicationSettings.windowHeight = 900U;
        package.applicationSettings.windowMode = Platform::WindowModeUVE::Maximized;
        package.applicationSettings.windowResizable = false;
        package.applicationSettings.windowBorderless = true;
        package.applicationSettings.windowAlwaysOnTop = true;
        package.applicationSettings.windowTransparent = true;
        package.applicationSettings.minimumWindowWidth = 640U;
        package.applicationSettings.minimumWindowHeight = 360U;
        package.applicationSettings.maximumWindowWidth = 1920U;
        package.applicationSettings.maximumWindowHeight = 1080U;
        package.applicationSettings.initialWindowPositionSpecified = true;
        package.applicationSettings.initialWindowPositionX = 120;
        package.applicationSettings.initialWindowPositionY = 80;
        package.applicationSettings.initialMonitorName = "Test Monitor";
        package.applicationSettings.highDpiAware = false;
        package.applicationSettings.perMonitorScaling = false;
        package.applicationSettings.contentScaleOverride = 1.25;
        package.applicationSettings.stretchMode = Platform::StretchModeUVE::Viewport;
        package.applicationSettings.aspectPolicy = Platform::AspectPolicyUVE::Keep;
        package.applicationSettings.integerOnlyScaling = true;
        package.applicationSettings.orientation = Platform::DisplayOrientationUVE::LandscapeRight;
        package.applicationSettings.allowedOrientations = {Platform::DisplayOrientationUVE::LandscapeRight,
                                                            Platform::DisplayOrientationUVE::Portrait};
        package.applicationSettings.vsyncMode = Platform::VSyncModeUVE::Adaptive;
        package.applicationSettings.focusedFrameRateCap = 144U;
        package.applicationSettings.unfocusedFrameRateCap = 30U;
        package.applicationSettings.allowDisplaySleep = false;
        std::filesystem::create_directories(projectRoot / "content" / "ui");
        WriteSample64PxPngIconUVE(projectRoot / "content" / "ui" / "cursor.png");
        package.applicationSettings.cursorImagePath = "ui/cursor.png";
        package.applicationSettings.cursorHotspotX = 5U;
        package.applicationSettings.cursorHotspotY = 6U;
        package.applicationSettings.cursorVisible = false;
        package.applicationSettings.cursorConfinedToWindow = true;
        package.applicationSettings.windowTitleFormat = "{projectName} - {sceneName}";
        package.applicationSettings.appendSceneNameInEditorPlayMode = true;
        ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());
    }

    [[nodiscard]] std::filesystem::path projectFile() const { return projectRoot / "project.uvproject"; }

    std::filesystem::path projectRoot;
};

TEST_F(ProjectLauncherUVETest, LoadAndActivate_LoadsSceneAndActivatesItsCamera) {
    Core::EngineCoreUVE authoringEngine(MakeHeadlessConfigUVE(projectRoot));
    authoringEngine.Init();
    ASSERT_TRUE(authoringEngine.Load());
    WriteProjectWithCameraSceneUVE(authoringEngine);
    authoringEngine.Shutdown();

    Core::EngineCoreUVE engine(MakeHeadlessConfigUVE(projectRoot));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const ProjectLaunchResultUVE result = LoadAndActivateProjectSceneUVE(engine, projectFile());

    EXPECT_TRUE(result.IsAcceptedUVE()) << result.message;
    const Scene::EntityUVE activeCamera = engine.GetActiveCameraUVE();
    EXPECT_NE(activeCamera, Scene::kInvalidEntityUVE);
    EXPECT_TRUE(engine.GetServicesUVE().GetEntityManagerUVE().HasComponentUVE<Scene::CameraComponentUVE>(activeCamera));
    engine.Shutdown();
}

TEST_F(ProjectLauncherUVETest, PrepareRuntime_ResolvesApplicationPolicyBeforeEngineInit) {
    Core::EngineCoreUVE authoringEngine(MakeHeadlessConfigUVE(projectRoot));
    authoringEngine.Init();
    ASSERT_TRUE(authoringEngine.Load());
    WriteProjectWithCameraSceneUVE(authoringEngine);
    authoringEngine.Shutdown();

    Core::EngineConfigUVE config = MakeHeadlessConfigUVE(projectRoot);
    const ProjectRuntimeSetupResultUVE setup = PrepareProjectRuntimeUVE(projectFile(), config);

    ASSERT_TRUE(setup.IsReadyUVE()) << setup.message;
    ASSERT_TRUE(setup.setup.has_value());
    const std::filesystem::path expectedUserData = std::filesystem::absolute(projectRoot) / "runtime-data";
    EXPECT_EQ(setup.setup->applicationIdentifier, "com.example.launcher");
    EXPECT_EQ(setup.setup->projectRoot, std::filesystem::absolute(projectRoot));
    EXPECT_EQ(setup.setup->contentRoot, std::filesystem::absolute(projectRoot) / "content");
    EXPECT_EQ(setup.setup->userDataDirectory, expectedUserData);
    EXPECT_EQ(config.userDataDirectoryPathUVE, expectedUserData);
    EXPECT_EQ(config.windowTitle, "Launcher Test Product");
    EXPECT_EQ(config.projectDisplayNameUVE, "Launcher Test Project");
    EXPECT_EQ(config.windowWidth, 1600U);
    EXPECT_EQ(config.windowHeight, 900U);
    EXPECT_EQ(config.windowModeUVE, Platform::WindowModeUVE::Maximized);
    EXPECT_FALSE(config.windowResizableUVE);
    EXPECT_TRUE(config.windowBorderlessUVE);
    EXPECT_TRUE(config.windowAlwaysOnTopUVE);
    EXPECT_TRUE(config.windowTransparentUVE);
    EXPECT_EQ(config.windowMinimumWidthUVE, 640U);
    EXPECT_EQ(config.windowMinimumHeightUVE, 360U);
    EXPECT_EQ(config.windowMaximumWidthUVE, 1920U);
    EXPECT_EQ(config.windowMaximumHeightUVE, 1080U);
    EXPECT_TRUE(config.windowPositionSpecifiedUVE);
    EXPECT_EQ(config.windowPositionXUVE, 120);
    EXPECT_EQ(config.windowPositionYUVE, 80);
    EXPECT_EQ(config.windowMonitorNameUVE, "Test Monitor");
    EXPECT_FALSE(config.highDpiAwareUVE);
    EXPECT_FALSE(config.perMonitorScalingUVE);
    EXPECT_DOUBLE_EQ(config.contentScaleOverrideUVE, 1.25);
    EXPECT_EQ(config.stretchModeUVE, Platform::StretchModeUVE::Viewport);
    EXPECT_EQ(config.aspectPolicyUVE, Platform::AspectPolicyUVE::Keep);
    EXPECT_TRUE(config.integerOnlyScalingUVE);
    EXPECT_EQ(config.orientationUVE, Platform::DisplayOrientationUVE::LandscapeRight);
    EXPECT_EQ(config.allowedOrientationsUVE,
              (std::vector<Platform::DisplayOrientationUVE>{Platform::DisplayOrientationUVE::LandscapeRight,
                                                            Platform::DisplayOrientationUVE::Portrait}));
    EXPECT_EQ(config.vsyncModeUVE, Platform::VSyncModeUVE::Adaptive);
    EXPECT_TRUE(config.vsyncModeExplicitUVE);
    EXPECT_EQ(config.focusedFrameRateCapUVE, 144U);
    EXPECT_EQ(config.unfocusedFrameRateCapUVE, 30U);
    EXPECT_FALSE(config.allowDisplaySleepUVE);
    EXPECT_EQ(config.cursorImagePathUVE,
              std::filesystem::canonical(std::filesystem::absolute(projectRoot) / "content" / "ui" / "cursor.png"));
    EXPECT_EQ(config.cursorHotspotXUVE, 5U);
    EXPECT_EQ(config.cursorHotspotYUVE, 6U);
    EXPECT_FALSE(config.cursorVisibleUVE);
    EXPECT_TRUE(config.cursorConfinedToWindowUVE);
    EXPECT_EQ(config.windowTitleFormatUVE, "{projectName} - {sceneName}");
    EXPECT_TRUE(config.appendSceneNameInEditorPlayModeUVE);
    EXPECT_EQ(config.bootSplashImagePathUVE,
              std::filesystem::canonical(std::filesystem::absolute(projectRoot) / "content" / "branding" /
                                          "splash.uvtex"));
    ASSERT_EQ(config.windowIconsUVE.size(), 1U);
    EXPECT_EQ(config.windowIconsUVE[0].width, 64U);
    EXPECT_EQ(config.windowIconsUVE[0].height, 64U);
    ASSERT_EQ(config.windowIconsUVE[0].rgba8.size(), 64U * 64U * 4U);
    EXPECT_EQ(config.windowIconsUVE[0].rgba8[0], 0x20U);
    EXPECT_EQ(config.windowIconsUVE[0].rgba8[1], 0x80U);
    EXPECT_EQ(config.windowIconsUVE[0].rgba8[2], 0xE0U);
    EXPECT_EQ(config.windowIconsUVE[0].rgba8[3], 0xFFU);
    EXPECT_FALSE(config.quitOnLastWindowClosedUVE);
    EXPECT_TRUE(config.enforceSingleInstanceUVE);
    EXPECT_EQ(config.settingsFilePath, expectedUserData / ".uvsettings");
    EXPECT_EQ(config.projectSettingsFilePath, std::filesystem::absolute(projectRoot) / ".uvsettings");
    EXPECT_EQ(config.projectContentRootUVE, std::filesystem::absolute(projectRoot) / "content");

    Core::EngineCoreUVE engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const ProjectLaunchResultUVE launch = LoadAndActivateProjectSceneUVE(engine, *setup.setup);
    EXPECT_TRUE(launch.IsAcceptedUVE()) << launch.message;
    EXPECT_NE(engine.GetActiveCameraUVE(), Scene::kInvalidEntityUVE);
    engine.Shutdown();
}

TEST_F(ProjectLauncherUVETest, LoadAndActivate_RejectsProjectWithNoStartupSceneConfigured) {
    Platform::EditorProjectPackageUVE package;
    package.revision = 1U;
    package.projectId = "no-startup-scene";
    package.displayName = "No Startup Scene";
    package.engineVersion = {0U, 1U, 0U, 1U};
    package.contentRoot = "content";
    package.assetDatabasePath = ".uvassetdb";
    package.settingsPath = ".uvsettings";
    ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());

    Core::EngineCoreUVE engine(MakeHeadlessConfigUVE(projectRoot));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const ProjectLaunchResultUVE result = LoadAndActivateProjectSceneUVE(engine, projectFile());

    EXPECT_FALSE(result.IsAcceptedUVE());
    EXPECT_EQ(result.code, ProjectLaunchCodeUVE::NoStartupSceneConfigured);
    EXPECT_EQ(engine.GetActiveCameraUVE(), Scene::kInvalidEntityUVE);
    engine.Shutdown();
}

TEST_F(ProjectLauncherUVETest, LoadAndActivate_RejectsMissingProjectFile) {
    Core::EngineCoreUVE engine(MakeHeadlessConfigUVE(projectRoot));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const ProjectLaunchResultUVE result =
        LoadAndActivateProjectSceneUVE(engine, projectRoot / "does_not_exist.uvproject");

    EXPECT_FALSE(result.IsAcceptedUVE());
    EXPECT_EQ(result.code, ProjectLaunchCodeUVE::InvalidProjectFile);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Pack::Tests
