// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "entity_viewport.h"

#include <algorithm>
#include <cstdint>
#include <optional>

#include "engine_services.h"

namespace UVE::App::Viewports {

namespace {

constexpr univex::render::StudioSettings MakeEntityStudioBackdrop() {
    univex::render::StudioSettings settings{};
    settings.skyZenith = {0.088F, 0.102F, 0.130F};
    settings.skyHorizon = {0.165F, 0.190F, 0.232F};
    settings.ground = {0.118F, 0.136F, 0.168F};
    return settings;
}

constexpr univex::render::PrototypeFloorSettings MakeEntityPrototypeFloor() {
    univex::render::PrototypeFloorSettings settings{};
    settings.tileDark = {0.155F, 0.175F, 0.210F};
    settings.tileLight = {0.205F, 0.230F, 0.272F};
    settings.minorLine = {0.255F, 0.285F, 0.335F};
    settings.majorLine = {0.345F, 0.390F, 0.460F};
    settings.blockLine = {0.470F, 0.545F, 0.655F};
    settings.horizonColor = {0.165F, 0.190F, 0.232F};
    settings.floorRadius = 160.0F;
    settings.fadeStart = 18.0F;
    settings.fadeEnd = 92.0F;
    settings.tileSize = 1.0F;
    return settings;
}

constexpr univex::render::StudioSettings kEntityStudioBackdrop = MakeEntityStudioBackdrop();
constexpr univex::render::PrototypeFloorSettings kEntityPrototypeFloor = MakeEntityPrototypeFloor();

} // namespace

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
    const univex::render::StudioBackdropRenderer* const studio = m_surface.StudioBackdrop();
    if (neutralFallback && studio != nullptr && !m_interaction.IsPreviewingCamera()) {
        studio->DrawBackdrop(m_interaction.Camera(), width, height, kEntityStudioBackdrop);
    } else {
        m_surface.RenderPass().RenderBackground(neutralFallback);
    }
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
        if (studio != nullptr && m_surface.RenderPass().Settings().viewGrid) {
            studio->DrawPrototypeFloor(m_interaction.Camera(), width, height, kEntityPrototypeFloor);
        } else {
            m_surface.RenderPass().RenderGrid(m_interaction.Camera(), width, height);
        }
    }
    if (wantOverlay) {
        m_interaction.RenderSelectionOutlineOrMask(m_surface, overlayState, width, height);
    }
    m_surface.RenderPass().RenderOverlay(m_interaction.Camera(), width, height);
    return m_surface.EndPassAndResolve(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
