#include "univex/render/GizmoRenderer.h"

#include "univex/camera/ViewportMetrics.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "univex/render/GizmoShaderSources.h"

namespace univex::render {

namespace {

// Per-vertex layout of the line pass: current end, other end, colour,
// (side, widthPx). Six of these per segment — two triangles.
constexpr GLsizei kLineStride = static_cast<GLsizei>(sizeof(float) * 11);
// Solid pass: position + normal + RGBA.
constexpr GLsizei kSolidStride = static_cast<GLsizei>(sizeof(float) * 10);

// At and above this alpha, blending leaves a triangle visually solid, so it is drawn in the
// opaque pass and keeps its depth writes.
constexpr float kOpaqueAlphaThreshold = 0.999f;

// Depth of a triangle's centroid along the view axis; larger is farther from the eye.
[[nodiscard]] float CentroidDepth(const univex::gizmo::GizmoTriangle& tri, const Vec3& viewDirection) {
    const Vec3 centroid{(tri.a.x + tri.b.x + tri.c.x) / 3.f,
                        (tri.a.y + tri.b.y + tri.c.y) / 3.f,
                        (tri.a.z + tri.b.z + tri.c.z) / 3.f};
    return univex::math::Dot(centroid, viewDirection);
}

void PushLineVertex(std::vector<float>& out, const Vec3& current, const Vec3& other,
                    const Vec3& color, float side, float widthPx) {
    out.insert(out.end(), {current.x, current.y, current.z,
                           other.x, other.y, other.z,
                           color.x, color.y, color.z,
                           side, widthPx});
}

} // namespace

GizmoRenderer::~GizmoRenderer() { Destroy(); }

GizmoRenderer::GizmoRenderer(GizmoRenderer&& other) noexcept
    : lineProgram_(std::move(other.lineProgram_)),
      solidProgram_(std::move(other.solidProgram_)),
      lineVao_(std::exchange(other.lineVao_, 0)),
      lineVbo_(std::exchange(other.lineVbo_, 0)),
      solidVao_(std::exchange(other.solidVao_, 0)),
      solidVbo_(std::exchange(other.solidVbo_, 0)) {}

GizmoRenderer& GizmoRenderer::operator=(GizmoRenderer&& other) noexcept {
    if (this != &other) {
        Destroy();
        lineProgram_ = std::move(other.lineProgram_);
        solidProgram_ = std::move(other.solidProgram_);
        lineVao_ = std::exchange(other.lineVao_, 0);
        lineVbo_ = std::exchange(other.lineVbo_, 0);
        solidVao_ = std::exchange(other.solidVao_, 0);
        solidVbo_ = std::exchange(other.solidVbo_, 0);
    }
    return *this;
}

void GizmoRenderer::Destroy() noexcept {
    if (lineVbo_ != 0)  { glDeleteBuffers(1, &lineVbo_);  lineVbo_ = 0; }
    if (lineVao_ != 0)  { glDeleteVertexArrays(1, &lineVao_); lineVao_ = 0; }
    if (solidVbo_ != 0) { glDeleteBuffers(1, &solidVbo_); solidVbo_ = 0; }
    if (solidVao_ != 0) { glDeleteVertexArrays(1, &solidVao_); solidVao_ = 0; }
}

std::optional<GizmoRenderer> GizmoRenderer::Create(std::string& outError) {
    auto lineProgram = ShaderProgram::Build(shaders::kGizmoLineVertexSource,
                                            shaders::kGizmoLineFragmentSource, outError);
    if (!lineProgram.has_value()) return std::nullopt;

    auto solidProgram = ShaderProgram::Build(shaders::kGizmoSolidVertexSource,
                                             shaders::kGizmoSolidFragmentSource, outError);
    if (!solidProgram.has_value()) return std::nullopt;

    GizmoRenderer renderer;
    renderer.lineProgram_ = std::move(*lineProgram);
    renderer.solidProgram_ = std::move(*solidProgram);

    glGenVertexArrays(1, &renderer.lineVao_);
    glBindVertexArray(renderer.lineVao_);
    glGenBuffers(1, &renderer.lineVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.lineVbo_);
    for (GLuint location = 0; location < 4; ++location) glEnableVertexAttribArray(location);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kLineStride, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kLineStride, reinterpret_cast<void*>(sizeof(float) * 3));
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, kLineStride, reinterpret_cast<void*>(sizeof(float) * 6));
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, kLineStride, reinterpret_cast<void*>(sizeof(float) * 9));

    glGenVertexArrays(1, &renderer.solidVao_);
    glBindVertexArray(renderer.solidVao_);
    glGenBuffers(1, &renderer.solidVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.solidVbo_);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kSolidStride, reinterpret_cast<void*>(0));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, kSolidStride, reinterpret_cast<void*>(sizeof(float) * 3));
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, kSolidStride, reinterpret_cast<void*>(sizeof(float) * 6));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return renderer;
}

void GizmoRenderer::UploadAndDrawTriangles(const GizmoMesh& mesh, const GizmoDrawParams& params,
                                           TrianglePassUVE pass) const {
    if (mesh.triangles.empty()) return;

    // Select this pass's half of the mesh. Alpha is authored per triangle, so the split is just a
    // threshold - anything the blend would leave indistinguishable from opaque is treated as
    // opaque, so it keeps its depth writes and goes on occluding the widget's own far side.
    const bool wantOpaque = pass == TrianglePassUVE::Opaque;
    std::vector<std::size_t> selected;
    selected.reserve(mesh.triangles.size());
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i) {
        if ((mesh.triangles[i].alpha >= kOpaqueAlphaThreshold) == wantOpaque) selected.push_back(i);
    }
    if (selected.empty()) return;

    if (!wantOpaque) {
        // Back to front along the view axis. The mesh is authored around the origin and placed by
        // a uniform positive scale, so ordering by the local centroid's depth is the same ordering
        // as in world space, without having to transform anything here.
        std::stable_sort(selected.begin(), selected.end(),
                         [&](std::size_t lhs, std::size_t rhs) {
                             return CentroidDepth(mesh.triangles[lhs], params.viewDirection) >
                                    CentroidDepth(mesh.triangles[rhs], params.viewDirection);
                         });
    }

    std::vector<float> vertices;
    vertices.reserve(selected.size() * 3 * 10);
    for (const std::size_t index : selected) {
        const auto& tri = mesh.triangles[index];
        // Flat (faceted) shading: one normal per triangle, replicated across
        // its 3 vertices - matches this mesh's own immediate/expanded style
        // (no shared vertex buffer to smooth across), and reads as the same
        // simple low-poly look the gizmo's flat colours already have.
        const Vec3 normal = univex::math::Normalize(
            univex::math::Cross(tri.b - tri.a, tri.c - tri.a));
        for (const Vec3& p : {tri.a, tri.b, tri.c}) {
            vertices.insert(vertices.end(),
                            {p.x, p.y, p.z, normal.x, normal.y, normal.z,
                             tri.color.x, tri.color.y, tri.color.z, tri.alpha});
        }
    }

    glBindVertexArray(solidVao_);
    glBindBuffer(GL_ARRAY_BUFFER, solidVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STREAM_DRAW);

    solidProgram_.Use();
    solidProgram_.SetMat4("uViewProj", params.viewProjection.Data());
    solidProgram_.SetVec3("uOrigin", params.origin.x, params.origin.y, params.origin.z);
    solidProgram_.SetFloat("uScale", params.scale);
    solidProgram_.SetFloat("uOpacity", params.opacity);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.triangles.size() * 3));
    glBindVertexArray(0);
}

void GizmoRenderer::UploadAndDrawLines(const GizmoMesh& mesh, const GizmoDrawParams& params) const {
    if (mesh.lines.empty()) return;

    std::vector<float> vertices;
    vertices.reserve(mesh.lines.size() * 6 * 11);
    for (const auto& line : mesh.lines) {
        // Quad corners: A-n, A+n, B+n, B-n. At B the computed normal is
        // flipped (its "other" end is A), so the side value flips with it.
        PushLineVertex(vertices, line.a, line.b, line.color, -1.f, line.widthPx);
        PushLineVertex(vertices, line.a, line.b, line.color, +1.f, line.widthPx);
        PushLineVertex(vertices, line.b, line.a, line.color, -1.f, line.widthPx);

        PushLineVertex(vertices, line.a, line.b, line.color, -1.f, line.widthPx);
        PushLineVertex(vertices, line.b, line.a, line.color, -1.f, line.widthPx);
        PushLineVertex(vertices, line.b, line.a, line.color, +1.f, line.widthPx);
    }

    glBindVertexArray(lineVao_);
    glBindBuffer(GL_ARRAY_BUFFER, lineVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STREAM_DRAW);

    lineProgram_.Use();
    lineProgram_.SetMat4("uViewProj", params.viewProjection.Data());
    lineProgram_.SetVec2("uViewportSize", params.viewportWidth, params.viewportHeight);
    lineProgram_.SetVec3("uOrigin", params.origin.x, params.origin.y, params.origin.z);
    lineProgram_.SetFloat("uScale", params.scale);
    lineProgram_.SetFloat("uOpacity", params.opacity);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(mesh.lines.size() * 6));
    glBindVertexArray(0);
}

void GizmoRenderer::Draw(const GizmoMesh& mesh, const GizmoDrawParams& params) const {
    if (!Valid() || mesh.Empty()) return;

    const GLboolean hadBlend = glIsEnabled(GL_BLEND);
    const GLboolean hadDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean hadCullFace = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);

    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE); // gizmo geometry is viewed from every side
    if (params.depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);

    // Opaque solids write depth so the widget occludes itself. Translucent solids and the strokes
    // drawn over them test against that depth but never add to it: a half-transparent handle must
    // not reject the geometry showing through it, and an anti-aliased stroke edge must not punch a
    // hole in whatever is behind it.
    const auto drawSolids = [&] {
        glDepthMask(params.depthWrite ? GL_TRUE : GL_FALSE);
        UploadAndDrawTriangles(mesh, params, TrianglePassUVE::Opaque);
        glDepthMask(GL_FALSE);
        UploadAndDrawTriangles(mesh, params, TrianglePassUVE::Translucent);
    };

    if (params.drawLinesFirst) {
        glDepthMask(GL_FALSE);
        UploadAndDrawLines(mesh, params);
        drawSolids();
    } else {
        drawSolids();
        glDepthMask(GL_FALSE);
        UploadAndDrawLines(mesh, params);
    }

    glDepthMask(depthMask);
    if (hadDepthTest == GL_FALSE) glDisable(GL_DEPTH_TEST); else glEnable(GL_DEPTH_TEST);
    if (hadCullFace == GL_TRUE) glEnable(GL_CULL_FACE);
    if (hadBlend == GL_FALSE) glDisable(GL_BLEND);
}

float GizmoRenderer::ScaleForPixelRadius(const univex::camera::OrbitCamera& camera,
                                         int framebufferHeight,
                                         float gizmoPixelRadius,
                                         const Vec3& pivot) {
    if (framebufferHeight <= 0) return 1.f;

    // World units per pixel at the PIVOT's depth (orthographic handled inside), so the widget
    // holds its on-screen size wherever the selected node happens to be relative to the camera's
    // orbit target.
    const float worldPerPixel =
        univex::camera::WorldPerPixelAtPointUVE(camera, framebufferHeight, pivot);

    // The gizmo's outermost handle sits ~1.9 gizmo units from the pivot, so
    // dividing by that turns "pixels to the outer edge" into a unit scale.
    constexpr float kGizmoOuterUnits = 1.9f;
    return (gizmoPixelRadius * worldPerPixel) / kGizmoOuterUnits;
}

} // namespace univex::render
