// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "entity_viewport.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "engine_services.h"

namespace UVE::App::Viewports {

EntityViewport::EntityViewport(Editor::Editor& editor, Core::EngineServices& services)
    : m_surface(services), m_interaction(editor, services.GetEntityManager()) {}

std::uint64_t EntityViewport::Render(const Math::Vector2& availableSize,
                                           Math::Vector2& outUsedSize,
                                           const Editor::Editor::ViewportOverlayState& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    m_surface.ApplyQualityPreset(overlayState.qualityPreset, overlayState);
    if (!m_surface.PrepareFrame(width, height)) {
        return 0U;
    }

    m_interaction.ApplyOverlayState(m_surface, overlayState, false);

    const std::optional<Scene::Entity> lookThroughCamera =
        m_interaction.UpdateCameraPreviewFlight(true, overlayState.input);

    m_interaction.UpdateInteraction(overlayState, width, height, false);
    m_interaction.AdvanceCameraAndPublishFocus(overlayState.input);

    m_surface.ApplyPreviewLighting(overlayState, true);

    const univex::integration::EditorMeshLayerResult meshResult =
        m_surface.RenderSceneMeshLayer(m_interaction.Camera(), width, height, lookThroughCamera);

    const bool wantOverlay = !m_interaction.IsPreviewingCamera();
    m_interaction.UpdateObjectGizmoOverlay(m_surface, overlayState, width, height, lookThroughCamera, wantOverlay);
    m_interaction.ExportViewForHost(overlayState, width, height, wantOverlay);
    outUsedSize = Math::Vector2{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPass(width, height);
    const bool neutralFallback = !m_surface.HasActiveWorldEnvironment();
    m_surface.RenderPass().RenderBackground(neutralFallback);
    m_surface.BlitMeshLayer(meshResult, m_interaction.Camera(), width, height);

    overlayState.selectionMask.clear();
    overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
    m_interaction.ExportGridForHost(m_surface, overlayState, false);
    if (overlayState.hostDrawsGizmos && !lookThroughCamera.has_value()) {
        m_interaction.ExportSceneDepthForHost(m_surface, overlayState, width, height);
    } else {
        overlayState.sceneDepth.clear();
        overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
    }

    if (!m_interaction.IsPreviewingCamera()) {
        m_surface.RenderPass().RenderGrid(m_interaction.Camera(), width, height);
    }
    if (wantOverlay) {
        m_interaction.RenderSelectionOutlineOrMask(m_surface, overlayState, width, height);
    }
    m_surface.RenderPass().RenderOverlay(m_interaction.Camera(), width, height);
    return m_surface.EndPassAndResolve(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
