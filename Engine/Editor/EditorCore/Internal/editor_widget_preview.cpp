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

void DestroyTreeUVE(Core::EngineServicesUVE& services, const Scene::EntityUVE root) {
    Scene::IEntityManagerUVE& em = services.GetEntityManagerUVE();
    if (root == Scene::kInvalidEntityUVE || !em.IsAliveUVE(root)) {
        return;
    }
    std::vector<Scene::EntityUVE> doomed{root};
    for (std::size_t i = 0U; i < doomed.size(); ++i) {
        const std::vector<Scene::EntityUVE> children = services.GetSceneGraphUVE().GetChildrenUVE(em, doomed[i]);
        doomed.insert(doomed.end(), children.begin(), children.end());
    }
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it) {
        if (em.IsAliveUVE(*it)) {
            em.DestroyEntityUVE(*it);
        }
    }
}

[[nodiscard]] Scene::EntityUVE FindNamedUnderUVE(Core::EngineServicesUVE& services, const Scene::EntityUVE root,
                                               const std::string_view name) {
    Scene::IEntityManagerUVE& em = services.GetEntityManagerUVE();
    std::vector<Scene::EntityUVE> open{root};
    for (std::size_t i = 0U; i < open.size() && i < 4096U; ++i) {
        if (open[i] != root && em.HasComponentUVE<Scene::NameComponentUVE>(open[i]) &&
            em.GetComponentUVE<Scene::NameComponentUVE>(open[i]).name == name) {
            return open[i];
        }
        const std::vector<Scene::EntityUVE> children = services.GetSceneGraphUVE().GetChildrenUVE(em, open[i]);
        open.insert(open.end(), children.begin(), children.end());
    }
    return Scene::kInvalidEntityUVE;
}

} // namespace

std::uint64_t EditorUVE::RenderWidgetPreviewUVE(const WidgetPreviewRequestUVE& request, const Math::Vector2UVE& size,
                                                Math::Vector2UVE& outUsed) {
    outUsed = Math::Vector2UVE{0.0F, 0.0F};
    if (request.assetPath.empty() || !m_viewportPanelRenderer || !m_documentSceneFactory || size.x < 8.0F || size.y < 8.0F) {
        return 0U;
    }
    if (!m_widgetPreview.has_value()) {
        WidgetPreviewUVE preview;
        preview.scene = m_documentSceneFactory();
        if (preview.scene == nullptr) {
            return 0U;
        }
        m_widgetPreview = std::move(preview);
    }
    WidgetPreviewUVE& preview = *m_widgetPreview;
    Core::EngineServicesUVE& services = preview.scene->GetServicesUVE();
    Scene::IEntityManagerUVE& em = services.GetEntityManagerUVE();
    Scene::ISceneGraphUVE& graph = services.GetSceneGraphUVE();

    // The asset, stood in the preview's world again when the widget names another.
    if (preview.assetPath != request.assetPath || (preview.root != Scene::kInvalidEntityUVE && !em.IsAliveUVE(preview.root))) {
        DestroyTreeUVE(services, preview.root);
        preview.root = Scene::kInvalidEntityUVE;
        preview.assetPath = request.assetPath;
        std::filesystem::path absolute = m_services->GetFileSystemUVE().ResolveRealPathUVE(request.assetPath);
        std::error_code error;
        if (absolute.empty() || !std::filesystem::is_regular_file(absolute, error)) {
            absolute = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot.parent_path() / request.assetPath;
        }
        if (std::filesystem::is_regular_file(absolute, error)) {
            const Asset::AssetGuidUVE guid = services.GetAssetDatabaseUVE().RegisterUVE(absolute);
            if (guid != Asset::kInvalidAssetGuidUVE) {
                preview.root = services.GetPrefabSystemUVE().InstantiateUVE(em, graph, services.GetAssetDatabaseUVE(), guid,
                                                                            Scene::kInvalidEntityUVE);
            }
        }
        preview.framedRadius = 0.0F;   // frame again
    }
    // The widget's Stage, rebuilt when it changed in the editor.
    if (request.stage != preview.stageBytes || (preview.stage != Scene::kInvalidEntityUVE && !em.IsAliveUVE(preview.stage))) {
        DestroyTreeUVE(services, preview.stage);
        preview.stage = Scene::kInvalidEntityUVE;
        preview.stageCamera = Scene::kInvalidEntityUVE;
        preview.stageBytes = request.stage;
        if (!request.stage.empty()) {
            const std::vector<Scene::EntityUVE> roots = services.GetSceneSerializerUVE().RestoreUVE(
                em, Scene::SceneSnapshotUVE{request.stage, Scene::SceneAssetTypeUVE::Prefab});
            if (roots.size() == 1U) {
                preview.stage = roots.front();
                preview.stageCamera = FindNamedUnderUVE(services, preview.stage, Scene::kWidgetStageCameraNameUVE);
                if (preview.stageCamera != Scene::kInvalidEntityUVE && em.HasComponentUVE<Scene::CameraComponentUVE>(preview.stageCamera)) {
                    em.GetComponentUVE<Scene::CameraComponentUVE>(preview.stageCamera).current = false;
                }
            }
        }
    }
    if (preview.root == Scene::kInvalidEntityUVE || !em.IsAliveUVE(preview.root) ||
        !em.HasComponentUVE<Scene::TransformComponentUVE>(preview.root)) {
        return 0U;
    }
    // On the Stage the asset stands on its ModelSpot; alone, at the middle of the studio.
    const Scene::EntityUVE spot = em.IsAliveUVE(preview.stage) ? FindNamedUnderUVE(services, preview.stage, Scene::kWidgetModelSpotNameUVE)
                                                              : Scene::kInvalidEntityUVE;
    {
        const Scene::EntityUVE parent = em.HasComponentUVE<Scene::HierarchyComponentUVE>(preview.root)
                                            ? em.GetComponentUVE<Scene::HierarchyComponentUVE>(preview.root).parent
                                            : Scene::kInvalidEntityUVE;
        if (parent != spot) {
            graph.SetParentUVE(em, preview.root, spot);
        }
    }

    // Turning as it will in the game.
    const auto now = std::chrono::steady_clock::now();
    if (preview.lastFrame.time_since_epoch().count() != 0) {
        const float dt = std::min(0.1F, std::chrono::duration<float>(now - preview.lastFrame).count());
        preview.spinDegrees = std::fmod(preview.spinDegrees + request.spinDegreesPerSecond * dt, 360.0F);
    }
    preview.lastFrame = now;
    Scene::TransformComponentUVE transform = em.GetComponentUVE<Scene::TransformComponentUVE>(preview.root);
    Math::QuaternionUVE turn{};
    if (Math::TryMakeAxisAngleUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                  (request.yawDegrees + preview.spinDegrees) * (Math::kPiUVE / 180.0F), turn)) {
        transform.localPosition = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        transform.localRotation = turn;
        transform.rotationEditMode = Scene::RotationEditModeUVE::Quaternion;
        graph.SetLocalTransformUVE(em, preview.root, transform);
    }
    graph.UpdateUVE(em);

    // Framed on the look-at height, as far back as the widget's camera stands.
    ViewportOverlayStateUVE& state = preview.overlay;
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
    state.input = EditorViewportInputUVE{};

    // On a Stage the view looks through its StageCamera, as the game will; else the studio.
    const bool throughStage = preview.stageCamera != Scene::kInvalidEntityUVE && em.IsAliveUVE(preview.stageCamera);
    state.studioView = !throughStage;

    // The view draws whatever document is active: for this call, the preview's world.
    Core::EngineServicesUVE* const previous = m_services;
    const Scene::EntityUVE previousCamera = m_previewCamera;
    m_services = &services;
    m_previewCamera = throughStage ? preview.stageCamera : Scene::kInvalidEntityUVE;
    std::uint64_t texture = 0U;
    try {
        texture = m_viewportPanelRenderer(ViewportContextUVE::WidgetPreview, size, outUsed, state);
    } catch (...) {
        m_services = previous;
        m_previewCamera = previousCamera;
        throw;
    }
    m_services = previous;
    m_previewCamera = previousCamera;
    return texture;
}

void EditorUVE::EndWidgetPreviewUVE() {
    if (!m_widgetPreview.has_value()) {
        return;
    }
    if (m_widgetPreview->scene != nullptr && m_documentSceneClosing) {
        m_documentSceneClosing(m_widgetPreview->scene->GetServicesUVE());
    }
    m_widgetPreview.reset();
}

} // namespace UVE::Editor
