// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "camera_preview_viewport.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "camera_3d.h"
#include "engine_services.h"

namespace UVE::App::Viewports {

CameraPreviewViewport::CameraPreviewViewport(Editor::Editor& editor,
                                                   Core::EngineServices& services)
    : m_editor(editor), m_surface(services) {}

std::uint64_t CameraPreviewViewport::Render(const Math::Vector2& availableSize,
                                                  Math::Vector2& outUsedSize,
                                                  const Editor::Editor::ViewportOverlayState& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrame(width, height)) {
        return 0U;
    }

    const Scene::Entity selectedEntity = m_editor.GetSelectedEntity();
    std::optional<Scene::Entity> lookThroughCamera;
    if (Scene::IsDocumentCameraEntity(m_surface.EntityManager(), selectedEntity)) {
        lookThroughCamera = selectedEntity;
    }

    m_surface.SetShadingMode(overlayState.shadingMode);
    m_surface.RenderPass().Settings().viewEnvironment = true;
    m_surface.RenderPass().Settings().viewGrid = false;
    m_surface.ApplyPreviewLighting(overlayState, true);

    const univex::integration::EditorMeshLayerResult meshResult =
        m_surface.RenderSceneMeshLayer(m_fallbackCamera, width, height, lookThroughCamera);

    overlayState.viewCamera.valid = false;
    overlayState.gizmoPivotValid = false;
    overlayState.gizmoLines.clear();
    overlayState.gizmoTriangles.clear();
    overlayState.selectionMask.clear();
    overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
    overlayState.sceneDepth.clear();
    overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
    overlayState.hostGrid.visible = false;

    outUsedSize = Math::Vector2{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPass(width, height);
    const bool neutralFallback = !m_surface.HasActiveWorldEnvironment();
    m_surface.RenderPass().RenderBackground(neutralFallback);
    m_surface.BlitMeshLayer(meshResult, m_fallbackCamera, width, height);
    return m_surface.EndPassAndResolve(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
