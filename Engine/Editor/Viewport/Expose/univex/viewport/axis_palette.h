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

struct AxisRgb {
    float r = 0.f;
    float g = 0.f;
    float b = 0.f;

    [[nodiscard]] constexpr AxisRgb Scaled(float factor) const {
        return AxisRgb{r * factor, g * factor, b * factor};
    }
};

inline constexpr AxisRgb kAxisColorX{1.000f, 0.220f, 0.220f}; // vivid red
inline constexpr AxisRgb kAxisColorY{0.180f, 1.000f, 0.420f}; // vivid green
inline constexpr AxisRgb kAxisColorZ{0.220f, 0.500f, 1.000f}; // vivid blue

/// The grid's axis lines run through the same origin the transform gizmo sits on, so drawing both
/// in the identical colour leaves the gizmo's own axes competing with the ground lines behind
/// them. The grid takes a darker variant of the same hues: still obviously the X/Y/Z axes, but
/// clearly the backdrop rather than the handle.
inline constexpr float kGridAxisDimFactor = 0.68f;

inline constexpr AxisRgb kGridAxisColorX = kAxisColorX.Scaled(kGridAxisDimFactor);
inline constexpr AxisRgb kGridAxisColorY = kAxisColorY.Scaled(kGridAxisDimFactor);
inline constexpr AxisRgb kGridAxisColorZ = kAxisColorZ.Scaled(kGridAxisDimFactor);

/// One author-chosen set of axis hues. The constants above are the defaults; this is the same
/// three colours as a value a caller can hold and change, which is what lets the editor offer a
/// colour picker for them.
///
/// The gizmo and grid colours are NOT stored separately. Deriving the grid's from the gizmo's
/// through kGridAxisDimFactor keeps "the grid is the darker backdrop for the same axis" as one
/// rule in one place; two independent sets would let a picker drift them apart until the grid
/// stopped reading as the same axis at all.
struct AxisPalette {
    AxisRgb x = kAxisColorX;
    AxisRgb y = kAxisColorY;
    AxisRgb z = kAxisColorZ;

    [[nodiscard]] constexpr AxisPalette GridVariant() const {
        return AxisPalette{x.Scaled(kGridAxisDimFactor), y.Scaled(kGridAxisDimFactor),
                              z.Scaled(kGridAxisDimFactor)};
    }
};

/// A channel a colour picker may legitimately produce. Anything outside 0..1, or not a number at
/// all, is rejected rather than clamped: a persisted file that says -3 or NaN is corrupt, and
/// silently reading it as 0 would hand back a palette the author never chose.
[[nodiscard]] constexpr bool IsAxisChannelValid(float channel) {
    return channel >= 0.f && channel <= 1.f;
}

[[nodiscard]] constexpr bool IsAxisRgbValid(const AxisRgb& color) {
    return IsAxisChannelValid(color.r) && IsAxisChannelValid(color.g) &&
           IsAxisChannelValid(color.b);
}

[[nodiscard]] constexpr bool IsAxisPaletteValid(const AxisPalette& palette) {
    return IsAxisRgbValid(palette.x) && IsAxisRgbValid(palette.y) &&
           IsAxisRgbValid(palette.z);
}

} // namespace univex::viewport
