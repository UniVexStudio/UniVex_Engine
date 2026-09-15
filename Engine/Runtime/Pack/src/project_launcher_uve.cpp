// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/pack/project_launcher_uve.h"

#include <optional>

#include "uve/core/engine_core_uve.h"
#include "uve/platform/editor_project_package_uve.h"
#include "uve/scene/components/camera_component_uve.h"
#include "uve/scene/components/world_transform_component_uve.h"
#include "uve/scene/i_entity_manager_uve.h"
#include "uve/scene/i_scene_graph_uve.h"
#include "uve/scene/scene_serializer_uve.h"

namespace UVE::Pack {
namespace {

[[nodiscard]] ProjectLaunchResultUVE MakeResultUVE(const ProjectLaunchCodeUVE code, std::string message) {
    return {code, std::move(message)};
}

// Mirrors Engine/App/src/editor/main.cpp's own FindGameCameraEntityUVE() exactly: this engine has
// no "main camera" tag/priority concept yet, so "first entity found with both components" is the
// same honest, simple convention already used by the editor's own Play-mode game-camera switch.
[[nodiscard]] std::optional<Scene::EntityUVE> FindFirstCameraEntityUVE(Scene::IEntityManagerUVE& entityManager) {
    std::optional<Scene::EntityUVE> found;
    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::CameraComponentUVE>(
        [&found](const Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE&,
                 const Scene::CameraComponentUVE&) {
            if (!found.has_value()) {
                found = entity;
            }
        });
    return found;
}

} // namespace

ProjectLaunchResultUVE LoadAndActivateProjectSceneUVE(Core::EngineCoreUVE& engine,
                                                       const std::filesystem::path& projectFile) {
    const Platform::EditorProjectPackageLoadResultUVE loaded =
        Platform::EditorProjectPackageCodecUVE::LoadUVE(projectFile);
    if (!loaded.IsAcceptedUVE()) {
        return MakeResultUVE(ProjectLaunchCodeUVE::InvalidProjectFile,
                             "Unable to load the .uveditor project file: " + loaded.result.message);
    }
    const Platform::EditorProjectPackageUVE& package = *loaded.package;
    if (package.startupScenePath.empty()) {
        return MakeResultUVE(ProjectLaunchCodeUVE::NoStartupSceneConfigured,
                             "The project has no startup scene configured (EditorProjectPackageUVE::"
                             "startupScenePath is empty) - nothing to load.");
    }

    const std::filesystem::path projectRoot = projectFile.parent_path();
    const std::filesystem::path scenePath = projectRoot / package.contentRoot / package.startupScenePath;

    Core::EngineServicesUVE& services = engine.GetServicesUVE();
    Scene::IEntityManagerUVE& entityManager = services.GetEntityManagerUVE();
    Scene::SceneSerializerUVE serializer;
    const std::vector<Scene::EntityUVE> loadedEntities = serializer.LoadUVE(entityManager, scenePath);
    if (loadedEntities.empty()) {
        return MakeResultUVE(ProjectLaunchCodeUVE::SceneLoadFailed,
                             "Failed to load the startup scene: " + scenePath.string());
    }

    // Newly-loaded entities only have their authored local transforms until the scene graph
    // computes world transforms from them - the camera lookup below (and the very first render)
    // both need a real WorldTransformComponentUVE to already be present.
    services.GetSceneGraphUVE().UpdateUVE(entityManager);

    const std::optional<Scene::EntityUVE> cameraEntity = FindFirstCameraEntityUVE(entityManager);
    if (cameraEntity.has_value()) {
        engine.SetActiveCameraUVE(*cameraEntity);
    }

    return MakeResultUVE(ProjectLaunchCodeUVE::Loaded, "Loaded startup scene: " + scenePath.string());
}

} // namespace UVE::Pack
