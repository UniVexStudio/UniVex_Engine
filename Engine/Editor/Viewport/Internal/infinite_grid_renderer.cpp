#include "univex/render/infinite_grid_renderer.h"
#include "univex/render/overlay_raster_state_gl.h"

#include "univex/camera/viewport_metrics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "univex/render/grid_shader_sources.h"

namespace univex::render {

namespace {

// A single triangle that covers the whole clip-space square. Bigger than
// the viewport on purpose: one triangle rasterises with no diagonal seam
// and no redundant fragments along a quad's shared edge.
constexpr std::array<float, 6> kFullscreenTriangle = {
    -1.f, -1.f,
     3.f, -1.f,
    -1.f,  3.f,
};

} // namespace

InfiniteGridRenderer::~InfiniteGridRenderer() { Destroy(); }

InfiniteGridRenderer::InfiniteGridRenderer(InfiniteGridRenderer&& other) noexcept
    : program_(std::move(other.program_)),
      uniforms_(other.uniforms_),
      vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      settings_(other.settings_) {}

InfiniteGridRenderer& InfiniteGridRenderer::operator=(InfiniteGridRenderer&& other) noexcept {
    if (this != &other) {
        Destroy();
        program_ = std::move(other.program_);
        uniforms_ = other.uniforms_;
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        settings_ = other.settings_;
    }
    return *this;
}

void InfiniteGridRenderer::Destroy() noexcept {
    if (vbo_ != 0) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_ != 0) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
}

std::optional<InfiniteGridRenderer> InfiniteGridRenderer::Create(std::string_view vertexSource,
                                                                 std::string_view fragmentSource,
                                                                 std::string& outError) {
    auto program = ShaderProgram::Build(vertexSource, fragmentSource, outError);
    if (!program.has_value()) return std::nullopt;

    InfiniteGridRenderer renderer;
    renderer.program_ = std::move(*program);
    renderer.uniforms_.viewProj = renderer.program_.UniformLocation("uViewProj");
    renderer.uniforms_.invViewProj = renderer.program_.UniformLocation("uInvViewProj");
    renderer.uniforms_.cameraPos = renderer.program_.UniformLocation("uCameraPos");
    renderer.uniforms_.baseSpacing = renderer.program_.UniformLocation("uBaseSpacing");
    renderer.uniforms_.targetCellPixels = renderer.program_.UniformLocation("uTargetCellPixels");
    renderer.uniforms_.lineWidthPixels = renderer.program_.UniformLocation("uLineWidthPixels");
    renderer.uniforms_.axisWidthPixels = renderer.program_.UniformLocation("uAxisWidthPixels");
    renderer.uniforms_.thinColor = renderer.program_.UniformLocation("uThinColor");
    renderer.uniforms_.midColor = renderer.program_.UniformLocation("uMidColor");
    renderer.uniforms_.thickColor = renderer.program_.UniformLocation("uThickColor");
    renderer.uniforms_.thinIntensity = renderer.program_.UniformLocation("uThinIntensity");
    renderer.uniforms_.midIntensity = renderer.program_.UniformLocation("uMidIntensity");
    renderer.uniforms_.thickIntensity = renderer.program_.UniformLocation("uThickIntensity");
    renderer.uniforms_.axisColorX = renderer.program_.UniformLocation("uAxisColorX");
    renderer.uniforms_.axisColorY = renderer.program_.UniformLocation("uAxisColorY");
    renderer.uniforms_.axisColorZ = renderer.program_.UniformLocation("uAxisColorZ");
    renderer.uniforms_.fadeStart = renderer.program_.UniformLocation("uFadeStart");
    renderer.uniforms_.fadeEnd = renderer.program_.UniformLocation("uFadeEnd");
    renderer.uniforms_.opacity = renderer.program_.UniformLocation("uOpacity");
    renderer.uniforms_.gridPlane = renderer.program_.UniformLocation("uGridPlane");

    glGenVertexArrays(1, &renderer.vao_);
    glBindVertexArray(renderer.vao_);

    glGenBuffers(1, &renderer.vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(kFullscreenTriangle.size() * sizeof(float)),
                 kFullscreenTriangle.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return renderer;
}

std::optional<InfiniteGridRenderer> InfiniteGridRenderer::CreateWithBuiltinShaders(std::string& outError) {
    return Create(shaders::kInfiniteGridVertexSource, shaders::kInfiniteGridFragmentSource, outError);
}

void InfiniteGridRenderer::Draw(const GridFrameParams& frame) const {
    if (!Valid()) return;

    // ---- remember the state we are about to change ------------------------
    const SavedOverlayRasterState savedState = SaveOverlayRasterState();
    const GLboolean scissorWasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    if (scissorWasEnabled == GL_TRUE) {
        glDisable(GL_SCISSOR_TEST);
    }
    // Foundation WP6: test against the scene, never occlude it - as data, applied table-driven.
    ApplyOverlayRasterState(kInfiniteGridState);

    program_.Use();
    if (uniforms_.viewProj >= 0) glUniformMatrix4fv(uniforms_.viewProj, 1, GL_FALSE, frame.viewProjection.Data());
    if (uniforms_.invViewProj >= 0) glUniformMatrix4fv(uniforms_.invViewProj, 1, GL_FALSE, frame.inverseViewProjection.Data());
    if (uniforms_.cameraPos >= 0) glUniform3f(uniforms_.cameraPos, frame.cameraPosition.x, frame.cameraPosition.y, frame.cameraPosition.z);

    if (uniforms_.baseSpacing >= 0) glUniform1f(uniforms_.baseSpacing, settings_.baseSpacing);
    if (uniforms_.targetCellPixels >= 0) glUniform1f(uniforms_.targetCellPixels, settings_.targetCellPixels);
    if (uniforms_.lineWidthPixels >= 0) glUniform1f(uniforms_.lineWidthPixels, settings_.lineWidthPixels);
    if (uniforms_.axisWidthPixels >= 0) glUniform1f(uniforms_.axisWidthPixels, settings_.axisWidthPixels);

    if (uniforms_.thinColor >= 0) glUniform3f(uniforms_.thinColor, settings_.thinColor.r, settings_.thinColor.g, settings_.thinColor.b);
    if (uniforms_.midColor >= 0) glUniform3f(uniforms_.midColor, settings_.midColor.r, settings_.midColor.g, settings_.midColor.b);
    if (uniforms_.thickColor >= 0) glUniform3f(uniforms_.thickColor, settings_.thickColor.r, settings_.thickColor.g, settings_.thickColor.b);
    if (uniforms_.thinIntensity >= 0) glUniform1f(uniforms_.thinIntensity, settings_.thinIntensity);
    if (uniforms_.midIntensity >= 0) glUniform1f(uniforms_.midIntensity, settings_.midIntensity);
    if (uniforms_.thickIntensity >= 0) glUniform1f(uniforms_.thickIntensity, settings_.thickIntensity);

    if (uniforms_.axisColorX >= 0) glUniform3f(uniforms_.axisColorX, settings_.axisColorX.r, settings_.axisColorX.g, settings_.axisColorX.b);
    if (uniforms_.axisColorY >= 0) glUniform3f(uniforms_.axisColorY, settings_.axisColorY.r, settings_.axisColorY.g, settings_.axisColorY.b);
    if (uniforms_.axisColorZ >= 0) glUniform3f(uniforms_.axisColorZ, settings_.axisColorZ.r, settings_.axisColorZ.g, settings_.axisColorZ.b);

    const float reference = frame.referenceDistance > 1e-4f ? frame.referenceDistance : 1e-4f;
    const float fadeStart = reference * settings_.fadeStartDistanceScale;
    const float fadeEnd = std::max(fadeStart + 0.5f, reference * settings_.fadeEndDistanceScale);
    if (uniforms_.fadeStart >= 0) glUniform1f(uniforms_.fadeStart, fadeStart);
    if (uniforms_.fadeEnd >= 0) glUniform1f(uniforms_.fadeEnd, fadeEnd);
    if (uniforms_.opacity >= 0) glUniform1f(uniforms_.opacity, settings_.opacity);
    if (uniforms_.gridPlane >= 0) glUniform1f(uniforms_.gridPlane, static_cast<float>(settings_.plane));

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    // ---- put it back ------------------------------------------------------
    if (scissorWasEnabled == GL_TRUE) {
        glEnable(GL_SCISSOR_TEST);
    }
    RestoreOverlayRasterState(savedState);
}

void InfiniteGridRenderer::Draw(const univex::camera::OrbitCamera& camera,
                                int framebufferWidth,
                                int framebufferHeight) const {
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;
    const float aspect = static_cast<float>(framebufferWidth) / static_cast<float>(framebufferHeight);

    GridFrameParams frame;
    frame.viewProjection = camera.ViewProjection(aspect);
    frame.inverseViewProjection = camera.InverseViewProjection(aspect);
    frame.cameraPosition = camera.Eye();
    frame.referenceDistance = camera.Distance();
    Draw(frame);
}

float WorldPerPixelAtPivot(const univex::camera::OrbitCamera& camera, int framebufferHeight) {
    return univex::camera::WorldPerPixelAtOrbitTargetUVE(camera, framebufferHeight);
}

} // namespace univex::render
