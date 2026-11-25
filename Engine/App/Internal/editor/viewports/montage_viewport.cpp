// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "montage_viewport.h"

#include <algorithm>
#include <cstdint>

#include "engine_services.h"

namespace UVE::App::Viewports {

MontageViewport::MontageViewport(Editor::Editor& editor, Core::EngineServices& services)
    : m_editor(editor), m_surface(services) {}

std::uint64_t MontageViewport::Render(const Math::Vector2& availableSize,
                                            Math::Vector2& outUsedSize,
                                            const Editor::Editor::ViewportOverlayState& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrame(width, height)) {
        return 0U;
    }

    m_surface.SetShadingMode(overlayState.shadingMode);
    m_surface.ApplyAxisPalette(m_editor, overlayState);
    m_studioCamera.ApplyStudioFraming(overlayState);
    m_studioCamera.HandleWheelDolly(overlayState.input);

    auto& renderPass = m_surface.RenderPass();
    renderPass.Settings().viewEnvironment = false;
    renderPass.Settings().viewGrid = false;
    ViewportStudioCamera::UploadBoneOverlays(renderPass, overlayState);
    renderPass.SetColliderWireframes({});
    renderPass.SetCameraFrustumMesh({});

    m_surface.ApplyPreviewLighting(overlayState, true);

    const univex::integration::EditorMeshLayerResult meshResult =
        m_surface.RenderSceneMeshLayer(m_studioCamera.Camera(), width, height);

    m_studioCamera.ExportCameraForHost(overlayState, width, height);
    outUsedSize = Math::Vector2{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPass(width, height);
    const univex::render::StudioBackdropRenderer* const studio = m_surface.StudioBackdrop();
    if (studio != nullptr) {
        studio->DrawBackdrop(m_studioCamera.Camera(), width, height);
    } else {
        renderPass.RenderBackground(false);
    }
    m_surface.BlitMeshLayer(meshResult, m_studioCamera.Camera(), width, height);
    if (studio != nullptr) {
        studio->DrawFloor(m_studioCamera.Camera(), width, height);
    }
    renderPass.RenderOverlay(m_studioCamera.Camera(), width, height);
    return m_surface.EndPassAndResolve(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
