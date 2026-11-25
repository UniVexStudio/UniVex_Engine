#include "viewport_render_pass.h"

#include "univex/camera/viewport_metrics.h"
#include "univex/render/overlay_raster_state.h"
#include "univex/render/overlay_raster_state_gl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace univex::app {

using univex::math::Normalize;
using univex::math::Vec3;
using univex::render::GizmoDrawParams;

namespace {

constexpr const char* kBackgroundVertexSource = R"GLSL(#version 330 core
layout(location = 0) in vec2 aClipPos;
out vec2 vClipPos;
void main() {
    vClipPos = aClipPos;
    gl_Position = vec4(aClipPos, 0.0, 1.0);
}
)GLSL";

constexpr const char* kBackgroundFragmentSource = R"GLSL(#version 330 core
in vec2 vClipPos;
out vec4 fragColor;
uniform bool uNeutralEditorFallback;
void main() {
    // Viewport background is clean neutral achromatic tones. A real WorldEnvironment is composited over it.
    float t = clamp(vClipPos.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 top = uNeutralEditorFallback ? vec3(0.205, 0.210, 0.220) : vec3(0.125, 0.130, 0.140);
    vec3 bottom = uNeutralEditorFallback ? vec3(0.285, 0.290, 0.300) : vec3(0.185, 0.190, 0.200);
    fragColor = vec4(mix(bottom, top, t), 1.0);
}
)GLSL";

constexpr std::array<float, 6> kFullscreenTriangle = {
    -1.f, -1.f,
     3.f, -1.f,
    -1.f,  3.f,
};

} // namespace

ViewportRenderPass::~ViewportRenderPass() { Destroy(); }

ViewportRenderPass::ViewportRenderPass(ViewportRenderPass&& other) noexcept
    : grid_(std::move(other.grid_)),
      gizmos_(std::move(other.gizmos_)),
      selectionOutline_(std::move(other.selectionOutline_)),
      selectionOutlineSettings_(other.selectionOutlineSettings_),
      backgroundProgram_(std::move(other.backgroundProgram_)),
      backgroundVao_(std::exchange(other.backgroundVao_, 0)),
      backgroundVbo_(std::exchange(other.backgroundVbo_, 0)),
      settings_(other.settings_),
      style_(other.style_) {}

ViewportRenderPass& ViewportRenderPass::operator=(ViewportRenderPass&& other) noexcept {
    if (this != &other) {
        Destroy();
        grid_ = std::move(other.grid_);
        gizmos_ = std::move(other.gizmos_);
        selectionOutline_ = std::move(other.selectionOutline_);
        selectionOutlineSettings_ = other.selectionOutlineSettings_;
        backgroundProgram_ = std::move(other.backgroundProgram_);
        backgroundVao_ = std::exchange(other.backgroundVao_, 0);
        backgroundVbo_ = std::exchange(other.backgroundVbo_, 0);
        settings_ = other.settings_;
        style_ = other.style_;
    }
    return *this;
}

void ViewportRenderPass::Destroy() noexcept {
    if (backgroundVbo_ != 0) { glDeleteBuffers(1, &backgroundVbo_); backgroundVbo_ = 0; }
    if (backgroundVao_ != 0) { glDeleteVertexArrays(1, &backgroundVao_); backgroundVao_ = 0; }
}

std::optional<ViewportRenderPass> ViewportRenderPass::Create(std::string& outError) {
    ViewportRenderPass pass;

    auto grid = univex::render::InfiniteGridRenderer::CreateWithBuiltinShaders(outError);
    if (!grid.has_value()) return std::nullopt;
    pass.grid_ = std::move(*grid);

    auto gizmos = univex::render::GizmoRenderer::Create(outError);
    if (!gizmos.has_value()) return std::nullopt;
    pass.gizmos_ = std::move(*gizmos);

    // The outline is an aid, not the viewport: if its shaders will not build, the viewport still
    // works and simply draws no outline.
    std::string outlineError;
    pass.selectionOutline_ = univex::render::SelectionOutlineRenderer::CreateWithBuiltinShaders(outlineError);

    auto background = univex::render::ShaderProgram::Build(kBackgroundVertexSource,
                                                           kBackgroundFragmentSource, outError);
    if (!background.has_value()) return std::nullopt;
    pass.backgroundProgram_ = std::move(*background);

    glGenVertexArrays(1, &pass.backgroundVao_);
    glBindVertexArray(pass.backgroundVao_);
    glGenBuffers(1, &pass.backgroundVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, pass.backgroundVbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(kFullscreenTriangle.size() * sizeof(float)),
                 kFullscreenTriangle.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return pass;
}

bool ViewportRenderPass::SetAxisPalette(const univex::viewport::AxisPalette& palette) {
    if (!univex::viewport::IsAxisPaletteValid(palette)) {
        return false;
    }
    // The nav gizmo reads its colours off the same GizmoStyle, so the corner widget follows for
    // free - there is no third place to keep in step.
    univex::viewport::ApplyAxisPalette(palette, style_, grid_.Settings());
    return true;
}

void ViewportRenderPass::SetGridOpacity(const float opacity) {
    grid_.Settings().opacity = std::isfinite(opacity) ? std::clamp(opacity, 0.f, 1.f) : 1.f;
}

void ViewportRenderPass::SetGridPlane(const univex::render::GridPlane plane) {
    grid_.Settings().plane = plane;
}

void ViewportRenderPass::SetSelectionOutline(const univex::render::SelectionOutlineSettings& settings) {
    selectionOutlineSettings_ = univex::render::SanitizeSelectionOutlineSettings(settings);
}

void ViewportRenderPass::RenderSelectionOutline(const OrbitCamera& camera, const int framebufferWidth,
                                                   const int framebufferHeight,
                                                   const std::vector<univex::render::SelectionOutlineVertex>& triangles) {
    if (!selectionOutline_.has_value() || framebufferWidth <= 0 || framebufferHeight <= 0) {
        return;
    }
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    selectionOutline_->Draw(camera.ViewProjection(aspect), framebufferWidth, framebufferHeight, triangles,
                            selectionOutlineSettings_);
}

bool ViewportRenderPass::ReadSelectionMask(const OrbitCamera& camera, const int framebufferWidth,
                                              const int framebufferHeight,
                                              const std::vector<univex::render::SelectionOutlineVertex>& triangles,
                                              std::vector<std::uint8_t>& outMask) {
    outMask.clear();
    if (!selectionOutline_.has_value() || framebufferWidth <= 0 || framebufferHeight <= 0 ||
        !selectionOutlineSettings_.visible) {
        return false;
    }
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    return selectionOutline_->ReadMask(camera.ViewProjection(aspect), framebufferWidth, framebufferHeight, triangles,
                                       outMask);
}

void ViewportRenderPass::SetGridCellSize(const float cellSize) {
    // The shader divides by this spacing; zero, negative or NaN would blank or corrupt the grid.
    if (std::isfinite(cellSize) && cellSize > 0.f) {
        grid_.Settings().baseSpacing = cellSize;
    }
}

univex::viewport::AxisPalette ViewportRenderPass::GetAxisPalette() const {
    return univex::viewport::AxisPaletteOf(style_);
}

void ViewportRenderPass::DrawBackground() const {
    const univex::render::SavedOverlayRasterState savedState =
        univex::render::SaveOverlayRasterState();

    // The backdrop sits behind everything by construction, so it neither tests
    // nor writes depth - writing far-plane depth here would be indistinguishable
    // from the clear, and testing would only cost fill rate.
    univex::render::ApplyOverlayRasterState(
        univex::render::WithDepthTest(univex::render::kStudioBackdropSkyState, false));
    backgroundProgram_.Use();
    glBindVertexArray(backgroundVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // Restore: anything drawn after this pass (the host's own scene geometry,
    // in the interleaved ordering) must not inherit a disabled depth test.
    univex::render::RestoreOverlayRasterState(savedState);
}

void ViewportRenderPass::DrawCameraFrustums(const OrbitCamera& camera, int width, int height) const {
    if (cameraFrustumMesh_.Empty()) {
        return;
    }
    const Vec3 viewDirection = Normalize(camera.Target() - camera.Eye());
    GizmoDrawParams params;
    params.viewProjection = camera.ViewProjection(static_cast<float>(width) / static_cast<float>(height));
    params.origin = Vec3{0.f, 0.f, 0.f};
    params.scale = 1.f;
    params.viewDirection = viewDirection;
    params.viewportWidth = static_cast<float>(width);
    params.viewportHeight = static_cast<float>(height);
    univex::render::EnableOverlayDepthWrite();
    glClear(GL_DEPTH_BUFFER_BIT);
    params.depthTest = true;
    params.depthWrite = true;
    gizmos_.Draw(cameraFrustumMesh_, params);
}

void ViewportRenderPass::DrawBones(const OrbitCamera& camera, int width, int height) const {
    const Vec3 viewDirection = Normalize(camera.Target() - camera.Eye());
    const univex::gizmo::GizmoMesh mesh = univex::gizmo::BuildBoneMesh(bones_, viewDirection);
    GizmoDrawParams params;
    params.viewProjection = camera.ViewProjection(static_cast<float>(width) / static_cast<float>(height));
    params.origin = Vec3{0.f, 0.f, 0.f}; // the bones are already in world space
    params.scale = 1.f;
    params.viewDirection = viewDirection;
    params.viewportWidth = static_cast<float>(width);
    params.viewportHeight = static_cast<float>(height);
    // X-ray, like the gizmo: a rig is normally inside its mesh, and a bone you cannot see is one
    // you cannot check. Depth is cleared so the bones still sort against each other.
    univex::render::EnableOverlayDepthWrite();
    glClear(GL_DEPTH_BUFFER_BIT);
    params.depthTest = true;
    params.depthWrite = true;
    gizmos_.Draw(mesh, params);
}

void ViewportRenderPass::DrawColliderWireframes(const OrbitCamera& camera, int width, int height) const {
    univex::gizmo::GizmoMesh mesh;
    mesh.lines = colliderLines_;
    GizmoDrawParams params;
    params.viewProjection = camera.ViewProjection(static_cast<float>(width) / static_cast<float>(height));
    params.origin = Vec3{0.f, 0.f, 0.f}; // already in world space, like the bones
    params.scale = 1.f;
    params.viewDirection = Normalize(camera.Target() - camera.Eye());
    params.viewportWidth = static_cast<float>(width);
    params.viewportHeight = static_cast<float>(height);
    // Depth-TESTED and not depth-writing: the shapes have to hide behind the geometry they belong
    // to, and two colliders that overlap must not paint over each other depending on draw order.
    params.depthTest = true;
    params.depthWrite = false;
    gizmos_.Draw(mesh, params);
}

void ViewportRenderPass::Clear(int framebufferWidth, int framebufferHeight) const {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    univex::render::EnableOverlayDepthWrite(); // clearing depth requires the write mask on
    glClearColor(0.043F, 0.055F, 0.086F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void ViewportRenderPass::RenderBackground(const bool neutralEditorFallback) const {
    if (!settings_.viewEnvironment) {
        return;
    }
    backgroundProgram_.Use();
    glUniform1i(backgroundProgram_.UniformLocation("uNeutralEditorFallback"), neutralEditorFallback ? 1 : 0);
    DrawBackground();
}

void ViewportRenderPass::RenderGrid(const OrbitCamera& camera,
                                       int framebufferWidth,
                                       int framebufferHeight) const {
    if (framebufferWidth <= 0 || framebufferHeight <= 0 || !settings_.viewGrid) return;
    // The grid depth-tests (infinite_grid.frag writes real gl_FragDepth) but does
    // not write depth, so scene geometry already in this buffer correctly occludes
    // it, while the grid never occludes anything drawn after it.
    //
    // The vertical Y axis line is drawn inside the grid's own shader now (InfiniteGridRenderer /
    // infinite_grid.frag) rather than as a separate GizmoRenderer pass, so it shares the exact
    // same per-pixel anti-aliasing and distance fade as the X/Z axis lines instead of visibly
    // seaming against them.
    grid_.Draw(camera, framebufferWidth, framebufferHeight);
}

void ViewportRenderPass::RenderOverlay(const OrbitCamera& camera,
                                          int framebufferWidth,
                                          int framebufferHeight) const {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;
    // Before the bones, because the bone pass clears depth to x-ray the rig through its mesh; a
    // collider drawn after that would come out in front of the wall it is behind.
    if (!colliderLines_.empty()) {
        DrawColliderWireframes(camera, framebufferWidth, framebufferHeight);
    }
    if (!bones_.empty()) {
        DrawBones(camera, framebufferWidth, framebufferHeight);
    }
    DrawCameraFrustums(camera, framebufferWidth, framebufferHeight);
}

void ViewportRenderPass::RenderFrame(const OrbitCamera& camera,
                                     int framebufferWidth,
                                     int framebufferHeight) const {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;
    Clear(framebufferWidth, framebufferHeight);
    RenderBackground();
    RenderGrid(camera, framebufferWidth, framebufferHeight);
    RenderOverlay(camera, framebufferWidth, framebufferHeight);
}

} // namespace univex::app
