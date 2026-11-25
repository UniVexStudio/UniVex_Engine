// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "widget_preview_viewport.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "engine_services.h"

namespace UVE::App::Viewports {

WidgetPreviewViewport::WidgetPreviewViewport(Editor::Editor& editor,
                                                   Core::EngineServices& services)
    : m_editor(editor), m_surface(services) {}

std::uint64_t WidgetPreviewViewport::Render(const Math::Vector2& availableSize,
                                                  Math::Vector2& outUsedSize,
                                                  const Editor::Editor::ViewportOverlayState& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrame(width, height)) {
        return 0U;
    }

    const bool useStudio = overlayState.studioView;
    std::optional<Scene::Entity> lookThroughCamera;
    if (useStudio) {
        m_studioCamera.ApplyStudioFraming(overlayState);
        m_studioCamera.Camera().Update(overlayState.input.deltaTime);
    } else {
        const Scene::Entity stageCamera = m_editor.GetPreviewCamera();
        if (stageCamera != Scene::kInvalidEntity) {
            lookThroughCamera = stageCamera;
        }
    }

    m_surface.SetShadingMode(overlayState.shadingMode);
    m_surface.RenderPass().Settings().viewEnvironment = !useStudio;
    m_surface.RenderPass().Settings().viewGrid = false;
    m_surface.ApplyPreviewLighting(overlayState, true);

    const univex::integration::EditorMeshLayerResult meshResult =
        m_surface.RenderSceneMeshLayer(m_studioCamera.Camera(), width, height, lookThroughCamera);

    m_studioCamera.ExportCameraForHost(overlayState, width, height);
    outUsedSize = Math::Vector2{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPass(width, height);
    const univex::render::StudioBackdropRenderer* const studio = useStudio ? m_surface.StudioBackdrop() : nullptr;
    if (studio != nullptr) {
        studio->DrawBackdrop(m_studioCamera.Camera(), width, height);
    } else {
        const bool neutralFallback = !m_surface.HasActiveWorldEnvironment();
        m_surface.RenderPass().RenderBackground(neutralFallback);
    }
    m_surface.BlitMeshLayer(meshResult, m_studioCamera.Camera(), width, height);
    if (studio != nullptr) {
        studio->DrawFloor(m_studioCamera.Camera(), width, height);
    }
    return m_surface.EndPassAndResolve(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
