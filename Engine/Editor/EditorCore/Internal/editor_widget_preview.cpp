// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// The Widget Editor's 3D View preview. A 3D View shows an entity asset in the game (the runtime
// stands it on a stage of its own); in the Designer the same asset stands in a small world of its
// own beside the level and the widget - lit, on the studio floor, turning as it will in the game -
// and the Designer shows that view where the widget is.
#include "editor.h"

#include "document_scene.h"
#include "i_asset_database.h"
#include "i_entity_manager.h"
#include "i_file_system.h"
#include "i_prefab_system.h"
#include "i_scene_graph.h"
#include "name_component.h"
#include "quaternion.h"
#include "scalar.h"
#include "scene_object_type.h"
#include "transform_component.h"
#include "camera_component.h"
#include "hierarchy_component.h"
#include "i_scene_serializer.h"
#include "ui_view_3d_component.h"
#include "engine_services.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <system_error>
#include <vector>

namespace UVE::Editor {
namespace {

void DestroyTree(Core::EngineServices& services, const Scene::Entity root) {
    Scene::IEntityManager& em = services.GetEntityManager();
    if (root == Scene::kInvalidEntity || !em.IsAlive(root)) {
        return;
    }
    std::vector<Scene::Entity> doomed{root};
    for (std::size_t i = 0U; i < doomed.size(); ++i) {
        const std::vector<Scene::Entity> children = services.GetSceneGraph().GetChildren(em, doomed[i]);
        doomed.insert(doomed.end(), children.begin(), children.end());
    }
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
        if (em.IsAlive(*it)) {
            em.DestroyEntity(*it);
        }
    }
}

[[nodiscard]] Scene::Entity FindNamedUnder(Core::EngineServices& services, const Scene::Entity root,
                                               const std::string_view name) {
    Scene::IEntityManager& em = services.GetEntityManager();
    std::vector<Scene::Entity> open{root};
    for (std::size_t i = 0U; i < open.size() && i < 4096U; ++i) {
        if (open[i] != root && em.HasComponent<Scene::NameComponent>(open[i]) &&
            em.GetComponent<Scene::NameComponent>(open[i]).name == name) {
            return open[i];
        }
        const std::vector<Scene::Entity> children = services.GetSceneGraph().GetChildren(em, open[i]);
        open.insert(open.end(), children.begin(), children.end());
    }
    return Scene::kInvalidEntity;
}

} // namespace

std::uint64_t Editor::RenderWidgetPreview(const WidgetPreviewRequest& request, const Math::Vector2& size,
                                                Math::Vector2& outUsed) {
    outUsed = Math::Vector2{0.0F, 0.0F};
    if (request.assetPath.empty() || !m_viewports.widgetPreview || !m_documentSceneFactory || size.x < 8.0F || size.y < 8.0F) {
        return 0U;
    }
    if (!m_widgetPreview.has_value()) {
        WidgetPreview preview;
        preview.scene = m_documentSceneFactory();
        if (preview.scene == nullptr) {
            return 0U;
        }
        m_widgetPreview = std::move(preview);
    }
    WidgetPreview& preview = *m_widgetPreview;
    Core::EngineServices& services = preview.scene->GetServices();
    Scene::IEntityManager& em = services.GetEntityManager();
    Scene::ISceneGraph& graph = services.GetSceneGraph();

    // The asset, stood in the preview's world again when the widget names another.
    if (preview.assetPath != request.assetPath || (preview.root != Scene::kInvalidEntity && !em.IsAlive(preview.root))) {
        DestroyTree(services, preview.root);
        preview.root = Scene::kInvalidEntity;
        preview.assetPath = request.assetPath;
        std::filesystem::path absolute = m_services->GetFileSystem().ResolveRealPath(request.assetPath);
        std::error_code error;
        if (absolute.empty() || !std::filesystem::is_regular_file(absolute, error)) {
            absolute = m_services->GetProjectFileIndex().GetSnapshot().contentRoot.parent_path() / request.assetPath;
        }
        if (std::filesystem::is_regular_file(absolute, error)) {
            const Asset::AssetGuid guid = services.GetAssetDatabase().Register(absolute);
            if (guid != Asset::kInvalidAssetGuid) {
                preview.root = services.GetPrefabSystem().Instantiate(em, graph, services.GetAssetDatabase(), guid,
                                                                            Scene::kInvalidEntity);
            }
        }
        preview.framedRadius = 0.0F;   // frame again
    }
    // The widget's Stage, rebuilt when it changed in the editor.
    if (request.stage != preview.stageBytes || (preview.stage != Scene::kInvalidEntity && !em.IsAlive(preview.stage))) {
        DestroyTree(services, preview.stage);
        preview.stage = Scene::kInvalidEntity;
        preview.stageCamera = Scene::kInvalidEntity;
        preview.stageBytes = request.stage;
        if (!request.stage.empty()) {
            const std::vector<Scene::Entity> roots = services.GetSceneSerializer().Restore(
                em, Scene::SceneSnapshot{request.stage, Scene::SceneAssetType::Prefab});
            if (roots.size() == 1U) {
                preview.stage = roots.front();
                preview.stageCamera = FindNamedUnder(services, preview.stage, Scene::kWidgetStageCameraName);
                if (preview.stageCamera != Scene::kInvalidEntity && em.HasComponent<Scene::CameraComponent>(preview.stageCamera)) {
                    em.GetComponent<Scene::CameraComponent>(preview.stageCamera).current = false;
                }
            }
        }
    }
    if (preview.root == Scene::kInvalidEntity || !em.IsAlive(preview.root) ||
        !em.HasComponent<Scene::TransformComponent>(preview.root)) {
        return 0U;
    }
    // On the Stage the asset stands on its ModelSpot; alone, at the middle of the studio.
    const Scene::Entity spot = em.IsAlive(preview.stage) ? FindNamedUnder(services, preview.stage, Scene::kWidgetModelSpotName)
                                                              : Scene::kInvalidEntity;
    {
        const Scene::Entity parent = em.HasComponent<Scene::HierarchyComponent>(preview.root)
                                            ? em.GetComponent<Scene::HierarchyComponent>(preview.root).parent
                                            : Scene::kInvalidEntity;
        if (parent != spot) {
            graph.SetParent(em, preview.root, spot);
        }
    }

    // Turning as it will in the game.
    const auto now = std::chrono::steady_clock::now();
    if (preview.lastFrame.time_since_epoch().count() != 0) {
        const float dt = std::min(0.1F, std::chrono::duration<float>(now - preview.lastFrame).count());
        preview.spinDegrees = std::fmod(preview.spinDegrees + request.spinDegreesPerSecond * dt, 360.0F);
    }
    preview.lastFrame = now;
    Scene::TransformComponent transform = em.GetComponent<Scene::TransformComponent>(preview.root);
    Math::Quaternion turn{};
    if (Math::TryMakeAxisAngle(Math::Vector3{0.0F, 1.0F, 0.0F},
                                  (request.yawDegrees + preview.spinDegrees) * (Math::kPi / 180.0F), turn)) {
        transform.localPosition = Math::Vector3{0.0F, 0.0F, 0.0F};
        transform.localRotation = turn;
        transform.rotationEditMode = Scene::RotationEditMode::Quaternion;
        graph.SetLocalTransform(em, preview.root, transform);
    }
    graph.Update(em);

    // Framed on the look-at height, as far back as the widget's camera stands.
    ViewportOverlayState& state = preview.overlay;
    const std::array<float, 3> target{0.0F, request.lookAtHeight, 0.0F};
    const float radius = std::max(0.5F, request.cameraDistance * 0.4F);
    if (target != preview.framedTarget || std::fabs(radius - preview.framedRadius) > 1.0e-3F) {
        preview.framedTarget = target;
        preview.framedRadius = radius;
        state.studioTarget = target;
        state.studioRadius = radius;
        ++state.studioFramingSerial;
    }
    state.studioView = true;
    state.entityAuthoringView = true;
    state.gameWorkspaceActive = false;
    state.hostDrawsGizmos = false;
    state.pointerOverOverlay = false;
    state.bones.clear();
    state.colliderLines.clear();
    state.input = EditorViewportInput{};

    // On a Stage the view looks through its StageCamera, as the game will; else the studio.
    const bool throughStage = preview.stageCamera != Scene::kInvalidEntity && em.IsAlive(preview.stageCamera);
    state.studioView = !throughStage;

    // The view draws whatever document is active: for this call, the preview's world.
    Core::EngineServices* const previous = m_services;
    const Scene::Entity previousCamera = m_previewCamera;
    m_services = &services;
    m_previewCamera = throughStage ? preview.stageCamera : Scene::kInvalidEntity;
    std::uint64_t texture = 0U;
    try {
        texture = m_viewports.widgetPreview(size, outUsed, state);
    } catch (...) {
        m_services = previous;
        m_previewCamera = previousCamera;
        throw;
    }
    m_services = previous;
    m_previewCamera = previousCamera;
    return texture;
}

void Editor::EndWidgetPreview() {
    if (!m_widgetPreview.has_value()) {
        return;
    }
    if (m_widgetPreview->scene != nullptr && m_documentSceneClosing) {
        m_documentSceneClosing(m_widgetPreview->scene->GetServices());
    }
    m_widgetPreview.reset();
}

} // namespace UVE::Editor
