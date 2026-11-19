// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "vfx_viewport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include "engine_services.h"
#include "i_renderer_3d.h"
#include "scope_guard.h"
#include "univex/render/infinite_grid_renderer.h"

namespace UVE::App::Viewports {

namespace {

constexpr float kDefaultStudioYawUVE = 1.18F;
constexpr float kDefaultStudioPitchUVE = 0.24F;
constexpr float kMinCameraEyeHeightUVE = 0.18F;
constexpr float kMinTargetHeightUVE = 0.35F;
constexpr float kMinOrbitDistanceUVE = 0.40F;
constexpr float kMaxOrbitDistanceUVE = 32.0F;

} // namespace

VfxViewportUVE::VfxViewportUVE(Editor::EditorUVE& editor, Core::EngineServicesUVE& services)
    : m_editor(editor), m_surface(services) {
    auto& camSettings = m_studioCamera.CameraUVE().Settings();
    camSettings.allowFullOrbit = false;
    camSettings.pitchMin = -0.45F;
    camSettings.pitchMax = 1.48F;
    camSettings.distanceMin = kMinOrbitDistanceUVE;
    camSettings.distanceMax = kMaxOrbitDistanceUVE;
}

void VfxViewportUVE::ClampCameraAboveStudioFloorUVE() {
    auto& camera = m_studioCamera.CameraUVE();
    univex::math::Vec3 target = camera.Target();
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) {
        target = univex::math::Vec3{0.0F, 1.8F, 0.0F};
    }
    target.y = std::clamp(target.y, kMinTargetHeightUVE, 12.0F);
    camera.SetTarget(target);

    const float distance = std::clamp(camera.Distance(), kMinOrbitDistanceUVE, kMaxOrbitDistanceUVE);
    camera.SetDistance(distance);

    const float minSinPitch = std::clamp((kMinCameraEyeHeightUVE - target.y) / std::max(distance, 0.1F), -0.95F, 0.95F);
    const float minFloorPitch = std::asin(minSinPitch);
    const float clampedPitch = std::clamp(camera.Pitch(),
                                          std::max(camera.Settings().pitchMin, minFloorPitch),
                                          camera.Settings().pitchMax);
    if (std::fabs(clampedPitch - camera.Pitch()) > 1.0e-5F) {
        camera.SetYawPitch(camera.Yaw(), clampedPitch);
    }
}

void VfxViewportUVE::ApplyStudioFramingAndInputUVE(
    const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
    const int viewportHeight) {
    using ButtonUVE = Editor::EditorViewportInputUVE::ButtonUVE;
    using KeyUVE = Editor::EditorViewportInputUVE::KeyUVE;
    using CursorUVE = Editor::EditorViewportInputUVE::CursorUVE;

    auto& camera = m_studioCamera.CameraUVE();
    camera.SetOrthographic(false);

    const auto resetToStudioTarget = [&]() {
        const univex::math::Vec3 target{
            overlayState.studioTarget[0],
            std::max(kMinTargetHeightUVE, overlayState.studioTarget[1]),
            overlayState.studioTarget[2]};
        const float radius = std::clamp(overlayState.studioRadius, 0.6F, 18.0F);
        camera.CancelAnimation();
        camera.Focus(target, radius);
        camera.SetYawPitch(kDefaultStudioYawUVE, kDefaultStudioPitchUVE);
        ClampCameraAboveStudioFloorUVE();
    };

    if (overlayState.studioFramingSerial != m_appliedFramingSerial) {
        m_appliedFramingSerial = overlayState.studioFramingSerial;
        resetToStudioTarget();
    }

    const Editor::EditorViewportInputUVE& input = overlayState.input;
    if (input.hovered && !input.wantTextInput &&
        (input.IsKeyPressedUVE(KeyUVE::F) || input.IsKeyPressedUVE(KeyUVE::Home))) {
        resetToStudioTarget();
    }

    if ((m_orbitDragging || m_panDragging || m_dollyDragging) &&
        !input.IsDownUVE(m_activeDragButton)) {
        m_orbitDragging = false;
        m_panDragging = false;
        m_dollyDragging = false;
    }

    if (!m_orbitDragging && !m_panDragging && !m_dollyDragging && input.hovered) {
        if (input.IsClickedUVE(ButtonUVE::Middle)) {
            m_panDragging = true;
            m_activeDragButton = ButtonUVE::Middle;
        } else if (input.IsClickedUVE(ButtonUVE::Right)) {
            m_activeDragButton = ButtonUVE::Right;
            if (input.alt) {
                m_dollyDragging = true;
            } else if (input.shift) {
                m_panDragging = true;
            } else {
                m_orbitDragging = true;
            }
        } else if (input.IsClickedUVE(ButtonUVE::Left)) {
            m_activeDragButton = ButtonUVE::Left;
            if (input.shift) {
                m_panDragging = true;
            } else {
                m_orbitDragging = true;
            }
        }
    }

    const float dx = std::isfinite(input.mouseDelta.x) ? input.mouseDelta.x : 0.0F;
    const float dy = std::isfinite(input.mouseDelta.y) ? input.mouseDelta.y : 0.0F;
    if (m_orbitDragging) {
        camera.CancelAnimation();
        if (std::fabs(dx) > 0.0F || std::fabs(dy) > 0.0F) {
            camera.Orbit(dx, dy);
        }
        input.RequestCursorUVE(CursorUVE::Hand);
    } else if (m_panDragging) {
        camera.CancelAnimation();
        if (std::fabs(dx) > 0.0F || std::fabs(dy) > 0.0F) {
            camera.Pan(dx, dy, std::max(1, viewportHeight));
        }
        input.RequestCursorUVE(CursorUVE::ResizeAll);
    } else if (m_dollyDragging) {
        camera.CancelAnimation();
        const float dollyNotches = (-dy + dx) * 0.04F;
        if (std::fabs(dollyNotches) > 0.0F) {
            camera.Dolly(dollyNotches);
        }
        input.RequestCursorUVE(CursorUVE::ResizeNS);
    }

    if (input.hovered && std::isfinite(input.wheel) && input.wheel != 0.0F) {
        camera.Dolly(input.wheel);
    }
    camera.Update(input.deltaTime);
    ClampCameraAboveStudioFloorUVE();
}

std::uint64_t VfxViewportUVE::RenderUVE(const Math::Vector2UVE& availableSize,
                                        Math::Vector2UVE& outUsedSize,
                                        const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrameUVE(width, height)) {
        return 0U;
    }

    m_surface.SetShadingModeUVE(overlayState.shadingMode);
    m_surface.ApplyAxisPaletteUVE(m_editor, overlayState);
    ApplyStudioFramingAndInputUVE(overlayState, height);

    auto& renderPass = m_surface.RenderPassUVE();
    renderPass.Settings().viewEnvironment = false;
    // The VFX studio viewport uses a real textured PBR studio floor mesh and never draws a grid.
    m_surface.ApplyGridAppearanceUVE(m_editor, overlayState, false, univex::render::GridPlane::XZ);
    renderPass.SetBonesUVE({});
    ViewportStudioCameraUVE::UploadColliderLinesUVE(renderPass, overlayState);
    renderPass.SetCameraFrustumMeshUVE({});

    // Disable the outdoor sun directional preview light so the VFX studio uses its dark studio
    // WorldEnvironment3D and 3-point studio light rig.
    m_surface.ApplyPreviewLightingUVE(overlayState, false);

    Render::IRenderer3DUVE* const renderer =
        m_surface.ServicesUVE() != nullptr ? &m_surface.ServicesUVE()->GetRenderer3DUVE() : nullptr;
    if (renderer != nullptr) {
        Render::StudioFloorSettingsUVE floorSettings{};
        floorSettings.enabledUVE = true;
        floorSettings.heightYUVE = 0.0F;
        floorSettings.tileScaleUVE = 0.45F;
        floorSettings.fadeStartRadiusMetersUVE = 9.0F;
        floorSettings.fadeEndRadiusMetersUVE = 34.0F;
        floorSettings.pivotPositionUVE = Math::Vector3UVE{
            overlayState.studioTarget[0], 0.0F, overlayState.studioTarget[2]};
        floorSettings.baseTintUVE = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
        floorSettings.roughnessScaleUVE = 1.0F;
        floorSettings.specularStrengthUVE = 0.58F;
        renderer->SetStudioFloorUVE(floorSettings);
    }
    Utilities::ScopeGuardUVE clearStudioFloor{[renderer]() noexcept {
        if (renderer != nullptr) {
            renderer->SetStudioFloorUVE(std::nullopt);
        }
    }};

    const univex::integration::EditorMeshLayerResultUVE meshResult =
        m_surface.RenderSceneMeshLayerUVE(m_studioCamera.CameraUVE(), width, height);

    m_studioCamera.ExportCameraForHostUVE(overlayState, width, height);
    outUsedSize = Math::Vector2UVE{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPassUVE(width, height);
    glClearColor(0.028F, 0.031F, 0.038F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_surface.BlitMeshLayerUVE(meshResult, m_studioCamera.CameraUVE(), width, height);
    renderPass.RenderOverlayUVE(m_studioCamera.CameraUVE(), width, height);
    return m_surface.EndPassAndResolveUVE(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
