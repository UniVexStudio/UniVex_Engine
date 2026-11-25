// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "project_launcher.h"

#include <array>
#include <thread>
#include "asset_cooker.h"
#include "asset_database.h"
#include "asset_handle.h"
#include "texture_asset.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "engine_core.h"
#include "application_runtime.h"
#include "editor_project_package.h"
#include "camera_component.h"
#include "transform_component.h"
#include "i_entity_manager.h"
#include "i_scene_graph.h"
#include "scene_serializer.h"

namespace UVE::Pack::Tests {
namespace {

[[nodiscard]] Core::EngineConfig MakeHeadlessConfig(const std::filesystem::path& scratchDirectory) {
    Core::EngineConfig config{};
    config.headless = true;
    config.enableConsoleLogging = false;
    config.logFilePath = scratchDirectory / "uve_project_launcher_tests.log";
    config.settingsFilePath = scratchDirectory / "uve_project_launcher_tests.uvsettings";
    config.assetDatabaseFilePath = scratchDirectory / "uve_project_launcher_tests.uvassetdb";
    config.threadPoolWorkerCount = 2;
    return config;
}

void WriteSample64PxPngIcon(const std::filesystem::path& path) {
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

class ProjectLauncherTest : public ::testing::Test {
protected:
    void SetUp() override {
        projectRoot = ::UVE::Tests::MakeTestCaseDirectory("root");
        std::filesystem::create_directories(projectRoot / "content" / "scenes");
    }

    void TearDown() override { std::filesystem::remove_all(projectRoot); }

    // Authors one entity with a real camera + world transform into a fresh in-memory entity
    // manager, saves it as the project's own scene file, then writes a matching .uvproject package
    // naming it as the startup scene - the same shape ProjectPackager expects a real authored
    // project to already have on disk.
    void WriteProjectWithCameraScene(Core::EngineCore& authoringEngine) {
        Core::EngineServices& services = authoringEngine.GetServices();
        Scene::IEntityManager& entityManager = services.GetEntityManager();
        const Scene::Entity camera = entityManager.CreateEntity();
        services.GetSceneGraph().AttachTransform(entityManager, camera, Scene::TransformComponent{});
        entityManager.AddComponent<Scene::CameraComponent>(camera);

        Scene::SceneSerializer serializer;
        ASSERT_TRUE(serializer.Save(entityManager, {camera}, projectRoot / "content" / "scenes" / "main.uvscene",
                                       Scene::SceneAssetType::Scene));

        Platform::EditorProjectPackage package;
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
        WriteSample64PxPngIcon(projectRoot / "content" / "icons" / "icon-64.png");
        package.applicationSettings.iconPathsByTarget[std::string{Platform::GetApplicationTarget()}] = {
            {64U, "icons/icon-64.png"}};
        package.applicationSettings.applicationIdentifiersByTarget[
            std::string{Platform::GetApplicationTarget()}] = "com.example.launcher";
        package.applicationSettings.userDataDirectoryName = "runtime-data";
        package.applicationSettings.portableUserData = true;
        package.applicationSettings.quitOnLastWindowClosed = false;
        package.applicationSettings.enforceSingleInstance = true;
        package.applicationSettings.windowWidth = 1600U;
        package.applicationSettings.windowHeight = 900U;
        package.applicationSettings.windowMode = Platform::WindowMode::Maximized;
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
        package.applicationSettings.stretchMode = Platform::StretchMode::Viewport;
        package.applicationSettings.aspectPolicy = Platform::AspectPolicy::Keep;
        package.applicationSettings.integerOnlyScaling = true;
        package.applicationSettings.orientation = Platform::DisplayOrientation::LandscapeRight;
        package.applicationSettings.allowedOrientations = {Platform::DisplayOrientation::LandscapeRight,
                                                            Platform::DisplayOrientation::Portrait};
        package.applicationSettings.vsyncMode = Platform::VSyncMode::Adaptive;
        package.applicationSettings.focusedFrameRateCap = 144U;
        package.applicationSettings.unfocusedFrameRateCap = 30U;
        package.applicationSettings.allowDisplaySleep = false;
        std::filesystem::create_directories(projectRoot / "content" / "ui");
        WriteSample64PxPngIcon(projectRoot / "content" / "ui" / "cursor.png");
        package.applicationSettings.cursorImagePath = "ui/cursor.png";
        package.applicationSettings.cursorHotspotX = 5U;
        package.applicationSettings.cursorHotspotY = 6U;
        package.applicationSettings.cursorVisible = false;
        package.applicationSettings.cursorConfinedToWindow = true;
        package.applicationSettings.windowTitleFormat = "{projectName} - {sceneName}";
        package.applicationSettings.appendSceneNameInEditorPlayMode = true;
        ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());
    }

    [[nodiscard]] std::filesystem::path projectFile() const { return projectRoot / "project.uvproject"; }

    std::filesystem::path projectRoot;
};

TEST_F(ProjectLauncherTest, CookedProjectLaunchesAndDemandLoadsAfterOriginalProjectRemoved) {
    Core::EngineCore author(MakeHeadlessConfig(projectRoot)); author.Init();
    ASSERT_TRUE(author.Load()); WriteProjectWithCameraScene(author); author.Shutdown();
    Asset::TextureAsset texture; texture.width=4;texture.height=4;texture.pixels.resize(64,std::byte{255});
    ASSERT_TRUE(Asset::SaveTextureAsset(texture,projectRoot/"content/hero.uvtex"));
    ASSERT_TRUE(Asset::SaveTextureAsset(texture,projectRoot/"content/branding/splash.uvtex"));
    Asset::AssetDatabase db; const auto guid=db.Register("content/hero.uvtex");
    ASSERT_TRUE(db.Save(projectRoot/".uvassetdb"));
    const auto cooked=::UVE::Tests::ScratchPath("relocated-cooked");
    const auto result=CookProjectAssets(projectFile(),cooked,CookTarget::Android);
    ASSERT_TRUE(result.success)<<result.message;
    std::filesystem::remove_all(projectRoot);
    auto config=MakeHeadlessConfig(cooked);
    const auto persistent=::UVE::Tests::ScratchPath("native-persistent-data");
    const auto setup=PrepareProjectRuntime(cooked/"project.uvproject",config,persistent);
    ASSERT_TRUE(setup.IsReady())<<setup.message;
    EXPECT_EQ(config.saveDirectoryPath,persistent/"saves");
    EXPECT_TRUE(config.cookedAssetPaths); EXPECT_EQ(config.assetResidentBudgetBytes,128U*1024U*1024U);
    Core::EngineCore runtime(config);runtime.Init();ASSERT_TRUE(runtime.Load());
    ASSERT_TRUE(LoadAndActivateProjectScene(runtime,*setup.setup).IsAccepted());
    auto& services=runtime.GetServices();auto& manager=services.GetAssetManager();
    EXPECT_EQ(manager.GetLoadedAssetCount(),0U);
    {
        auto h=manager.Load<Asset::TextureAsset>(guid,services.GetAssetDatabase());
        for(unsigned i=0;i<200000U&&!h.IsReady()&&!h.HasFailed();++i) std::this_thread::yield();
        ASSERT_TRUE(h.IsReady())<<h.GetFailureReason();EXPECT_EQ(h.TryGet()->width,4U);
    }
    manager.CollectGarbage();EXPECT_EQ(manager.GetLoadedAssetCount(),0U);
    runtime.Shutdown();std::filesystem::remove_all(cooked);
    EXPECT_TRUE(std::filesystem::is_directory(persistent));std::filesystem::remove_all(persistent);
}

TEST_F(ProjectLauncherTest, LoadAndActivate_LoadsSceneAndActivatesItsCamera) {
    Core::EngineCore authoringEngine(MakeHeadlessConfig(projectRoot));
    authoringEngine.Init();
    ASSERT_TRUE(authoringEngine.Load());
    WriteProjectWithCameraScene(authoringEngine);
    authoringEngine.Shutdown();

    Core::EngineCore engine(MakeHeadlessConfig(projectRoot));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const ProjectLaunchResult result = LoadAndActivateProjectScene(engine, projectFile());

    EXPECT_TRUE(result.IsAccepted()) << result.message;
    const Scene::Entity activeCamera = engine.GetActiveCamera();
    EXPECT_NE(activeCamera, Scene::kInvalidEntity);
    EXPECT_TRUE(engine.GetServices().GetEntityManager().HasComponent<Scene::CameraComponent>(activeCamera));
    engine.Shutdown();
}

TEST_F(ProjectLauncherTest, PrepareRuntime_ResolvesApplicationPolicyBeforeEngineInit) {
    Core::EngineCore authoringEngine(MakeHeadlessConfig(projectRoot));
    authoringEngine.Init();
    ASSERT_TRUE(authoringEngine.Load());
    WriteProjectWithCameraScene(authoringEngine);
    authoringEngine.Shutdown();

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const ProjectRuntimeSetupResult setup = PrepareProjectRuntime(projectFile(), config);

    ASSERT_TRUE(setup.IsReady()) << setup.message;
    ASSERT_TRUE(setup.setup.has_value());
    const std::filesystem::path expectedUserData = std::filesystem::absolute(projectRoot) / "runtime-data";
    EXPECT_EQ(setup.setup->applicationIdentifier, "com.example.launcher");
    EXPECT_EQ(setup.setup->projectRoot, std::filesystem::absolute(projectRoot));
    EXPECT_EQ(setup.setup->contentRoot, std::filesystem::absolute(projectRoot) / "content");
    EXPECT_EQ(setup.setup->userDataDirectory, expectedUserData);
    EXPECT_EQ(config.userDataDirectoryPath, expectedUserData);
    EXPECT_EQ(config.windowTitle, "Launcher Test Product");
    EXPECT_EQ(config.projectDisplayName, "Launcher Test Project");
    EXPECT_EQ(config.windowWidth, 1600U);
    EXPECT_EQ(config.windowHeight, 900U);
    EXPECT_EQ(config.windowMode, Platform::WindowMode::Maximized);
    EXPECT_FALSE(config.windowResizable);
    EXPECT_TRUE(config.windowBorderless);
    EXPECT_TRUE(config.windowAlwaysOnTop);
    EXPECT_TRUE(config.windowTransparent);
    EXPECT_EQ(config.windowMinimumWidth, 640U);
    EXPECT_EQ(config.windowMinimumHeight, 360U);
    EXPECT_EQ(config.windowMaximumWidth, 1920U);
    EXPECT_EQ(config.windowMaximumHeight, 1080U);
    EXPECT_TRUE(config.windowPositionSpecified);
    EXPECT_EQ(config.windowPositionX, 120);
    EXPECT_EQ(config.windowPositionY, 80);
    EXPECT_EQ(config.windowMonitorName, "Test Monitor");
    EXPECT_FALSE(config.highDpiAware);
    EXPECT_FALSE(config.perMonitorScaling);
    EXPECT_DOUBLE_EQ(config.contentScaleOverride, 1.25);
    EXPECT_EQ(config.stretchMode, Platform::StretchMode::Viewport);
    EXPECT_EQ(config.aspectPolicy, Platform::AspectPolicy::Keep);
    EXPECT_TRUE(config.integerOnlyScaling);
    EXPECT_EQ(config.orientation, Platform::DisplayOrientation::LandscapeRight);
    EXPECT_EQ(config.allowedOrientations,
              (std::vector<Platform::DisplayOrientation>{Platform::DisplayOrientation::LandscapeRight,
                                                            Platform::DisplayOrientation::Portrait}));
    EXPECT_EQ(config.vsyncMode, Platform::VSyncMode::Adaptive);
    EXPECT_TRUE(config.vsyncModeExplicit);
    EXPECT_EQ(config.focusedFrameRateCap, 144U);
    EXPECT_EQ(config.unfocusedFrameRateCap, 30U);
    EXPECT_FALSE(config.allowDisplaySleep);
    EXPECT_EQ(config.cursorImagePath,
              std::filesystem::canonical(std::filesystem::absolute(projectRoot) / "content" / "ui" / "cursor.png"));
    EXPECT_EQ(config.cursorHotspotX, 5U);
    EXPECT_EQ(config.cursorHotspotY, 6U);
    EXPECT_FALSE(config.cursorVisible);
    EXPECT_TRUE(config.cursorConfinedToWindow);
    EXPECT_EQ(config.windowTitleFormat, "{projectName} - {sceneName}");
    EXPECT_TRUE(config.appendSceneNameInEditorPlayMode);
    EXPECT_EQ(config.bootSplashImagePath,
              std::filesystem::canonical(std::filesystem::absolute(projectRoot) / "content" / "branding" /
                                          "splash.uvtex"));
    ASSERT_EQ(config.windowIcons.size(), 1U);
    EXPECT_EQ(config.windowIcons[0].width, 64U);
    EXPECT_EQ(config.windowIcons[0].height, 64U);
    ASSERT_EQ(config.windowIcons[0].rgba8.size(), 64U * 64U * 4U);
    EXPECT_EQ(config.windowIcons[0].rgba8[0], 0x20U);
    EXPECT_EQ(config.windowIcons[0].rgba8[1], 0x80U);
    EXPECT_EQ(config.windowIcons[0].rgba8[2], 0xE0U);
    EXPECT_EQ(config.windowIcons[0].rgba8[3], 0xFFU);
    EXPECT_FALSE(config.quitOnLastWindowClosed);
    EXPECT_TRUE(config.enforceSingleInstance);
    EXPECT_EQ(config.settingsFilePath, expectedUserData / ".uvsettings");
    EXPECT_EQ(config.projectSettingsFilePath, std::filesystem::absolute(projectRoot) / ".uvsettings");
    EXPECT_EQ(config.projectContentRoot, std::filesystem::absolute(projectRoot) / "content");

    Core::EngineCore engine(config);
    engine.Init();
    ASSERT_TRUE(engine.Load());
    const ProjectLaunchResult launch = LoadAndActivateProjectScene(engine, *setup.setup);
    EXPECT_TRUE(launch.IsAccepted()) << launch.message;
    EXPECT_NE(engine.GetActiveCamera(), Scene::kInvalidEntity);
    engine.Shutdown();
}

TEST_F(ProjectLauncherTest, LoadAndActivate_RejectsProjectWithNoStartupSceneConfigured) {
    Platform::EditorProjectPackage package;
    package.revision = 1U;
    package.projectId = "no-startup-scene";
    package.displayName = "No Startup Scene";
    package.engineVersion = {0U, 1U, 0U, 1U};
    package.contentRoot = "content";
    package.assetDatabasePath = ".uvassetdb";
    package.settingsPath = ".uvsettings";
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    Core::EngineCore engine(MakeHeadlessConfig(projectRoot));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const ProjectLaunchResult result = LoadAndActivateProjectScene(engine, projectFile());

    EXPECT_FALSE(result.IsAccepted());
    EXPECT_EQ(result.code, ProjectLaunchCode::NoStartupSceneConfigured);
    EXPECT_EQ(engine.GetActiveCamera(), Scene::kInvalidEntity);
    engine.Shutdown();
}

TEST_F(ProjectLauncherTest, LoadAndActivate_RejectsMissingProjectFile) {
    Core::EngineCore engine(MakeHeadlessConfig(projectRoot));
    engine.Init();
    ASSERT_TRUE(engine.Load());

    const ProjectLaunchResult result =
        LoadAndActivateProjectScene(engine, projectRoot / "does_not_exist.uvproject");

    EXPECT_FALSE(result.IsAccepted());
    EXPECT_EQ(result.code, ProjectLaunchCode::InvalidProjectFile);
    engine.Shutdown();
}

/// The engine version the authoring tests open projects with; MakeAuthoringPackage records it.
inline constexpr Core::Version kAuthoringEngineVersion{0U, 0U, 1U, 0U};

[[nodiscard]] Platform::EditorProjectPackage MakeAuthoringPackage() {
    Platform::EditorProjectPackage package;
    package.revision = 1U;
    package.projectId = "editor-authoring-project";
    package.displayName = "Editor Authoring Project";
    package.engineVersion = {kAuthoringEngineVersion.major, kAuthoringEngineVersion.minor,
                             kAuthoringEngineVersion.patch, kAuthoringEngineVersion.build};
    package.contentRoot = "content";
    package.assetDatabasePath = ".uvassetdb";
    package.settingsPath = "project.uvsettings";
    return package;
}

TEST_F(ProjectLauncherTest, PrepareEditor_PointsConfigAtProjectAuthoringData) {
    Platform::EditorProjectPackage package = MakeAuthoringPackage();
    package.startupScenePath = "scenes/not_saved_yet.uvscene";
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const std::filesystem::path userSettings = config.settingsFilePath;
    const std::uint32_t editorWindowWidth = config.windowWidth;
    const std::filesystem::path logFileName = config.logFilePath.filename();
    const ProjectEditorSetupResult result =
        PrepareProjectEditor(projectFile(), kAuthoringEngineVersion, config);

    ASSERT_TRUE(result.IsReady()) << result.message;
    const std::filesystem::path root = std::filesystem::canonical(projectRoot);
    EXPECT_EQ(result.setup->projectRoot, root);
    EXPECT_EQ(result.setup->contentRoot, root / "content");
    EXPECT_EQ(result.setup->startupScene, root / "content" / "scenes" / "not_saved_yet.uvscene");
    EXPECT_EQ(config.projectContentRoot, root / "content");
    EXPECT_EQ(config.projectRootDirectory, root);
    EXPECT_EQ(config.projectSettingsFilePath, root / "project.uvsettings");
    EXPECT_EQ(config.assetDatabaseFilePath, root / ".uvassetdb");
    EXPECT_EQ(config.inputMapFilePath.parent_path(), root);
    EXPECT_EQ(config.derivedArtifactCacheRoot.parent_path().parent_path().parent_path(), root);
    EXPECT_EQ(config.projectDisplayName, package.displayName);
    EXPECT_EQ(config.logFilePath,
              root / kProjectEditorLogDirectoryName / logFileName);
    EXPECT_TRUE(std::filesystem::is_directory(config.logFilePath.parent_path()));
    // The editor keeps its own per-user preferences and window.
    EXPECT_EQ(config.settingsFilePath, userSettings);
    EXPECT_EQ(config.windowWidth, editorWindowWidth);
}

TEST_F(ProjectLauncherTest, PrepareEditor_AcceptsProjectWithoutStartupScene) {
    ASSERT_TRUE(
        Platform::EditorProjectPackageCodec::Save(projectFile(), MakeAuthoringPackage()).IsAccepted());

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const ProjectEditorSetupResult result =
        PrepareProjectEditor(projectFile(), kAuthoringEngineVersion, config);

    ASSERT_TRUE(result.IsReady()) << result.message;
    EXPECT_TRUE(result.setup->startupScene.empty());
}

TEST_F(ProjectLauncherTest, PrepareEditor_RejectsStartupSceneOutsideContentRoot) {
    // The codec refuses to save such a manifest, so the escaping path is written into the file
    // directly, as a hand-edited or hostile project would carry it.
    constexpr std::string_view kInsideScene = "scenes/inside.uvscene";
    constexpr std::string_view kEscapingScene = "../outside.uvscene";
    Platform::EditorProjectPackage package = MakeAuthoringPackage();
    package.startupScenePath = kInsideScene;
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());
    std::string manifest;
    {
        std::ifstream input(projectFile());
        manifest.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    const std::size_t scenePosition = manifest.find(kInsideScene);
    ASSERT_NE(scenePosition, std::string::npos);
    manifest.replace(scenePosition, kInsideScene.size(), kEscapingScene);
    {
        std::ofstream output(projectFile(), std::ios::trunc);
        output << manifest;
    }

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const Core::EngineConfig before = config;
    const ProjectEditorSetupResult result =
        PrepareProjectEditor(projectFile(), kAuthoringEngineVersion, config);

    EXPECT_FALSE(result.IsReady());
    EXPECT_EQ(config.projectContentRoot, before.projectContentRoot);
    EXPECT_EQ(config.logFilePath, before.logFilePath);
}

TEST_F(ProjectLauncherTest, PrepareEditor_RejectsMissingContentRootWithoutTouchingConfig) {
    Platform::EditorProjectPackage package = MakeAuthoringPackage();
    package.contentRoot = "missing_content";
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const Core::EngineConfig before = config;
    const ProjectEditorSetupResult result =
        PrepareProjectEditor(projectFile(), kAuthoringEngineVersion, config);

    EXPECT_EQ(result.code, ProjectEditorSetupCode::ContentRootInvalid);
    EXPECT_EQ(config.projectContentRoot, before.projectContentRoot);
    EXPECT_EQ(config.assetDatabaseFilePath, before.assetDatabaseFilePath);
}

TEST_F(ProjectLauncherTest, PrepareEditor_RejectsMissingProjectFile) {
    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const ProjectEditorSetupResult result =
        PrepareProjectEditor(projectRoot / "does_not_exist.uvproject", kAuthoringEngineVersion, config);

    EXPECT_FALSE(result.IsReady());
    EXPECT_EQ(result.code, ProjectEditorSetupCode::InvalidProjectFile);
}

TEST_F(ProjectLauncherTest, RecordEngineVersion_UpdatesManifestThroughRevisionCheck) {
    ASSERT_TRUE(
        Platform::EditorProjectPackageCodec::Save(projectFile(), MakeAuthoringPackage()).IsAccepted());
    const Core::Version engineVersion{2U, 3U, 4U, 5U};

    ASSERT_TRUE(RecordProjectEngineVersion(projectFile(), engineVersion).IsAccepted());
    const Platform::EditorProjectPackageLoadResult updated =
        Platform::EditorProjectPackageCodec::Load(projectFile());
    ASSERT_TRUE(updated.IsAccepted());
    EXPECT_EQ(updated.package->engineVersion, (Platform::EditorProjectVersion{2U, 3U, 4U, 5U}));
    EXPECT_EQ(updated.package->revision, 2U);

    // Recording the same version again leaves the manifest and its revision untouched.
    ASSERT_TRUE(RecordProjectEngineVersion(projectFile(), engineVersion).IsAccepted());
    EXPECT_EQ(Platform::EditorProjectPackageCodec::Load(projectFile()).package->revision, 2U);
}

TEST_F(ProjectLauncherTest, RecordEngineVersion_ReportsMissingProjectFile) {
    EXPECT_FALSE(
        RecordProjectEngineVersion(projectRoot / "does_not_exist.uvproject", Core::Version{1U, 0U, 0U, 0U})
            .IsAccepted());
}

TEST_F(ProjectLauncherTest, PrepareEditor_RefusesProjectFromIncompatibleNewerEngine) {
    Platform::EditorProjectPackage package = MakeAuthoringPackage();
    package.engineVersion.major = kAuthoringEngineVersion.major + 1U;
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const Core::EngineConfig before = config;
    const ProjectEditorSetupResult result =
        PrepareProjectEditor(projectFile(), kAuthoringEngineVersion, config);

    EXPECT_EQ(result.code, ProjectEditorSetupCode::EngineTooOld);
    EXPECT_EQ(config.projectContentRoot, before.projectContentRoot);
    EXPECT_EQ(config.logFilePath, before.logFilePath);
}

TEST_F(ProjectLauncherTest, PrepareEditor_ReportsCompatibilityOfOlderProject) {
    Platform::EditorProjectPackage package = MakeAuthoringPackage();
    const Core::Version newerEngine{kAuthoringEngineVersion.major, kAuthoringEngineVersion.minor + 1U,
                                       kAuthoringEngineVersion.patch, kAuthoringEngineVersion.build};
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    const ProjectEditorSetupResult result = PrepareProjectEditor(projectFile(), newerEngine, config);

    ASSERT_TRUE(result.IsReady()) << result.message;
    EXPECT_EQ(result.setup->compatibility.verdict, ProjectCompatibilityVerdict::OlderNeedsUpgrade);
}

TEST_F(ProjectLauncherTest, PrepareEditor_PropagatesProjectRendererProfileToEngineConfig) {
    Platform::EditorProjectPackage package = MakeAuthoringPackage();
    package.rendererProfile = Platform::RendererProfile::Compatibility;
    ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(projectFile(), package).IsAccepted());

    Core::EngineConfig config = MakeHeadlessConfig(projectRoot);
    config.requestedRendererProfile = Platform::RendererProfile::ForwardPlus;
    const ProjectEditorSetupResult result =
        PrepareProjectEditor(projectFile(), kAuthoringEngineVersion, config);

    ASSERT_TRUE(result.IsReady()) << result.message;
    EXPECT_EQ(config.requestedRendererProfile, Platform::RendererProfile::Compatibility);
}

} // namespace
} // namespace UVE::Pack::Tests
