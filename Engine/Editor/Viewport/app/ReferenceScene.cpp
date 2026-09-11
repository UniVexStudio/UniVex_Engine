#include "ReferenceScene.h"

#include <array>
#include <utility>
#include <vector>

namespace univex::app {

namespace {

constexpr const char* kVertexSource = R"GLSL(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aColor;

uniform mat4 uViewProj;
uniform mat4 uModel;
uniform float uHalfExtent;

out vec3 vColor;

void main() {
    vColor = aColor;
    // Centred on its own local origin, matching the transform gizmo's pivot (the entity's actual
    // world position, per SetGizmoPivotOverride()) - previously shifted up by uHalfExtent so a
    // single origin-relative demo cube would visually "sit on" the ground plane, but that put the
    // mesh's own visual center out of sync with where its gizmo (and its real transform) actually
    // is once uModel carries a real per-entity world matrix instead of the identity.
    vec3 local = aPosition * uHalfExtent;
    gl_Position = uViewProj * uModel * vec4(local, 1.0);
}
)GLSL";

constexpr const char* kFragmentSource = R"GLSL(#version 330 core
in vec3 vColor;

// Alpha below zero means "use the per-face colour"; otherwise every fragment
// takes uOverrideColor, which is how Unshaded and Wireframe are drawn without
// needing a second program.
uniform vec4 uOverrideColor;

out vec4 fragColor;

void main() {
    fragColor = (uOverrideColor.a < 0.0) ? vec4(vColor, 1.0)
                                         : vec4(uOverrideColor.rgb, 1.0);
}
)GLSL";

// 6 faces x 2 triangles x 3 vertices, each face a constant colour.
// Layout per vertex: px, py, pz, r, g, b.
std::vector<float> BuildCubeVertices() {
    struct Face {
        std::array<float, 3> origin;
        std::array<float, 3> edgeU;
        std::array<float, 3> edgeV;
        std::array<float, 3> color;
    };

    // edgeU x edgeV must point OUT of the cube: with GL_CCW front faces and
    // back-face culling on, getting this backwards silently deletes the face.
    const std::array<Face, 6> faces = {{
        {{ 1, -1, -1}, { 0, 2,  0}, { 0, 0,  2}, {0.42f, 0.45f, 0.52f}}, // +X
        {{-1, -1,  1}, { 0, 2,  0}, { 0, 0, -2}, {0.30f, 0.33f, 0.40f}}, // -X
        {{-1,  1, -1}, { 0, 0,  2}, { 2, 0,  0}, {0.52f, 0.56f, 0.64f}}, // +Y (top)
        {{-1, -1,  1}, { 0, 0, -2}, { 2, 0,  0}, {0.22f, 0.24f, 0.30f}}, // -Y (bottom)
        {{-1, -1,  1}, { 2, 0,  0}, { 0, 2,  0}, {0.36f, 0.39f, 0.47f}}, // +Z
        {{ 1, -1, -1}, {-2, 0,  0}, { 0, 2,  0}, {0.26f, 0.29f, 0.36f}}, // -Z
    }};

    std::vector<float> out;
    out.reserve(6 * 6 * 6);
    for (const Face& face : faces) {
        const auto corner = [&](float u, float v) {
            return std::array<float, 3>{
                face.origin[0] + face.edgeU[0] * u + face.edgeV[0] * v,
                face.origin[1] + face.edgeU[1] * u + face.edgeV[1] * v,
                face.origin[2] + face.edgeU[2] * u + face.edgeV[2] * v,
            };
        };
        const std::array<std::array<float, 3>, 6> tri = {
            corner(0, 0), corner(1, 0), corner(1, 1),
            corner(0, 0), corner(1, 1), corner(0, 1),
        };
        for (const auto& p : tri) {
            out.insert(out.end(), {p[0], p[1], p[2], face.color[0], face.color[1], face.color[2]});
        }
    }
    return out;
}

} // namespace

ReferenceScene::~ReferenceScene() { Destroy(); }

ReferenceScene::ReferenceScene(ReferenceScene&& other) noexcept
    : program_(std::move(other.program_)),
      vao_(std::exchange(other.vao_, 0)),
      vbo_(std::exchange(other.vbo_, 0)),
      vertexCount_(std::exchange(other.vertexCount_, 0)) {}

ReferenceScene& ReferenceScene::operator=(ReferenceScene&& other) noexcept {
    if (this != &other) {
        Destroy();
        program_ = std::move(other.program_);
        vao_ = std::exchange(other.vao_, 0);
        vbo_ = std::exchange(other.vbo_, 0);
        vertexCount_ = std::exchange(other.vertexCount_, 0);
    }
    return *this;
}

void ReferenceScene::Destroy() noexcept {
    if (vbo_ != 0) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (vao_ != 0) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    vertexCount_ = 0;
}

std::optional<ReferenceScene> ReferenceScene::Create(std::string& outError) {
    auto program = univex::render::ShaderProgram::Build(kVertexSource, kFragmentSource, outError);
    if (!program.has_value()) return std::nullopt;

    ReferenceScene scene;
    scene.program_ = std::move(*program);

    const std::vector<float> vertices = BuildCubeVertices();
    scene.vertexCount_ = static_cast<GLsizei>(vertices.size() / 6);

    glGenVertexArrays(1, &scene.vao_);
    glBindVertexArray(scene.vao_);
    glGenBuffers(1, &scene.vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, scene.vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                          reinterpret_cast<void*>(3 * sizeof(float)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return scene;
}

void ReferenceScene::Draw(const univex::math::Mat4& viewProjection, const univex::math::Mat4& model,
                          float halfExtent, univex::viewport::DisplayMode display) const {
    if (vao_ == 0 || !program_.Valid()) return;

    using univex::viewport::DisplayMode;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    const bool wireframe = (display == DisplayMode::Wireframe);
    if (wireframe) {
        // Back faces have to stay for wireframe, otherwise the far edges of
        // the box vanish and it stops reading as a wireframe at all.
        glDisable(GL_CULL_FACE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    } else {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    }

    program_.Use();
    program_.SetMat4("uViewProj", viewProjection.Data());
    program_.SetMat4("uModel", model.Data());
    program_.SetFloat("uHalfExtent", halfExtent);
    switch (display) {
        case DisplayMode::Normal:
            program_.SetVec4("uOverrideColor", 0.f, 0.f, 0.f, -1.f);
            break;
        case DisplayMode::Wireframe:
            program_.SetVec4("uOverrideColor", 0.62f, 0.67f, 0.78f, 1.f);
            break;
        case DisplayMode::Unshaded:
            program_.SetVec4("uOverrideColor", 0.60f, 0.63f, 0.70f, 1.f);
            break;
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    glBindVertexArray(0);

    if (wireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_CULL_FACE);
}

} // namespace univex::app
