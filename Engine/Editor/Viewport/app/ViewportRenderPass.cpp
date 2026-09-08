#include "ViewportRenderPass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace univex::app {

using univex::gizmo::BuildGizmoMesh;
using univex::gizmo::BuildNavGizmoMeshes;
using univex::gizmo::NavViewHalfExtent;
using univex::math::Normalize;
using univex::math::Vec3;
using univex::render::GizmoDrawParams;
using univex::render::GridSettings;

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
void main() {
    // Vertical gradient, darker at the top. Gives the horizon something to
    // sit against so the grid's fade reads as distance rather than as the
    // grid simply stopping.
    float t = clamp(vClipPos.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 top    = vec3(0.043, 0.055, 0.086);
    vec3 bottom = vec3(0.086, 0.102, 0.145);
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
      scene_(std::move(other.scene_)),
      backgroundProgram_(std::move(other.backgroundProgram_)),
      backgroundVao_(std::exchange(other.backgroundVao_, 0)),
      backgroundVbo_(std::exchange(other.backgroundVbo_, 0)),
      settings_(other.settings_),
      style_(other.style_),
      gizmoMode_(other.gizmoMode_),
      cubeHalfExtent_(other.cubeHalfExtent_),
      entitySource_(other.entitySource_),
      gizmoPivotOverride_(other.gizmoPivotOverride_) {}

ViewportRenderPass& ViewportRenderPass::operator=(ViewportRenderPass&& other) noexcept {
    if (this != &other) {
        Destroy();
        grid_ = std::move(other.grid_);
        gizmos_ = std::move(other.gizmos_);
        scene_ = std::move(other.scene_);
        backgroundProgram_ = std::move(other.backgroundProgram_);
        backgroundVao_ = std::exchange(other.backgroundVao_, 0);
        backgroundVbo_ = std::exchange(other.backgroundVbo_, 0);
        settings_ = other.settings_;
        style_ = other.style_;
        gizmoMode_ = other.gizmoMode_;
        cubeHalfExtent_ = other.cubeHalfExtent_;
        entitySource_ = other.entitySource_;
        gizmoPivotOverride_ = other.gizmoPivotOverride_;
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

    auto scene = ReferenceScene::Create(outError);
    if (!scene.has_value()) return std::nullopt;
    pass.scene_ = std::move(*scene);

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

Mat4 ViewportRenderPass::NavViewMatrix(const OrbitCamera& camera) {
    // Same orientation as the main camera, but always three units out from
    // the origin: the widget shows which way the world is facing, not where
    // the camera happens to be.
    const Vec3 offset = Normalize(camera.Eye() - camera.Target());
    return Mat4::LookAt(offset * 3.f, Vec3{0.f, 0.f, 0.f}, Vec3{0.f, 1.f, 0.f});
}

Mat4 ViewportRenderPass::NavViewProjection(const GizmoStyle& style, const OrbitCamera& camera) {
    const float halfExtent = NavViewHalfExtent(style);
    // Always orthographic and always square: a perspective nav gizmo would
    // make the near balls bigger than the far ones, which is exactly the
    // depth cue you do not want when the point is comparing directions.
    const Mat4 projection = Mat4::Orthographic(halfExtent, 1.f, -10.f, 10.f);
    return Mat4::Multiply(projection, NavViewMatrix(camera));
}

NavViewportRect ViewportRenderPass::NavViewportRectFor(const GizmoStyle& style,
                                                       int framebufferWidth,
                                                       int framebufferHeight) {
    NavViewportRect rect;
    rect.size = static_cast<int>(style.navPixelSize);
    const int margin = static_cast<int>(style.navMarginPx);
    rect.x = framebufferWidth - rect.size - margin;
    rect.y = framebufferHeight - rect.size - margin; // GL viewport origin is bottom-left
    return rect;
}

void ViewportRenderPass::DrawBackground() const {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    backgroundProgram_.Use();
    glBindVertexArray(backgroundVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
}

void ViewportRenderPass::DrawWorldYAxis(const OrbitCamera& camera, int width, int height) const {
    // The ground grid's own shader (InfiniteGridRenderer) only evaluates the XZ plane, so it has
    // no notion of a vertical Y line at all - completing the grid to the usual red/green/blue
    // 3-axis convention needs a real 3D line drawn through the origin, not a grid-shader tweak.
    // Reuses the same line-drawing pass already proven for the transform/nav gizmos (GizmoRenderer)
    // with depth testing turned on, but deliberately matches the ground grid's own red/blue axis
    // lines in the two ways that actually make it read as part of the same grid rather than a
    // separate overlay pasted on top of it:
    //   - same pixel width (GridSettings::axisWidthPixels, not the transform gizmo's own unrelated
    //     axisLineWidthPx - those two were previously different values by coincidence, not design)
    //   - the same kind of distance fade the ground grid's own horizon fade uses (smoothstep over
    //     fadeStartDistanceScale/fadeEndDistanceScale * the camera's orbit distance), approximated
    //     here as several short segments each drawn with a lower GizmoDrawParams::opacity the
    //     further they are from the camera - GizmoLine itself has no per-segment alpha, so this
    //     is genuine alpha blending against whatever is actually behind it (gradient background or
    //     scene geometry) rather than a guess at a flat color to fade toward.
    using univex::gizmo::GizmoLine;
    using univex::gizmo::GizmoMesh;
    using univex::math::Length;

    const GridSettings& gridSettings = grid_.Settings();
    const float axisWidthPixels = gridSettings.axisWidthPixels;
    const float halfLength = std::max(50.f, camera.Distance() * 3.f);
    const float fadeStart = camera.Distance() * gridSettings.fadeStartDistanceScale;
    const float fadeEnd = std::max(fadeStart + 1e-3f, camera.Distance() * gridSettings.fadeEndDistanceScale);

    GizmoDrawParams params;
    params.viewProjection = camera.ViewProjection(static_cast<float>(width) / static_cast<float>(height));
    params.origin = Vec3{0.f, 0.f, 0.f};
    params.scale = 1.f; // authored directly in world units, not gizmo units
    params.viewportWidth = static_cast<float>(width);
    params.viewportHeight = static_cast<float>(height);
    params.depthTest = true;
    params.depthWrite = false; // test against the scene, but never occlude it - matches the grid

    constexpr int kSegmentsPerSideUVE = 10;
    for (const float side : {-1.f, 1.f}) {
        for (int index = 0; index < kSegmentsPerSideUVE; ++index) {
            const float t0 = static_cast<float>(index) / static_cast<float>(kSegmentsPerSideUVE);
            const float t1 = static_cast<float>(index + 1) / static_cast<float>(kSegmentsPerSideUVE);
            const Vec3 pointA{0.f, side * halfLength * t0, 0.f};
            const Vec3 pointB{0.f, side * halfLength * t1, 0.f};
            const Vec3 midpoint{0.f, side * halfLength * (t0 + t1) * 0.5f, 0.f};
            const float distanceFromCamera = Length(midpoint - camera.Eye());
            const float fadeRatio = std::clamp((distanceFromCamera - fadeStart) / (fadeEnd - fadeStart), 0.f, 1.f);
            const float smoothFade = fadeRatio * fadeRatio * (3.f - 2.f * fadeRatio); // smoothstep
            const float opacity = 1.f - smoothFade;
            if (opacity < 0.01f) {
                continue;
            }
            GizmoMesh mesh;
            mesh.lines.push_back(GizmoLine{pointA, pointB, style_.axisColorY, axisWidthPixels});
            params.opacity = opacity;
            gizmos_.Draw(mesh, params);
        }
    }
}

void ViewportRenderPass::DrawTransformGizmo(const OrbitCamera& camera, int width, int height) const {
    const Vec3 viewDirection = Normalize(camera.Target() - camera.Eye());
    const float scale = univex::render::GizmoRenderer::ScaleForPixelRadius(camera, height,
                                                                          style_.gizmoPixelRadius);
    // Gizmo units per pixel: one pixel is worldPerPixel world units, and one
    // gizmo unit is `scale` world units.
    const float worldPerPixel = univex::render::WorldPerPixelAtPivot(camera, height);
    const float unitsPerPixel = (scale > 0.f) ? worldPerPixel / scale : 1.f;
    const auto mesh = BuildGizmoMesh(gizmoMode_, style_, viewDirection, unitsPerPixel);

    GizmoDrawParams params;
    params.viewProjection = camera.ViewProjection(static_cast<float>(width) / static_cast<float>(height));
    params.origin = gizmoPivotOverride_.value_or(camera.Target());
    params.scale = scale;
    params.viewportWidth = static_cast<float>(width);
    params.viewportHeight = static_cast<float>(height);
    // Clear depth first: the gizmo then draws over the whole scene (a handle
    // hidden inside the object it moves is useless) while still depth-sorting
    // against itself.
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    params.depthTest = true;
    params.depthWrite = true;
    gizmos_.Draw(mesh, params);
}

void ViewportRenderPass::DrawNavGizmo(const OrbitCamera& camera, int width, int height) const {
    const NavViewportRect rect = NavViewportRectFor(style_, width, height);
    if (rect.size <= 0 || rect.x < 0 || rect.y < 0) return;

    const Vec3 viewDirection = Normalize(camera.Target() - camera.Eye());
    const auto meshes = BuildNavGizmoMeshes(style_, viewDirection);

    glViewport(rect.x, rect.y, rect.size, rect.size);

    GizmoDrawParams params;
    params.viewProjection = NavViewProjection(style_, camera);
    params.origin = Vec3{0.f, 0.f, 0.f};
    params.scale = 1.f;
    params.viewportWidth = static_cast<float>(rect.size);
    params.viewportHeight = static_cast<float>(rect.size);
    params.depthTest = false; // six discs, painter-sorted in the builder

    // Three passes in order: stubs, then balls, then the letters on top. The
    // letters are strokes, so they ride the line pass and inherit its
    // analytic anti-aliasing rather than needing a font texture.
    gizmos_.Draw(meshes.underlay, params);
    gizmos_.Draw(meshes.overlay, params);

    glViewport(0, 0, width, height);
}

void ViewportRenderPass::RenderFrame(const OrbitCamera& camera,
                                     int framebufferWidth,
                                     int framebufferHeight) const {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;

    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glDepthMask(GL_TRUE); // clearing depth requires the write mask on
    glClearColor(0.043f, 0.055f, 0.086f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    if (settings_.viewEnvironment) DrawBackground();

    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);
    const Mat4 viewProjection = camera.ViewProjection(aspect);

    if (settings_.viewSceneGeometry) {
        if (entitySource_ != nullptr) {
            // One proxy cube per real host-engine entity, each at its own
            // world position/scale, instead of the single origin-relative
            // demo cube. No rotation yet - see EntityTransformSource.h.
            for (const auto& entity : entitySource_->GetEntityTransformsUVE()) {
                Mat4 model = Mat4::Identity();
                model.Set(0, 3, entity.positionX);
                model.Set(1, 3, entity.positionY);
                model.Set(2, 3, entity.positionZ);
                const float extent = cubeHalfExtent_ * entity.uniformScale;
                scene_.Draw(viewProjection, model, extent, settings_.display);
            }
        } else {
            scene_.Draw(viewProjection, cubeHalfExtent_, settings_.display);
        }
    }
    if (settings_.viewGrid) {
        grid_.Draw(camera, framebufferWidth, framebufferHeight);
        DrawWorldYAxis(camera, framebufferWidth, framebufferHeight);
    }
    if (settings_.viewTransformGizmo && gizmoMode_ != GizmoMode::Select) {
        DrawTransformGizmo(camera, framebufferWidth, framebufferHeight);
    }
    if (settings_.viewGizmos) {
        DrawNavGizmo(camera, framebufferWidth, framebufferHeight);
    }
}

} // namespace univex::app
