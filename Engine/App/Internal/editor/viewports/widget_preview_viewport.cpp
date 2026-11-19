// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "widget_preview_viewport.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "engine_services.h"

namespace UVE::App::Viewports {

WidgetPreviewViewportUVE::WidgetPreviewViewportUVE(Editor::EditorUVE& editor,
                                                   Core::EngineServicesUVE& services)
    : m_editor(editor), m_surface(services) {}

std::uint64_t WidgetPreviewViewportUVE::RenderUVE(const Math::Vector2UVE& availableSize,
                                                  Math::Vector2UVE& outUsedSize,
                                                  const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrameUVE(width, height)) {
        return 0U;
    }

    const bool useStudio = overlayState.studioView;
    std::optional<Scene::EntityUVE> lookThroughCamera;
    if (useStudio) {
        m_studioCamera.ApplyStudioFramingUVE(overlayState);
        m_studioCamera.CameraUVE().Update(overlayState.input.deltaTime);
    } else {
        const Scene::EntityUVE stageCamera = m_editor.GetPreviewCameraUVE();
        if (stageCamera != Scene::kInvalidEntityUVE) {
            lookThroughCamera = stageCamera;
        }
    }

    m_surface.SetShadingModeUVE(overlayState.shadingMode);
    m_surface.RenderPassUVE().Settings().viewEnvironment = !useStudio;
    m_surface.RenderPassUVE().Settings().viewGrid = false;
    m_surface.ApplyPreviewLightingUVE(overlayState, true);

    const univex::integration::EditorMeshLayerResultUVE meshResult =
        m_surface.RenderSceneMeshLayerUVE(m_studioCamera.CameraUVE(), width, height, lookThroughCamera);

    m_studioCamera.ExportCameraForHostUVE(overlayState, width, height);
    outUsedSize = Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPassUVE(width, height);
    const univex::render::StudioBackdropRenderer* const studio = useStudio ? m_surface.StudioBackdropUVE() : nullptr;
    if (studio != nullptr) {
        studio->DrawBackdrop(m_studioCamera.CameraUVE(), width, height);
    } else {
        const bool neutralFallback = !m_surface.HasActiveWorldEnvironmentUVE();
        m_surface.RenderPassUVE().RenderBackgroundUVE(neutralFallback);
    }
    m_surface.BlitMeshLayerUVE(meshResult, m_studioCamera.CameraUVE(), width, height);
    if (studio != nullptr) {
        studio->DrawFloor(m_studioCamera.CameraUVE(), width, height);
    }
    return m_surface.EndPassAndResolveUVE(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
