#include "univex/render/OverlayRasterStateGl.h"

#include "univex/render/GlApi.h"

namespace univex::render {

namespace {

[[nodiscard]] GLenum ToGlDepthFunc(const OverlayDepthCompare compare) {
    switch (compare) {
        case OverlayDepthCompare::Less: return GL_LESS;
        case OverlayDepthCompare::LessOrEqual: return GL_LEQUAL;
        case OverlayDepthCompare::Always: return GL_ALWAYS;
    }
    return GL_LESS;
}

} // namespace

void ApplyOverlayRasterState(const OverlayRasterState& state) {
    if (state.depthTest) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(ToGlDepthFunc(state.depthCompare));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(state.depthWrite ? GL_TRUE : GL_FALSE);

    switch (state.blend) {
        case OverlayBlendMode::Opaque:
            glDisable(GL_BLEND);
            break;
        case OverlayBlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
    }

    switch (state.cull) {
        case OverlayCullMode::Inherit:
            break;
        case OverlayCullMode::None:
            glDisable(GL_CULL_FACE);
            break;
        case OverlayCullMode::Back:
            glEnable(GL_CULL_FACE);
            glCullFace(GL_BACK);
            break;
    }
}

SavedOverlayRasterState SaveOverlayRasterState() {
    SavedOverlayRasterState saved{};
    saved.depthTestEnabled = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    GLboolean depthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    saved.depthWriteMask = depthMask == GL_TRUE;
    GLint depthFunc = GL_LESS;
    glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
    saved.depthFunc = depthFunc;
    saved.blendEnabled = glIsEnabled(GL_BLEND) == GL_TRUE;
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &saved.blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &saved.blendEquationAlpha);
    glGetIntegerv(GL_BLEND_SRC_RGB, &saved.blendSrcRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &saved.blendDstRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &saved.blendSrcAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &saved.blendDstAlpha);
    saved.cullFaceEnabled = glIsEnabled(GL_CULL_FACE) == GL_TRUE;
    return saved;
}

void RestoreOverlayRasterState(const SavedOverlayRasterState& saved) {
    glDepthMask(saved.depthWriteMask ? GL_TRUE : GL_FALSE);
    glDepthFunc(static_cast<GLenum>(saved.depthFunc));
    if (saved.depthTestEnabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glBlendEquationSeparate(static_cast<GLenum>(saved.blendEquationRgb),
                            static_cast<GLenum>(saved.blendEquationAlpha));
    glBlendFuncSeparate(static_cast<GLenum>(saved.blendSrcRgb),
                        static_cast<GLenum>(saved.blendDstRgb),
                        static_cast<GLenum>(saved.blendSrcAlpha),
                        static_cast<GLenum>(saved.blendDstAlpha));
    if (saved.blendEnabled) {
        glEnable(GL_BLEND);
    } else {
        glDisable(GL_BLEND);
    }
    if (saved.cullFaceEnabled) {
        glEnable(GL_CULL_FACE);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void EnableOverlayDepthWrite() {
    glDepthMask(GL_TRUE);
}

} // namespace univex::render
