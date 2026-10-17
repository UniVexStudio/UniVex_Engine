// univex/render/OverlayRasterStateGl.h
// -----------------------------------------------------------------------
// The ONE place in univex_viewport_gl that turns an OverlayRasterState into
// glEnable/glDisable(GL_DEPTH_TEST), glDepthMask, glDepthFunc, blend and
// cull calls. Overlay renderers call this instead of issuing those calls
// themselves (Foundation WP6, candidate B).
// -----------------------------------------------------------------------
#pragma once

#include <cstdint>

#include "univex/render/OverlayRasterState.h"

namespace univex::render {

/// Snapshot of the caller's GL depth/blend/cull state captured before an overlay pass runs and
/// restored afterwards so every glDepthMask / glDepthFunc call in univex_viewport_gl lives inside
/// OverlayRasterStateGl.cpp.
struct SavedOverlayRasterState {
    bool depthTestEnabled = false;
    bool depthWriteMask = true;
    std::int32_t depthFunc = 0;
    bool blendEnabled = false;
    std::int32_t blendEquationRgb = 0;
    std::int32_t blendEquationAlpha = 0;
    std::int32_t blendSrcRgb = 0;
    std::int32_t blendDstRgb = 0;
    std::int32_t blendSrcAlpha = 0;
    std::int32_t blendDstAlpha = 0;
    bool cullFaceEnabled = false;
};

/// Applies `state` to the current GL context. Blend `Alpha` means straight alpha with the alpha
/// channel accumulated as ONE / ONE_MINUS_SRC_ALPHA (what every overlay pass used);
/// `OverlayCullMode::Inherit` leaves GL_CULL_FACE untouched.
void ApplyOverlayRasterState(const OverlayRasterState& state);

/// Captures the current GL depth/blend/cull state so an overlay pass can restore it on exit.
[[nodiscard]] SavedOverlayRasterState SaveOverlayRasterState();

/// Restores a snapshot previously captured by `SaveOverlayRasterState()`.
void RestoreOverlayRasterState(const SavedOverlayRasterState& saved);

/// Enables the GL depth write mask before a depth-buffer clear (glClear(GL_DEPTH_BUFFER_BIT) is a
/// no-op while the depth write mask is false).
void EnableOverlayDepthWrite();

} // namespace univex::render
