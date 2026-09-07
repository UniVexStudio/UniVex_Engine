// app/ViewportRenderPass.h
// -----------------------------------------------------------------------
// One frame of the viewport, in draw order:
//
//   background gradient  (View Environment)
//   scene geometry       (Normal / Wireframe / Unshaded)
//   infinite ground grid (View Grid)
//   transform gizmo      (View Transform Gizmo)   - on top, no depth test
//   orientation gizmo    (View Gizmos)            - own corner viewport
//
// Both the interactive demo and the headless capture tool render through
// this same function on purpose — a screenshot is only evidence about the
// app if it came out of the same code path the app runs.
// -----------------------------------------------------------------------
#pragma once

#include <optional>
#include <string>

#include "ReferenceScene.h"
#include "univex/camera/OrbitCamera.h"
#include "univex/gizmo/GizmoGeometry.h"
#include "univex/gizmo/GizmoStyle.h"
#include "univex/gizmo/NavGizmo.h"
#include "univex/integration/EntityTransformSource.h"
#include "univex/render/GizmoRenderer.h"
#include "univex/render/InfiniteGridRenderer.h"
#include "univex/viewport/ViewportSettings.h"

namespace univex::app {

using univex::camera::OrbitCamera;
using univex::gizmo::GizmoMode;
using univex::gizmo::GizmoStyle;
using univex::math::Mat4;

// Where the orientation gizmo lives, in OpenGL viewport coordinates
// (origin bottom-left).
struct NavViewportRect {
    int x = 0;
    int y = 0;
    int size = 0;
};

class ViewportRenderPass {
public:
    ViewportRenderPass() = default;
    ~ViewportRenderPass();

    ViewportRenderPass(const ViewportRenderPass&) = delete;
    ViewportRenderPass& operator=(const ViewportRenderPass&) = delete;
    ViewportRenderPass(ViewportRenderPass&& other) noexcept;
    ViewportRenderPass& operator=(ViewportRenderPass&& other) noexcept;

    [[nodiscard]] static std::optional<ViewportRenderPass> Create(std::string& outError);

    void RenderFrame(const OrbitCamera& camera, int framebufferWidth, int framebufferHeight) const;

    [[nodiscard]] univex::render::InfiniteGridRenderer& Grid() { return grid_; }
    [[nodiscard]] const univex::render::InfiniteGridRenderer& Grid() const { return grid_; }

    [[nodiscard]] univex::viewport::ViewportSettings& Settings() { return settings_; }
    [[nodiscard]] const univex::viewport::ViewportSettings& Settings() const { return settings_; }

    [[nodiscard]] GizmoStyle& Style() { return style_; }
    [[nodiscard]] const GizmoStyle& Style() const { return style_; }

    void SetGizmoMode(GizmoMode mode) { gizmoMode_ = mode; }
    [[nodiscard]] GizmoMode Mode() const { return gizmoMode_; }

    void SetCubeHalfExtent(float halfExtent) { cubeHalfExtent_ = halfExtent; }

    // When set, scene geometry is one proxy cube per entity returned by
    // `source` (drawn at that entity's world position/scale) instead of the
    // single origin-relative demo cube. `source` is not owned - the caller
    // (the host engine's viewport integration) keeps it alive at least as
    // long as it stays set here. Pass nullptr to restore the original
    // single-cube demo behavior.
    void SetEntitySource(const univex::integration::IEntityTransformSourceUVE* source) {
        entitySource_ = source;
    }

    // ---- nav gizmo geometry, shared with input handling -------------------
    // The nav gizmo's own camera: the main camera's rotation, no translation.
    [[nodiscard]] static Mat4 NavViewMatrix(const OrbitCamera& camera);
    [[nodiscard]] static Mat4 NavViewProjection(const GizmoStyle& style, const OrbitCamera& camera);
    [[nodiscard]] static NavViewportRect NavViewportRectFor(const GizmoStyle& style,
                                                            int framebufferWidth,
                                                            int framebufferHeight);

private:
    void DrawBackground() const;
    void DrawTransformGizmo(const OrbitCamera& camera, int width, int height) const;
    void DrawNavGizmo(const OrbitCamera& camera, int width, int height) const;
    void Destroy() noexcept;

    univex::render::InfiniteGridRenderer grid_;
    univex::render::GizmoRenderer gizmos_;
    ReferenceScene scene_;
    univex::render::ShaderProgram backgroundProgram_;
    GLuint backgroundVao_ = 0;
    GLuint backgroundVbo_ = 0;

    univex::viewport::ViewportSettings settings_{};
    GizmoStyle style_{};
    GizmoMode gizmoMode_ = GizmoMode::Universal;
    float cubeHalfExtent_ = 0.75f;
    const univex::integration::IEntityTransformSourceUVE* entitySource_ = nullptr;
};

} // namespace univex::app
