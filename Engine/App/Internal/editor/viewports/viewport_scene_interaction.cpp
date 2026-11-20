// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "viewport_scene_interaction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "camera_component.h"
#include "engine_services.h"
#include "gl_render_device.h"
#include "i_entity_manager.h"
#include "integration/entity_picker.h"
#include "integration/math_conversions.h"
#include "integration/selection_outline_geometry.h"
#include "logging_macros.h"
#include "univex/gizmo/bone_shape.h"
#include "univex/render/infinite_grid_renderer.h"
#include "univex/render/selection_outline.h"
#include "viewport_object_gizmos.h"
#include "viewport_surface.h"
#include "world_transform_component.h"

namespace UVE::App::Viewports {
namespace {

using VpInputUVE = Editor::EditorViewportInputUVE;
using VpBtnUVE = VpInputUVE::ButtonUVE;
using VpKeyUVE = VpInputUVE::KeyUVE;

// How far the pointer may travel (in view pixels) and still count as a click rather than a drag.
constexpr float kNavClickSlopPixelsUVE = 4.0F;

// Scene depth for the PRISM shell's overlays: each output texel is the nearest surface of a 2x2
// block of the renderer's depth target, as the distance along the editor view's forward axis from
// its eye (world units), or 1e30 where nothing was drawn.
constexpr std::string_view kHostDepthFragmentShaderUVE = R"(#version 330 core
uniform sampler2D uDepth;
uniform mat4 uInvViewProj;
uniform vec3 uEye;
uniform vec3 uForward;
uniform vec2 uOutSize;
out vec4 FragColor;
void main() {
    ivec2 size = textureSize(uDepth, 0);
    vec2 uv = gl_FragCoord.xy / uOutSize;
    ivec2 base = ivec2(uv * vec2(size));
    float d = 1.0;
    float u = 0.0;
    float v = 0.0;
    for (int y = -1; y <= 0; ++y) {
        for (int x = -1; x <= 0; ++x) {
            ivec2 p = clamp(base + ivec2(x, y), ivec2(0), size - 1);
            float s = texelFetch(uDepth, p, 0).r;
            if (s < d) {
                d = s;
                u = (float(p.x) + 0.5) / float(size.x);
                v = (float(p.y) + 0.5) / float(size.y);
            }
        }
    }
    if (d >= 0.999999) {
        FragColor = vec4(1.0e30);
        return;
    }
    vec4 world = uInvViewProj * vec4(u * 2.0 - 1.0, v * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    world /= world.w;
    FragColor = vec4(dot(world.xyz - uEye, uForward));
}
)";

} // namespace

ViewportSceneInteractionUVE::ViewportSceneInteractionUVE(Editor::EditorUVE& editor,
                                                         Scene::IEntityManagerUVE& entityManager)
    : m_editor(editor), m_entityManager(entityManager) {
    m_camera.Settings().allowFullOrbit = true;
}

ViewportSceneInteractionUVE::~ViewportSceneInteractionUVE() {
    if (m_hostDepthPbo[0] != 0U || m_hostDepthPbo[1] != 0U) {
        glDeleteBuffers(2, m_hostDepthPbo.data());
        m_hostDepthPbo = {};
    }
    if (m_hostDepthTexture != 0U) {
        glDeleteTextures(1, &m_hostDepthTexture);
        m_hostDepthTexture = 0U;
    }
    if (m_hostDepthFbo != 0U) {
        glDeleteFramebuffers(1, &m_hostDepthFbo);
        m_hostDepthFbo = 0U;
    }
}

ViewportCameraPoseUVE ViewportSceneInteractionUVE::CapturePoseUVE() const noexcept {
    return ViewportCameraPoseUVE{m_camera.Target(), m_camera.Yaw(), m_camera.Pitch(), m_camera.Distance(),
                                 m_camera.IsOrthographic()};
}

void ViewportSceneInteractionUVE::RestorePoseUVE(const ViewportCameraPoseUVE& pose) {
    m_camera.CancelAnimation();
    m_camera.SetTarget(pose.target);
    m_camera.SetYawPitch(pose.yaw, pose.pitch);
    m_camera.SetDistance(pose.distance);
    m_camera.SetOrthographic(pose.orthographic);
}

univex::math::Vec3 ViewportSceneInteractionUVE::NamedViewDirectionUVE(const Editor::EditorUVE::ViewportViewUVE view) {
    using ViewportViewUVE = Editor::EditorUVE::ViewportViewUVE;
    switch (view) {
        case ViewportViewUVE::Top: return {0.0F, 1.0F, 0.0F};
        case ViewportViewUVE::Bottom: return {0.0F, -1.0F, 0.0F};
        case ViewportViewUVE::Front: return {0.0F, 0.0F, 1.0F};
        case ViewportViewUVE::Back: return {0.0F, 0.0F, -1.0F};
        case ViewportViewUVE::Right: return {1.0F, 0.0F, 0.0F};
        case ViewportViewUVE::Left: return {-1.0F, 0.0F, 0.0F};
        case ViewportViewUVE::User: break;
    }
    return {0.0F, 0.0F, 0.0F};
}

void ViewportSceneInteractionUVE::ApplyNamedViewUVE(const Editor::EditorUVE::ViewportViewUVE view) {
    using ViewportViewUVE = Editor::EditorUVE::ViewportViewUVE;
    constexpr float kHalfPi = 1.5707963F;
    const auto& limits = m_camera.Settings();
    switch (view) {
        case ViewportViewUVE::Top: m_camera.SnapToYawPitch(kHalfPi, limits.pitchMax); return;
        case ViewportViewUVE::Bottom: m_camera.SnapToYawPitch(kHalfPi, limits.pitchMin); return;
        case ViewportViewUVE::User: return;
        default: m_camera.SnapToDirection(NamedViewDirectionUVE(view)); return;
    }
}

void ViewportSceneInteractionUVE::FocusCameraOnEntityUVE(const Scene::EntityUVE entity) {
    if (const std::optional<Editor::EditorViewportBookmarkUVE> markerView =
            m_editor.ComposeMarker3DFocusBookmarkUVE(entity)) {
        m_editor.NotifyViewportOrbitedUVE();
        m_camera.CancelAnimation();
        m_camera.SetTarget(univex::integration::FromUveVector3UVE(markerView->target));
        m_camera.SetYawPitch(markerView->yawRadians, markerView->pitchRadians);
        m_camera.SetDistance(markerView->distance);
    } else if (const std::optional<Math::Vector3UVE> focusTarget = m_editor.ResolveEntityFocusTargetUVE(entity)) {
        m_camera.CancelAnimation();
        m_camera.SetTarget(univex::integration::FromUveVector3UVE(*focusTarget));
    }
}

void ViewportSceneInteractionUVE::ApplyOverlayStateUVE(ViewportSurfaceUVE& surface,
                                                       const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                                       const bool suppressGrid) {
    surface.SetShadingModeUVE(overlayState.shadingMode);
    auto& renderPass = surface.RenderPassUVE();
    auto& settings = renderPass.Settings();
    settings.projection = overlayState.orthographic ? univex::viewport::ProjectionMode::Orthographic
                                                    : univex::viewport::ProjectionMode::Perspective;
    settings.viewEnvironment = true;
    m_camera.SetOrthographic(overlayState.orthographic);

    if (overlayState.viewRequestSerial != m_appliedViewRequestSerial) {
        m_appliedViewRequestSerial = overlayState.viewRequestSerial;
        ApplyNamedViewUVE(overlayState.view);
    }
    if (overlayState.focusRequestSerial != m_appliedFocusRequestSerial) {
        m_appliedFocusRequestSerial = overlayState.focusRequestSerial;
        FocusCameraOnEntityUVE(overlayState.focusEntity);
    }

    const univex::math::Vec3 viewAxis = NamedViewDirectionUVE(overlayState.view);
    surface.ApplyGridAppearanceUVE(m_editor, overlayState, overlayState.gridVisible && !suppressGrid,
                                   univex::render::GridPlaneFacing(viewAxis.x, viewAxis.y, viewAxis.z));

    constexpr float kSelectionOutlineVisualScaleUVE = 0.20F;
    renderPass.SetSelectionOutlineUVE(univex::render::SelectionOutlineSettings{
        overlayState.selectionOutlineVisible, overlayState.selectionOutlineColor.r, overlayState.selectionOutlineColor.g,
        overlayState.selectionOutlineColor.b,
        overlayState.selectionOutlineThickness * kSelectionOutlineVisualScaleUVE});

    m_pointerOverOverlay = overlayState.pointerOverOverlay;
    surface.ApplyAxisPaletteUVE(m_editor, overlayState);

    const auto toVec3 = [](const std::array<float, 3>& value) {
        return univex::math::Vec3{value[0], value[1], value[2]};
    };

    std::vector<univex::gizmo::BoneOverlayUVE> bones;
    bones.reserve(overlayState.bones.size());
    for (const auto& bone : overlayState.bones) {
        bones.push_back(univex::gizmo::BoneOverlayUVE{toVec3(bone.head), toVec3(bone.tail), toVec3(bone.side),
                                                      bone.hasLink, toVec3(bone.linkFrom), bone.skeletonSelected,
                                                      bone.boneSelected, bone.hasColour, toVec3(bone.colour),
                                                      bone.jointOnly, bone.helper});
    }
    if (overlayState.hostDrawsGizmos) {
        const univex::math::Vec3 view = univex::math::Normalize(m_camera.Target() - m_camera.Eye());
        m_hostBonesMesh = univex::gizmo::BuildBoneMeshUVE(bones, view);
        renderPass.SetBonesUVE({});
    } else {
        m_hostBonesMesh.Clear();
        renderPass.SetBonesUVE(std::move(bones));
    }

    constexpr float kColliderWireRedUVE = 0.20F;
    constexpr float kColliderWireGreenUVE = 0.95F;
    constexpr float kColliderWireBlueUVE = 0.45F;
    constexpr float kColliderWireWidthPixelsUVE = 1.5F;
    constexpr float kColliderHighlightWidthPixelsUVE = 2.5F;
    const univex::math::Vec3 wireColour{kColliderWireRedUVE, kColliderWireGreenUVE, kColliderWireBlueUVE};
    const univex::math::Vec3 highlightColour{0.70F, 1.00F, 0.82F};
    std::vector<univex::gizmo::GizmoLine> colliderLines;
    colliderLines.reserve(overlayState.colliderLines.size());
    for (const auto& line : overlayState.colliderLines) {
        colliderLines.push_back(univex::gizmo::GizmoLine{toVec3(line.from), toVec3(line.to),
                                                         line.highlighted ? highlightColour : wireColour,
                                                         line.highlighted ? kColliderHighlightWidthPixelsUVE
                                                                          : kColliderWireWidthPixelsUVE});
    }
    if (overlayState.hostDrawsGizmos) {
        m_hostColliderLines = std::move(colliderLines);
        renderPass.SetColliderWireframesUVE({});
    } else {
        m_hostColliderLines.clear();
        renderPass.SetColliderWireframesUVE(std::move(colliderLines));
    }
}

std::optional<Scene::EntityUVE> ViewportSceneInteractionUVE::UpdateCameraPreviewFlightUVE(
    const bool allowPreview,
    const Editor::EditorViewportInputUVE& input) {
    const Scene::EntityUVE previewCamera = m_editor.GetPreviewCameraUVE();
    const bool wantPreview = allowPreview && (previewCamera != Scene::kInvalidEntityUVE);
    if (wantPreview && !m_previewing) {
        m_poseBeforePreview = CapturePoseUVE();
        m_fovBeforePreview = m_camera.Settings().fovYRadians;
        m_previewing = true;
        if (const std::optional<CameraFlightPoseUVE> into = CameraEntityPoseUVE(previewCamera)) {
            m_camera.CancelAnimation();
            m_flight = CameraFlightUVE{CurrentFlightPoseUVE(), *into, 0.0F, true};
            m_camera.SetOrthographic(false);
        }
    } else if (!wantPreview && m_previewing) {
        if (m_poseBeforePreview.has_value()) {
            m_camera.CancelAnimation();
            const CameraFlightPoseUVE back{m_poseBeforePreview->target, m_poseBeforePreview->yaw,
                                           m_poseBeforePreview->pitch, m_poseBeforePreview->distance,
                                           m_fovBeforePreview};
            const std::optional<CameraFlightPoseUVE> from =
                m_flight.has_value() ? std::optional<CameraFlightPoseUVE>{CurrentFlightPoseUVE()}
                                     : CameraEntityPoseUVE(m_lastPreviewCamera);
            if (from.has_value()) {
                ApplyFlightPoseUVE(*from);
                m_flight = CameraFlightUVE{*from, back, 0.0F, false};
            } else {
                m_flight.reset();
                ApplyFlightPoseUVE(back);
            }
            m_camera.SetOrthographic(m_poseBeforePreview->orthographic);
            m_poseBeforePreview.reset();
        }
        m_previewing = false;
    }

    if (previewCamera != Scene::kInvalidEntityUVE) {
        m_lastPreviewCamera = previewCamera;
    }

    if (m_flight.has_value()) {
        m_flight->t = std::min(1.0F, m_flight->t + input.deltaTime / kCameraFlightSecondsUVE);
        const float t = m_flight->t;
        const float e = t * t * (3.0F - 2.0F * t);
        const CameraFlightPoseUVE& a = m_flight->from;
        const CameraFlightPoseUVE& b = m_flight->to;
        float yawDelta = b.yaw - a.yaw;
        while (yawDelta > 3.14159265F) {
            yawDelta -= 6.2831853F;
        }
        while (yawDelta < -3.14159265F) {
            yawDelta += 6.2831853F;
        }
        ApplyFlightPoseUVE(CameraFlightPoseUVE{a.target + (b.target - a.target) * e, a.yaw + yawDelta * e,
                                               a.pitch + (b.pitch - a.pitch) * e,
                                               a.distance + (b.distance - a.distance) * e,
                                               a.fovY + (b.fovY - a.fovY) * e});
        if (t >= 1.0F) {
            if (!m_flight->entering) {
                m_camera.Settings().fovYRadians = m_fovBeforePreview;
            }
            m_flight.reset();
        }
    }

    if (wantPreview && !m_flight.has_value()) {
        return previewCamera;
    }
    return std::nullopt;
}

void ViewportSceneInteractionUVE::UpdateInteractionUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                                       const int width,
                                                       const int height,
                                                       const bool skipAuthoringNavigation) {
    UpdateSelectionGizmoUVE();
    if (overlayState.hostDrawsGizmos &&
        (overlayState.orbitRequestX != 0.0F || overlayState.orbitRequestY != 0.0F)) {
        m_camera.Orbit(overlayState.orbitRequestX, overlayState.orbitRequestY);
        m_editor.NotifyViewportOrbitedUVE();
    }

    const VpInputUVE& input = overlayState.input;
    constexpr bool kPointerTaken = false;
    UpdateSelectionFromMouseUVE(input, width, height, kPointerTaken);
    UpdateEntityContextToolbarFromMouseUVE(input, width, height, kPointerTaken);

    if (!m_previewing && !skipAuthoringNavigation && !UpdateFlyCameraUVE(input, kPointerTaken)) {
        UpdateOrbitPanZoomFromMouseUVE(input, height, kPointerTaken);
        UpdateViewportViewHotkeysUVE(input);
        UpdateViewportBookmarkHotkeysUVE(input);
    }
}

void ViewportSceneInteractionUVE::AdvanceCameraAndPublishFocusUVE(const Editor::EditorViewportInputUVE& input) {
    m_camera.Update(input.deltaTime);
    const univex::math::Vec3 cameraFocus = m_camera.Target();
    m_editor.SetViewportCameraFocusUVE(Math::Vector3UVE{cameraFocus.x, cameraFocus.y, cameraFocus.z});
}

void ViewportSceneInteractionUVE::UpdateObjectGizmoOverlayUVE(
    ViewportSurfaceUVE& surface,
    const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
    const int width,
    const int height,
    const std::optional<Scene::EntityUVE> lookThroughCamera,
    const bool wantOverlay) {
    auto& renderPass = surface.RenderPassUVE();
    if (wantOverlay) {
        const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
        const Scene::EntityUVE lookThrough = lookThroughCamera.value_or(Scene::kInvalidEntityUVE);
        const std::size_t entityCount = m_entityManager.GetEntityCountUVE();

        m_overlayRefreshTimerSeconds += overlayState.input.deltaTime;
        const bool timerElapsed = m_overlayRefreshTimerSeconds >= 0.10F;
        const bool stateChanged = !m_cachedOverlayActive ||
                                  std::fabs(aspect - m_cachedOverlayAspect) > 0.001F ||
                                  lookThrough != m_cachedLookThroughCamera ||
                                  entityCount != m_cachedOverlayEntityCount;
        if (stateChanged || timerElapsed) {
            m_overlayRefreshTimerSeconds = 0.0F;
            m_cachedOverlayActive = true;
            m_cachedOverlayAspect = aspect;
            m_cachedLookThroughCamera = lookThrough;
            m_cachedOverlayEntityCount = entityCount;

            ViewportObjectGizmoBuildResultUVE built = ViewportObjectGizmosUVE::BuildAllObjectGizmosUVE(
                m_entityManager, lookThrough, aspect, overlayState.hostDrawsGizmos);
            if (overlayState.hostDrawsGizmos) {
                m_hostOverlayMesh = std::move(built.mesh);
                m_hostOverlayOccludedFrom = built.colliderLinesStart;
                renderPass.SetCameraFrustumMeshUVE({});
            } else {
                m_hostOverlayMesh.Clear();
                renderPass.SetCameraFrustumMeshUVE(std::move(built.mesh));
            }
        }
    } else {
        m_hostOverlayMesh.Clear();
        if (m_cachedOverlayActive) {
            renderPass.SetCameraFrustumMeshUVE({});
            m_cachedOverlayActive = false;
        }
    }
}

void ViewportSceneInteractionUVE::ExportViewForHostUVE(const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                                       const int width,
                                                       const int height,
                                                       const bool wantOverlay) {
    auto& cam = overlayState.viewCamera;
    const univex::math::Vec3 eye = m_camera.Eye();
    const univex::math::Vec3 forward = univex::math::Normalize(m_camera.Target() - eye);
    const univex::math::Vec3 right = univex::math::Normalize(univex::math::Cross(forward, m_camera.Up()));
    const univex::math::Vec3 up = univex::math::Normalize(univex::math::Cross(right, forward));
    cam.valid = true;
    cam.eye = {eye.x, eye.y, eye.z};
    cam.forward = {forward.x, forward.y, forward.z};
    cam.right = {right.x, right.y, right.z};
    cam.up = {up.x, up.y, up.z};
    cam.fovYRadians = m_camera.Settings().fovYRadians;
    cam.nearPlane = m_camera.NearPlane();
    cam.orthographic = m_camera.IsOrthographic();
    cam.orthographicHalfHeight = m_camera.OrthographicHalfHeight();
    cam.width = static_cast<float>(width);
    cam.height = static_cast<float>(height);

    overlayState.gizmoPivotValid = m_gizmoPivot.has_value();
    if (m_gizmoPivot.has_value()) {
        overlayState.gizmoPivot = {m_gizmoPivot->x, m_gizmoPivot->y, m_gizmoPivot->z};
    }
    overlayState.gizmoLines.clear();
    overlayState.gizmoTriangles.clear();
    if (!overlayState.hostDrawsGizmos || !wantOverlay) {
        return;
    }

    const auto a3 = [](const univex::math::Vec3& v) { return std::array<float, 3>{v.x, v.y, v.z}; };
    const auto append = [&](const univex::gizmo::GizmoMesh& mesh) {
        for (const univex::gizmo::GizmoLine& line : mesh.lines) {
            overlayState.gizmoLines.push_back({a3(line.a), a3(line.b), a3(line.color), line.widthPx});
        }
        for (const univex::gizmo::GizmoTriangle& tri : mesh.triangles) {
            overlayState.gizmoTriangles.push_back({a3(tri.a), a3(tri.b), a3(tri.c), a3(tri.color), tri.alpha});
        }
    };
    append(m_hostOverlayMesh);
    for (std::size_t i = m_hostOverlayOccludedFrom; i < m_hostOverlayMesh.lines.size(); ++i) {
        overlayState.gizmoLines[overlayState.gizmoLines.size() - m_hostOverlayMesh.lines.size() + i].occluded = true;
    }
    append(m_hostBonesMesh);
    for (const univex::gizmo::GizmoLine& line : m_hostColliderLines) {
        overlayState.gizmoLines.push_back({a3(line.a), a3(line.b), a3(line.color), line.widthPx, true});
    }
}

void ViewportSceneInteractionUVE::ExportGridForHostUVE(const ViewportSurfaceUVE& surface,
                                                       const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
                                                       const bool want) {
    auto& out = overlayState.hostGrid;
    const univex::render::GridSettings& grid = surface.RenderPassUVE().Grid().Settings();
    out.visible = want && surface.RenderPassUVE().Settings().viewGrid;
    out.plane = static_cast<int>(grid.plane);
    out.baseSpacing = grid.baseSpacing;
    out.targetCellPixels = grid.targetCellPixels;
    out.lineWidth = grid.lineWidthPixels;
    out.axisWidth = grid.axisWidthPixels;
    const auto rgb = [](const univex::render::GridColor& c) { return std::array<float, 3>{c.r, c.g, c.b}; };
    out.thin = rgb(grid.thinColor);
    out.mid = rgb(grid.midColor);
    out.thick = rgb(grid.thickColor);
    out.thinIntensity = grid.thinIntensity;
    out.midIntensity = grid.midIntensity;
    out.thickIntensity = grid.thickIntensity;
    out.axis = {rgb(grid.axisColorX), rgb(grid.axisColorY), rgb(grid.axisColorZ)};
    out.fadeStart = grid.fadeStartDistanceScale;
    out.fadeEnd = grid.fadeEndDistanceScale;
    out.opacity = grid.opacity;
}

void ViewportSceneInteractionUVE::ExportSceneDepthForHostUVE(
    ViewportSurfaceUVE& surface,
    const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
    const int width,
    const int height) {
    overlayState.sceneDepth.clear();
    overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;

    Core::EngineServicesUVE& services = surface.MutableServicesUVE();
    const Render::SceneDepthExportUVE depth = services.GetRenderer3DUVE().GetLastSceneDepthUVE();
    auto* const glDevice = dynamic_cast<Render::GlRenderDeviceUVE*>(&services.GetRenderDeviceUVE());
    if (!depth.valid || glDevice == nullptr || depth.width == 0U || depth.height == 0U ||
        !EnsureHostDepthUVE(surface, width, height)) {
        return;
    }
    const GLuint depthTexture = static_cast<GLuint>(glDevice->GetNativeTextureIdUVE(depth.depthTexture));
    const GLuint fullscreenVao = surface.FullscreenVaoUVE();
    if (depthTexture == 0U || fullscreenVao == 0U) {
        return;
    }

    GLint previousDrawFbo = 0;
    GLint previousReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFbo);
    std::array<GLint, 4> previousViewport{};
    glGetIntegerv(GL_VIEWPORT, previousViewport.data());
    const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean blend = glIsEnabled(GL_BLEND);

    glBindFramebuffer(GL_FRAMEBUFFER, m_hostDepthFbo);
    glViewport(0, 0, m_hostDepthWidth, m_hostDepthHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    m_hostDepthProgram->Use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    glUniform1i(m_hostDepthProgram->UniformLocation("uDepth"), 0);
    glUniformMatrix4fv(m_hostDepthProgram->UniformLocation("uInvViewProj"), 1, GL_TRUE,
                       &depth.inverseViewProjection.m[0][0]);
    const univex::math::Vec3 eye = m_camera.Eye();
    const univex::math::Vec3 forward = univex::math::Normalize(m_camera.Target() - eye);
    glUniform3f(m_hostDepthProgram->UniformLocation("uEye"), eye.x, eye.y, eye.z);
    glUniform3f(m_hostDepthProgram->UniformLocation("uForward"), forward.x, forward.y, forward.z);
    glUniform2f(m_hostDepthProgram->UniformLocation("uOutSize"), static_cast<float>(m_hostDepthWidth),
                static_cast<float>(m_hostDepthHeight));
    glBindVertexArray(fullscreenVao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Double pixel-pack buffer readback avoids CPU stalls while keeping gizmo occlusion responsive.
    const std::size_t count = static_cast<std::size_t>(m_hostDepthWidth) * static_cast<std::size_t>(m_hostDepthHeight);
    const auto bytes = static_cast<GLsizeiptr>(count * sizeof(float));
    if (m_hostDepthPbo[0] == 0U) {
        glGenBuffers(2, m_hostDepthPbo.data());
    }
    for (std::size_t i = 0U; i < 2U; ++i) {
        if (m_hostDepthPboBytes[i] != bytes) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hostDepthPbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
            m_hostDepthPboBytes[i] = bytes;
            m_hostDepthPboFilled[i] = false;
        }
    }
    const std::size_t write = m_hostDepthPboNext;
    const std::size_t read = 1U - write;
    GLint packAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_hostDepthFbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hostDepthPbo[write]);
    glReadPixels(0, 0, m_hostDepthWidth, m_hostDepthHeight, GL_RED, GL_FLOAT, nullptr);
    m_hostDepthPboFilled[write] = true;
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    if (m_hostDepthPboFilled[read]) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hostDepthPbo[read]);
        if (const auto* const rows =
                static_cast<const float*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT))) {
            overlayState.sceneDepth.resize(count);
            const std::size_t stride = static_cast<std::size_t>(m_hostDepthWidth);
            for (int y = 0; y < m_hostDepthHeight; ++y) {
                std::copy_n(rows + static_cast<std::size_t>(m_hostDepthHeight - 1 - y) * stride, stride,
                            overlayState.sceneDepth.data() + static_cast<std::size_t>(y) * stride);
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            overlayState.sceneDepthWidth = m_hostDepthWidth;
            overlayState.sceneDepthHeight = m_hostDepthHeight;
        }
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    m_hostDepthPboNext = read;

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFbo));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    if (depthTest == GL_TRUE) {
        glEnable(GL_DEPTH_TEST);
    }
    if (blend == GL_TRUE) {
        glEnable(GL_BLEND);
    }
}

void ViewportSceneInteractionUVE::RenderSelectionOutlineOrMaskUVE(
    ViewportSurfaceUVE& surface,
    const Editor::EditorUVE::ViewportOverlayStateUVE& overlayState,
    const int width,
    const int height) {
    const auto& selected = m_editor.GetSelectedEntitiesUVE();
    const auto active = m_editor.GetSelectedEntityUVE();
    if (selected.empty()) {
        m_cachedSelectionTriangles.clear();
        m_lastSelectedEntities.clear();
        m_lastSelectedEntity = Scene::kInvalidEntityUVE;
        m_lastSelectedSnapshots.clear();
        return;
    }

    bool needsRebuild = false;
    if (selected != m_lastSelectedEntities || active != m_lastSelectedEntity) {
        needsRebuild = true;
    } else if (AnySelectedEntityMovedUVE()) {
        needsRebuild = true;
    }

    if (needsRebuild) {
        m_cachedSelectionTriangles =
            univex::integration::CollectSelectionOutlineTrianglesUVE(m_entityManager, selected, active);
        m_lastSelectedEntities = selected;
        m_lastSelectedEntity = active;
        m_lastSelectedSnapshots.clear();
        m_lastSelectedSnapshots.reserve(selected.size());
        for (const auto ent : selected) {
            if (m_entityManager.IsAliveUVE(ent) &&
                m_entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(ent)) {
                const auto& wt = m_entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(ent);
                m_lastSelectedSnapshots.push_back({ent, wt.worldPosition, wt.worldRotation});
            }
        }
    }

    if (m_cachedSelectionTriangles.empty()) {
        return;
    }

    auto& renderPass = surface.RenderPassUVE();
    if (overlayState.hostDrawsGizmos) {
        const univex::render::SelectionOutlineSettings& outline = renderPass.SelectionOutlineSettingsUVE();
        std::uint64_t maskKey = 1469598103934665603ULL;
        const auto mixKey = [&maskKey](const void* data, const std::size_t size) {
            const auto* p = static_cast<const unsigned char*>(data);
            for (std::size_t i = 0U; i < size; ++i) {
                maskKey = (maskKey ^ p[i]) * 1099511628211ULL;
            }
        };
        const univex::math::Mat4 viewProjection =
            m_camera.ViewProjection(static_cast<float>(width) / static_cast<float>(std::max(height, 1)));
        mixKey(&viewProjection, sizeof(viewProjection));
        const int sizes[2] = {width, height};
        mixKey(sizes, sizeof(sizes));
        mixKey(m_cachedSelectionTriangles.data(),
               m_cachedSelectionTriangles.size() * sizeof(m_cachedSelectionTriangles.front()));

        bool maskRead = false;
        if (maskKey == m_lastMaskKey && !m_lastMask.empty()) {
            overlayState.selectionMask = m_lastMask;
            maskRead = true;
        } else if (renderPass.ReadSelectionMaskUVE(m_camera, width, height, m_cachedSelectionTriangles,
                                                   overlayState.selectionMask)) {
            m_lastMask = overlayState.selectionMask;
            m_lastMaskKey = maskKey;
            maskRead = true;
        }
        if (maskRead) {
            overlayState.selectionMaskWidth = width;
            overlayState.selectionMaskHeight = height;
            overlayState.selectionOutlineRadius = static_cast<int>(std::lround(outline.thicknessPixels));
            overlayState.selectionOutlineRgb = {outline.r, outline.g, outline.b};
        }
        glBindFramebuffer(GL_FRAMEBUFFER, surface.MsaaFboUVE());
        glViewport(0, 0, width, height);
    } else {
        renderPass.RenderSelectionOutlineUVE(m_camera, width, height, m_cachedSelectionTriangles);
    }
}

ViewportSceneInteractionUVE::CameraFlightPoseUVE ViewportSceneInteractionUVE::CurrentFlightPoseUVE() const {
    return CameraFlightPoseUVE{m_camera.Target(), m_camera.Yaw(), m_camera.Pitch(), m_camera.Distance(),
                               m_camera.Settings().fovYRadians};
}

void ViewportSceneInteractionUVE::ApplyFlightPoseUVE(const CameraFlightPoseUVE& pose) {
    m_camera.SetTarget(pose.target);
    m_camera.SetYawPitch(pose.yaw, pose.pitch);
    m_camera.SetDistance(pose.distance);
    m_camera.Settings().fovYRadians = pose.fovY;
}

std::optional<ViewportSceneInteractionUVE::CameraFlightPoseUVE> ViewportSceneInteractionUVE::CameraEntityPoseUVE(
    const Scene::EntityUVE entity) const {
    if (entity == Scene::kInvalidEntityUVE || !m_entityManager.IsAliveUVE(entity) ||
        !m_entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
        return std::nullopt;
    }
    const auto& world = m_entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
    const Math::Vector3UVE f = Math::RotateVectorUVE(world.worldRotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    const float length = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    if (!(length > 1e-5F)) {
        return std::nullopt;
    }
    const univex::math::Vec3 forward{f.x / length, f.y / length, f.z / length};
    const float distance = std::max(1.0F, m_camera.Distance() * 0.25F);
    const univex::math::Vec3 eye{world.worldPosition.x, world.worldPosition.y, world.worldPosition.z};
    CameraFlightPoseUVE pose{};
    pose.target = eye + forward * distance;
    pose.distance = distance;
    pose.pitch = std::asin(std::clamp(-forward.y, -1.0F, 1.0F));
    pose.yaw = std::atan2(-forward.z, -forward.x);
    pose.fovY = m_camera.Settings().fovYRadians;
    if (m_entityManager.HasComponentUVE<Scene::CameraComponentUVE>(entity)) {
        const float degrees = m_entityManager.GetComponentUVE<Scene::CameraComponentUVE>(entity).fieldOfViewDegrees;
        if (degrees > 1.0F && degrees < 179.0F) {
            pose.fovY = degrees * 3.14159265F / 180.0F;
        }
    }
    return pose;
}

void ViewportSceneInteractionUVE::UpdateSelectionGizmoUVE() {
    univex::math::Vec3 centroid{0.0F, 0.0F, 0.0F};
    int contributing = 0;
    for (const Scene::EntityUVE entity : m_editor.GetSelectedEntitiesUVE()) {
        if (entity == Scene::kInvalidEntityUVE ||
            !m_entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(entity)) {
            continue;
        }
        const auto& worldTransform = m_entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(entity);
        centroid += univex::integration::FromUveVector3UVE(worldTransform.worldPosition);
        ++contributing;
    }
    if (contributing > 0) {
        m_gizmoPivot = centroid * (1.0F / static_cast<float>(contributing));
    } else {
        m_gizmoPivot.reset();
    }
}

void ViewportSceneInteractionUVE::UpdateSelectionFromMouseUVE(const Editor::EditorViewportInputUVE& input,
                                                              const int width,
                                                              const int height,
                                                              const bool suppressed) {
    if (suppressed) {
        m_selectionPressActive = false;
        return;
    }
    if (!m_pointerOverOverlay && input.hovered && input.IsClickedUVE(VpBtnUVE::Left)) {
        const Math::Vector2UVE imageOrigin = input.imageOrigin;
        m_selectionPressActive = true;
        m_selectionPressX = input.mousePos.x - imageOrigin.x;
        m_selectionPressY = input.mousePos.y - imageOrigin.y;
    }
    if (!m_selectionPressActive || !input.IsReleasedUVE(VpBtnUVE::Left)) {
        return;
    }
    m_selectionPressActive = false;

    const Math::Vector2UVE dragDelta = input.DragDeltaUVE(VpBtnUVE::Left);
    if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
        return;
    }

    const univex::integration::EntityPickResultUVE pick = univex::integration::PickEntityAtPixelUVE(
        m_entityManager, m_camera, width, height, m_selectionPressX, m_selectionPressY);

    const bool extend = input.ctrl;
    if (!pick.hit) {
        if (!extend) {
            m_editor.ClearSelectionUVE();
        }
        return;
    }
    if (extend) {
        m_editor.ToggleEntitySelectionUVE(pick.entity);
    } else {
        m_editor.SelectEntityUVE(pick.entity);
    }
}

void ViewportSceneInteractionUVE::UpdateEntityContextToolbarFromMouseUVE(const Editor::EditorViewportInputUVE& input,
                                                                         const int width,
                                                                         const int height,
                                                                         const bool suppressed) {
    if (suppressed) {
        m_contextToolbarPressActive = false;
        return;
    }
    if (!m_pointerOverOverlay && input.hovered && input.IsClickedUVE(VpBtnUVE::Right)) {
        const Math::Vector2UVE imageOrigin = input.imageOrigin;
        m_contextToolbarPressActive = true;
        m_contextToolbarPressX = input.mousePos.x - imageOrigin.x;
        m_contextToolbarPressY = input.mousePos.y - imageOrigin.y;
    }
    if (!m_contextToolbarPressActive || !input.IsReleasedUVE(VpBtnUVE::Right)) {
        return;
    }
    m_contextToolbarPressActive = false;

    const Math::Vector2UVE dragDelta = input.DragDeltaUVE(VpBtnUVE::Right);
    if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > kNavClickSlopPixelsUVE) {
        return;
    }

    const univex::integration::EntityPickResultUVE pick = univex::integration::PickEntityAtPixelUVE(
        m_entityManager, m_camera, width, height, m_contextToolbarPressX, m_contextToolbarPressY);
    if (!pick.hit) {
        m_editor.ClearEntityContextToolbarUVE();
        return;
    }

    m_editor.SelectEntityUVE(pick.entity);
    if (!m_entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(pick.entity)) {
        m_editor.ClearEntityContextToolbarUVE();
        return;
    }
    const auto& worldTransform = m_entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(pick.entity);
    float anchorPixelX = 0.0F;
    float anchorPixelY = 0.0F;
    if (univex::integration::ProjectWorldPointToPixelUVE(m_camera, width, height, worldTransform.worldPosition,
                                                         anchorPixelX, anchorPixelY)) {
        m_editor.SetEntityContextToolbarAnchorUVE(pick.entity, anchorPixelX, anchorPixelY);
    } else {
        m_editor.ClearEntityContextToolbarUVE();
    }
}

void ViewportSceneInteractionUVE::UpdateOrbitPanZoomFromMouseUVE(const Editor::EditorViewportInputUVE& input,
                                                                 const int framebufferHeight,
                                                                 const bool suppressOrbit) {
    if (m_pointerOverOverlay || !input.hovered) {
        return;
    }
    const auto& prefs = m_editor.GetExtendedPreferencesUVE();
    const float orbitSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0));
    const float panSens = static_cast<float>(std::clamp(prefs.panSensitivity, 0.05, 10.0));
    const float zoomSens = static_cast<float>(std::clamp(prefs.zoomSensitivity, 0.05, 10.0));

    if (!suppressOrbit && input.alt && input.IsDraggingUVE(VpBtnUVE::Left, 0.0F)) {
        m_camera.Orbit(input.mouseDelta.x * orbitSens, input.mouseDelta.y * orbitSens);
        m_editor.NotifyViewportOrbitedUVE();
    } else if (input.IsDraggingUVE(VpBtnUVE::Middle, 0.0F)) {
        m_camera.Pan(input.mouseDelta.x * panSens, input.mouseDelta.y * panSens, framebufferHeight);
    }
    if (input.wheel != 0.0F) {
        m_camera.Dolly(input.wheel * zoomSens);
    }
}

bool ViewportSceneInteractionUVE::UpdateFlyCameraUVE(const Editor::EditorViewportInputUVE& input,
                                                     const bool suppressed) {
    if (!input.IsDownUVE(VpBtnUVE::Right)) {
        if (m_flying || m_flyArmed) {
            m_flying = false;
            m_flyArmed = false;
            m_flyVelocity = univex::math::Vec3{0.0F, 0.0F, 0.0F};
            m_editor.SetViewportFlyingUVE(false);
            m_editor.SetViewportFlyHeldUVE(false);
        }
        return false;
    }
    if (!m_flying && !m_flyArmed) {
        if (suppressed || m_pointerOverOverlay || !input.hovered || !input.IsClickedUVE(VpBtnUVE::Right)) {
            return false;
        }
        m_flyArmed = true;
        m_editor.SetViewportFlyHeldUVE(true);
    }
    const bool moveKey = input.IsKeyDownUVE(VpKeyUVE::W) || input.IsKeyDownUVE(VpKeyUVE::A) ||
                         input.IsKeyDownUVE(VpKeyUVE::S) || input.IsKeyDownUVE(VpKeyUVE::D) ||
                         input.IsKeyDownUVE(VpKeyUVE::Q) || input.IsKeyDownUVE(VpKeyUVE::E);
    if (!m_flying) {
        const Math::Vector2UVE drag = input.DragDeltaUVE(VpBtnUVE::Right);
        if (!moveKey && std::fabs(drag.x) + std::fabs(drag.y) <= kNavClickSlopPixelsUVE) {
            return false;
        }
        m_flying = true;
        m_editor.SetViewportFlyingUVE(true);
    }

    const auto& prefs = m_editor.GetExtendedPreferencesUVE();
    const float lookSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0)) * 0.8F;
    if (input.mouseDelta.x != 0.0F || input.mouseDelta.y != 0.0F) {
        m_camera.Look(input.mouseDelta.x * lookSens, input.mouseDelta.y * lookSens);
        m_editor.NotifyViewportOrbitedUVE();
    }
    if (input.wheel != 0.0F) {
        m_flySpeed = std::clamp(m_flySpeed * std::pow(1.25F, input.wheel), 0.25F, 500.0F);
    }

    univex::math::Vec3 forward = univex::math::Normalize(m_camera.Target() - m_camera.Eye());
    if (univex::math::Length(forward) < 0.001F) {
        forward = univex::math::Vec3{0.0F, 0.0F, -1.0F};
    }
    const univex::math::Vec3 up{0.0F, 1.0F, 0.0F};
    univex::math::Vec3 right = univex::math::Cross(forward, up);
    right = univex::math::Length(right) < 0.001F ? univex::math::Vec3{1.0F, 0.0F, 0.0F}
                                                 : univex::math::Normalize(right);
    univex::math::Vec3 wish{0.0F, 0.0F, 0.0F};
    if (input.IsKeyDownUVE(VpKeyUVE::W)) {
        wish = wish + forward;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::S)) {
        wish = wish - forward;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::D)) {
        wish = wish + right;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::A)) {
        wish = wish - right;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::E)) {
        wish = wish + up;
    }
    if (input.IsKeyDownUVE(VpKeyUVE::Q)) {
        wish = wish - up;
    }
    const float wishLength = univex::math::Length(wish);
    const float speed = m_flySpeed * (input.shift ? 3.0F : 1.0F);
    const univex::math::Vec3 wanted =
        wishLength > 0.001F ? wish * (speed / wishLength) : univex::math::Vec3{0.0F, 0.0F, 0.0F};
    const float dt = std::clamp(input.deltaTime, 0.0F, 0.1F);
    const float blend = 1.0F - std::exp(-dt * 14.0F);
    m_flyVelocity = m_flyVelocity + (wanted - m_flyVelocity) * blend;
    if (univex::math::Length(m_flyVelocity) > 0.0005F) {
        m_camera.Translate(m_flyVelocity * dt);
    }
    return true;
}

void ViewportSceneInteractionUVE::UpdateViewportViewHotkeysUVE(const Editor::EditorViewportInputUVE& input) {
    if (!input.hovered || input.wantTextInput) {
        return;
    }
    struct DigitKeysUVE final {
        int digit;
        VpKeyUVE keypad;
        VpKeyUVE row;
    };
    static constexpr DigitKeysUVE kKeysUVE[] = {
        {1, VpKeyUVE::Keypad1, VpKeyUVE::Num1},
        {3, VpKeyUVE::Keypad3, VpKeyUVE::Num3},
        {5, VpKeyUVE::Keypad5, VpKeyUVE::Num5},
        {7, VpKeyUVE::Keypad7, VpKeyUVE::Num7},
    };
    for (const DigitKeysUVE& keys : kKeysUVE) {
        const bool pressed = input.IsKeyPressedUVE(keys.keypad) || (input.alt && input.IsKeyPressedUVE(keys.row));
        if (!pressed) {
            continue;
        }
        if (keys.digit == 5) {
            m_editor.SetViewportOrthographicUVE(!m_editor.IsViewportOrthographicUVE());
            continue;
        }
        if (const auto view = Editor::EditorUVE::GetViewportViewForKeypadDigitUVE(keys.digit, input.ctrl)) {
            m_editor.RequestViewportViewUVE(*view);
        }
    }
}

void ViewportSceneInteractionUVE::UpdateViewportBookmarkHotkeysUVE(const Editor::EditorViewportInputUVE& input) {
    if (!input.hovered || input.wantTextInput || input.alt) {
        return;
    }
    static constexpr VpKeyUVE kDigitKeysUVE[] = {
        VpKeyUVE::Num1, VpKeyUVE::Num2, VpKeyUVE::Num3, VpKeyUVE::Num4, VpKeyUVE::Num5,
        VpKeyUVE::Num6, VpKeyUVE::Num7, VpKeyUVE::Num8, VpKeyUVE::Num9, VpKeyUVE::Num0,
    };
    for (std::size_t order = 0U; order < 10U; ++order) {
        const std::size_t slot = (order + 1U) % 10U;
        if (!input.IsKeyPressedUVE(kDigitKeysUVE[order])) {
            continue;
        }
        if (input.ctrl) {
            const univex::math::Vec3 target = m_camera.Target();
            static_cast<void>(m_editor.SetViewportBookmarkUVE(
                slot, Editor::EditorViewportBookmarkUVE{Math::Vector3UVE{target.x, target.y, target.z},
                                                        m_camera.Yaw(), m_camera.Pitch(), m_camera.Distance()}));
            continue;
        }
        const std::optional<Editor::EditorViewportBookmarkUVE> bookmark = m_editor.GetViewportBookmarkUVE(slot);
        if (!bookmark.has_value()) {
            continue;
        }
        m_editor.NotifyViewportOrbitedUVE();
        m_camera.CancelAnimation();
        m_camera.SetTarget(univex::integration::FromUveVector3UVE(bookmark->target));
        m_camera.SetYawPitch(bookmark->yawRadians, bookmark->pitchRadians);
        m_camera.SetDistance(bookmark->distance);
    }
    if (input.IsKeyPressedUVE(VpKeyUVE::F) && !input.ctrl) {
        static_cast<void>(m_editor.RequestViewportFocusUVE(m_editor.GetSelectedEntityUVE()));
    }
}

bool ViewportSceneInteractionUVE::EnsureHostDepthUVE(ViewportSurfaceUVE& surface,
                                                     const int width,
                                                     const int height) {
    if (!m_hostDepthProgram.has_value()) {
        std::string error;
        m_hostDepthProgram = univex::render::ShaderProgram::Build(
            ViewportSurfaceUVE::kFullscreenTriangleVertexShaderUVE, kHostDepthFragmentShaderUVE, error);
        if (!m_hostDepthProgram.has_value()) {
            UVE_ERROR("uve_editor_app: viewport depth export shader build failed: {}", error);
            m_hostDepthFailed = true;
        }
    }
    if (m_hostDepthFailed || !m_hostDepthProgram.has_value() || surface.FullscreenVaoUVE() == 0U) {
        return false;
    }
    const int w = std::max(1, (width + 1) / 2);
    const int h = std::max(1, (height + 1) / 2);
    if (m_hostDepthFbo != 0U && w == m_hostDepthWidth && h == m_hostDepthHeight) {
        return true;
    }
    if (m_hostDepthTexture == 0U) {
        glGenTextures(1, &m_hostDepthTexture);
    }
    if (m_hostDepthFbo == 0U) {
        glGenFramebuffers(1, &m_hostDepthFbo);
    }
    glBindTexture(GL_TEXTURE_2D, m_hostDepthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);

    GLint previousFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_hostDepthFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_hostDepthTexture, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previousFbo));
    m_hostDepthWidth = complete ? w : 0;
    m_hostDepthHeight = complete ? h : 0;
    return complete;
}

bool ViewportSceneInteractionUVE::AnySelectedEntityMovedUVE() const {
    for (const auto& snap : m_lastSelectedSnapshots) {
        if (!m_entityManager.IsAliveUVE(snap.entity) ||
            !m_entityManager.HasComponentUVE<Scene::WorldTransformComponentUVE>(snap.entity)) {
            return true;
        }
        const auto& wt = m_entityManager.GetComponentUVE<Scene::WorldTransformComponentUVE>(snap.entity);
        if (wt.dirty) {
            return true;
        }
        if (snap.position != wt.worldPosition || snap.rotation != wt.worldRotation) {
            return true;
        }
    }
    return false;
}

} // namespace UVE::App::Viewports
