// Engine/Editor/Viewport/Internal/StudioBackdropRenderer.cpp

#include "univex/render/StudioBackdropRenderer.h"

#include <array>
#include <utility>

namespace univex::render {
namespace {

constexpr const char* kBackdropVertexSource = R"GLSL(#version 330 core
layout(location = 0) in vec2 aClipPos;
out vec2 vClipPos;
void main() {
    vClipPos = aClipPos;
    gl_Position = vec4(aClipPos, 0.0, 1.0);
}
)GLSL";

// The colour of the world in the direction of each pixel: sky above the horizon, ground below,
// meeting in a soft haze so the floor's faded edge and the horizon read as one distance.
constexpr const char* kBackdropFragmentSource = R"GLSL(#version 330 core
in vec2 vClipPos;
out vec4 fragColor;
uniform mat4 uInverseViewProj;
uniform vec3 uEye;
uniform vec3 uZenith;
uniform vec3 uHorizon;
uniform vec3 uGround;
void main() {
    vec4 far = uInverseViewProj * vec4(vClipPos, 1.0, 1.0);
    vec3 direction = normalize(far.xyz / far.w - uEye);
    float up = direction.y;
    vec3 sky = mix(uHorizon, uZenith, smoothstep(0.0, 0.55, up));
    vec3 ground = mix(uHorizon, uGround, smoothstep(0.0, 0.08, -up));
    fragColor = vec4(up >= 0.0 ? sky : ground, 1.0);
}
)GLSL";

constexpr const char* kFloorVertexSource = R"GLSL(#version 330 core
layout(location = 0) in vec2 aCorner;
uniform mat4 uViewProj;
uniform float uRadius;
out vec2 vFloor;
void main() {
    vFloor = aCorner * uRadius;
    gl_Position = uViewProj * vec4(vFloor.x, 0.0, vFloor.y, 1.0);
}
)GLSL";

// Lines drawn per pixel (one pixel wide at any distance), a quarter-metre minor grid under the
// metre lines, and a fade by distance from the centre: the floor ends in fog, not at a rim.
constexpr const char* kFloorFragmentSource = R"GLSL(#version 330 core
in vec2 vFloor;
out vec4 fragColor;
uniform vec3 uFloor;
uniform vec3 uLine;
uniform float uRadius;
uniform float uFadeStart;
uniform float uSpacing;
float lineAt(float spacing) {
    vec2 cell = vFloor / spacing;
    vec2 width = fwidth(cell);
    vec2 distanceToLine = abs(fract(cell - 0.5) - 0.5) / max(width, vec2(1e-5));
    return 1.0 - min(min(distanceToLine.x, distanceToLine.y), 1.0);
}
void main() {
    float major = lineAt(uSpacing);
    float minor = lineAt(uSpacing * 0.25) * 0.35;
    vec3 colour = mix(uFloor, uLine, max(major, minor));
    float fog = smoothstep(uRadius * uFadeStart, uRadius, length(vFloor));
    fragColor = vec4(colour, 1.0 - fog);
}
)GLSL";

constexpr std::array<float, 6> kFullscreenTriangle{-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
constexpr std::array<float, 12> kFloorQuad{-1.f, -1.f, 1.f, -1.f, 1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

template <std::size_t N>
void MakeBufferUVE(GLuint& vao, GLuint& vbo, const std::array<float, N>& vertices) {
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)), vertices.data(),
                 GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

[[nodiscard]] float AspectOf(const int width, const int height) {
    return static_cast<float>(width) / static_cast<float>(height > 0 ? height : 1);
}

} // namespace

StudioBackdropRenderer::~StudioBackdropRenderer() { Destroy(); }

StudioBackdropRenderer::StudioBackdropRenderer(StudioBackdropRenderer&& other) noexcept
    : backdropProgram_(std::move(other.backdropProgram_)),
      floorProgram_(std::move(other.floorProgram_)),
      triangleVao_(std::exchange(other.triangleVao_, 0)),
      triangleVbo_(std::exchange(other.triangleVbo_, 0)),
      floorVao_(std::exchange(other.floorVao_, 0)),
      floorVbo_(std::exchange(other.floorVbo_, 0)),
      settings_(other.settings_) {}

StudioBackdropRenderer& StudioBackdropRenderer::operator=(StudioBackdropRenderer&& other) noexcept {
    if (this != &other) {
        Destroy();
        backdropProgram_ = std::move(other.backdropProgram_);
        floorProgram_ = std::move(other.floorProgram_);
        triangleVao_ = std::exchange(other.triangleVao_, 0);
        triangleVbo_ = std::exchange(other.triangleVbo_, 0);
        floorVao_ = std::exchange(other.floorVao_, 0);
        floorVbo_ = std::exchange(other.floorVbo_, 0);
        settings_ = other.settings_;
    }
    return *this;
}

void StudioBackdropRenderer::Destroy() noexcept {
    if (triangleVbo_ != 0) { glDeleteBuffers(1, &triangleVbo_); triangleVbo_ = 0; }
    if (triangleVao_ != 0) { glDeleteVertexArrays(1, &triangleVao_); triangleVao_ = 0; }
    if (floorVbo_ != 0) { glDeleteBuffers(1, &floorVbo_); floorVbo_ = 0; }
    if (floorVao_ != 0) { glDeleteVertexArrays(1, &floorVao_); floorVao_ = 0; }
}

std::optional<StudioBackdropRenderer> StudioBackdropRenderer::Create(std::string& outError) {
    StudioBackdropRenderer studio;
    auto backdrop = ShaderProgram::Build(kBackdropVertexSource, kBackdropFragmentSource, outError);
    if (!backdrop.has_value()) return std::nullopt;
    auto floor = ShaderProgram::Build(kFloorVertexSource, kFloorFragmentSource, outError);
    if (!floor.has_value()) return std::nullopt;
    studio.backdropProgram_ = std::move(*backdrop);
    studio.floorProgram_ = std::move(*floor);
    MakeBufferUVE(studio.triangleVao_, studio.triangleVbo_, kFullscreenTriangle);
    MakeBufferUVE(studio.floorVao_, studio.floorVbo_, kFloorQuad);
    return studio;
}

void StudioBackdropRenderer::DrawBackdrop(const univex::camera::OrbitCamera& camera, const int width,
                                          const int height) const {
    if (width <= 0 || height <= 0) return;
    const GLboolean hadDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean hadBlend = glIsEnabled(GL_BLEND);
    GLint depthMask = GL_TRUE;
    glGetIntegerv(GL_DEPTH_WRITEMASK, &depthMask);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    const StudioSettings& s = settings_;
    backdropProgram_.Use();
    backdropProgram_.SetMat4("uInverseViewProj", camera.InverseViewProjection(AspectOf(width, height)).Data());
    const auto eye = camera.Eye();
    backdropProgram_.SetVec3("uEye", eye.x, eye.y, eye.z);
    backdropProgram_.SetVec3("uZenith", s.skyZenith.r, s.skyZenith.g, s.skyZenith.b);
    backdropProgram_.SetVec3("uHorizon", s.skyHorizon.r, s.skyHorizon.g, s.skyHorizon.b);
    backdropProgram_.SetVec3("uGround", s.ground.r, s.ground.g, s.ground.b);
    glBindVertexArray(triangleVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    glDepthMask(static_cast<GLboolean>(depthMask));
    if (hadDepthTest == GL_TRUE) glEnable(GL_DEPTH_TEST);
    if (hadBlend == GL_TRUE) glEnable(GL_BLEND);
}

void StudioBackdropRenderer::DrawFloor(const univex::camera::OrbitCamera& camera, const int width,
                                       const int height) const {
    if (width <= 0 || height <= 0) return;
    const GLboolean hadDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean hadBlend = glIsEnabled(GL_BLEND);
    const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    GLint depthMask = GL_TRUE;
    glGetIntegerv(GL_DEPTH_WRITEMASK, &depthMask);
    // Tests against the scene so feet hide the floor under them; writes nothing, so its faded
    // edge never hides what is drawn after it.
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    const StudioSettings& s = settings_;
    floorProgram_.Use();
    floorProgram_.SetMat4("uViewProj", camera.ViewProjection(AspectOf(width, height)).Data());
    floorProgram_.SetFloat("uRadius", s.floorRadius);
    floorProgram_.SetFloat("uFadeStart", s.fadeStart);
    floorProgram_.SetFloat("uSpacing", s.lineSpacing);
    floorProgram_.SetVec3("uFloor", s.floor.r, s.floor.g, s.floor.b);
    floorProgram_.SetVec3("uLine", s.line.r, s.line.g, s.line.b);
    glBindVertexArray(floorVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDepthFunc(GL_LESS);
    glDepthMask(static_cast<GLboolean>(depthMask));
    if (hadDepthTest == GL_FALSE) glDisable(GL_DEPTH_TEST);
    if (hadBlend == GL_FALSE) glDisable(GL_BLEND);
    if (hadCull == GL_TRUE) glEnable(GL_CULL_FACE);
}

} // namespace univex::render
