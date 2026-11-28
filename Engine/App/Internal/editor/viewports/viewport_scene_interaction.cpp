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

#include "aabb.h"
#include "camera_component.h"
#include "editor_internal_entity_component.h"
#include "engine_services.h"
#include "gl_render_device.h"
#include "i_entity_manager.h"
#include "integration/entity_picker.h"
#include "integration/math_conversions.h"
#include "integration/selection_outline_geometry.h"
#include "logging_macros.h"
#include "matrix4x4.h"
#include "primitive_geometry.h"
#include "primitive_mesh_component.h"
#include "univex/gizmo/bone_shape.h"
#include "univex/render/infinite_grid_renderer.h"
#include "univex/render/selection_outline.h"
#include "viewport_object_gizmos.h"
#include "viewport_surface.h"
#include "visibility_component.h"
#include "world_transform_component.h"

namespace UVE::App::Viewports {
namespace {

using VpInput = Editor::EditorViewportInput;
using VpBtn = VpInput::Button;
using VpKey = VpInput::Key;

// How far the pointer may travel (in view pixels) and still count as a click rather than a drag.
constexpr float kNavClickSlopPixels = 4.0F;

// Scene depth for the PRISM shell's overlays: each output texel is the nearest surface of a 2x2
// block of the renderer's depth target, as the distance along the editor view's forward axis from
// its eye (world units), or 1e30 where nothing was drawn.
#if defined(UNIVEX_GL_ES)
constexpr float kHostDepthMaxRangeMeters = 10000.0F;
constexpr std::string_view kHostDepthFragmentShader = R"(#version 330 core
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
        FragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }
    vec4 world = uInvViewProj * vec4(u * 2.0 - 1.0, v * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    world /= world.w;
    float dist = clamp(dot(world.xyz - uEye, uForward) / 10000.0, 0.0, 1.0);
    float scaled = floor(dist * 16777215.0 + 0.5);
    float r = floor(scaled / 65536.0);
    float g = floor(mod(scaled, 65536.0) / 256.0);
    float b = mod(scaled, 256.0);
    FragColor = vec4(r / 255.0, g / 255.0, b / 255.0, 1.0);
}
)";
#else
constexpr std::string_view kHostDepthFragmentShader = R"(#version 330 core
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
#endif

} // namespace

ViewportSceneInteraction::ViewportSceneInteraction(Editor::Editor& editor,
                                                         Scene::IEntityManager& entityManager)
    : m_editor(editor), m_entityManager(entityManager) {
    m_camera.Settings().allowFullOrbit = true;
    const auto& prefs = m_editor.GetExtendedPreferences();
    m_camera.Settings().fovYRadians = static_cast<float>(prefs.defaultCameraFovDegrees) * 3.14159265F / 180.0F;
    m_flySpeed = static_cast<float>(prefs.freelookBaseSpeed);
}

ViewportSceneInteraction::~ViewportSceneInteraction() {
    if (m_hostDepthPbo[0] != 0U || m_hostDepthPbo[1] != 0U) {
        glDeleteBuffers(2, m_hostDepthPbo.data());
        m_hostDepthPbo = {};
    }
    for (GLsync& fence : m_hostDepthFence) {
        if (fence != nullptr) {
            glDeleteSync(fence);
            fence = nullptr;
        }
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

ViewportCameraPose ViewportSceneInteraction::CapturePose() const noexcept {
    return ViewportCameraPose{m_camera.Target(), m_camera.Yaw(), m_camera.Pitch(), m_camera.Distance(),
                                 m_camera.IsOrthographic()};
}

void ViewportSceneInteraction::RestorePose(const ViewportCameraPose& pose) {
    m_camera.CancelAnimation();
    m_camera.SetTarget(pose.target);
    m_camera.SetYawPitch(pose.yaw, pose.pitch);
    m_camera.SetDistance(pose.distance);
    m_camera.SetOrthographic(pose.orthographic);
}

univex::math::Vec3 ViewportSceneInteraction::NamedViewDirection(const Editor::Editor::ViewportView view) {
    using ViewportView = Editor::Editor::ViewportView;
    switch (view) {
        case ViewportView::Top: return {0.0F, 1.0F, 0.0F};
        case ViewportView::Bottom: return {0.0F, -1.0F, 0.0F};
        case ViewportView::Front: return {0.0F, 0.0F, 1.0F};
        case ViewportView::Back: return {0.0F, 0.0F, -1.0F};
        case ViewportView::Right: return {1.0F, 0.0F, 0.0F};
        case ViewportView::Left: return {-1.0F, 0.0F, 0.0F};
        case ViewportView::User: break;
    }
    return {0.0F, 0.0F, 0.0F};
}

void ViewportSceneInteraction::ApplyNamedView(const Editor::Editor::ViewportView view) {
    using ViewportView = Editor::Editor::ViewportView;
    constexpr float kHalfPi = 1.5707963F;
    const auto& limits = m_camera.Settings();
    switch (view) {
        case ViewportView::Top: m_camera.SnapToYawPitch(kHalfPi, limits.pitchMax); return;
        case ViewportView::Bottom: m_camera.SnapToYawPitch(kHalfPi, limits.pitchMin); return;
        case ViewportView::User: return;
        default: m_camera.SnapToDirection(NamedViewDirection(view)); return;
    }
}

void ViewportSceneInteraction::FocusCameraOnEntity(const Scene::Entity entity) {
    if (const std::optional<Editor::EditorViewportBookmark> markerView =
            m_editor.ComposeMarker3DFocusBookmark(entity)) {
        m_editor.NotifyViewportOrbited();
        m_camera.CancelAnimation();
        m_camera.SetTarget(univex::integration::FromUveVector3(markerView->target));
        m_camera.SetYawPitch(markerView->yawRadians, markerView->pitchRadians);
        m_camera.SetDistance(markerView->distance);
    } else if (const std::optional<Math::Vector3> focusTarget = m_editor.ResolveEntityFocusTarget(entity)) {
        m_camera.CancelAnimation();
        m_camera.SetTarget(univex::integration::FromUveVector3(*focusTarget));
    }
}

void ViewportSceneInteraction::ApplyOverlayState(ViewportSurface& surface,
                                                       const Editor::Editor::ViewportOverlayState& overlayState,
                                                       const bool suppressGrid) {
    surface.SetShadingMode(overlayState.shadingMode);
    auto& renderPass = surface.RenderPass();
    auto& settings = renderPass.Settings();
    settings.projection = overlayState.orthographic ? univex::viewport::ProjectionMode::Orthographic
                                                    : univex::viewport::ProjectionMode::Perspective;
    settings.viewEnvironment = true;
    m_camera.SetOrthographic(overlayState.orthographic);

    if (overlayState.viewRequestSerial != m_appliedViewRequestSerial) {
        m_appliedViewRequestSerial = overlayState.viewRequestSerial;
        ApplyNamedView(overlayState.view);
    }
    if (overlayState.focusRequestSerial != m_appliedFocusRequestSerial) {
        m_appliedFocusRequestSerial = overlayState.focusRequestSerial;
        FocusCameraOnEntity(overlayState.focusEntity);
    }

    const univex::math::Vec3 viewAxis = NamedViewDirection(overlayState.view);
    surface.ApplyGridAppearance(m_editor, overlayState, overlayState.gridVisible && !suppressGrid,
                                   univex::render::GridPlaneFacing(viewAxis.x, viewAxis.y, viewAxis.z));

    constexpr float kSelectionOutlineVisualScale = 0.20F;
    renderPass.SetSelectionOutline(univex::render::SelectionOutlineSettings{
        overlayState.selectionOutlineVisible, overlayState.selectionOutlineColor.r, overlayState.selectionOutlineColor.g,
        overlayState.selectionOutlineColor.b,
        overlayState.selectionOutlineThickness * kSelectionOutlineVisualScale});

    m_pointerOverOverlay = overlayState.pointerOverOverlay;
    surface.ApplyAxisPalette(m_editor, overlayState);

    const auto toVec3 = [](const std::array<float, 3>& value) {
        return univex::math::Vec3{value[0], value[1], value[2]};
    };

    std::vector<univex::gizmo::BoneOverlay> bones;
    bones.reserve(overlayState.bones.size());
    for (const auto& bone : overlayState.bones) {
        bones.push_back(univex::gizmo::BoneOverlay{toVec3(bone.head), toVec3(bone.tail), toVec3(bone.side),
                                                      bone.hasLink, toVec3(bone.linkFrom), bone.skeletonSelected,
                                                      bone.boneSelected, bone.hasColour, toVec3(bone.colour),
                                                      bone.jointOnly, bone.helper});
    }
    if (overlayState.hostDrawsGizmos) {
        const univex::math::Vec3 view = univex::math::Normalize(m_camera.Target() - m_camera.Eye());
        m_hostBonesMesh = univex::gizmo::BuildBoneMesh(bones, view);
        renderPass.SetBones({});
    } else {
        m_hostBonesMesh.Clear();
        renderPass.SetBones(std::move(bones));
    }

    constexpr float kColliderWireRed = 0.20F;
    constexpr float kColliderWireGreen = 0.95F;
    constexpr float kColliderWireBlue = 0.45F;
    constexpr float kColliderWireWidthPixels = 1.5F;
    constexpr float kColliderHighlightWidthPixels = 2.5F;
    const univex::math::Vec3 wireColour{kColliderWireRed, kColliderWireGreen, kColliderWireBlue};
    const univex::math::Vec3 highlightColour{0.70F, 1.00F, 0.82F};
    std::vector<univex::gizmo::GizmoLine> colliderLines;
    std::vector<bool> colliderOccluded;
    colliderLines.reserve(overlayState.colliderLines.size());
    colliderOccluded.reserve(overlayState.colliderLines.size());
    for (const auto& line : overlayState.colliderLines) {
        const univex::math::Vec3 colour =
            line.hasColour ? toVec3(line.colour) : (line.highlighted ? highlightColour : wireColour);
        colliderLines.push_back(univex::gizmo::GizmoLine{toVec3(line.from), toVec3(line.to), colour,
                                                         line.highlighted ? kColliderHighlightWidthPixels
                                                                          : kColliderWireWidthPixels});
        colliderOccluded.push_back(!line.xray);
    }
    if (overlayState.hostDrawsGizmos) {
        m_hostColliderLines = std::move(colliderLines);
        m_hostColliderOccluded = std::move(colliderOccluded);
        renderPass.SetColliderWireframes({});
    } else {
        m_hostColliderLines.clear();
        m_hostColliderOccluded.clear();
        renderPass.SetColliderWireframes(std::move(colliderLines));
    }
}

std::optional<Scene::Entity> ViewportSceneInteraction::UpdateCameraPreviewFlight(
    const bool allowPreview,
    const Editor::EditorViewportInput& input) {
    const Scene::Entity previewCamera = m_editor.GetPreviewCamera();
    const bool wantPreview = allowPreview && (previewCamera != Scene::kInvalidEntity);
    if (wantPreview && !m_previewing) {
        m_poseBeforePreview = CapturePose();
        m_fovBeforePreview = m_camera.Settings().fovYRadians;
        m_previewing = true;
        if (const std::optional<CameraFlightPose> into = CameraEntityPose(previewCamera)) {
            m_camera.CancelAnimation();
            m_flight = CameraFlight{CurrentFlightPose(), *into, 0.0F, true};
            m_camera.SetOrthographic(false);
        }
    } else if (!wantPreview && m_previewing) {
        if (m_poseBeforePreview.has_value()) {
            m_camera.CancelAnimation();
            const CameraFlightPose back{m_poseBeforePreview->target, m_poseBeforePreview->yaw,
                                           m_poseBeforePreview->pitch, m_poseBeforePreview->distance,
                                           m_fovBeforePreview};
            const std::optional<CameraFlightPose> from =
                m_flight.has_value() ? std::optional<CameraFlightPose>{CurrentFlightPose()}
                                     : CameraEntityPose(m_lastPreviewCamera);
            if (from.has_value()) {
                ApplyFlightPose(*from);
                m_flight = CameraFlight{*from, back, 0.0F, false};
            } else {
                m_flight.reset();
                ApplyFlightPose(back);
            }
            m_camera.SetOrthographic(m_poseBeforePreview->orthographic);
            m_poseBeforePreview.reset();
        }
        m_previewing = false;
    }

    if (previewCamera != Scene::kInvalidEntity) {
        m_lastPreviewCamera = previewCamera;
    }

    if (m_flight.has_value()) {
        m_flight->t = std::min(1.0F, m_flight->t + input.deltaTime / kCameraFlightSeconds);
        const float t = m_flight->t;
        const float e = t * t * (3.0F - 2.0F * t);
        const CameraFlightPose& a = m_flight->from;
        const CameraFlightPose& b = m_flight->to;
        float yawDelta = b.yaw - a.yaw;
        while (yawDelta > 3.14159265F) {
            yawDelta -= 6.2831853F;
        }
        while (yawDelta < -3.14159265F) {
            yawDelta += 6.2831853F;
        }
        ApplyFlightPose(CameraFlightPose{a.target + (b.target - a.target) * e, a.yaw + yawDelta * e,
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

void ViewportSceneInteraction::UpdateInteraction(const Editor::Editor::ViewportOverlayState& overlayState,
                                                       const int width,
                                                       const int height,
                                                       const bool skipAuthoringNavigation) {
    UpdateSelectionGizmo();
    if (overlayState.hostDrawsGizmos &&
        (overlayState.orbitRequestX != 0.0F || overlayState.orbitRequestY != 0.0F)) {
        m_camera.Orbit(overlayState.orbitRequestX, overlayState.orbitRequestY);
        m_editor.NotifyViewportOrbited();
    }

    const VpInput& input = overlayState.input;
    constexpr bool kPointerTaken = false;
    UpdateSelectionFromMouse(overlayState, width, height, kPointerTaken);
    UpdateEntityContextToolbarFromMouse(input, width, height, kPointerTaken);

    if (!m_previewing && !skipAuthoringNavigation) {
        UpdateTouchControllerCamera(overlayState);
        if (!UpdateFlyCamera(input, kPointerTaken)) {
            UpdateOrbitPanZoomFromMouse(input, height, kPointerTaken);
            UpdateViewportViewHotkeys(input);
            UpdateViewportBookmarkHotkeys(input);
        }
    }
}

void ViewportSceneInteraction::AdvanceCameraAndPublishFocus(const Editor::EditorViewportInput& input) {
    m_camera.Update(input.deltaTime);
    const univex::math::Vec3 cameraFocus = m_camera.Target();
    m_editor.SetViewportCameraFocus(Math::Vector3{cameraFocus.x, cameraFocus.y, cameraFocus.z});
}

void ViewportSceneInteraction::UpdateObjectGizmoOverlay(
    ViewportSurface& surface,
    const Editor::Editor::ViewportOverlayState& overlayState,
    const int width,
    const int height,
    const std::optional<Scene::Entity> lookThroughCamera,
    const bool wantOverlay) {
    auto& renderPass = surface.RenderPass();
    if (wantOverlay) {
        const float aspect = static_cast<float>(width) / static_cast<float>(std::max(height, 1));
        const Scene::Entity lookThrough = lookThroughCamera.value_or(Scene::kInvalidEntity);
        const std::size_t entityCount = m_entityManager.GetEntityCount();

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

            ViewportObjectGizmoBuildResult built = ViewportObjectGizmos::BuildAllObjectGizmos(
                m_entityManager, lookThrough, aspect, overlayState.hostDrawsGizmos);
            if (overlayState.hostDrawsGizmos) {
                m_hostOverlayMesh = std::move(built.mesh);
                m_hostOverlayOccludedFrom = built.colliderLinesStart;
                renderPass.SetCameraFrustumMesh({});
            } else {
                m_hostOverlayMesh.Clear();
                renderPass.SetCameraFrustumMesh(std::move(built.mesh));
            }
        }
    } else {
        m_hostOverlayMesh.Clear();
        if (m_cachedOverlayActive) {
            renderPass.SetCameraFrustumMesh({});
            m_cachedOverlayActive = false;
        }
    }
}

void ViewportSceneInteraction::ExportViewForHost(const Editor::Editor::ViewportOverlayState& overlayState,
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
    for (std::size_t i = 0U; i < m_hostColliderLines.size(); ++i) {
        const univex::gizmo::GizmoLine& line = m_hostColliderLines[i];
        const bool occluded = i < m_hostColliderOccluded.size() ? m_hostColliderOccluded[i] : true;
        overlayState.gizmoLines.push_back({a3(line.a), a3(line.b), a3(line.color), line.widthPx, occluded});
    }
}

void ViewportSceneInteraction::ExportGridForHost(const ViewportSurface& surface,
                                                       const Editor::Editor::ViewportOverlayState& overlayState,
                                                       const bool want) {
    auto& out = overlayState.hostGrid;
    const univex::render::GridSettings& grid = surface.RenderPass().Grid().Settings();
    out.visible = want && surface.RenderPass().Settings().viewGrid;
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

void ViewportSceneInteraction::ExportSceneDepthForHost(
    ViewportSurface& surface,
    const Editor::Editor::ViewportOverlayState& overlayState,
    const int width,
    const int height) {
    overlayState.sceneDepth.clear();
    overlayState.sceneDepthWidth = overlayState.sceneDepthHeight = 0;

    if (surface.IsCompatibilityProfile()) {
        // Compatibility profile skips the extra depth-linearization FBO pass and glReadPixels
        // GPU->CPU readback altogether to maximize frame pacing on low-end TBDR GPUs.
        return;
    }

    Core::EngineServices& services = surface.MutableServices();
    const Render::SceneDepthExport depth = services.GetRenderer3D().GetLastSceneDepth();
    auto* const glDevice = dynamic_cast<Render::GlRenderDevice*>(&services.GetRenderDevice());
    if (!depth.valid || glDevice == nullptr || depth.width == 0U || depth.height == 0U ||
        !EnsureHostDepth(surface, width, height)) {
        return;
    }

    // Reuse the last depth snapshot while static, and throttle readbacks during camera motion on
    // both desktop and mobile to avoid redundant fullscreen depth passes and pipeline flushes.
    std::uint64_t depthKey = 1469598103934665603ULL;
    const auto mixDepthKey = [&depthKey](const void* data, const std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0U; i < size; ++i) {
            depthKey = (depthKey ^ bytes[i]) * 1099511628211ULL;
        }
    };
    mixDepthKey(&depth.inverseViewProjection, sizeof(depth.inverseViewProjection));
    const int depthDims[2] = {m_hostDepthWidth, m_hostDepthHeight};
    mixDepthKey(depthDims, sizeof(depthDims));
    const std::size_t entityCount = m_entityManager.GetEntityCount();
    mixDepthKey(&entityCount, sizeof(entityCount));
    const bool hasCachedDepth = !m_cachedHostDepth.empty() && m_cachedHostDepthWidth == m_hostDepthWidth &&
                                m_cachedHostDepthHeight == m_hostDepthHeight;
    const bool touchDriving = std::fabs(overlayState.touchMoveX) > 1e-3F ||
                              std::fabs(overlayState.touchMoveY) > 1e-3F ||
                              std::fabs(overlayState.touchMoveUp) > 1e-3F ||
                              std::fabs(overlayState.touchLookX) > 1e-3F ||
                              std::fabs(overlayState.touchLookY) > 1e-3F ||
                              std::fabs(overlayState.touchZoomDelta) > 1e-3F;
    const bool pointerActive = touchDriving || overlayState.input.IsDown(VpBtn::Left) ||
                               overlayState.input.IsDown(VpBtn::Middle) ||
                               overlayState.input.IsDown(VpBtn::Right);
    ++m_hostDepthFrameCounter;
    if (hasCachedDepth && ((depthKey == m_cachedHostDepthKey && !pointerActive) ||
                           ((depthKey != m_cachedHostDepthKey || touchDriving) && (m_hostDepthFrameCounter % 8U) != 0U))) {
        overlayState.sceneDepth = m_cachedHostDepth;
        overlayState.sceneDepthWidth = m_cachedHostDepthWidth;
        overlayState.sceneDepthHeight = m_cachedHostDepthHeight;
        return;
    }

    const GLuint depthTexture = static_cast<GLuint>(glDevice->GetNativeTextureId(depth.depthTexture));
    const GLuint fullscreenVao = surface.FullscreenVao();
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
    std::array<float, 16> invViewProjColMajor{};
    for (std::size_t r = 0U; r < 4U; ++r) {
        for (std::size_t col = 0U; col < 4U; ++col) {
            invViewProjColMajor[col * 4U + r] = depth.inverseViewProjection.m[r][col];
        }
    }
    glUniformMatrix4fv(m_hostDepthProgram->UniformLocation("uInvViewProj"), 1, GL_FALSE,
                       invViewProjColMajor.data());
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
#if defined(UNIVEX_GL_ES)
    glReadPixels(0, 0, m_hostDepthWidth, m_hostDepthHeight, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    glReadPixels(0, 0, m_hostDepthWidth, m_hostDepthHeight, GL_RED, GL_FLOAT, nullptr);
#endif
    m_hostDepthPboFilled[write] = true;
    if (m_hostDepthFence[write] != nullptr) {
        glDeleteSync(m_hostDepthFence[write]);
    }
    m_hostDepthFence[write] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, packAlignment);
    // Map last frame's buffer only once the GPU has written it: mapping earlier made the CPU wait for
    // the GPU to finish that whole frame, so a GPU-heavy view (a mesh filling it) also stalled the CPU.
    // Until then the previous depth is kept, at most a frame or two older.
    bool readReady = m_hostDepthPboFilled[read];
    if (readReady && m_hostDepthFence[read] != nullptr) {
        const GLenum wait = glClientWaitSync(m_hostDepthFence[read], 0, 0);
        readReady = wait == GL_ALREADY_SIGNALED || wait == GL_CONDITION_SATISFIED;
    }
    if (!readReady && m_hostDepthPboFilled[read] && !m_cachedHostDepth.empty() &&
        m_cachedHostDepthWidth == m_hostDepthWidth && m_cachedHostDepthHeight == m_hostDepthHeight) {
        overlayState.sceneDepth = m_cachedHostDepth;
        overlayState.sceneDepthWidth = m_cachedHostDepthWidth;
        overlayState.sceneDepthHeight = m_cachedHostDepthHeight;
    }
    if (readReady) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_hostDepthPbo[read]);
#if defined(UNIVEX_GL_ES)
        if (const auto* const rows =
                static_cast<const std::uint8_t*>(glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT))) {
            overlayState.sceneDepth.resize(count);
            const std::size_t stride = static_cast<std::size_t>(m_hostDepthWidth);
            for (int y = 0; y < m_hostDepthHeight; ++y) {
                const std::size_t srcRow = static_cast<std::size_t>(m_hostDepthHeight - 1 - y) * stride;
                const std::size_t dstRow = static_cast<std::size_t>(y) * stride;
                for (std::size_t x = 0; x < stride; ++x) {
                    const std::uint8_t* const px = rows + (srcRow + x) * 4U;
                    if (px[3] == 0U) {
                        overlayState.sceneDepth[dstRow + x] = 1.0e30F;
                    } else {
                        const std::uint32_t packed = (static_cast<std::uint32_t>(px[0]) << 16U) |
                                                     (static_cast<std::uint32_t>(px[1]) << 8U) |
                                                     static_cast<std::uint32_t>(px[2]);
                        overlayState.sceneDepth[dstRow + x] =
                            (static_cast<float>(packed) / 16777215.0F) * kHostDepthMaxRangeMeters;
                    }
                }
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
            overlayState.sceneDepthWidth = m_hostDepthWidth;
            overlayState.sceneDepthHeight = m_hostDepthHeight;
            m_cachedHostDepth = overlayState.sceneDepth;
            m_cachedHostDepthWidth = m_hostDepthWidth;
            m_cachedHostDepthHeight = m_hostDepthHeight;
            m_cachedHostDepthKey = depthKey;
        }
#else
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
            m_cachedHostDepth = overlayState.sceneDepth;
            m_cachedHostDepthWidth = m_hostDepthWidth;
            m_cachedHostDepthHeight = m_hostDepthHeight;
            m_cachedHostDepthKey = depthKey;
        }
#endif
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

void ViewportSceneInteraction::RenderSelectionOutlineOrMask(
    ViewportSurface& surface,
    const Editor::Editor::ViewportOverlayState& overlayState,
    const int width,
    const int height) {
    const auto& selected = m_editor.GetSelectedEntities();
    const auto active = m_editor.GetSelectedEntity();
    if (selected.empty()) {
        m_cachedSelectionTriangles.clear();
        m_lastSelectedEntities.clear();
        m_lastSelectedEntity = Scene::kInvalidEntity;
        m_lastSelectedSnapshots.clear();
        return;
    }

    bool needsRebuild = false;
    if (selected != m_lastSelectedEntities || active != m_lastSelectedEntity) {
        needsRebuild = true;
    } else if (AnySelectedEntityMoved()) {
        needsRebuild = true;
    }

    if (needsRebuild) {
        m_cachedSelectionTriangles =
            univex::integration::CollectSelectionOutlineTriangles(m_entityManager, selected, active);
        m_lastSelectedEntities = selected;
        m_lastSelectedEntity = active;
        m_lastSelectedSnapshots.clear();
        m_lastSelectedSnapshots.reserve(selected.size());
        for (const auto ent : selected) {
            if (m_entityManager.IsAlive(ent) &&
                m_entityManager.HasComponent<Scene::WorldTransformComponent>(ent)) {
                const auto& wt = m_entityManager.GetComponent<Scene::WorldTransformComponent>(ent);
                m_lastSelectedSnapshots.push_back({ent, wt.worldPosition, wt.worldRotation});
            }
        }
    }

    if (m_cachedSelectionTriangles.empty()) {
        return;
    }

    auto& renderPass = surface.RenderPass();
    if (overlayState.hostDrawsGizmos && !surface.PreferGpuSelectionOutline()) {
        const univex::render::SelectionOutlineSettings& outline = renderPass.SelectionOutlineSettings();
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
        } else if (renderPass.ReadSelectionMask(m_camera, width, height, m_cachedSelectionTriangles,
                                                   overlayState.selectionMask)) {
            m_lastMask = overlayState.selectionMask;
            m_lastMaskKey = maskKey;
            maskRead = true;
        }
        if (maskRead) {
            const float coordScale = std::max(0.25F, overlayState.input.dpiScale);
            overlayState.selectionMaskWidth = width;
            overlayState.selectionMaskHeight = height;
            overlayState.selectionOutlineRadius =
                std::max(1, static_cast<int>(std::lround(outline.thicknessPixels * coordScale)));
            overlayState.selectionOutlineRgb = {outline.r, outline.g, outline.b};
        }
        glBindFramebuffer(GL_FRAMEBUFFER, surface.ActiveDrawFbo());
        glViewport(0, 0, width, height);
    } else {
        // Pure GPU selection outline: zero glReadPixels stall and automatically smoothed by FXAA 3.11.
        renderPass.RenderSelectionOutline(m_camera, width, height, m_cachedSelectionTriangles);
    }
}

ViewportSceneInteraction::CameraFlightPose ViewportSceneInteraction::CurrentFlightPose() const {
    return CameraFlightPose{m_camera.Target(), m_camera.Yaw(), m_camera.Pitch(), m_camera.Distance(),
                               m_camera.Settings().fovYRadians};
}

void ViewportSceneInteraction::ApplyFlightPose(const CameraFlightPose& pose) {
    m_camera.SetTarget(pose.target);
    m_camera.SetYawPitch(pose.yaw, pose.pitch);
    m_camera.SetDistance(pose.distance);
    m_camera.Settings().fovYRadians = pose.fovY;
}

std::optional<ViewportSceneInteraction::CameraFlightPose> ViewportSceneInteraction::CameraEntityPose(
    const Scene::Entity entity) const {
    if (entity == Scene::kInvalidEntity || !m_entityManager.IsAlive(entity) ||
        !m_entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
        return std::nullopt;
    }
    const auto& world = m_entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
    const Math::Vector3 f = Math::RotateVector(world.worldRotation, Math::Vector3{0.0F, 0.0F, -1.0F});
    const float length = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
    if (!(length > 1e-5F)) {
        return std::nullopt;
    }
    const univex::math::Vec3 forward{f.x / length, f.y / length, f.z / length};
    const float distance = std::max(1.0F, m_camera.Distance() * 0.25F);
    const univex::math::Vec3 eye{world.worldPosition.x, world.worldPosition.y, world.worldPosition.z};
    CameraFlightPose pose{};
    pose.target = eye + forward * distance;
    pose.distance = distance;
    pose.pitch = std::asin(std::clamp(-forward.y, -1.0F, 1.0F));
    pose.yaw = std::atan2(-forward.z, -forward.x);
    pose.fovY = m_camera.Settings().fovYRadians;
    if (m_entityManager.HasComponent<Scene::CameraComponent>(entity)) {
        const float degrees = m_entityManager.GetComponent<Scene::CameraComponent>(entity).fieldOfViewDegrees;
        if (degrees > 1.0F && degrees < 179.0F) {
            pose.fovY = degrees * 3.14159265F / 180.0F;
        }
    }
    return pose;
}

void ViewportSceneInteraction::UpdateSelectionGizmo() {
    univex::math::Vec3 centroid{0.0F, 0.0F, 0.0F};
    int contributing = 0;
    for (const Scene::Entity entity : m_editor.GetSelectedEntities()) {
        if (entity == Scene::kInvalidEntity ||
            !m_entityManager.HasComponent<Scene::WorldTransformComponent>(entity)) {
            continue;
        }
        const auto& worldTransform = m_entityManager.GetComponent<Scene::WorldTransformComponent>(entity);
        centroid += univex::integration::FromUveVector3(worldTransform.worldPosition);
        ++contributing;
    }
    if (contributing > 0) {
        m_gizmoPivot = centroid * (1.0F / static_cast<float>(contributing));
    } else {
        m_gizmoPivot.reset();
    }
}

void ViewportSceneInteraction::UpdateSelectionFromMouse(
    const Editor::Editor::ViewportOverlayState& overlayState,
    const int width,
    const int height,
    const bool suppressed) {
    const Editor::EditorViewportInput& input = overlayState.input;
    overlayState.marqueeActive = false;

    const bool multiOrNav = input.alt || input.IsDown(VpBtn::Middle) || input.IsDown(VpBtn::Right) ||
                            std::fabs(input.wheel) > 1e-4F;
    if (suppressed || multiOrNav) {
        m_selectionPressActive = false;
        m_marqueeActive = false;
        return;
    }
    if (!m_pointerOverOverlay && input.hovered && input.IsClicked(VpBtn::Left)) {
        const Math::Vector2 imageOrigin = input.imageOrigin;
        m_selectionPressActive = true;
        m_marqueeActive = false;
        m_selectionPressX = input.mousePos.x - imageOrigin.x;
        m_selectionPressY = input.mousePos.y - imageOrigin.y;
    }
    if (!m_selectionPressActive) {
        return;
    }

    const float coordScale = std::max(0.25F, input.dpiScale);
    const float invCoordScale = 1.0F / coordScale;
    const float localX = std::clamp(input.mousePos.x - input.imageOrigin.x, 0.0F, static_cast<float>(width));
    const float localY = std::clamp(input.mousePos.y - input.imageOrigin.y, 0.0F, static_cast<float>(height));
    const float slop = (input.touchNavigation ? 8.0F : 6.0F) * coordScale;

    if (input.IsDown(VpBtn::Left) && !input.IsReleased(VpBtn::Left)) {
        if ((std::fabs(localX - m_selectionPressX) + std::fabs(localY - m_selectionPressY)) > slop) {
            m_marqueeActive = true;
        }
        if (m_marqueeActive) {
            overlayState.marqueeActive = true;
            overlayState.marqueeStartX = std::clamp(m_selectionPressX, 0.0F, static_cast<float>(width)) * invCoordScale;
            overlayState.marqueeStartY = std::clamp(m_selectionPressY, 0.0F, static_cast<float>(height)) * invCoordScale;
            overlayState.marqueeEndX = localX * invCoordScale;
            overlayState.marqueeEndY = localY * invCoordScale;
        }
        return;
    }

    if (!input.IsReleased(VpBtn::Left)) {
        m_selectionPressActive = false;
        m_marqueeActive = false;
        return;
    }

    const bool wasMarquee = m_marqueeActive;
    m_selectionPressActive = false;
    m_marqueeActive = false;

    if (wasMarquee) {
        const float startX = std::clamp(m_selectionPressX, 0.0F, static_cast<float>(width));
        const float startY = std::clamp(m_selectionPressY, 0.0F, static_cast<float>(height));
        CommitMarqueeSelection(input, width, height, std::min(startX, localX), std::min(startY, localY),
                                  std::max(startX, localX), std::max(startY, localY));
        return;
    }

    const Math::Vector2 dragDelta = input.DragDelta(VpBtn::Left);
    if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > slop) {
        return;
    }

    const univex::integration::EntityPickResult pick = univex::integration::PickEntityAtPixel(
        m_entityManager, m_camera, width, height, m_selectionPressX, m_selectionPressY);

    const bool extend = input.ctrl;
    if (!pick.hit) {
        if (!extend) {
            m_editor.ClearSelection();
        }
        return;
    }
    if (extend) {
        m_editor.ToggleEntitySelection(pick.entity);
    } else {
        m_editor.SelectEntity(pick.entity);
    }
}

void ViewportSceneInteraction::CommitMarqueeSelection(const Editor::EditorViewportInput& input,
                                                            const int width,
                                                            const int height,
                                                            const float minX,
                                                            const float minY,
                                                            const float maxX,
                                                            const float maxY) {
    if (width <= 0 || height <= 0 || (maxX - minX) < 1.0F || (maxY - minY) < 1.0F) {
        return;
    }

    std::vector<Scene::Entity> hits;
    m_entityManager.ForEach<Scene::WorldTransformComponent>(
        [&](const Scene::Entity entity, const Scene::WorldTransformComponent& worldTransform) {
            if (worldTransform.dirty || m_entityManager.HasComponent<Scene::EditorInternalEntityComponent>(entity) ||
                m_editor.IsEntityLocked(entity)) {
                return;
            }
            if (m_entityManager.HasComponent<Scene::VisibilityComponent>(entity)) {
                const auto& vis = m_entityManager.GetComponent<Scene::VisibilityComponent>(entity);
                if (!vis.visible || !vis.visibleInHierarchy) {
                    return;
                }
            }

            bool matched = false;
            float px = 0.0F;
            float py = 0.0F;
            if (univex::integration::ProjectWorldPointToPixel(m_camera, width, height, worldTransform.worldPosition,
                                                                 px, py)) {
                if (px >= minX && px <= maxX && py >= minY && py <= maxY) {
                    matched = true;
                }
            }

            if (!matched && m_entityManager.HasComponent<Scene::PrimitiveMeshComponent>(entity)) {
                const auto& primitive = m_entityManager.GetComponent<Scene::PrimitiveMeshComponent>(entity);
                Math::Matrix4x4 worldMatrix{};
                if (Scene::IsPrimitiveMeshComponentValid(primitive) &&
                    univex::integration::TryComputeWorldMatrix(worldTransform, worldMatrix)) {
                    const Math::Aabb bounds =
                        Render::GetPrimitiveGeometry(primitive.kind).localBounds.Transform(worldMatrix);
                    float boxMinX = std::numeric_limits<float>::max();
                    float boxMinY = std::numeric_limits<float>::max();
                    float boxMaxX = std::numeric_limits<float>::lowest();
                    float boxMaxY = std::numeric_limits<float>::lowest();
                    bool anyProjected = false;
                    for (int cx = 0; cx < 2; ++cx) {
                        for (int cy = 0; cy < 2; ++cy) {
                            for (int cz = 0; cz < 2; ++cz) {
                                const Math::Vector3 corner{cx == 0 ? bounds.min.x : bounds.max.x,
                                                              cy == 0 ? bounds.min.y : bounds.max.y,
                                                              cz == 0 ? bounds.min.z : bounds.max.z};
                                float cornerX = 0.0F;
                                float cornerY = 0.0F;
                                if (univex::integration::ProjectWorldPointToPixel(m_camera, width, height, corner,
                                                                                     cornerX, cornerY)) {
                                    boxMinX = std::min(boxMinX, cornerX);
                                    boxMinY = std::min(boxMinY, cornerY);
                                    boxMaxX = std::max(boxMaxX, cornerX);
                                    boxMaxY = std::max(boxMaxY, cornerY);
                                    anyProjected = true;
                                }
                            }
                        }
                    }
                    if (anyProjected && boxMaxX >= minX && boxMinX <= maxX && boxMaxY >= minY && boxMinY <= maxY) {
                        matched = true;
                    }
                }
            }

            if (matched) {
                hits.push_back(entity);
            }
        });

    const auto isSelected = [this](const Scene::Entity entity) {
        const auto& sel = m_editor.GetSelectedEntities();
        return std::find(sel.begin(), sel.end(), entity) != sel.end();
    };

    if (input.ctrl) {
        for (const Scene::Entity entity : hits) {
            m_editor.ToggleEntitySelection(entity);
        }
        return;
    }
    if (input.shift) {
        for (const Scene::Entity entity : hits) {
            if (!isSelected(entity)) {
                m_editor.ToggleEntitySelection(entity);
            }
        }
        return;
    }

    m_editor.ClearSelection();
    if (!hits.empty()) {
        m_editor.SelectEntity(hits.front());
        for (std::size_t i = 1U; i < hits.size(); ++i) {
            if (!isSelected(hits[i])) {
                m_editor.ToggleEntitySelection(hits[i]);
            }
        }
    }
}

void ViewportSceneInteraction::UpdateEntityContextToolbarFromMouse(const Editor::EditorViewportInput& input,
                                                                         const int width,
                                                                         const int height,
                                                                         const bool suppressed) {
    if (suppressed) {
        m_contextToolbarPressActive = false;
        return;
    }
    if (!m_pointerOverOverlay && input.hovered && input.IsClicked(VpBtn::Right)) {
        const Math::Vector2 imageOrigin = input.imageOrigin;
        m_contextToolbarPressActive = true;
        m_contextToolbarPressX = input.mousePos.x - imageOrigin.x;
        m_contextToolbarPressY = input.mousePos.y - imageOrigin.y;
    }
    if (!m_contextToolbarPressActive || !input.IsReleased(VpBtn::Right)) {
        return;
    }
    m_contextToolbarPressActive = false;

    const float coordScale = std::max(0.25F, input.dpiScale);
    const float slop = (input.touchNavigation ? 8.0F : kNavClickSlopPixels) * coordScale;
    const Math::Vector2 dragDelta = input.DragDelta(VpBtn::Right);
    if ((std::fabs(dragDelta.x) + std::fabs(dragDelta.y)) > slop) {
        return;
    }

    const univex::integration::EntityPickResult pick = univex::integration::PickEntityAtPixel(
        m_entityManager, m_camera, width, height, m_contextToolbarPressX, m_contextToolbarPressY);
    if (!pick.hit) {
        m_editor.ClearEntityContextToolbar();
        return;
    }

    m_editor.SelectEntity(pick.entity);
    if (!m_entityManager.HasComponent<Scene::WorldTransformComponent>(pick.entity)) {
        m_editor.ClearEntityContextToolbar();
        return;
    }
    const auto& worldTransform = m_entityManager.GetComponent<Scene::WorldTransformComponent>(pick.entity);
    float anchorPixelX = 0.0F;
    float anchorPixelY = 0.0F;
    if (univex::integration::ProjectWorldPointToPixel(m_camera, width, height, worldTransform.worldPosition,
                                                         anchorPixelX, anchorPixelY)) {
        const float invScale = 1.0F / coordScale;
        m_editor.SetEntityContextToolbarAnchor(pick.entity, anchorPixelX * invScale, anchorPixelY * invScale);
    } else {
        m_editor.ClearEntityContextToolbar();
    }
}

void ViewportSceneInteraction::UpdateTouchControllerCamera(
    const Editor::Editor::ViewportOverlayState& overlayState) {
    const float dt = std::clamp(overlayState.input.deltaTime, 0.0F, 0.1F);
    if (dt <= 0.0F) {
        return;
    }
    const auto& prefs = m_editor.GetExtendedPreferences();
    const float orbitSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0));
    const float zoomSens = static_cast<float>(std::clamp(prefs.zoomSensitivity, 0.05, 10.0));

    if (std::fabs(overlayState.touchLookX) > 1e-3F || std::fabs(overlayState.touchLookY) > 1e-3F) {
        constexpr float kLookPixelsPerSecond = 340.0F;
        m_camera.Look(overlayState.touchLookX * kLookPixelsPerSecond * orbitSens * dt,
                      overlayState.touchLookY * kLookPixelsPerSecond * orbitSens * dt);
        m_editor.NotifyViewportOrbited();
    }
    if (std::fabs(overlayState.touchZoomDelta) > 1e-3F) {
        m_camera.Dolly(overlayState.touchZoomDelta * zoomSens * dt * 6.0F);
    }

    const bool hasMove = std::fabs(overlayState.touchMoveX) > 1e-3F || std::fabs(overlayState.touchMoveY) > 1e-3F ||
                         std::fabs(overlayState.touchMoveUp) > 1e-3F;
    univex::math::Vec3 wanted{0.0F, 0.0F, 0.0F};
    if (hasMove) {
        univex::math::Vec3 forward = univex::math::Normalize(m_camera.Target() - m_camera.Eye());
        if (univex::math::Length(forward) < 0.001F) {
            forward = univex::math::Vec3{0.0F, 0.0F, -1.0F};
        }
        const univex::math::Vec3 up{0.0F, 1.0F, 0.0F};
        univex::math::Vec3 right = univex::math::Cross(forward, up);
        right = univex::math::Length(right) < 0.001F ? univex::math::Vec3{1.0F, 0.0F, 0.0F}
                                                     : univex::math::Normalize(right);
        univex::math::Vec3 wish = forward * overlayState.touchMoveY + right * overlayState.touchMoveX +
                                  up * overlayState.touchMoveUp;
        const float wishLen = univex::math::Length(wish);
        if (wishLen > 1.0F) {
            wish = wish * (1.0F / wishLen);
        }
        wanted = wish * m_flySpeed;
    }

    const float blend = 1.0F - std::exp(-dt * 14.0F);
    m_touchMoveVelocity = m_touchMoveVelocity + (wanted - m_touchMoveVelocity) * blend;
    if (univex::math::Length(m_touchMoveVelocity) > 0.001F) {
        m_camera.Translate(m_touchMoveVelocity * dt);
    } else if (!hasMove) {
        m_touchMoveVelocity = univex::math::Vec3{0.0F, 0.0F, 0.0F};
    }
}

void ViewportSceneInteraction::UpdateOrbitPanZoomFromMouse(const Editor::EditorViewportInput& input,
                                                                 const int framebufferHeight,
                                                                 const bool suppressOrbit) {
    if (m_pointerOverOverlay || !input.hovered) {
        return;
    }
    const auto& prefs = m_editor.GetExtendedPreferences();
    const float orbitSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0));
    const float panSens = static_cast<float>(std::clamp(prefs.panSensitivity, 0.05, 10.0));
    const float zoomSens = static_cast<float>(std::clamp(prefs.zoomSensitivity, 0.05, 10.0));
    const float coordScale = std::max(0.25F, input.dpiScale);
    const float invScale = 1.0F / coordScale;

    // Desktop: Alt+LMB orbits, MMB pans, wheel dollies (plain LMB hold+drag draws Marquee Selection).
    // Android touch: 1-finger empty drag draws Marquee Selection, on-screen dual-stick controller
    // drives Move/Look/Up/Down/Zoom, and 2-finger drag (Middle) / pinch (wheel) pan & dolly smoothly.
    const bool desktopOrbit = !input.touchNavigation && !suppressOrbit && input.alt &&
                              input.IsDragging(VpBtn::Left, 0.0F);
    if (desktopOrbit) {
        m_camera.Orbit(input.mouseDelta.x * invScale * orbitSens, input.mouseDelta.y * invScale * orbitSens);
        m_editor.NotifyViewportOrbited();
    } else if (input.IsDragging(VpBtn::Middle, 0.0F)) {
        m_camera.Pan(input.mouseDelta.x * panSens, input.mouseDelta.y * panSens, framebufferHeight);
    }
    if (input.wheel != 0.0F) {
        m_camera.Dolly(input.wheel * zoomSens);
    }
}

bool ViewportSceneInteraction::UpdateFlyCamera(const Editor::EditorViewportInput& input,
                                                     const bool suppressed) {
    // Pure-touch hosts use long-press Right click strictly for context menus, never WASD fly mode.
    if (input.touchNavigation) {
        return false;
    }
    if (!input.IsDown(VpBtn::Right)) {
        if (m_flying || m_flyArmed) {
            m_flying = false;
            m_flyArmed = false;
            m_flyVelocity = univex::math::Vec3{0.0F, 0.0F, 0.0F};
            m_editor.SetViewportFlying(false);
            m_editor.SetViewportFlyHeld(false);
        }
        return false;
    }
    if (!m_flying && !m_flyArmed) {
        if (suppressed || m_pointerOverOverlay || !input.hovered || !input.IsClicked(VpBtn::Right)) {
            return false;
        }
        m_flyArmed = true;
        m_editor.SetViewportFlyHeld(true);
    }
    const bool moveKey = input.IsKeyDown(VpKey::W) || input.IsKeyDown(VpKey::A) ||
                         input.IsKeyDown(VpKey::S) || input.IsKeyDown(VpKey::D) ||
                         input.IsKeyDown(VpKey::Q) || input.IsKeyDown(VpKey::E);
    if (!m_flying) {
        const Math::Vector2 drag = input.DragDelta(VpBtn::Right);
        if (!moveKey && std::fabs(drag.x) + std::fabs(drag.y) <= kNavClickSlopPixels) {
            return false;
        }
        m_flying = true;
        m_editor.SetViewportFlying(true);
    }

    const auto& prefs = m_editor.GetExtendedPreferences();
    const float lookSens = static_cast<float>(std::clamp(prefs.orbitSensitivity, 0.05, 10.0)) * 0.8F;
    if (input.mouseDelta.x != 0.0F || input.mouseDelta.y != 0.0F) {
        m_camera.Look(input.mouseDelta.x * lookSens, input.mouseDelta.y * lookSens);
        m_editor.NotifyViewportOrbited();
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
    if (input.IsKeyDown(VpKey::W)) {
        wish = wish + forward;
    }
    if (input.IsKeyDown(VpKey::S)) {
        wish = wish - forward;
    }
    if (input.IsKeyDown(VpKey::D)) {
        wish = wish + right;
    }
    if (input.IsKeyDown(VpKey::A)) {
        wish = wish - right;
    }
    if (input.IsKeyDown(VpKey::E)) {
        wish = wish + up;
    }
    if (input.IsKeyDown(VpKey::Q)) {
        wish = wish - up;
    }
    const float wishLength = univex::math::Length(wish);
    const float speed = m_flySpeed * (input.shift ? 3.0F : 1.0F);
    const univex::math::Vec3 wanted =
        wishLength > 0.001F ? wish * (speed / wishLength) : univex::math::Vec3{0.0F, 0.0F, 0.0F};
    const float dt = std::clamp(input.deltaTime, 0.0F, 0.1F);
    // cameraInertiaSmoothing is the velocity blend's time constant (s); 0 follows input instantly.
    const double smoothing = m_editor.GetExtendedPreferences().cameraInertiaSmoothing;
    const float blend = smoothing > 0.001 ? 1.0F - std::exp(-dt * static_cast<float>(2.1 / smoothing)) : 1.0F;
    m_flyVelocity = m_flyVelocity + (wanted - m_flyVelocity) * blend;
    if (univex::math::Length(m_flyVelocity) > 0.0005F) {
        m_camera.Translate(m_flyVelocity * dt);
    }
    return true;
}

void ViewportSceneInteraction::UpdateViewportViewHotkeys(const Editor::EditorViewportInput& input) {
    if (!input.hovered || input.wantTextInput) {
        return;
    }
    struct DigitKeys final {
        int digit;
        VpKey keypad;
        VpKey row;
    };
    static constexpr DigitKeys kKeys[] = {
        {1, VpKey::Keypad1, VpKey::Num1},
        {3, VpKey::Keypad3, VpKey::Num3},
        {5, VpKey::Keypad5, VpKey::Num5},
        {7, VpKey::Keypad7, VpKey::Num7},
    };
    for (const DigitKeys& keys : kKeys) {
        const bool pressed = input.IsKeyPressed(keys.keypad) || (input.alt && input.IsKeyPressed(keys.row));
        if (!pressed) {
            continue;
        }
        if (keys.digit == 5) {
            m_editor.SetViewportOrthographic(!m_editor.IsViewportOrthographic());
            continue;
        }
        if (const auto view = Editor::Editor::GetViewportViewForKeypadDigit(keys.digit, input.ctrl)) {
            m_editor.RequestViewportView(*view);
        }
    }
}

void ViewportSceneInteraction::UpdateViewportBookmarkHotkeys(const Editor::EditorViewportInput& input) {
    if (!input.hovered || input.wantTextInput || input.alt) {
        return;
    }
    static constexpr VpKey kDigitKeys[] = {
        VpKey::Num1, VpKey::Num2, VpKey::Num3, VpKey::Num4, VpKey::Num5,
        VpKey::Num6, VpKey::Num7, VpKey::Num8, VpKey::Num9, VpKey::Num0,
    };
    for (std::size_t order = 0U; order < 10U; ++order) {
        const std::size_t slot = (order + 1U) % 10U;
        if (!input.IsKeyPressed(kDigitKeys[order])) {
            continue;
        }
        if (input.ctrl) {
            const univex::math::Vec3 target = m_camera.Target();
            static_cast<void>(m_editor.SetViewportBookmark(
                slot, Editor::EditorViewportBookmark{Math::Vector3{target.x, target.y, target.z},
                                                        m_camera.Yaw(), m_camera.Pitch(), m_camera.Distance()}));
            continue;
        }
        const std::optional<Editor::EditorViewportBookmark> bookmark = m_editor.GetViewportBookmark(slot);
        if (!bookmark.has_value()) {
            continue;
        }
        m_editor.NotifyViewportOrbited();
        m_camera.CancelAnimation();
        m_camera.SetTarget(univex::integration::FromUveVector3(bookmark->target));
        m_camera.SetYawPitch(bookmark->yawRadians, bookmark->pitchRadians);
        m_camera.SetDistance(bookmark->distance);
    }
    if (input.IsKeyPressed(VpKey::F) && !input.ctrl) {
        static_cast<void>(m_editor.RequestViewportFocus(m_editor.GetSelectedEntity()));
    }
}

bool ViewportSceneInteraction::EnsureHostDepth(ViewportSurface& surface,
                                                     const int width,
                                                     const int height) {
    if (m_hostDepthFailed) {
        return false;
    }
    if (!m_hostDepthProgram.has_value()) {
        std::string error;
        m_hostDepthProgram = univex::render::ShaderProgram::Build(
            ViewportSurface::kFullscreenTriangleVertexShader, kHostDepthFragmentShader, error);
        if (!m_hostDepthProgram.has_value()) {
            UVE_ERROR("uve_editor_app: viewport depth export shader build failed: {}", error);
            m_hostDepthFailed = true;
            return false;
        }
    }
    if (surface.FullscreenVao() == 0U) {
        return false;
    }
#if defined(UNIVEX_GL_ES)
    const int w = std::max(1, (width + 3) / 4);
    const int h = std::max(1, (height + 3) / 4);
#else
    const int w = std::max(1, (width + 1) / 2);
    const int h = std::max(1, (height + 1) / 2);
#endif
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
#if defined(UNIVEX_GL_ES)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
#else
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, w, h, 0, GL_RED, GL_FLOAT, nullptr);
#endif
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

bool ViewportSceneInteraction::AnySelectedEntityMoved() const {
    for (const auto& snap : m_lastSelectedSnapshots) {
        if (!m_entityManager.IsAlive(snap.entity) ||
            !m_entityManager.HasComponent<Scene::WorldTransformComponent>(snap.entity)) {
            return true;
        }
        const auto& wt = m_entityManager.GetComponent<Scene::WorldTransformComponent>(snap.entity);
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
