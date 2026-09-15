// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/pack/project_launcher_uve.h"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <gtest/gtest.h>

#include "uve/core/engine_core_uve.h"
#include "uve/platform/editor_project_package_uve.h"
#include "uve/scene/components/camera_component_uve.h"
#include "uve/scene/components/transform_component_uve.h"
#include "uve/scene/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/scene_serializer_uve.h"

namespace UVE::Pack::Tests {
namespace {

[[nodiscard]] std::filesystem::path MakeUniqueTestDirectoryUVE(const std::string& label) {
    static std::atomic<unsigned int> nextId{0U};
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                            ("uve_project_launcher_test_" + label + "_" +
                                             std::to_string(nextId.fetch_add(1U)));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}

[[nodiscard]] Core::EngineConfigUVE MakeHeadlessConfigUVE(const std::filesystem::path& scratchDirectory) {
    Core::EngineConfigUVE config{};
    config.headlessUVE = true;
    config.enableConsoleLogging = false;
    config.logFilePath = scratchDirectory / "uve_project_launcher_tests.log";
    config.settingsFilePath = scratchDirectory / "uve_project_launcher_tests.uvesettings";
    config.assetDatabaseFilePath = scratchDirectory / "uve_project_launcher_tests.uveassetdb";
    config.threadPoolWorkerCount = 2;
    return config;
}

class ProjectLauncherUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        projectRoot = MakeUniqueTestDirectoryUVE("root");
        std::filesystem::create_directories(projectRoot / "content" / "scenes");
    }

    void TearDown() override { std::filesystem::remove_all(projectRoot); }

    // Authors one entity with a real camera + world transform into a fresh in-memory entity
    // manager, saves it as the project's own scene file, then writes a matching .uveditor package
    // naming it as the startup scene - the same shape ProjectPackagerUVE expects a real authored
    // project to already have on disk.
    void WriteProjectWithCameraSceneUVE(Core::EngineCoreUVE& authoringEngine) {
        Core::EngineServicesUVE& services = authoringEngine.GetServicesUVE();
        Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
        const Scene::EntityUVE camera = entityManager.CreateEntityUVE();
        services.GetSceneGraphUVE().AttachTransformUVE(entityManager, camera, Scene::TransformComponentUVE{});
        entityManager.AddComponentUVE<Scene::CameraComponentUVE>(camera);

        Scene::SceneSerializerUVE serializer;
        ASSERT_TRUE(serializer.SaveUVE(entityManager, {camera}, projectRoot / "content" / "scenes" / "main.uvescene",
                                       Scene::SceneAssetTypeUVE::Scene));

        Platform::EditorProjectPackageUVE package;
        package.revision = 1U;
        package.projectId = "launcher-test-project";
        package.displayName = "Launcher Test Project";
        package.engineVersion = {0U, 1U, 0U, 1U};
        package.contentRoot = "content";
        package.assetDatabasePath = ".uveassetdb";
        package.settingsPath = ".uvesettings";
        package.startupScenePath = "scenes/main.uvescene";
        ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(projectFile(), package).IsAcceptedUVE());
    }

    [[nodiscard]] std::filesystem::path projectFile() const { return projectRoot / "project.uveditor"; }

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

TEST_F(ProjectLauncherUVETest, LoadAndActivate_RejectsProjectWithNoStartupSceneConfigured) {
    Platform::EditorProjectPackageUVE package;
    package.revision = 1U;
    package.projectId = "no-startup-scene";
    package.displayName = "No Startup Scene";
    package.engineVersion = {0U, 1U, 0U, 1U};
    package.contentRoot = "content";
    package.assetDatabasePath = ".uveassetdb";
    package.settingsPath = ".uvesettings";
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
        LoadAndActivateProjectSceneUVE(engine, projectRoot / "does_not_exist.uveditor");

    EXPECT_FALSE(result.IsAcceptedUVE());
    EXPECT_EQ(result.code, ProjectLaunchCodeUVE::InvalidProjectFile);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Pack::Tests
