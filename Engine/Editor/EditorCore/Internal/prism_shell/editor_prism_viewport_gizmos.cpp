// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_viewport_gizmos.h"

#include "scene_object_type.h"
#include "quaternion.h"
#include "light_component.h"
#include "directional_light_3d.h"
#include "camera_component.h"
#include "transform_component.h"
#include "world_transform_component.h"

#include "prism/Application.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace UVE::Editor {
namespace {

using ViewUVE = EditorUVE::ViewportViewUVE;
using KindUVE = Scene::Objects::SceneObjectKindUVE;

constexpr float kNavSizeUVE = 89.0F;
constexpr float kNavMarginUVE = 6.0F;
constexpr float kBillboardSizeUVE = 24.0F;
constexpr float kNavDragThresholdUVE = 3.0F;
/// How strongly an object gizmo shows through the geometry in front of it (0 hides it).
constexpr float kHiddenGizmoAlphaUVE = 0.22F;
/// A click this close to a camera or light glyph's line picks its object.
constexpr float kGlyphPickPixelsUVE = 5.0F;
/// Camera and light glyph sizes in world units: about the size of a 1-unit box.
constexpr float kCameraGlyphWorldUVE = 1.1F;
constexpr float kLightGlyphWorldUVE = 0.9F;

[[nodiscard]] prism::Vec3 V3UVE(const std::array<float, 3>& v) { return prism::Vec3{v[0], v[1], v[2]}; }

[[nodiscard]] prism::Color ColorUVE(const std::array<float, 3>& c, const float alpha = 1.0F) {
    const auto b = [](const float v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0F, 1.0F) * 255.0F)); };
    return prism::Color(b(c[0]), b(c[1]), b(c[2]), b(alpha));
}

/// The facing a navigation ball snaps to: the camera moves to that side and looks back across the
/// origin (the +X ball shows the Right view, which looks down -X).
[[nodiscard]] ViewUVE FacingForBallUVE(const int id) {
    switch (id) {
        case 0: return ViewUVE::Right;
        case 1: return ViewUVE::Left;
        case 2: return ViewUVE::Top;
        case 3: return ViewUVE::Bottom;
        case 4: return ViewUVE::Front;
        case 5: return ViewUVE::Back;
        default: return ViewUVE::User;
    }
}

/// Objects that are shown as a billboard (they have no surface of their own to pick).
[[nodiscard]] bool HasBillboardUVE(const KindUVE kind) {
    switch (kind) {
        case KindUVE::Camera3D:
        case KindUVE::DirectionalLight3D:
        case KindUVE::Light3D:
        case KindUVE::WorldEnvironment3D:
        case KindUVE::ReflectionProbe3D:
        case KindUVE::FogVolume3D:
            return true;
        default:
            return false;
    }
}

} // namespace

EditorPrismViewportGizmosUVE::EditorPrismViewportGizmosUVE(EditorUVE& editor) : m_editor(editor) {
    // Snapping is the editor's (PreviewTranslate/PreviewTransformGestureUVE quantise), not the gizmo's.
    m_gizmo.snap = false;
}

std::array<float, 2> EditorPrismViewportGizmosUVE::TakeOrbitRequestUVE() {
    const std::array<float, 2> request = m_orbitRequest;
    m_orbitRequest = {0.0F, 0.0F};
    return request;
}

bool EditorPrismViewportGizmosUVE::UpdateCameraUVE() {
    const auto& cam = m_editor.m_viewportOverlayState.viewCamera;
    m_cameraValid = cam.valid && cam.height > 0.0F && rect().w > 1.0F;
    if (!m_cameraValid) {
        return false;
    }
    const prism::Rect r = rect();
    m_camera.eye = V3UVE(cam.eye);
    m_camera.right = V3UVE(cam.right);
    m_camera.up = V3UVE(cam.up);
    m_camera.fwd = V3UVE(cam.forward);
    m_camera.cx = r.x + cam.width * 0.5F;
    m_camera.cy = r.y + cam.height * 0.5F;
    m_camera.viewH = cam.height;
    m_camera.nearPlane = std::max(0.01F, cam.nearPlane);
    m_camera.focal = (cam.height * 0.5F) / std::max(1e-3F, std::tan(cam.fovYRadians * 0.5F));
    m_camera.ortho = cam.orthographic;
    m_camera.orthoScale = (cam.height * 0.5F) / std::max(1e-3F, cam.orthographicHalfHeight);
    if (m_camera.ortho) {
        // Depth only orders and clips; keep the eye well behind anything on screen.
        m_camera.eye = m_camera.eye - m_camera.fwd * 1000.0F;
    }
    return true;
}

bool EditorPrismViewportGizmosUVE::TransformGizmoActiveUVE() const {
    const auto& state = m_editor.m_viewportOverlayState;
    return m_cameraValid && state.gizmoPivotValid && !state.gameWorkspaceActive && m_editor.HasSingleDocumentSelectionUVE() &&
           m_editor.IsAuthoringCommandAllowedUVE();
}

void EditorPrismViewportGizmosUVE::SyncUVE() {
    if (!UpdateCameraUVE()) {
        m_billboards.clear();
        return;
    }
    const auto& state = m_editor.m_viewportOverlayState;
    using Mode = EditorUVE::ViewportGizmoModeUVE;
    switch (state.gizmoMode) {
        case Mode::Move: m_gizmo.mode = prism::GizmoMode::Move; break;
        case Mode::Rotate: m_gizmo.mode = prism::GizmoMode::Rotate; break;
        case Mode::Scale: m_gizmo.mode = prism::GizmoMode::Scale; break;
        case Mode::Universal: m_gizmo.mode = prism::GizmoMode::Universal; break;
    }
    m_gizmo.setCamera(m_camera);
    if (state.gizmoPivotValid && m_dragging != DragUVE::Transform) {
        m_gizmo.setTarget(prism::Mat4::translation(V3UVE(state.gizmoPivot)));
    }
    m_nav.setCamera(m_camera);
    const prism::Rect r = rect();
    const auto& cam = state.viewCamera;

    // What this view paints, taken now: the level's and the Entity Editor's views share one overlay
    // state, so by paint time it holds whichever view rendered last.
    m_lines = state.gizmoLines;
    m_triangles = state.gizmoTriangles;
    m_aspect = state.viewCamera.height > 0.0F ? state.viewCamera.width / state.viewCamera.height : 1.6F;
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
        m_depth.area = prism::Rect{r.x, r.y, cam.width, cam.height};
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
        m_grid.thin = ColorUVE(grid.thin);
        m_grid.mid = ColorUVE(grid.mid);
        m_grid.thick = ColorUVE(grid.thick);
        m_grid.thinIntensity = grid.thinIntensity;
        m_grid.midIntensity = grid.midIntensity;
        m_grid.thickIntensity = grid.thickIntensity;
        for (int i = 0; i < 3; ++i) {
            m_grid.axis[i] = ColorUVE(grid.axis[static_cast<std::size_t>(i)]);
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
                                         state.selectionOutlineRadius, ColorUVE(state.selectionOutlineRgb),
                                         m_outlinePixels, box)) {
            const float sx = cam.width / static_cast<float>(state.selectionMaskWidth);
            const float sy = cam.height / static_cast<float>(state.selectionMaskHeight);
            m_outlineRect = prism::Rect{r.x + static_cast<float>(box[0]) * sx, r.y + static_cast<float>(box[1]) * sy,
                                        static_cast<float>(box[2]) * sx, static_cast<float>(box[3]) * sy};
            m_outlineSize = {box[2], box[3]};
        }
    }
    m_nav.setArea(prism::Rect{r.x + r.w - kNavSizeUVE - kNavMarginUVE, r.y + kNavMarginUVE, kNavSizeUVE, kNavSizeUVE});

    // Billboards for objects with no surface of their own, at their world position.
    m_billboards.clear();
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const std::function<void(Scene::EntityUVE)> visit = [&](const Scene::EntityUVE e) {
        const KindUVE kind = Scene::ResolveSceneObjectKindUVE(em, e);
        if (HasBillboardUVE(kind) && em.HasComponentUVE<Scene::WorldTransformComponentUVE>(e)) {
            const auto& world = em.GetComponentUVE<Scene::WorldTransformComponentUVE>(e);
            BillboardUVE board;
            board.entity = e;
            board.position = prism::Vec3{world.worldPosition.x, world.worldPosition.y, world.worldPosition.z};
            board.icon = m_icons.ObjectUVE(kind, 32);
            // Cameras and lights look along their local -Z (the engine's camera and light convention).
            const Math::Vector3UVE forward = Math::RotateVectorUVE(world.worldRotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
            const Math::Vector3UVE up = Math::RotateVectorUVE(world.worldRotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
            board.forward = prism::Vec3{forward.x, forward.y, forward.z};
            board.up = prism::Vec3{up.x, up.y, up.z};
            if (em.HasComponentUVE<Scene::CameraComponentUVE>(e)) {
                const auto& camera = em.GetComponentUVE<Scene::CameraComponentUVE>(e);
                board.glyph = GlyphUVE::Camera;
                board.fovYDegrees = camera.fieldOfViewDegrees;
                board.orthographic = camera.projection == Scene::CameraProjectionModeUVE::Orthographic;
                board.range = std::clamp(camera.farPlane, 0.5F, 6.0F);
            } else if (em.HasComponentUVE<Scene::DirectionalLight3DComponentUVE>(e)) {
                board.glyph = GlyphUVE::DirectionalLight;
            } else if (em.HasComponentUVE<Scene::LightComponentUVE>(e)) {
                const auto& light = em.GetComponentUVE<Scene::LightComponentUVE>(e);
                board.glyph = light.type == Scene::LightTypeUVE::Spot          ? GlyphUVE::SpotLight
                              : light.type == Scene::LightTypeUVE::Directional ? GlyphUVE::DirectionalLight
                                                                              : GlyphUVE::Icon;
                board.spotHalfAngleDegrees = light.spotAngleDegrees;
                board.range = light.range;
                board.tint = prism::Color(static_cast<std::uint8_t>(std::clamp(light.color.r * 0.5F + 0.5F, 0.0F, 1.0F) * 255.0F),
                                          static_cast<std::uint8_t>(std::clamp(light.color.g * 0.5F + 0.5F, 0.0F, 1.0F) * 255.0F),
                                          static_cast<std::uint8_t>(std::clamp(light.color.b * 0.5F + 0.5F, 0.0F, 1.0F) * 255.0F));
            }
            if (board.glyph == GlyphUVE::DirectionalLight) {
                board.tint = prism::Color(255, 222, 140);
            }
            m_billboards.push_back(std::move(board));
        }
        for (const Scene::EntityUVE child : m_editor.m_services->GetSceneGraphUVE().GetChildrenUVE(em, e)) {
            visit(child);
        }
    };
    if (!m_gameWorkspace) {
        for (const Scene::EntityUVE root : m_editor.GetDocumentRootsUVE()) {
            visit(root);
        }
    }
    invalidate();
}

int EditorPrismViewportGizmosUVE::BillboardAtUVE(const prism::Vec2 p) const {
    for (std::size_t i = 0; i < m_billboards.size(); ++i) {
        prism::Vec2 s;
        if (!m_camera.project(m_billboards[i].position, s)) {
            continue;
        }
        const bool iconShown = m_billboards[i].glyph != GlyphUVE::Camera;
        if (iconShown && std::abs(p.x - s.x) <= kBillboardSizeUVE * 0.5F && std::abs(p.y - s.y) <= kBillboardSizeUVE * 0.5F) {
            return static_cast<int>(i);
        }
    }
    // Then the glyphs themselves: a click on any of their lines picks the object.
    int best = -1;
    float bestDistance = kGlyphPickPixelsUVE;
    for (std::size_t i = 0; i < m_billboards.size(); ++i) {
        const float d = prism::pickDistance(p, m_billboards[i].segments);
        if (d <= bestDistance) {
            bestDistance = d;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void EditorPrismViewportGizmosUVE::UpdateHoverUVE(const prism::Vec2 p) {
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
        } else if (TransformGizmoActiveUVE() && (gizmoHover = m_gizmo.hit(p)) >= 0) {
            hot = true;
        } else if (BillboardAtUVE(p) >= 0) {
            hot = true;
        }
    }
    m_nav.setHover(navHover);
    m_gizmo.setHover(gizmoHover);
    if (hot != m_hot) {
        m_hot = hot;
    }
    invalidate();
}

bool EditorPrismViewportGizmosUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left || !m_cameraValid) {
        return false;
    }
    UpdateHoverUVE(e.pos);
    if (m_nav.contains(e.pos)) {
        m_dragging = DragUVE::Nav;
        m_navPress = e.pos;
        m_navLast = e.pos;
        m_navMoved = false;
        m_navPressedBall = m_nav.hit(e.pos);
        m_nav.setActive(true);
        prism::Application::get().captureMouse(this);
        return true;
    }
    if (TransformGizmoActiveUVE()) {
        const int id = m_gizmo.hit(e.pos);
        if (id >= 0 && m_gizmo.begin(id, e.pos)) {
            EditorToolSessionModeUVE mode = EditorToolSessionModeUVE::Translate;
            if (m_gizmo.activeOp() == prism::GizmoOp::Rotate) {
                mode = EditorToolSessionModeUVE::Rotate;
            } else if (m_gizmo.activeOp() == prism::GizmoOp::Scale) {
                mode = EditorToolSessionModeUVE::Scale;
            }
            if (m_editor.GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
                static_cast<void>(m_editor.CommitTransformGestureUVE());
            }
            const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
            const Scene::EntityUVE selected = m_editor.GetSelectedEntityUVE();
            if (em.HasComponentUVE<Scene::TransformComponentUVE>(selected)) {
                m_baselineScale = em.GetComponentUVE<Scene::TransformComponentUVE>(selected).localScale;
            }
            if (m_editor.BeginTransformGestureUVE(mode)) {
                m_dragging = DragUVE::Transform;
                prism::Application::get().captureMouse(this);
                return true;
            }
            m_gizmo.end();
        }
    }
    const int billboard = BillboardAtUVE(e.pos);
    if (billboard >= 0) {
        m_editor.SelectEntityUVE(m_billboards[static_cast<std::size_t>(billboard)].entity);
        return true;
    }
    return false;
}

bool EditorPrismViewportGizmosUVE::onMouseMove(const prism::MouseEvent& e) {
    if (m_dragging == DragUVE::Nav) {
        const float dx = e.pos.x - m_navLast.x;
        const float dy = e.pos.y - m_navLast.y;
        if (std::abs(e.pos.x - m_navPress.x) > kNavDragThresholdUVE || std::abs(e.pos.y - m_navPress.y) > kNavDragThresholdUVE) {
            m_navMoved = true;
        }
        if (m_navMoved) {
            m_orbitRequest[0] += dx;
            m_orbitRequest[1] += dy;
        }
        m_navLast = e.pos;
        return true;
    }
    if (m_dragging == DragUVE::Transform) {
        const prism::GizmoDelta d = m_gizmo.drag(e.pos, (e.mods & prism::ModShift) != 0);
        const auto axisOf = [](const int axis) {
            return axis == 0 ? EditorTransformAxisUVE::X
                   : axis == 1 ? EditorTransformAxisUVE::Y
                   : axis == 2 ? EditorTransformAxisUVE::Z
                               : EditorTransformAxisUVE::None;
        };
        if (d.op == prism::GizmoOp::Move) {
            static_cast<void>(m_editor.PreviewTranslateGestureUVE(Math::Vector3UVE{d.translate.x, d.translate.y, d.translate.z}));
        } else if (d.op == prism::GizmoOp::Rotate) {
            // The editor's rotate amount is radians about the axis, measured from the press.
            static_cast<void>(m_editor.PreviewTransformGestureUVE(axisOf(d.axis), d.angleDeg * 0.01745329252F));
        } else if (d.op == prism::GizmoOp::Scale) {
            // The editor's scale amount is added to the scale captured at Begin; the gizmo reports a
            // multiplier, so it becomes baseline * (multiplier - 1).
            const int axis = d.axis;
            const float multiplier = axis == 1 ? d.scale.y : axis == 2 ? d.scale.z : d.scale.x;
            const float base = axis == 1 ? m_baselineScale.y : axis == 2 ? m_baselineScale.z : m_baselineScale.x;
            static_cast<void>(m_editor.PreviewTransformGestureUVE(axis == 3 ? EditorTransformAxisUVE::None : axisOf(axis),
                                                                  base * (multiplier - 1.0F)));
        }
        invalidate();
        return true;
    }
    UpdateHoverUVE(e.pos);
    return false;
}

bool EditorPrismViewportGizmosUVE::onMouseUp(const prism::MouseEvent& e) {
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (m_dragging == DragUVE::Nav) {
        if (!m_navMoved && m_navPressedBall >= 0) {
            m_editor.RequestViewportViewUVE(FacingForBallUVE(m_navPressedBall));
        }
        m_dragging = DragUVE::None;
        m_nav.setActive(false);
        prism::Application::get().releaseMouse();
        UpdateHoverUVE(e.pos);
        return true;
    }
    if (m_dragging == DragUVE::Transform) {
        if (m_editor.GetToolSessionPhaseUVE() == EditorToolSessionPhaseUVE::Previewing) {
            static_cast<void>(m_editor.CommitTransformGestureUVE());
        }
        m_gizmo.end();
        m_dragging = DragUVE::None;
        prism::Application::get().releaseMouse();
        UpdateHoverUVE(e.pos);
        return true;
    }
    return false;
}

void EditorPrismViewportGizmosUVE::onMouseLeave() {
    if (m_dragging == DragUVE::None) {
        m_hot = false;
        m_nav.setHover(-1);
        m_gizmo.setHover(-1);
        invalidate();
    }
}

void EditorPrismViewportGizmosUVE::paint(prism::Canvas& c) {
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
        prism::drawWorldTriangle(c, m_camera, V3UVE(tri.a), V3UVE(tri.b), V3UVE(tri.c), ColorUVE(tri.color, tri.alpha),
                                 depth, kHiddenGizmoAlphaUVE);
    }
    for (const auto& line : m_lines) {
        prism::drawWorldLine(c, m_camera, V3UVE(line.a), V3UVE(line.b), ColorUVE(line.color),
                             std::max(1.0F, line.widthPx), depth, line.occluded ? 0.0F : kHiddenGizmoAlphaUVE);
    }
    // Camera and light glyphs (PRISM's own), then the billboards, selected one ringed.
    for (BillboardUVE& b : m_billboards) {
        b.segments.clear();
        if (b.glyph == GlyphUVE::Icon) {
            continue;
        }
        // The camera the view looks through draws no glyph of its own: it would frame the view.
        if (b.glyph == GlyphUVE::Camera && b.entity == m_editor.GetPreviewCameraUVE()) {
            continue;
        }
        const bool selected = m_editor.IsEntitySelectedUVE(b.entity);
        prism::ObjectGizmoStyle style;
        style.color = selected ? prism::Color(255, 168, 64) : b.tint;
        style.width = selected ? 2.0F : 1.5F;
        style.depth = depth;
        style.hiddenAlpha = kHiddenGizmoAlphaUVE;
        style.outSegments = &b.segments;
        switch (b.glyph) {
            case GlyphUVE::Camera:
                // World sized, about as big as a 1-unit box; the frustum (selected) a little further out.
                style.worldSize = kCameraGlyphWorldUVE;
                prism::drawCameraGizmo(c, m_camera, b.position, b.forward, b.up, b.fovYDegrees,
                                       m_aspect,
                                       b.orthographic, selected, std::min(b.range, kCameraGlyphWorldUVE * 1.6F), style);
                break;
            case GlyphUVE::DirectionalLight:
                style.worldSize = kLightGlyphWorldUVE;
                prism::drawDirectionalLightGizmo(c, m_camera, b.position, b.forward, style);
                break;
            case GlyphUVE::SpotLight:
                // The whole cone when selected; a short one otherwise, so a long range does not clutter the view.
                style.worldSize = kLightGlyphWorldUVE;
                prism::drawSpotLightGizmo(c, m_camera, b.position, b.forward, b.spotHalfAngleDegrees,
                                          selected ? b.range : std::min(b.range, kLightGlyphWorldUVE * 1.2F), style);
                break;
            case GlyphUVE::Icon:
                break;
        }
    }
    for (const BillboardUVE& b : m_billboards) {
        if (b.glyph == GlyphUVE::Camera) {
            continue; // the camera glyph is its own icon
        }
        prism::Vec2 s;
        float z = 0.0F;
        if (!m_camera.project(b.position, s, &z)) {
            continue;
        }
        const bool hidden = depth != nullptr && !depth->visible(s, z);
        const prism::Rect box{std::floor(s.x - kBillboardSizeUVE * 0.5F), std::floor(s.y - kBillboardSizeUVE * 0.5F),
                              kBillboardSizeUVE, kBillboardSizeUVE};
        const bool selected = m_editor.IsEntitySelectedUVE(b.entity);
        c.fillCircle(s, kBillboardSizeUVE * 0.62F, prism::Color(14, 16, 22, selected ? 210 : 150));
        if (selected) {
            c.strokeCircle(s, kBillboardSizeUVE * 0.62F, prism::theme().accent, 1.5F);
        }
        if (b.icon) {
            c.blitBlend(b.icon.pixels, b.icon.size, b.icon.size, b.icon.size, box);
        }
        if (hidden && !selected) {
            // Behind the scene: shown dimmed, so it can still be found and clicked.
            c.fillCircle(s, kBillboardSizeUVE * 0.62F, prism::Color(14, 16, 22, 150));
        }
    }
    // The selection's transform handles.
    if (TransformGizmoActiveUVE()) {
        m_gizmo.draw(c);
        if (m_dragging == DragUVE::Transform && !m_gizmo.label().empty()) {
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
    c.popClip();
}

} // namespace UVE::Editor
