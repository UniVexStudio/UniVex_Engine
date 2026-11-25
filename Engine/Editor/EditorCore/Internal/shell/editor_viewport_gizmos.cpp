// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_viewport_gizmos.h"

#include "scene_object_type.h"
#include "quaternion.h"
#include "light_component.h"
#include "directional_light_3d.h"
#include "camera_component.h"
#include "children_index.h"
#include "transform_component.h"
#include "world_transform_component.h"

#include "prism/Application.h"
#include "prism/Theme.h"

#if defined(__ANDROID__)
#include "android_glfw.h"
#endif

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <functional>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

using View = Editor::ViewportView;
using Kind = Scene::Objects::SceneObjectKind;

constexpr float kNavSize = 89.0F;
constexpr float kNavMargin = 6.0F;
constexpr float kBillboardSize = 24.0F;
constexpr float kNavDragThreshold = 3.0F;
/// How strongly an object gizmo shows through the geometry in front of it (0 hides it).
constexpr float kHiddenGizmoAlpha = 0.22F;
/// A click this close to a camera or light glyph's line picks its object.
constexpr float kGlyphPickPixels = 5.0F;
/// Camera and light glyph sizes in world units: about the size of a 1-unit box.
constexpr float kCameraGlyphWorld = 1.1F;
constexpr float kLightGlyphWorld = 0.9F;

#if defined(__ANDROID__)
EditorViewportGizmos* g_activeTouchController = nullptr;
#endif

[[nodiscard]] bool RectContainsExpanded(const prism::Rect& rect, const prism::Vec2 p, const float pad) {
    return p.x >= rect.x - pad && p.x <= rect.right() + pad && p.y >= rect.y - pad && p.y <= rect.bottom() + pad;
}

[[nodiscard]] float ApplyDeadzone(const float value, const float deadzone) {
    const float absVal = std::abs(value);
    if (absVal <= deadzone) {
        return 0.0F;
    }
    const float rescaled = (absVal - deadzone) / std::max(1.0e-3F, 1.0F - deadzone);
    return value < 0.0F ? -std::min(1.0F, rescaled) : std::min(1.0F, rescaled);
}

[[nodiscard]] prism::Vec3 V3(const std::array<float, 3>& v) { return prism::Vec3{v[0], v[1], v[2]}; }

[[nodiscard]] prism::Color Color(const std::array<float, 3>& c, const float alpha = 1.0F) {
    const auto b = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(b(c[0]), b(c[1]), b(c[2]), b(alpha));
}

/// The facing a navigation ball snaps to: the camera moves to that side and looks back across the
/// origin (the +X ball shows the Right view, which looks down -X).
[[nodiscard]] View FacingForBall(const int id) {
    switch (id) {
        case 0: return View::Right;
        case 1: return View::Left;
        case 2: return View::Top;
        case 3: return View::Bottom;
        case 4: return View::Front;
        case 5: return View::Back;
        default: return View::User;
    }
}

/// Objects that are shown as a billboard (they have no surface of their own to pick).
[[nodiscard]] bool HasBillboard(const Kind kind) {
    switch (kind) {
        case Kind::Camera3D:
        case Kind::DirectionalLight3D:
        case Kind::Light3D:
        case Kind::WorldEnvironment3D:
        case Kind::ReflectionProbe3D:
        case Kind::FogVolume3D:
            return true;
        default:
            return false;
    }
}

} // namespace

EditorViewportGizmos::EditorViewportGizmos(Editor& editor) : m_editor(editor) {
    // Snapping is the editor's (PreviewTranslate/PreviewTransformGesture quantise), not the gizmo's.
    m_gizmo.snap = false;
#if defined(__ANDROID__)
    g_activeTouchController = this;
    Window::SetAndroidGlfwTouchClaimHook([](const float px, const float py) -> bool {
        return g_activeTouchController != nullptr && g_activeTouchController->TryClaimAndroidTouch(px, py);
    });
#endif
}

EditorViewportGizmos::~EditorViewportGizmos() {
#if defined(__ANDROID__)
    if (g_activeTouchController == this) {
        g_activeTouchController = nullptr;
        Window::SetAndroidGlfwTouchClaimHook({});
    }
#endif
}

bool EditorViewportGizmos::TouchControllerVisible() const {
#if defined(__ANDROID__)
    if (Window::AndroidGlfwHasHardwareInput()) {
        return false;
    }
    const prism::Rect r = rect();
    if (!visible() || m_gameWorkspace || r.w < 160.0F || r.h < 120.0F) {
        return false;
    }
    if (prism::Application::exists()) {
        const int winId = prism::Application::get().activeWindow();
        if (prism::Application::get().windowHasBlockingPopup(winId)) {
            return false;
        }
    }
    return true;
#else
    return false;
#endif
}

EditorViewportGizmos::TouchControllerLayout EditorViewportGizmos::ComputeTouchLayout() const {
    TouchControllerLayout layout{};
    const prism::Rect r = rect();
    const float padRadius = std::clamp(std::min(r.w, r.h) * 0.14F, 30.0F, 44.0F);
    constexpr float kMargin = 12.0F;
    layout.padRadius = padRadius;
    layout.thumbRadius = padRadius * 0.40F;
    layout.leftCenter = prism::Vec2{r.x + kMargin + padRadius, r.bottom() - kMargin - padRadius};
    layout.rightCenter = prism::Vec2{r.right() - kMargin - padRadius, r.bottom() - kMargin - padRadius};

    constexpr float kBtnW = 28.0F;
    constexpr float kBtnH = 24.0F;
    constexpr float kBtnGap = 4.0F;
    const float clusterRight = layout.rightCenter.x - padRadius - 10.0F;
    const float clusterBottom = r.bottom() - kMargin;
    const float col0X = clusterRight - (kBtnW * 2.0F + kBtnGap);
    const float col1X = clusterRight - kBtnW;
    const float row0Y = clusterBottom - (kBtnH * 2.0F + kBtnGap);
    const float row1Y = clusterBottom - kBtnH;
    layout.upBtn = prism::Rect{col0X, row0Y, kBtnW, kBtnH};
    layout.downBtn = prism::Rect{col0X, row1Y, kBtnW, kBtnH};
    layout.zoomInBtn = prism::Rect{col1X, row0Y, kBtnW, kBtnH};
    layout.zoomOutBtn = prism::Rect{col1X, row1Y, kBtnW, kBtnH};
    layout.toggleBtn = prism::Rect{r.x + 8.0F, r.y + 8.0F, 30.0F, 20.0F};
    return layout;
}

bool EditorViewportGizmos::TryClaimAndroidTouch(const float physicalX, const float physicalY) {
#if defined(__ANDROID__)
    if (!TouchControllerVisible() || !prism::Application::exists()) {
        return false;
    }
    const float uiScale = std::max(1.0F, prism::Application::get().platform().dpiScale());
    const prism::Vec2 p{physicalX / uiScale, physicalY / uiScale};
    const TouchControllerLayout layout = ComputeTouchLayout();
    if (RectContainsExpanded(layout.toggleBtn, p, 5.0F)) {
        m_touchControllerEnabled = !m_touchControllerEnabled;
        m_leftStickPointerId = -1;
        m_rightStickPointerId = -1;
        m_leftThumbOffset = prism::Vec2{0.0F, 0.0F};
        m_rightThumbOffset = prism::Vec2{0.0F, 0.0F};
        m_touchUpHeld = m_touchDownHeld = m_touchZoomInHeld = m_touchZoomOutHeld = false;
        invalidate();
        return true;
    }
    if (!m_touchControllerEnabled) {
        return false;
    }
    const float stickHitRadius = layout.padRadius + 10.0F;
    if (std::hypot(p.x - layout.leftCenter.x, p.y - layout.leftCenter.y) <= stickHitRadius) {
        return true;
    }
    if (std::hypot(p.x - layout.rightCenter.x, p.y - layout.rightCenter.y) <= stickHitRadius) {
        return true;
    }
    if (RectContainsExpanded(layout.upBtn, p, 4.0F) || RectContainsExpanded(layout.downBtn, p, 4.0F) ||
        RectContainsExpanded(layout.zoomInBtn, p, 4.0F) || RectContainsExpanded(layout.zoomOutBtn, p, 4.0F)) {
        return true;
    }
    return false;
#else
    static_cast<void>(physicalX);
    static_cast<void>(physicalY);
    return false;
#endif
}

void EditorViewportGizmos::ApplyTouchControllerToState(Editor::ViewportOverlayState& state) {
    state.touchMoveX = 0.0F;
    state.touchMoveY = 0.0F;
    state.touchMoveUp = 0.0F;
    state.touchLookX = 0.0F;
    state.touchLookY = 0.0F;
    state.touchZoomDelta = 0.0F;

#if defined(__ANDROID__)
    g_activeTouchController = this;
    m_touchControllerVisible = TouchControllerVisible();
    m_touchUpHeld = false;
    m_touchDownHeld = false;
    m_touchZoomInHeld = false;
    m_touchZoomOutHeld = false;

    if (!m_touchControllerVisible || !m_touchControllerEnabled || !prism::Application::exists()) {
        m_leftStickPointerId = -1;
        m_rightStickPointerId = -1;
        m_leftThumbOffset = prism::Vec2{0.0F, 0.0F};
        m_rightThumbOffset = prism::Vec2{0.0F, 0.0F};
        return;
    }

    const float uiScale = std::max(1.0F, prism::Application::get().platform().dpiScale());
    const TouchControllerLayout layout = ComputeTouchLayout();
    const std::vector<Window::AndroidTouchContact> contacts = Window::AndroidGlfwTouchContacts();

    bool leftAlive = false;
    bool rightAlive = false;
    for (const Window::AndroidTouchContact& c : contacts) {
        if (!c.claimed) {
            continue;
        }
        if (c.id == m_leftStickPointerId) {
            leftAlive = true;
        }
        if (c.id == m_rightStickPointerId) {
            rightAlive = true;
        }
    }
    if (!leftAlive) {
        m_leftStickPointerId = -1;
        m_leftThumbOffset = prism::Vec2{0.0F, 0.0F};
    }
    if (!rightAlive) {
        m_rightStickPointerId = -1;
        m_rightThumbOffset = prism::Vec2{0.0F, 0.0F};
    }

    const auto updateStick = [&](const prism::Vec2 p, const prism::Vec2 center, prism::Vec2& outOffset) {
        float dx = p.x - center.x;
        float dy = p.y - center.y;
        const float len = std::hypot(dx, dy);
        if (len > layout.padRadius && len > 1.0e-4F) {
            const float k = layout.padRadius / len;
            dx *= k;
            dy *= k;
        }
        outOffset = prism::Vec2{dx, dy};
    };

    const float stickBindRadius = layout.padRadius + 14.0F;
    for (const Window::AndroidTouchContact& c : contacts) {
        if (!c.claimed) {
            continue;
        }
        const prism::Vec2 p{c.x / uiScale, c.y / uiScale};
        if (c.id == m_leftStickPointerId) {
            updateStick(p, layout.leftCenter, m_leftThumbOffset);
            continue;
        }
        if (c.id == m_rightStickPointerId) {
            updateStick(p, layout.rightCenter, m_rightThumbOffset);
            continue;
        }
        if (m_leftStickPointerId < 0 &&
            std::hypot(p.x - layout.leftCenter.x, p.y - layout.leftCenter.y) <= stickBindRadius) {
            m_leftStickPointerId = c.id;
            updateStick(p, layout.leftCenter, m_leftThumbOffset);
        } else if (m_rightStickPointerId < 0 &&
                   std::hypot(p.x - layout.rightCenter.x, p.y - layout.rightCenter.y) <= stickBindRadius) {
            m_rightStickPointerId = c.id;
            updateStick(p, layout.rightCenter, m_rightThumbOffset);
        } else if (RectContainsExpanded(layout.upBtn, p, 6.0F)) {
            m_touchUpHeld = true;
            state.touchMoveUp += 1.0F;
        } else if (RectContainsExpanded(layout.downBtn, p, 6.0F)) {
            m_touchDownHeld = true;
            state.touchMoveUp -= 1.0F;
        } else if (RectContainsExpanded(layout.zoomInBtn, p, 6.0F)) {
            m_touchZoomInHeld = true;
            state.touchZoomDelta += 1.0F;
        } else if (RectContainsExpanded(layout.zoomOutBtn, p, 6.0F)) {
            m_touchZoomOutHeld = true;
            state.touchZoomDelta -= 1.0F;
        }
    }

    if (m_leftStickPointerId >= 0 && layout.padRadius > 1.0F) {
        state.touchMoveX = ApplyDeadzone(m_leftThumbOffset.x / layout.padRadius, 0.08F);
        state.touchMoveY = ApplyDeadzone(-m_leftThumbOffset.y / layout.padRadius, 0.08F);
    }
    if (m_rightStickPointerId >= 0 && layout.padRadius > 1.0F) {
        state.touchLookX = ApplyDeadzone(m_rightThumbOffset.x / layout.padRadius, 0.06F);
        state.touchLookY = ApplyDeadzone(m_rightThumbOffset.y / layout.padRadius, 0.06F);
    }
#endif
}

std::array<float, 2> EditorViewportGizmos::TakeOrbitRequest() {
    const std::array<float, 2> request = m_orbitRequest;
    m_orbitRequest = {0.0F, 0.0F};
    return request;
}

bool EditorViewportGizmos::UpdateCamera() {
    const auto& cam = m_editor.m_viewportOverlayState.viewCamera;
    m_cameraValid = cam.valid && cam.height > 0.0F && rect().w > 1.0F && rect().h > 1.0F;
    if (!m_cameraValid) {
        return false;
    }
    const prism::Rect r = rect();
    // Project onto the PRISM widget's logical rect even when the underlying 3D viewport renders at
    // higher physical pixel density (Retina/HiDPI), keeping gizmos, grids, and outlines 1:1 aligned.
    m_camera.eye = V3(cam.eye);
    m_camera.right = V3(cam.right);
    m_camera.up = V3(cam.up);
    m_camera.fwd = V3(cam.forward);
    m_camera.cx = r.x + r.w * 0.5F;
    m_camera.cy = r.y + r.h * 0.5F;
    m_camera.viewH = r.h;
    m_camera.nearPlane = std::max(0.01F, cam.nearPlane);
    m_camera.focal = (r.h * 0.5F) / std::max(1e-3F, std::tan(cam.fovYRadians * 0.5F));
    m_camera.ortho = cam.orthographic;
    m_camera.orthoScale = (r.h * 0.5F) / std::max(1e-3F, cam.orthographicHalfHeight);
    if (m_camera.ortho) {
        // Depth only orders and clips; keep the eye well behind anything on screen.
        m_camera.eye = m_camera.eye - m_camera.fwd * 1000.0F;
    }
    return true;
}

bool EditorViewportGizmos::TransformGizmoActive() const {
    const auto& state = m_editor.m_viewportOverlayState;
    return m_cameraValid && state.gizmoPivotValid && !state.gameWorkspaceActive && m_editor.HasSingleDocumentSelection() &&
           m_editor.IsAuthoringCommandAllowed();
}

void EditorViewportGizmos::Sync() {
    if (!UpdateCamera()) {
        m_billboards.clear();
        return;
    }
    // gizmoSizePx is the preference's on-screen size; 96 px is the widget's nominal (size == 1).
    m_gizmo.size = static_cast<float>(m_editor.GetExtendedPreferences().gizmoSizePx / 96.0);
    m_gizmo.hitScale = prism::Application::get().platform().singleSurface() ? 2.0F : 1.0F;
    // gizmoOpacity fades the axis and hot colours. Floored at 0.5 so the gizmo can never become hard to see.
    const double opacity = std::clamp(m_editor.GetExtendedPreferences().gizmoOpacity, 0.5, 1.0);
    const auto alphaByte = static_cast<std::uint8_t>(opacity * 255.0);
    for (auto& colour : m_gizmo.axisColor) {
        colour.a = alphaByte;
    }
    m_gizmo.hotColor.a = alphaByte;
    const auto& state = m_editor.m_viewportOverlayState;
    using Mode = Editor::ViewportGizmoMode;
    switch (state.gizmoMode) {
        case Mode::Move: m_gizmo.mode = prism::GizmoMode::Move; break;
        case Mode::Rotate: m_gizmo.mode = prism::GizmoMode::Rotate; break;
        case Mode::Scale: m_gizmo.mode = prism::GizmoMode::Scale; break;
        case Mode::Universal: m_gizmo.mode = prism::GizmoMode::Universal; break;
    }
    m_gizmo.setCamera(m_camera);
    if (state.gizmoPivotValid && m_dragging != Drag::Transform) {
        m_gizmo.setTarget(prism::Mat4::translation(V3(state.gizmoPivot)));
    }
    m_nav.setCamera(m_camera);
    const prism::Rect r = rect();

    // What this view paints, taken now: the level's and the Entity Editor's views share one overlay
    // state, so by paint time it holds whichever view rendered last.
    m_lines = state.gizmoLines;
    m_triangles = state.gizmoTriangles;
    m_aspect = r.h > 0.0F ? r.w / r.h : 1.6F;
    m_gameWorkspace = state.gameWorkspaceActive;
    // Occlusion: the renderer's scene depth over the whole view.
    m_depth = prism::GizmoDepth{};
    if (!state.sceneDepth.empty() && state.sceneDepthWidth > 0 && state.sceneDepthHeight > 0 &&
        state.sceneDepth.size() >= static_cast<std::size_t>(state.sceneDepthWidth) * static_cast<std::size_t>(state.sceneDepthHeight)) {
        // A copy of its own: the level's and the Entity Editor's views render into the same overlay
        // state, so by paint time the shared buffer holds the other view's depth.
        m_depthCopy.assign(state.sceneDepth.begin(), state.sceneDepth.end());
        m_depth.data = m_depthCopy.data();
        m_depth.width = state.sceneDepthWidth;
        m_depth.height = state.sceneDepthHeight;
        m_depth.area = prism::Rect{r.x, r.y, r.w, r.h};
        m_depth.zOffset = m_camera.ortho ? 1000.0F : 0.0F;
        // An orthographic eye sits far back, so a tolerance relative to its distance would swallow
        // whole objects; there the tolerance is absolute.
        m_depth.bias = m_camera.ortho ? 0.0F : 0.004F;
        m_depth.minTolerance = m_camera.ortho ? 0.03F : 0.02F;
    }

    // The ground grid, with the renderer's own settings.
    const auto& grid = state.hostGrid;
    m_gridVisible = grid.visible && !state.gameWorkspaceActive;
    if (m_gridVisible) {
        m_grid.plane = grid.plane == 1 ? prism::WorldGrid::Plane::XY
                       : grid.plane == 2 ? prism::WorldGrid::Plane::ZY
                                         : prism::WorldGrid::Plane::XZ;
        m_grid.baseSpacing = grid.baseSpacing;
        m_grid.targetCellPixels = grid.targetCellPixels;
        m_grid.lineWidth = grid.lineWidth;
        m_grid.axisWidth = grid.axisWidth;
        m_grid.thin = Color(grid.thin);
        m_grid.mid = Color(grid.mid);
        m_grid.thick = Color(grid.thick);
        m_grid.thinIntensity = grid.thinIntensity;
        m_grid.midIntensity = grid.midIntensity;
        m_grid.thickIntensity = grid.thickIntensity;
        for (int i = 0; i < 3; ++i) {
            m_grid.axis[i] = Color(grid.axis[static_cast<std::size_t>(i)]);
        }
        m_grid.fadeStart = grid.fadeStart;
        m_grid.fadeEnd = grid.fadeEnd;
        m_grid.opacity = grid.opacity;
    }

    // The selection outline band around the renderer's silhouette mask.
    m_outlinePixels.clear();
    if (!state.selectionMask.empty() && state.selectionMaskWidth > 0 && state.selectionMaskHeight > 0) {
        int box[4] = {0, 0, 0, 0};
        if (prism::buildSelectionOutline(state.selectionMask.data(), state.selectionMaskWidth, state.selectionMaskHeight,
                                         state.selectionOutlineRadius, Color(state.selectionOutlineRgb),
                                         m_outlinePixels, box)) {
            const float sx = r.w / static_cast<float>(state.selectionMaskWidth);
            const float sy = r.h / static_cast<float>(state.selectionMaskHeight);
            m_outlineRect = prism::Rect{r.x + static_cast<float>(box[0]) * sx, r.y + static_cast<float>(box[1]) * sy,
                                        static_cast<float>(box[2]) * sx, static_cast<float>(box[3]) * sy};
            m_outlineSize = {box[2], box[3]};
        }
    }
    m_nav.setArea(prism::Rect{r.x + r.w - kNavSize - kNavMargin, r.y + kNavMargin, kNavSize, kNavSize});

    // Billboards for objects with no surface of their own, at their world position.
    m_billboards.clear();
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    Scene::ChildrenIndex childrenIndex;
    if (!m_gameWorkspace) {
        childrenIndex.Rebuild(em);
    }
    const std::function<void(Scene::Entity)> visit = [&](const Scene::Entity e) {
        const Kind kind = Scene::ResolveSceneObjectKind(em, e);
        if (HasBillboard(kind) && em.HasComponent<Scene::WorldTransformComponent>(e)) {
            const auto& world = em.GetComponent<Scene::WorldTransformComponent>(e);
            Billboard board;
            board.entity = e;
            board.position = prism::Vec3{world.worldPosition.x, world.worldPosition.y, world.worldPosition.z};
            board.icon = m_icons.Object(kind, 32);
            // Cameras and lights look along their local -Z (the engine's camera and light convention).
            const Math::Vector3 forward = Math::RotateVector(world.worldRotation, Math::Vector3{0.0F, 0.0F, -1.0F});
            const Math::Vector3 up = Math::RotateVector(world.worldRotation, Math::Vector3{0.0F, 1.0F, 0.0F});
            board.forward = prism::Vec3{forward.x, forward.y, forward.z};
            board.up = prism::Vec3{up.x, up.y, up.z};
            if (em.HasComponent<Scene::CameraComponent>(e)) {
                const auto& camera = em.GetComponent<Scene::CameraComponent>(e);
                board.glyph = Glyph::Camera;
                board.fovYDegrees = camera.fieldOfViewDegrees;
                board.orthographic = camera.projection == Scene::CameraProjectionMode::Orthographic;
                board.range = std::clamp(camera.farPlane, 0.5F, 6.0F);
            } else if (em.HasComponent<Scene::DirectionalLight3DComponent>(e)) {
                board.glyph = Glyph::DirectionalLight;
            } else if (em.HasComponent<Scene::LightComponent>(e)) {
                const auto& light = em.GetComponent<Scene::LightComponent>(e);
                board.glyph = light.type == Scene::LightType::Spot          ? Glyph::SpotLight
                              : light.type == Scene::LightType::Directional ? Glyph::DirectionalLight
                                                                              : Glyph::Icon;
                board.spotHalfAngleDegrees = light.spotAngleDegrees;
                board.range = light.range;
                board.tint = prism::Color(static_cast<std::uint8_t>(std::clamp(light.color.r * 0.5F + 0.5F, 0.0F, 1.0F) * 255.0F),
                                          static_cast<std::uint8_t>(std::clamp(light.color.g * 0.5F + 0.5F, 0.0F, 1.0F) * 255.0F),
                                          static_cast<std::uint8_t>(std::clamp(light.color.b * 0.5F + 0.5F, 0.0F, 1.0F) * 255.0F));
            }
            if (board.glyph == Glyph::DirectionalLight) {
                board.tint = prism::Color(255, 222, 140);
            }
            m_billboards.push_back(std::move(board));
        }
        for (const Scene::Entity child : childrenIndex.GetChildren(e)) {
            visit(child);
        }
    };
    if (!m_gameWorkspace) {
        for (const Scene::Entity root : m_editor.GetDocumentRoots()) {
            visit(root);
        }
    }
    // Repainted only when what it draws moved: the camera, the view, the gizmo, the lines and
    // triangles, the billboards, the outline, or the scene depth the grid is hidden by. A still view
    // costs the UI nothing.
    std::uint64_t hash = 1469598103934665603ULL;
    const auto mix = [&hash](const void* data, const std::size_t bytes) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0U; i < bytes; ++i) {
            hash = (hash ^ p[i]) * 1099511628211ULL;
        }
    };
    mix(&m_camera, sizeof(m_camera));
    const prism::Rect view = rect();
    mix(&view, sizeof(view));
    mix(&state.gizmoPivot, sizeof(state.gizmoPivot));
    mix(&state.gizmoMode, sizeof(state.gizmoMode));
    const std::size_t counts[4] = {m_lines.size(), m_triangles.size(), m_billboards.size(), m_outlinePixels.size()};
    mix(counts, sizeof(counts));
    for (const Billboard& board : m_billboards) {
        mix(&board.position, sizeof(board.position));
    }
    if (!m_lines.empty()) {
        mix(m_lines.data(), std::min<std::size_t>(m_lines.size(), 64U) * sizeof(m_lines.front()));
    }
    mix(&m_outlineRect, sizeof(m_outlineRect));
    if (m_depth.data != nullptr) {
        const std::size_t total = static_cast<std::size_t>(m_depth.width) * static_cast<std::size_t>(m_depth.height);
        const std::size_t stride = std::max<std::size_t>(1U, total / 512U);
        for (std::size_t i = 0U; i < total; i += stride) {
            mix(&m_depth.data[i], sizeof(m_depth.data[i]));
        }
    }
    m_marqueeActive = state.marqueeActive && !m_gameWorkspace;
    if (m_marqueeActive) {
        const float x0 = r.x + std::min(state.marqueeStartX, state.marqueeEndX);
        const float y0 = r.y + std::min(state.marqueeStartY, state.marqueeEndY);
        const float x1 = r.x + std::max(state.marqueeStartX, state.marqueeEndX);
        const float y1 = r.y + std::max(state.marqueeStartY, state.marqueeEndY);
        m_marqueeRect = prism::Rect{x0, y0, std::max(1.0F, x1 - x0), std::max(1.0F, y1 - y0)};
    } else {
        m_marqueeRect = prism::Rect{};
    }
    m_touchControllerVisible = TouchControllerVisible();

    mix(&m_gridVisible, sizeof(m_gridVisible));
    mix(&m_marqueeActive, sizeof(m_marqueeActive));
    mix(&m_marqueeRect, sizeof(m_marqueeRect));
    mix(&m_touchControllerVisible, sizeof(m_touchControllerVisible));
    mix(&m_touchControllerEnabled, sizeof(m_touchControllerEnabled));
    mix(&m_leftThumbOffset, sizeof(m_leftThumbOffset));
    mix(&m_rightThumbOffset, sizeof(m_rightThumbOffset));
    const std::uint8_t touchBtnBits = static_cast<std::uint8_t>((m_touchUpHeld ? 1U : 0U) |
                                                                (m_touchDownHeld ? 2U : 0U) |
                                                                (m_touchZoomInHeld ? 4U : 0U) |
                                                                (m_touchZoomOutHeld ? 8U : 0U));
    mix(&touchBtnBits, sizeof(touchBtnBits));
    if (hash != m_drawnHash) {
        m_drawnHash = hash;
        invalidate();
    }
}

int EditorViewportGizmos::BillboardAt(const prism::Vec2 p) const {
    const float hitScale = prism::Application::get().platform().singleSurface() ? 1.75F : 1.0F;
    const float halfIcon = kBillboardSize * 0.5F * hitScale;
    for (std::size_t i = 0; i < m_billboards.size(); ++i) {
        prism::Vec2 s;
        if (!m_camera.project(m_billboards[i].position, s)) {
            continue;
        }
        const bool iconShown = m_billboards[i].glyph != Glyph::Camera;
        if (iconShown && std::abs(p.x - s.x) <= halfIcon && std::abs(p.y - s.y) <= halfIcon) {
            return static_cast<int>(i);
        }
    }
    // Then the glyphs themselves: a click on any of their lines picks the object.
    int best = -1;
    float bestDistance = kGlyphPickPixels * hitScale;
    for (std::size_t i = 0; i < m_billboards.size(); ++i) {
        const float d = prism::pickDistance(p, m_billboards[i].segments);
        if (d <= bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void EditorViewportGizmos::UpdateHover(const prism::Vec2 p) {
    bool hot = false;
    int gizmoHover = -1;
    int navHover = -1;
    if (m_cameraValid) {
        if (m_nav.contains(p)) {
            navHover = m_nav.hit(p);
            if (navHover < 0) {
                navHover = -2; // over the disc: draggable
            }
            hot = true;
        } else if (TransformGizmoActive() && (gizmoHover = m_gizmo.hit(p)) >= 0) {
            hot = true;
        } else if (BillboardAt(p) >= 0) {
            hot = true;
        }
    }
    const bool hoverChanged =
        (navHover != m_lastNavHover) || (gizmoHover != m_lastGizmoHover) || (hot != m_hot);
    m_lastNavHover = navHover;
    m_lastGizmoHover = gizmoHover;
    m_nav.setHover(navHover);
    m_gizmo.setHover(gizmoHover);
    m_hot = hot;
    if (hoverChanged) {
        invalidate();
    }
}

bool EditorViewportGizmos::onMouseDown(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left || !m_cameraValid) {
        return false;
    }
    UpdateHover(e.pos);
    if (m_nav.contains(e.pos)) {
        m_dragging = Drag::Nav;
        m_navPress = e.pos;
        m_navLast = e.pos;
        m_navMoved = false;
        m_navPressedBall = m_nav.hit(e.pos);
        m_nav.setActive(true);
        prism::Application::get().captureMouse(this);
        return true;
    }
    if (TransformGizmoActive()) {
        const int id = m_gizmo.hit(e.pos);
        if (id >= 0 && m_gizmo.begin(id, e.pos)) {
            EditorToolSessionMode mode = EditorToolSessionMode::Translate;
            if (m_gizmo.activeOp() == prism::GizmoOp::Rotate) {
                mode = EditorToolSessionMode::Rotate;
            } else if (m_gizmo.activeOp() == prism::GizmoOp::Scale) {
                mode = EditorToolSessionMode::Scale;
            }
            if (m_editor.GetToolSessionPhase() == EditorToolSessionPhase::Previewing) {
                static_cast<void>(m_editor.CommitTransformGesture());
            }
            const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
            const Scene::Entity selected = m_editor.GetSelectedEntity();
            if (em.HasComponent<Scene::TransformComponent>(selected)) {
                m_baselineScale = em.GetComponent<Scene::TransformComponent>(selected).localScale;
            }
            if (m_editor.BeginTransformGesture(mode)) {
                m_dragging = Drag::Transform;
                prism::Application::get().captureMouse(this);
                return true;
            }
            m_gizmo.end();
        }
    }
    const int billboard = BillboardAt(e.pos);
    if (billboard >= 0) {
        m_editor.SelectEntity(m_billboards[static_cast<std::size_t>(billboard)].entity);
        return true;
    }
    return false;
}

bool EditorViewportGizmos::onMouseMove(const prism::MouseEvent& e) {
    if (m_dragging == Drag::Nav) {
        const float dx = e.pos.x - m_navLast.x;
        const float dy = e.pos.y - m_navLast.y;
        if (std::abs(e.pos.x - m_navPress.x) > kNavDragThreshold || std::abs(e.pos.y - m_navPress.y) > kNavDragThreshold) {
            m_navMoved = true;
        }
        if (m_navMoved) {
            m_orbitRequest[0] += dx;
            m_orbitRequest[1] += dy;
        }
        m_navLast = e.pos;
        return true;
    }
    if (m_dragging == Drag::Transform) {
        const prism::GizmoDelta d = m_gizmo.drag(e.pos, (e.mods & prism::ModShift) != 0);
        const auto axisOf = [](const int axis) {
            return axis == 0 ? EditorTransformAxis::X
                   : axis == 1 ? EditorTransformAxis::Y
                   : axis == 2 ? EditorTransformAxis::Z
                               : EditorTransformAxis::None;
        };
        if (d.op == prism::GizmoOp::Move) {
            static_cast<void>(m_editor.PreviewTranslateGesture(Math::Vector3{d.translate.x, d.translate.y, d.translate.z}));
        } else if (d.op == prism::GizmoOp::Rotate) {
            // The editor's rotate amount is radians about the axis, measured from the press.
            static_cast<void>(m_editor.PreviewTransformGesture(axisOf(d.axis), d.angleDeg * 0.01745329252F));
        } else if (d.op == prism::GizmoOp::Scale) {
            // The editor's scale amount is added to the scale captured at Begin; the gizmo reports a
            // multiplier, so it becomes baseline * (multiplier - 1).
            const int axis = d.axis;
            const float multiplier = axis == 1 ? d.scale.y : axis == 2 ? d.scale.z : d.scale.x;
            const float base = axis == 1 ? m_baselineScale.y : axis == 2 ? m_baselineScale.z : m_baselineScale.x;
            static_cast<void>(m_editor.PreviewTransformGesture(axis == 3 ? EditorTransformAxis::None : axisOf(axis),
                                                                  base * (multiplier - 1.0F)));
        }
        invalidate();
        return true;
    }
    UpdateHover(e.pos);
    return false;
}

bool EditorViewportGizmos::onMouseUp(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (m_dragging == Drag::Nav) {
        if (!m_navMoved && m_navPressedBall >= 0) {
            m_editor.RequestViewportView(FacingForBall(m_navPressedBall));
        }
        m_dragging = Drag::None;
        m_nav.setActive(false);
        prism::Application::get().releaseMouse();
        UpdateHover(e.pos);
        return true;
    }
    if (m_dragging == Drag::Transform) {
        if (m_editor.GetToolSessionPhase() == EditorToolSessionPhase::Previewing) {
            static_cast<void>(m_editor.CommitTransformGesture());
        }
        m_gizmo.end();
        m_dragging = Drag::None;
        prism::Application::get().releaseMouse();
        UpdateHover(e.pos);
        return true;
    }
    return false;
}

void EditorViewportGizmos::onMouseLeave() {
    if (m_dragging == Drag::None) {
        const bool hoverChanged = m_hot || (m_lastNavHover != -1) || (m_lastGizmoHover != -1);
        m_hot = false;
        m_lastNavHover = -1;
        m_lastGizmoHover = -1;
        m_nav.setHover(-1);
        m_gizmo.setHover(-1);
        if (hoverChanged) {
            invalidate();
        }
    }
}

void EditorViewportGizmos::paint(prism::Canvas& c) {
    if (!m_cameraValid) {
        return;
    }
    const prism::Rect r = rect();
    c.pushClip(r);
    const auto& state = m_editor.m_viewportOverlayState;
    const prism::GizmoDepth* const depth = m_depth.valid() ? &m_depth : nullptr;
    // The ground grid, hidden behind the scene.
    if (m_gridVisible) {
        prism::drawWorldGrid(c, m_camera, m_grid, depth);
    }
    // The selection outline, over the scene and the grid.
    if (!m_outlinePixels.empty()) {
        c.blitBlend(m_outlinePixels.data(), m_outlineSize[0], m_outlineSize[1], m_outlineSize[0], m_outlineRect);
    }
    // Object gizmos: the engine's own geometry for this view, faint where the scene is in front.
    for (const auto& tri : m_triangles) {
        prism::drawWorldTriangle(c, m_camera, V3(tri.a), V3(tri.b), V3(tri.c), Color(tri.color, tri.alpha),
                                 depth, kHiddenGizmoAlpha);
    }
    for (const auto& line : m_lines) {
        prism::drawWorldLine(c, m_camera, V3(line.a), V3(line.b), Color(line.color),
                             std::max(1.0F, line.widthPx), depth, line.occluded ? 0.0F : kHiddenGizmoAlpha);
    }
    // Camera and light glyphs (PRISM's own), then the billboards, selected one ringed.
    for (Billboard& b : m_billboards) {
        b.segments.clear();
        if (b.glyph == Glyph::Icon) {
            continue;
        }
        // The camera the view looks through draws no glyph of its own: it would frame the view.
        if (b.glyph == Glyph::Camera && b.entity == m_editor.GetPreviewCamera()) {
            continue;
        }
        const bool selected = m_editor.IsEntitySelected(b.entity);
        prism::ObjectGizmoStyle style;
        style.color = selected ? prism::Color(255, 168, 64) : b.tint;
        style.width = selected ? 2.0F : 1.5F;
        style.depth = depth;
        style.hiddenAlpha = kHiddenGizmoAlpha;
        style.outSegments = &b.segments;
        switch (b.glyph) {
            case Glyph::Camera:
                // World sized, about as big as a 1-unit box; the frustum (selected) a little further out.
                style.worldSize = kCameraGlyphWorld;
                prism::drawCameraGizmo(c, m_camera, b.position, b.forward, b.up, b.fovYDegrees,
                                       m_aspect,
                                       b.orthographic, selected, std::min(b.range, kCameraGlyphWorld * 1.6F), style);
                break;
            case Glyph::DirectionalLight:
                style.worldSize = kLightGlyphWorld;
                prism::drawDirectionalLightGizmo(c, m_camera, b.position, b.forward, style);
                break;
            case Glyph::SpotLight:
                // The whole cone when selected; a short one otherwise, so a long range does not clutter the view.
                style.worldSize = kLightGlyphWorld;
                prism::drawSpotLightGizmo(c, m_camera, b.position, b.forward, b.spotHalfAngleDegrees,
                                          selected ? b.range : std::min(b.range, kLightGlyphWorld * 1.2F), style);
                break;
            case Glyph::Icon:
                break;
        }
    }
    for (const Billboard& b : m_billboards) {
        if (b.glyph == Glyph::Camera) {
            continue; // the camera glyph is its own icon
        }
        prism::Vec2 s;
        float z = 0.0F;
        if (!m_camera.project(b.position, s, &z)) {
            continue;
        }
        const bool hidden = depth != nullptr && !depth->visible(s, z);
        const prism::Rect box{std::floor(s.x - kBillboardSize * 0.5F), std::floor(s.y - kBillboardSize * 0.5F),
                              kBillboardSize, kBillboardSize};
        const bool selected = m_editor.IsEntitySelected(b.entity);
        c.fillCircle(s, kBillboardSize * 0.62F, prism::Color(14, 16, 22, selected ? 210 : 150));
        if (selected) {
            c.strokeCircle(s, kBillboardSize * 0.62F, prism::theme().highlight, 1.5F);
        }
        if (b.icon) {
            c.blitBlend(b.icon.pixels, b.icon.size, b.icon.size, b.icon.size, box);
        }
        if (hidden && !selected) {
            // Behind the scene: shown dimmed, so it can still be found and clicked.
            c.fillCircle(s, kBillboardSize * 0.62F, prism::Color(14, 16, 22, 150));
        }
    }
    // The selection's transform handles.
    if (TransformGizmoActive()) {
        m_gizmo.draw(c);
        if (m_dragging == Drag::Transform && !m_gizmo.label().empty()) {
            const prism::Theme& t = prism::theme();
            const prism::Vec2 m = prism::Application::get().mousePos();
            const prism::Rect tag{m.x + 14.0F, m.y + 10.0F, t.small->width(m_gizmo.label()) + 12.0F, 18.0F};
            c.fillRoundRect(tag, 3.0F, prism::Color(14, 16, 22, 220));
            c.textIn(*t.small, tag, m_gizmo.label(), t.textBright, prism::HAlign::Center, false);
        }
    }
    // Corner navigation.
    if (!m_gameWorkspace) {
        m_nav.draw(c);
    }
    // Marquee box selection rectangle (Desktop LMB hold+drag & Android non-controller hold+drag).
    if (m_marqueeActive && m_marqueeRect.w >= 1.0F && m_marqueeRect.h >= 1.0F) {
        c.fillRect(m_marqueeRect, prism::Color(66, 150, 250, 46));
        c.strokeRect(m_marqueeRect, prism::Color(110, 185, 255, 230), 1);
    }
#if defined(__ANDROID__)
    // Android-only Godot-style floating dual-thumb viewport controller (auto-hidden when mouse/KB active).
    if (m_touchControllerVisible) {
        const TouchControllerLayout layout = ComputeTouchLayout();
        const prism::Theme& t = prism::theme();

        // Compact top-left toggle pill to show/hide the on-screen controller.
        const prism::Color toggleBg = m_touchControllerEnabled ? prism::Color(16, 20, 28, 175)
                                                               : prism::Color(16, 20, 28, 110);
        const prism::Color toggleBorder = m_touchControllerEnabled ? prism::Color(102, 178, 255, 195)
                                                                   : prism::Color(200, 210, 225, 90);
        c.fillRoundRect(layout.toggleBtn, 4.0F, toggleBg);
        c.strokeRoundRect(layout.toggleBtn, 4.0F, toggleBorder, 1);
        c.textIn(*t.small, layout.toggleBtn, "PAD",
                 m_touchControllerEnabled ? prism::Color(215, 235, 255, 235) : prism::Color(195, 202, 215, 160),
                 prism::HAlign::Center, false);

        if (m_touchControllerEnabled) {
            const auto drawStick = [&](const prism::Vec2 center, const prism::Vec2 offset, const bool active,
                                       const char* label) {
                const float rad = layout.padRadius;
                c.fillCircle(center, rad, prism::Color(14, 17, 24, active ? 138 : 94));
                c.strokeCircle(center, rad,
                               active ? prism::Color(102, 178, 255, 215) : prism::Color(220, 228, 242, 108), 1.5F);
                const float tickIn = rad - 7.0F;
                const float tickOut = rad - 2.0F;
                const prism::Color tickCol(220, 228, 242, active ? 130 : 72);
                c.line(prism::Vec2{center.x, center.y - tickOut}, prism::Vec2{center.x, center.y - tickIn}, tickCol,
                       1.0F);
                c.line(prism::Vec2{center.x, center.y + tickIn}, prism::Vec2{center.x, center.y + tickOut}, tickCol,
                       1.0F);
                c.line(prism::Vec2{center.x - tickOut, center.y}, prism::Vec2{center.x - tickIn, center.y}, tickCol,
                       1.0F);
                c.line(prism::Vec2{center.x + tickIn, center.y}, prism::Vec2{center.x + tickOut, center.y}, tickCol,
                       1.0F);

                const prism::Vec2 nub{center.x + offset.x, center.y + offset.y};
                c.fillCircle(nub, layout.thumbRadius,
                             active ? prism::Color(92, 168, 255, 215) : prism::Color(225, 232, 245, 145));
                c.strokeCircle(nub, layout.thumbRadius,
                               active ? prism::Color(255, 255, 255, 235) : prism::Color(255, 255, 255, 165), 1.25F);

                const prism::Rect labelRect{center.x - rad, center.y + rad - 13.0F, rad * 2.0F, 12.0F};
                c.textIn(*t.small, labelRect, label, prism::Color(215, 225, 240, active ? 205 : 135),
                         prism::HAlign::Center, false);
            };

            drawStick(layout.leftCenter, m_leftThumbOffset, m_leftStickPointerId >= 0, "MOVE");
            drawStick(layout.rightCenter, m_rightThumbOffset, m_rightStickPointerId >= 0, "LOOK");

            const auto drawActionBtn = [&](const prism::Rect& btn, const bool held, const int glyphKind) {
                c.fillRoundRect(btn, 5.0F,
                                held ? prism::Color(86, 162, 255, 195) : prism::Color(14, 17, 24, 118));
                c.strokeRoundRect(btn, 5.0F,
                                  held ? prism::Color(190, 225, 255, 240) : prism::Color(220, 228, 242, 112), 1);
                const prism::Color fg = held ? prism::Color(255, 255, 255, 250) : prism::Color(225, 232, 245, 215);
                const float cx = btn.x + btn.w * 0.5F;
                const float cy = btn.y + btn.h * 0.5F;
                if (glyphKind == 0) {
                    // Up chevron
                    c.line(prism::Vec2{cx - 4.5F, cy + 2.0F}, prism::Vec2{cx, cy - 3.0F}, fg, 1.6F);
                    c.line(prism::Vec2{cx, cy - 3.0F}, prism::Vec2{cx + 4.5F, cy + 2.0F}, fg, 1.6F);
                } else if (glyphKind == 1) {
                    // Down chevron
                    c.line(prism::Vec2{cx - 4.5F, cy - 2.0F}, prism::Vec2{cx, cy + 3.0F}, fg, 1.6F);
                    c.line(prism::Vec2{cx, cy + 3.0F}, prism::Vec2{cx + 4.5F, cy - 2.0F}, fg, 1.6F);
                } else if (glyphKind == 2) {
                    // Zoom in (+)
                    c.line(prism::Vec2{cx - 4.5F, cy}, prism::Vec2{cx + 4.5F, cy}, fg, 1.6F);
                    c.line(prism::Vec2{cx, cy - 4.5F}, prism::Vec2{cx, cy + 4.5F}, fg, 1.6F);
                } else {
                    // Zoom out (-)
                    c.line(prism::Vec2{cx - 4.5F, cy}, prism::Vec2{cx + 4.5F, cy}, fg, 1.6F);
                }
            };

            drawActionBtn(layout.upBtn, m_touchUpHeld, 0);
            drawActionBtn(layout.downBtn, m_touchDownHeld, 1);
            drawActionBtn(layout.zoomInBtn, m_touchZoomInHeld, 2);
            drawActionBtn(layout.zoomOutBtn, m_touchZoomOutHeld, 3);
        }
    }
#endif
    c.popClip();
}

} // namespace UVE::Editor
