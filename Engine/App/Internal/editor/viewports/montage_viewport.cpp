// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "montage_viewport.h"

#include <algorithm>
#include <cstdint>

#include "engine_services.h"

namespace UVE::App::Viewports {

MontageViewportUVE::MontageViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services)
    : m_editor(editor), m_surface(services) {}

std::uint64_t MontageViewportUVE::RenderUVE(const Math::Vector2UVE& availableSize,
                                            Math::Vector2UVE& outUsedSize,
                                            const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrameUVE(width, height)) {
        return 0U;
    }

    m_surface.SetShadingModeUVE(overlayState.shadingMode);
    m_surface.ApplyAxisPaletteUVE(m_editor, overlayState);
    m_studioCamera.ApplyStudioFramingUVE(overlayState);
    m_studioCamera.HandleWheelDollyUVE(overlayState.input);

    auto& renderPass = m_surface.RenderPassUVE();
    renderPass.Settings().viewEnvironment = false;
    renderPass.Settings().viewGrid = false;
    ViewportStudioCameraUVE::UploadBoneOverlaysUVE(renderPass, overlayState);
    renderPass.SetColliderWireframesUVE({});
    renderPass.SetCameraFrustumMeshUVE({});

    m_surface.ApplyPreviewLightingUVE(overlayState, true);

    const univex::integration::EditorMeshLayerResultUVE meshResult =
        m_surface.RenderSceneMeshLayerUVE(m_studioCamera.CameraUVE(), width, height);

    m_studioCamera.ExportCameraForHostUVE(overlayState, width, height);
    outUsedSize = Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPassUVE(width, height);
    const univex::render::StudioBackdropRenderer* const studio = m_surface.StudioBackdropUVE();
    if (studio != nullptr) {
        studio->DrawBackdrop(m_studioCamera.CameraUVE(), width, height);
    } else {
        renderPass.RenderBackgroundUVE(false);
    }
    m_surface.BlitMeshLayerUVE(meshResult, m_studioCamera.CameraUVE(), width, height);
    if (studio != nullptr) {
        studio->DrawFloor(m_studioCamera.CameraUVE(), width, height);
    }
    renderPass.RenderOverlayUVE(m_studioCamera.CameraUVE(), width, height);
    return m_surface.EndPassAndResolveUVE(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
