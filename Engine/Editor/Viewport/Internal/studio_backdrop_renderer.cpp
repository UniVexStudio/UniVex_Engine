// Engine/Editor/Viewport/Internal/StudioBackdropRenderer.cpp

#include "univex/render/studio_backdrop_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "univex/render/overlay_raster_state.h"
#include "univex/render/overlay_raster_state_gl.h"

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
    // The floor is backdrop, not geometry: it shows only where nothing of the entity is drawn, so a
    // mesh that reaches below y = 0 is never cut off by it and never flickers against it.
    gl_FragDepth = 1.0;
}
)GLSL";

// Camera-following infinite prototype studio floor at y = -0.0005.
// Uses hardware interpolated depth (no gl_FragDepth write) so mobile GPUs keep full Early-Z
// rejection behind characters while preventing z-fighting with feet resting at y = 0.
constexpr const char* kPrototypeFloorVertexSource = R"GLSL(#version 330 core
layout(location = 0) in vec2 aCorner;
uniform mat4 uViewProj;
uniform vec2 uCenterXZ;
uniform float uRadius;
out vec3 vWorldPos;
void main() {
    vec2 worldXZ = uCenterXZ + aCorner * uRadius;
    vWorldPos = vec3(worldXZ.x, -0.0005, worldXZ.y);
    gl_Position = uViewProj * vec4(vWorldPos, 1.0);
}
)GLSL";

// Analytical box-filtered prototype checkerboard + 0.25 m / 1 m / 5 m anti-aliased seams,
// 1 m corner crosshairs, subtle X/Z studio origin axes, overhead studio pool light, and smooth
// radial + grazing-angle fade into the infinity wall horizon (zero texture VRAM, zero moire).
constexpr const char* kPrototypeFloorFragmentSource = R"GLSL(#version 330 core
in vec3 vWorldPos;
out vec4 fragColor;
uniform vec3 uEye;
uniform vec3 uTileDark;
uniform vec3 uTileLight;
uniform vec3 uMinorLine;
uniform vec3 uMajorLine;
uniform vec3 uBlockLine;
uniform vec3 uHorizon;
uniform float uTileSize;
uniform float uFadeStart;
uniform float uFadeEnd;

float filteredChecker(vec2 p) {
    vec2 w = max(fwidth(p), vec2(1e-4));
    vec2 i = 2.0 * (abs(fract((p - 0.5 * w) * 0.5) - 0.5) -
                    abs(fract((p + 0.5 * w) * 0.5) - 0.5)) / w;
    return 0.5 - 0.5 * i.x * i.y;
}

float gridLine(vec2 coord, float widthScale) {
    vec2 fw = max(fwidth(coord), vec2(1e-5));
    vec2 d = abs(fract(coord - 0.5) - 0.5) / fw;
    float line = 1.0 - min(min(d.x, d.y), 1.0);
    float cellPx = 1.0 / max(fw.x, fw.y);
    float lodFade = clamp((cellPx - 2.0) / 6.0, 0.0, 1.0);
    return clamp(line * widthScale, 0.0, 1.0) * lodFade;
}

float cornerCross(vec2 coord) {
    vec2 fw = max(fwidth(coord), vec2(1e-5));
    vec2 d = abs(fract(coord + 0.5) - 0.5);
    float arm = 0.085;
    float barX = (1.0 - smoothstep(0.0, fw.y * 1.4, d.y)) * (1.0 - smoothstep(arm, arm + fw.x, d.x));
    float barY = (1.0 - smoothstep(0.0, fw.x * 1.4, d.x)) * (1.0 - smoothstep(arm, arm + fw.y, d.y));
    float cellPx = 1.0 / max(fw.x, fw.y);
    float lodFade = clamp((cellPx - 18.0) / 24.0, 0.0, 1.0);
    return max(barX, barY) * lodFade;
}

void main() {
    float spacing = max(uTileSize, 0.05);
    vec2 tileCoord = vWorldPos.xz / spacing;

    // 1 m prototype checker with subtle 0.25 m micro-tile variation inside each metre square.
    float macroChecker = filteredChecker(tileCoord);
    float microChecker = filteredChecker(tileCoord * 4.0);
    vec3 baseTile = mix(uTileDark, uTileLight, macroChecker);
    baseTile += (microChecker - 0.5) * 0.022;

    // Gentle studio pool highlight centered at world origin so characters sit on a bright stage.
    float originDist = length(vWorldPos.xz);
    float studioPool = exp(-originDist * originDist * 0.018);
    baseTile += vec3(0.036, 0.040, 0.048) * studioPool;

    // Layered anti-aliased prototype grid seams (0.25 m minor, 1 m tile, 5 m studio block).
    float minor = gridLine(tileCoord * 4.0, 0.90) * 0.36;
    float major = gridLine(tileCoord, 1.05) * 0.72;
    float block = gridLine(tileCoord * 0.2, 1.35) * 0.88;
    float cross = cornerCross(tileCoord) * 0.55;

    vec3 colour = mix(baseTile, uMinorLine, minor);
    colour = mix(colour, uMajorLine, major);
    colour = mix(colour, uBlockLine, max(block, cross));

    // Subtle origin axis ribbons (red +X/-X along z = 0, blue +Z/-Z along x = 0).
    vec2 fwWorld = max(fwidth(vWorldPos.xz), vec2(1e-4));
    float xAxis = 1.0 - smoothstep(0.0, fwWorld.y * 1.5, abs(vWorldPos.z));
    float zAxis = 1.0 - smoothstep(0.0, fwWorld.x * 1.5, abs(vWorldPos.x));
    float axisFade = 1.0 - smoothstep(10.0, 42.0, originDist);
    colour = mix(colour, vec3(0.84, 0.30, 0.30), xAxis * 0.65 * axisFade);
    colour = mix(colour, vec3(0.30, 0.56, 0.88), zAxis * 0.65 * axisFade);

    // Seamless blend into the infinity cyclorama wall by camera distance and grazing angle.
    vec3 toFrag = vWorldPos - uEye;
    float camDist = length(toFrag);
    vec3 viewDir = toFrag / max(camDist, 1e-4);
    float distFade = 1.0 - smoothstep(uFadeStart, uFadeEnd, camDist);
    float grazeFade = smoothstep(0.012, 0.12, abs(viewDir.y));
    float visibility = distFade * grazeFade;

    // Tint slightly toward horizon before alpha fade so the transition is completely seamless.
    colour = mix(uHorizon, colour, clamp(visibility * 1.15, 0.0, 1.0));

    // When orbiting underneath y = 0, turn the floor into a faint translucent reference sheet
    // so the character remains visible from below.
    float underside = mix(0.24, 1.0, smoothstep(-0.15, 0.08, uEye.y));
    float alpha = visibility * underside;
    fragColor = vec4(colour, alpha);
}
)GLSL";

constexpr std::array<float, 6> kFullscreenTriangle{-1.f, -1.f, 3.f, -1.f, -1.f, 3.f};
constexpr std::array<float, 12> kFloorQuad{-1.f, -1.f, 1.f, -1.f, 1.f, 1.f, -1.f, -1.f, 1.f, 1.f, -1.f, 1.f};

template <std::size_t N>
void MakeBuffer(GLuint& vao, GLuint& vbo, const std::array<float, N>& vertices) {
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
      prototypeFloorProgram_(std::move(other.prototypeFloorProgram_)),
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
        prototypeFloorProgram_ = std::move(other.prototypeFloorProgram_);
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
    auto prototypeFloor = ShaderProgram::Build(kPrototypeFloorVertexSource, kPrototypeFloorFragmentSource, outError);
    if (!prototypeFloor.has_value()) return std::nullopt;
    studio.backdropProgram_ = std::move(*backdrop);
    studio.floorProgram_ = std::move(*floor);
    studio.prototypeFloorProgram_ = std::move(*prototypeFloor);
    MakeBuffer(studio.triangleVao_, studio.triangleVbo_, kFullscreenTriangle);
    MakeBuffer(studio.floorVao_, studio.floorVbo_, kFloorQuad);
    return studio;
}

void StudioBackdropRenderer::DrawBackdrop(const univex::camera::OrbitCamera& camera, const int width,
                                          const int height) const {
    DrawBackdrop(camera, width, height, settings_);
}

void StudioBackdropRenderer::DrawBackdrop(const univex::camera::OrbitCamera& camera,
                                          const int width,
                                          const int height,
                                          const StudioSettings& customSettings) const {
    if (width <= 0 || height <= 0) return;
    const SavedOverlayRasterState savedState = SaveOverlayRasterState();
    ApplyOverlayRasterState(WithDepthTest(kStudioBackdropSkyState, false));

    backdropProgram_.Use();
    backdropProgram_.SetMat4("uInverseViewProj", camera.InverseViewProjection(AspectOf(width, height)).Data());
    const auto eye = camera.Eye();
    backdropProgram_.SetVec3("uEye", eye.x, eye.y, eye.z);
    backdropProgram_.SetVec3("uZenith", customSettings.skyZenith.r, customSettings.skyZenith.g,
                             customSettings.skyZenith.b);
    backdropProgram_.SetVec3("uHorizon", customSettings.skyHorizon.r, customSettings.skyHorizon.g,
                             customSettings.skyHorizon.b);
    backdropProgram_.SetVec3("uGround", customSettings.ground.r, customSettings.ground.g, customSettings.ground.b);
    glBindVertexArray(triangleVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);

    RestoreOverlayRasterState(savedState);
}

void StudioBackdropRenderer::DrawFloor(const univex::camera::OrbitCamera& camera, const int width,
                                       const int height) const {
    if (width <= 0 || height <= 0) return;
    const SavedOverlayRasterState savedState = SaveOverlayRasterState();
    // Tests against the scene so feet hide the floor under them; writes nothing, so its faded
    // edge never hides what is drawn after it.
    ApplyOverlayRasterState(kStudioBackdropFloorState);

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

    RestoreOverlayRasterState(savedState);
}

void StudioBackdropRenderer::DrawPrototypeFloor(const univex::camera::OrbitCamera& camera,
                                                const int width,
                                                const int height,
                                                const PrototypeFloorSettings& customSettings) const {
    if (width <= 0 || height <= 0 || !prototypeFloorProgram_.Valid()) return;
    const SavedOverlayRasterState savedState = SaveOverlayRasterState();
    // Depth-tested against scene meshes (Early-Z safe, no gl_FragDepth write) and alpha-blended
    // seamlessly over the infinity cyclorama wall without occluding later editor overlays.
    ApplyOverlayRasterState(kStudioBackdropFloorState);

    const auto eye = camera.Eye();
    const float altitude = std::abs(eye.y);
    const float radius = std::max(customSettings.floorRadius, altitude * 8.0f + 48.0f);
    const float fadeStart = std::max(customSettings.fadeStart, altitude * 1.4f);
    const float fadeEnd = std::max(customSettings.fadeEnd, fadeStart + std::max(45.0f, altitude * 4.5f));

    prototypeFloorProgram_.Use();
    prototypeFloorProgram_.SetMat4("uViewProj", camera.ViewProjection(AspectOf(width, height)).Data());
    prototypeFloorProgram_.SetVec2("uCenterXZ", eye.x, eye.z);
    prototypeFloorProgram_.SetFloat("uRadius", radius);
    prototypeFloorProgram_.SetVec3("uEye", eye.x, eye.y, eye.z);
    prototypeFloorProgram_.SetVec3("uTileDark", customSettings.tileDark.r, customSettings.tileDark.g,
                                   customSettings.tileDark.b);
    prototypeFloorProgram_.SetVec3("uTileLight", customSettings.tileLight.r, customSettings.tileLight.g,
                                   customSettings.tileLight.b);
    prototypeFloorProgram_.SetVec3("uMinorLine", customSettings.minorLine.r, customSettings.minorLine.g,
                                   customSettings.minorLine.b);
    prototypeFloorProgram_.SetVec3("uMajorLine", customSettings.majorLine.r, customSettings.majorLine.g,
                                   customSettings.majorLine.b);
    prototypeFloorProgram_.SetVec3("uBlockLine", customSettings.blockLine.r, customSettings.blockLine.g,
                                   customSettings.blockLine.b);
    prototypeFloorProgram_.SetVec3("uHorizon", customSettings.horizonColor.r, customSettings.horizonColor.g,
                                   customSettings.horizonColor.b);
    prototypeFloorProgram_.SetFloat("uTileSize", customSettings.tileSize);
    prototypeFloorProgram_.SetFloat("uFadeStart", fadeStart);
    prototypeFloorProgram_.SetFloat("uFadeEnd", fadeEnd);
    glBindVertexArray(floorVao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    RestoreOverlayRasterState(savedState);
}

} // namespace univex::render
