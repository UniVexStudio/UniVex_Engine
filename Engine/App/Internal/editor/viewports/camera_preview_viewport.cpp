// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "camera_preview_viewport.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "camera_3d.h"
#include "engine_services.h"

namespace UVE::App::Viewports {

CameraPreviewViewportUVE::CameraPreviewViewportUVE(Editor::EditorUVE& editor,
                                                   Core::EngineServicesUVE& services)
    : m_editor(editor), m_surface(services) {}

std::uint64_t CameraPreviewViewportUVE::RenderUVE(const Math::Vector2UVE& availableSize,
                                                  Math::Vector2UVE& outUsedSize,
                                                  const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrameUVE(width, height)) {
        return 0U;
    }

    const Scene::EntityUVE selectedEntity = m_editor.GetSelectedEntityUVE();
    std::optional<Scene::EntityUVE> lookThroughCamera;
    if (Scene::IsDocumentCameraEntityUVE(m_surface.EntityManagerUVE(), selectedEntity)) {
        lookThroughCamera = selectedEntity;
    }

    m_surface.SetShadingModeUVE(overlayState.shadingMode);
    m_surface.RenderPassUVE().Settings().viewEnvironment = true;
    m_surface.RenderPassUVE().Settings().viewGrid = false;
    m_surface.ApplyPreviewLightingUVE(overlayState, true);

    const univex::integration::EditorMeshLayerResultUVE meshResult =
        m_surface.RenderSceneMeshLayerUVE(m_fallbackCamera, width, height, lookThroughCamera);

    overlayState.viewCamera.valid = false;
    overlayState.gizmoPivotValid = false;
    overlayState.gizmoLines.clear();
    overlayState.gizmoTriangles.clear();
    overlayState.selectionMask.clear();
    overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
    overlayState.sceneDepth.clear();
    overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
    overlayState.hostGrid.visible = false;

    outUsedSize = Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPassUVE(width, height);
    const bool neutralFallback = !m_surface.HasActiveWorldEnvironmentUVE();
    m_surface.RenderPassUVE().RenderBackgroundUVE(neutralFallback);
    m_surface.BlitMeshLayerUVE(meshResult, m_fallbackCamera, width, height);
    return m_surface.EndPassAndResolveUVE(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
