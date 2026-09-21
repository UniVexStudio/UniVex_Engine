// univex/viewport/AxisPalette.h
// -----------------------------------------------------------------------
// The one definition of what the three world axes look like.
//
// X is red, Y is green, Z is blue - the convention every 3D tool shares, and the thing a user
// reads a viewport by. They were previously written out twice, once in GizmoStyle and once in
// GridSettings, with a comment in one asking the other to be kept in step: two copies of a
// constant that must agree is a drift waiting to happen, and nothing would have caught it.
//
// The values are deliberately not pure primaries. Saturated red on a dark backdrop bleeds and
// reads as an error state, and pure blue is close to unreadable at one-pixel line widths; these
// keep the hue unmistakable while staying legible as thin geometry.
// -----------------------------------------------------------------------
#pragma once

namespace univex::viewport {

struct AxisRgbUVE {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;

    [[nodiscard]] constexpr AxisRgbUVE Scaled(float factor) const {
        return AxisRgbUVE{r * factor, g * factor, b * factor};
    }
};

inline constexpr AxisRgbUVE kAxisColorXUVE{1.000f, 0.365f, 0.365f}; // #ff5d5d
inline constexpr AxisRgbUVE kAxisColorYUVE{0.373f, 0.878f, 0.541f}; // #5fe08a
inline constexpr AxisRgbUVE kAxisColorZUVE{0.357f, 0.616f, 1.000f}; // #5b9dff

/// The grid's axis lines run through the same origin the transform gizmo sits on, so drawing both
/// in the identical colour leaves the gizmo's own axes competing with the ground lines behind
/// them. The grid takes a darker variant of the same hues: still obviously the X/Y/Z axes, but
/// clearly the backdrop rather than the handle.
inline constexpr float kGridAxisDimFactorUVE = 0.68f;

inline constexpr AxisRgbUVE kGridAxisColorXUVE = kAxisColorXUVE.Scaled(kGridAxisDimFactorUVE);
inline constexpr AxisRgbUVE kGridAxisColorYUVE = kAxisColorYUVE.Scaled(kGridAxisDimFactorUVE);
inline constexpr AxisRgbUVE kGridAxisColorZUVE = kAxisColorZUVE.Scaled(kGridAxisDimFactorUVE);

} // namespace univex::viewport
