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

constexpr float kDefaultStudioYaw = 1.18F;
constexpr float kDefaultStudioPitch = 0.24F;
constexpr float kMinCameraEyeHeight = 0.18F;
constexpr float kMinTargetHeight = 0.35F;
constexpr float kMinOrbitDistance = 0.40F;
constexpr float kMaxOrbitDistance = 32.0F;

} // namespace

VfxViewport::VfxViewport(Editor::Editor& editor, Core::EngineServices& services)
    : m_editor(editor), m_surface(services) {
    auto& camSettings = m_studioCamera.Camera().Settings();
    camSettings.allowFullOrbit = false;
    camSettings.pitchMin = -0.45F;
    camSettings.pitchMax = 1.48F;
    camSettings.distanceMin = kMinOrbitDistance;
    camSettings.distanceMax = kMaxOrbitDistance;
}

void VfxViewport::ClampCameraAboveStudioFloor() {
    auto& camera = m_studioCamera.Camera();
    univex::math::Vec3 target = camera.Target();
    if (!std::isfinite(target.x) || !std::isfinite(target.y) || !std::isfinite(target.z)) {
        target = univex::math::Vec3{0.0F, 1.8F, 0.0F};
    }
    target.y = std::clamp(target.y, kMinTargetHeight, 12.0F);
    camera.SetTarget(target);

    const float distance = std::clamp(camera.Distance(), kMinOrbitDistance, kMaxOrbitDistance);
    camera.SetDistance(distance);

    const float minSinPitch = std::clamp((kMinCameraEyeHeight - target.y) / std::max(distance, 0.1F), -0.95F, 0.95F);
    const float minFloorPitch = std::asin(minSinPitch);
    const float clampedPitch = std::clamp(camera.Pitch(),
                                          std::max(camera.Settings().pitchMin, minFloorPitch),
                                          camera.Settings().pitchMax);
    if (std::fabs(clampedPitch - camera.Pitch()) > 1.0e-5F) {
        camera.SetYawPitch(camera.Yaw(), clampedPitch);
    }
}

void VfxViewport::ApplyStudioFramingAndInput(
    const Editor::Editor::ViewportOverlayState& overlayState,
    const int viewportHeight) {
    using Button = Editor::EditorViewportInput::Button;
    using Key = Editor::EditorViewportInput::Key;
    using Cursor = Editor::EditorViewportInput::Cursor;

    auto& camera = m_studioCamera.Camera();
    camera.SetOrthographic(false);

    const auto resetToStudioTarget = [&]() {
        const univex::math::Vec3 target{
            overlayState.studioTarget[0],
            std::max(kMinTargetHeight, overlayState.studioTarget[1]),
            overlayState.studioTarget[2]};
        const float radius = std::clamp(overlayState.studioRadius, 0.6F, 18.0F);
        camera.CancelAnimation();
        camera.Focus(target, radius);
        camera.SetYawPitch(kDefaultStudioYaw, kDefaultStudioPitch);
        ClampCameraAboveStudioFloor();
    };

    if (overlayState.studioFramingSerial != m_appliedFramingSerial) {
        m_appliedFramingSerial = overlayState.studioFramingSerial;
        resetToStudioTarget();
    }

    const Editor::EditorViewportInput& input = overlayState.input;
    if (input.hovered && !input.wantTextInput &&
        (input.IsKeyPressed(Key::F) || input.IsKeyPressed(Key::Home))) {
        resetToStudioTarget();
    }

    if ((m_orbitDragging || m_panDragging || m_dollyDragging) &&
        !input.IsDown(m_activeDragButton)) {
        m_orbitDragging = false;
        m_panDragging = false;
        m_dollyDragging = false;
    }

    if (!m_orbitDragging && !m_panDragging && !m_dollyDragging && input.hovered) {
        if (input.IsClicked(Button::Middle)) {
            m_panDragging = true;
            m_activeDragButton = Button::Middle;
        } else if (input.IsClicked(Button::Right)) {
            m_activeDragButton = Button::Right;
            if (input.alt) {
                m_dollyDragging = true;
            } else if (input.shift) {
                m_panDragging = true;
            } else {
                m_orbitDragging = true;
            }
        } else if (input.IsClicked(Button::Left)) {
            m_activeDragButton = Button::Left;
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
        input.RequestCursor(Cursor::Hand);
    } else if (m_panDragging) {
        camera.CancelAnimation();
        if (std::fabs(dx) > 0.0F || std::fabs(dy) > 0.0F) {
            camera.Pan(dx, dy, std::max(1, viewportHeight));
        }
        input.RequestCursor(Cursor::ResizeAll);
    } else if (m_dollyDragging) {
        camera.CancelAnimation();
        const float dollyNotches = (-dy + dx) * 0.04F;
        if (std::fabs(dollyNotches) > 0.0F) {
            camera.Dolly(dollyNotches);
        }
        input.RequestCursor(Cursor::ResizeNS);
    }

    if (input.hovered && std::isfinite(input.wheel) && input.wheel != 0.0F) {
        camera.Dolly(input.wheel);
    }
    camera.Update(input.deltaTime);
    ClampCameraAboveStudioFloor();
}

std::uint64_t VfxViewport::Render(const Math::Vector2& availableSize,
                                        Math::Vector2& outUsedSize,
                                        const Editor::Editor::ViewportOverlayState& overlayState) {
    const int width = std::max(1, static_cast<int>(availableSize.x));
    const int height = std::max(1, static_cast<int>(availableSize.y));
    if (!m_surface.PrepareFrame(width, height)) {
        return 0U;
    }

    m_surface.SetShadingMode(overlayState.shadingMode);
    m_surface.ApplyAxisPalette(m_editor, overlayState);
    ApplyStudioFramingAndInput(overlayState, height);

    auto& renderPass = m_surface.RenderPass();
    renderPass.Settings().viewEnvironment = false;
    // The VFX studio viewport uses a real textured PBR studio floor mesh and never draws a grid.
    m_surface.ApplyGridAppearance(m_editor, overlayState, false, univex::render::GridPlane::XZ);
    renderPass.SetBones({});
    ViewportStudioCamera::UploadColliderLines(renderPass, overlayState);
    renderPass.SetCameraFrustumMesh({});

    // Disable the outdoor sun directional preview light so the VFX studio uses its dark studio
    // WorldEnvironment3D and 3-point studio light rig.
    m_surface.ApplyPreviewLighting(overlayState, false);

    Render::IRenderer3D* const renderer =
        m_surface.Services() != nullptr ? &m_surface.Services()->GetRenderer3D() : nullptr;
    if (renderer != nullptr) {
        Render::StudioFloorSettings floorSettings{};
        floorSettings.enabled = true;
        floorSettings.heightY = 0.0F;
        floorSettings.tileScale = 0.45F;
        floorSettings.fadeStartRadiusMeters = 9.0F;
        floorSettings.fadeEndRadiusMeters = 34.0F;
        floorSettings.pivotPosition = Math::Vector3{
            overlayState.studioTarget[0], 0.0F, overlayState.studioTarget[2]};
        floorSettings.baseTint = Math::Vector3{1.0F, 1.0F, 1.0F};
        floorSettings.roughnessScale = 1.0F;
        floorSettings.specularStrength = 0.58F;
        renderer->SetStudioFloor(floorSettings);
    }
    Utilities::ScopeGuard clearStudioFloor{[renderer]() noexcept {
        if (renderer != nullptr) {
            renderer->SetStudioFloor(std::nullopt);
        }
    }};

    const univex::integration::EditorMeshLayerResult meshResult =
        m_surface.RenderSceneMeshLayer(m_studioCamera.Camera(), width, height);

    m_studioCamera.ExportCameraForHost(overlayState, width, height);
    outUsedSize = Math::Vector2{static_cast<float>(width), static_cast<float>(height)};

    const GLint previousFbo = m_surface.BeginPass(width, height);
    glClearColor(0.028F, 0.031F, 0.038F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_surface.BlitMeshLayer(meshResult, m_studioCamera.Camera(), width, height);
    renderPass.RenderOverlay(m_studioCamera.Camera(), width, height);
    return m_surface.EndPassAndResolve(width, height, previousFbo);
}

} // namespace UVE::App::Viewports
