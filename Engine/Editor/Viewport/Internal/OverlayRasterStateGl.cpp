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

} // namespace univex::render
