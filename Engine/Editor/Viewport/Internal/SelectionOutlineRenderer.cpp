#include "univex/render/SelectionOutlineRenderer.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <utility>

#include "univex/render/SelectionOutlineShaderSources.h"

namespace univex::render {

namespace {

constexpr std::array<float, 6> kFullscreenTriangle = {
    -1.f, -1.f,
     3.f, -1.f,
    -1.f,  3.f,
};

} // namespace

SelectionOutlineRenderer::~SelectionOutlineRenderer() { Destroy(); }

SelectionOutlineRenderer::SelectionOutlineRenderer(SelectionOutlineRenderer&& other) noexcept
    : maskProgram_(std::move(other.maskProgram_)),
      outlineProgram_(std::move(other.outlineProgram_)),
      triangleVao_(std::exchange(other.triangleVao_, 0)),
      triangleVbo_(std::exchange(other.triangleVbo_, 0)),
      fullscreenVao_(std::exchange(other.fullscreenVao_, 0)),
      fullscreenVbo_(std::exchange(other.fullscreenVbo_, 0)),
      maskFbo_(std::exchange(other.maskFbo_, 0)),
      maskTexture_(std::exchange(other.maskTexture_, 0)),
      maskWidth_(std::exchange(other.maskWidth_, 0)),
      maskHeight_(std::exchange(other.maskHeight_, 0)) {}

SelectionOutlineRenderer& SelectionOutlineRenderer::operator=(SelectionOutlineRenderer&& other) noexcept {
    if (this != &other) {
        Destroy();
        maskProgram_ = std::move(other.maskProgram_);
        outlineProgram_ = std::move(other.outlineProgram_);
        triangleVao_ = std::exchange(other.triangleVao_, 0);
        triangleVbo_ = std::exchange(other.triangleVbo_, 0);
        fullscreenVao_ = std::exchange(other.fullscreenVao_, 0);
        fullscreenVbo_ = std::exchange(other.fullscreenVbo_, 0);
        maskFbo_ = std::exchange(other.maskFbo_, 0);
        maskTexture_ = std::exchange(other.maskTexture_, 0);
        maskWidth_ = std::exchange(other.maskWidth_, 0);
        maskHeight_ = std::exchange(other.maskHeight_, 0);
    }
    return *this;
}

void SelectionOutlineRenderer::Destroy() noexcept {
    if (maskFbo_ != 0) { glDeleteFramebuffers(1, &maskFbo_); maskFbo_ = 0; }
    if (maskTexture_ != 0) { glDeleteTextures(1, &maskTexture_); maskTexture_ = 0; }
    if (triangleVbo_ != 0) { glDeleteBuffers(1, &triangleVbo_); triangleVbo_ = 0; }
    if (triangleVao_ != 0) { glDeleteVertexArrays(1, &triangleVao_); triangleVao_ = 0; }
    if (fullscreenVbo_ != 0) { glDeleteBuffers(1, &fullscreenVbo_); fullscreenVbo_ = 0; }
    if (fullscreenVao_ != 0) { glDeleteVertexArrays(1, &fullscreenVao_); fullscreenVao_ = 0; }
    maskWidth_ = 0;
    maskHeight_ = 0;
}

std::optional<SelectionOutlineRenderer> SelectionOutlineRenderer::CreateWithBuiltinShaders(std::string& outError) {
    SelectionOutlineRenderer renderer;
    auto mask = ShaderProgram::Build(shaders::kSelectionMaskVertexSource, shaders::kSelectionMaskFragmentSource,
                                     outError);
    if (!mask.has_value()) return std::nullopt;
    renderer.maskProgram_ = std::move(*mask);
    auto outline = ShaderProgram::Build(shaders::kSelectionOutlineVertexSource,
                                        shaders::kSelectionOutlineFragmentSource, outError);
    if (!outline.has_value()) return std::nullopt;
    renderer.outlineProgram_ = std::move(*outline);

    // Selected triangles: position (xyz) and selection weight, refilled every frame.
    glGenVertexArrays(1, &renderer.triangleVao_);
    glBindVertexArray(renderer.triangleVao_);
    glGenBuffers(1, &renderer.triangleVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.triangleVbo_);
    constexpr GLsizei kStride = static_cast<GLsizei>(sizeof(SelectionOutlineVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, kStride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, kStride,
                          reinterpret_cast<const void*>(offsetof(SelectionOutlineVertex, weight)));

    glGenVertexArrays(1, &renderer.fullscreenVao_);
    glBindVertexArray(renderer.fullscreenVao_);
    glGenBuffers(1, &renderer.fullscreenVbo_);
    glBindBuffer(GL_ARRAY_BUFFER, renderer.fullscreenVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(kFullscreenTriangle.size() * sizeof(float)),
                 kFullscreenTriangle.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return renderer;
}

bool SelectionOutlineRenderer::EnsureMask(const int width, const int height) {
    if (maskFbo_ != 0 && maskWidth_ == width && maskHeight_ == height) {
        return true;
    }
    if (maskTexture_ == 0) glGenTextures(1, &maskTexture_);
    if (maskFbo_ == 0) glGenFramebuffers(1, &maskFbo_);
    glBindTexture(GL_TEXTURE_2D, maskTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, maskTexture_, 0);
    const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    maskWidth_ = complete ? width : 0;
    maskHeight_ = complete ? height : 0;
    return complete;
}

void SelectionOutlineRenderer::Draw(const univex::math::Mat4& viewProjection, const int width, const int height,
                                    const std::vector<SelectionOutlineVertex>& triangles,
                                    const SelectionOutlineSettings& requested) {
    const SelectionOutlineSettings settings = SanitizeSelectionOutlineSettings(requested);
    if (!Valid() || !settings.visible || triangles.size() < 3U || width <= 0 || height <= 0) {
        return;
    }

    // ---- remember the state we are about to change ------------------------
    GLint drawFbo = 0;
    GLint readFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFbo);
    std::array<GLint, 4> viewport{};
    glGetIntegerv(GL_VIEWPORT, viewport.data());
    std::array<GLfloat, 4> clearColor{};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, clearColor.data());
    const GLboolean hadBlend = glIsEnabled(GL_BLEND);
    const GLboolean hadDepthTest = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean hadCull = glIsEnabled(GL_CULL_FACE);
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    GLint blendEquationRgb = GL_FUNC_ADD, blendEquationAlpha = GL_FUNC_ADD;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blendEquationAlpha);
    GLint blendSrcRgb = GL_ONE, blendDstRgb = GL_ZERO, blendSrcAlpha = GL_ONE, blendDstAlpha = GL_ZERO;
    glGetIntegerv(GL_BLEND_SRC_RGB, &blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDstAlpha);
    GLint activeTexture = GL_TEXTURE0;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);

    if (EnsureMask(width, height)) {
        // ---- pass 1: the silhouette mask ----------------------------------
        // No depth and no culling: the whole selected shape counts, front or back. Overlapping
        // selections keep the stronger weight.
        glBindFramebuffer(GL_FRAMEBUFFER, maskFbo_);
        glViewport(0, 0, width, height);
        glClearColor(0.f, 0.f, 0.f, 0.f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_MAX);
        glBlendFunc(GL_ONE, GL_ONE);
        maskProgram_.Use();
        maskProgram_.SetMat4("uViewProj", viewProjection.Data());
        glBindVertexArray(triangleVao_);
        glBindBuffer(GL_ARRAY_BUFFER, triangleVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(triangles.size() * sizeof(SelectionOutlineVertex)),
                     triangles.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(triangles.size() - (triangles.size() % 3U)));
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // ---- pass 2: the band, into the caller's framebuffer --------------
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        outlineProgram_.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, maskTexture_);
        glUniform1i(outlineProgram_.UniformLocation("uMask"), 0);
        glUniform1i(outlineProgram_.UniformLocation("uRadius"),
                    static_cast<GLint>(std::lround(settings.thicknessPixels)));
        outlineProgram_.SetVec3("uColor", settings.r, settings.g, settings.b);
        glBindVertexArray(fullscreenVao_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glBindVertexArray(0);

    // ---- put it back ------------------------------------------------------
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(drawFbo));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(readFbo));
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glClearColor(clearColor[0], clearColor[1], clearColor[2], clearColor[3]);
    glActiveTexture(static_cast<GLenum>(activeTexture));
    glDepthMask(depthMask);
    if (hadDepthTest == GL_TRUE) glEnable(GL_DEPTH_TEST);
    if (hadCull == GL_TRUE) glEnable(GL_CULL_FACE);
    glBlendEquationSeparate(static_cast<GLenum>(blendEquationRgb), static_cast<GLenum>(blendEquationAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(blendSrcRgb), static_cast<GLenum>(blendDstRgb),
                        static_cast<GLenum>(blendSrcAlpha), static_cast<GLenum>(blendDstAlpha));
    if (hadBlend == GL_FALSE) glDisable(GL_BLEND);
}

} // namespace univex::render
