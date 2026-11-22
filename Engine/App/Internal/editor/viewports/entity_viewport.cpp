// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "entity_viewport.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "engine_services.h"

namespace UVE::App::Viewports {

EntityViewportUVE::EntityViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services)
    : m_surface(services), m_interaction(editor, services.GetEntityManagerUVE()) {}

std::uint64_t EntityViewportUVE::RenderUVE(const Math::Vector2UVE& availableSize,
                                           Math::Vector2UVE& outUsedSize,
                                           const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    m_surface.ApplyQualityPresetUVE(overlayState.qualityPreset, overlayState);
    if (!m_surface.PrepareFrameUVE(width, height)) {
        return 0U;
    }

    m_interaction.ApplyOverlayStateUVE(m_surface, overlayState, false);

    const std::optional<Scene::EntityUVE> lookThroughCamera =
        m_interaction.UpdateCameraPreviewFlightUVE(true, overlayState.input);

    m_interaction.UpdateInteractionUVE(overlayState, width, height, false);
    m_interaction.AdvanceCameraAndPublishFocusUVE(overlayState.input);

    m_surface.ApplyPreviewLightingUVE(overlayState, true);

    const univex::integration::EditorMeshLayerResultUVE meshResult =
        m_surface.RenderSceneMeshLayerUVE(m_interaction.CameraUVE(), width, height, lookThroughCamera);

    const bool wantOverlay = !m_interaction.IsPreviewingCameraUVE();
    m_interaction.UpdateObjectGizmoOverlayUVE(m_surface, overlayState, width, height, lookThroughCamera, wantOverlay);
    m_interaction.ExportViewForHostUVE(overlayState, width, height, wantOverlay);
    outUsedSize = Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPassUVE(width, height);
    const bool neutralFallback = !m_surface.HasActiveWorldEnvironmentUVE();
    m_surface.RenderPassUVE().RenderBackgroundUVE(neutralFallback);
    m_surface.BlitMeshLayerUVE(meshResult, m_interaction.CameraUVE(), width, height);

    overlayState.selectionMask.clear();
    overlayState.selectionMaskWidth = overlayState.selectionMaskHeight = 0;
    m_interaction.ExportGridForHostUVE(m_surface, overlayState, false);
    if (overlayState.hostDrawsGizmos && !lookThroughCamera.has_value()) {
        m_interaction.ExportSceneDepthForHostUVE(m_surface, overlayState, width, height);
    } else {
        overlayState.sceneDepth.clear();
        overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;
    }

    if (!m_interaction.IsPreviewingCameraUVE()) {
        m_surface.RenderPassUVE().RenderGridUVE(m_interaction.CameraUVE(), width, height);
    }
    if (wantOverlay) {
        m_interaction.RenderSelectionOutlineOrMaskUVE(m_surface, overlayState, width, height);
    }
    m_surface.RenderPassUVE().RenderOverlayUVE(m_interaction.CameraUVE(), width, height);
    return m_surface.EndPassAndResolveUVE(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
