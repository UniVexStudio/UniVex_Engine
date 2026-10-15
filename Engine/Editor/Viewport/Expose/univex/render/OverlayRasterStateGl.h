// univex/render/OverlayRasterStateGl.h
// -----------------------------------------------------------------------
// The ONE place in univex_viewport_gl that turns an OverlayRasterState into
// glEnable/glDisable(GL_DEPTH_TEST), glDepthMask, glDepthFunc, blend and
// cull calls. Overlay renderers call this instead of issuing those calls
// themselves (Foundation WP6, candidate B).
// -----------------------------------------------------------------------
#pragma once

#include "univex/render/OverlayRasterState.h"

namespace univex::render {

/// Applies `state` to the current GL context. Blend `Alpha` means straight alpha with the alpha
/// channel accumulated as ONE / ONE_MINUS_SRC_ALPHA (what every overlay pass used);
/// `OverlayCullMode::Inherit` leaves GL_CULL_FACE untouched.
void ApplyOverlayRasterState(const OverlayRasterState& state);

} // namespace univex::render
