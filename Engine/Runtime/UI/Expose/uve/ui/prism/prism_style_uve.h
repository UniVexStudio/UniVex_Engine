// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include "uve/math/color_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/ui/prism/prism_geometry_uve.h"

namespace UVE::UI::Prism {

/// Measurement unit for sizes, paddings, and margins.
enum class PrismUnitUVE : std::uint8_t {
    Pixel,   // Concrete device-independent pixels (e.g. 240.0f)
    Percent, // Normalized fraction 0.0f - 1.0f relative to parent container
    Auto,    // Fit to content size
};

/// 1D scalar dimension with unit semantics.
struct PrismLengthUVE final {
    float value = 0.0F;
    PrismUnitUVE unit = PrismUnitUVE::Pixel;

    constexpr PrismLengthUVE() noexcept = default;
    constexpr explicit PrismLengthUVE(float px) noexcept : value(px), unit(PrismUnitUVE::Pixel) {}
    constexpr PrismLengthUVE(float val, PrismUnitUVE u) noexcept : value(val), unit(u) {}

    [[nodiscard]] static constexpr PrismLengthUVE Auto() noexcept {
        return PrismLengthUVE{0.0F, PrismUnitUVE::Auto};
    }
    [[nodiscard]] static constexpr PrismLengthUVE Px(float px) noexcept {
        return PrismLengthUVE{px, PrismUnitUVE::Pixel};
    }
    [[nodiscard]] static constexpr PrismLengthUVE Pct(float percent) noexcept {
        return PrismLengthUVE{percent, PrismUnitUVE::Percent};
    }

    [[nodiscard]] constexpr bool IsAuto() const noexcept { return unit == PrismUnitUVE::Auto; }

    [[nodiscard]] float Resolve(float parentExtent, float contentExtent) const noexcept {
        switch (unit) {
            case PrismUnitUVE::Pixel:   return value;
            case PrismUnitUVE::Percent: return value * parentExtent;
            case PrismUnitUVE::Auto:    return contentExtent;
        }
        return value;
    }

    [[nodiscard]] constexpr bool operator==(const PrismLengthUVE&) const noexcept = default;
};

/// 4-sided inset (Left, Top, Right, Bottom) for Padding, Margin, or Borders.
struct PrismEdgesUVE final {
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;

    constexpr PrismEdgesUVE() noexcept = default;
    constexpr explicit PrismEdgesUVE(float uniform) noexcept
        : left(uniform), top(uniform), right(uniform), bottom(uniform) {}
    constexpr PrismEdgesUVE(float horizontal, float vertical) noexcept
        : left(horizontal), top(vertical), right(horizontal), bottom(vertical) {}
    constexpr PrismEdgesUVE(float l, float t, float r, float b) noexcept
        : left(l), top(t), right(r), bottom(b) {}

    [[nodiscard]] constexpr float Horizontal() const noexcept { return left + right; }
    [[nodiscard]] constexpr float Vertical() const noexcept { return top + bottom; }

    [[nodiscard]] constexpr bool operator==(const PrismEdgesUVE&) const noexcept = default;
};

/// Direction of linear arrangement.
enum class PrismFlexDirectionUVE : std::uint8_t {
    Row,    // Horizontal left-to-right
    Column, // Vertical top-to-bottom
};

/// Alignment along the main axis.
enum class PrismJustifyUVE : std::uint8_t {
    Start,        // Packed toward start
    Center,       // Centered
    End,          // Packed toward end
    SpaceBetween, // First item at start, last at end, equal spacing between
};

/// Alignment along the cross axis.
enum class PrismAlignUVE : std::uint8_t {
    Start,   // Top (for Row) or Left (for Column)
    Center,  // Centered along cross axis
    End,     // Bottom (for Row) or Right (for Column)
    Stretch, // Stretch to occupy entire cross axis extent
};

/// Complete box styling & layout descriptor for a Prism widget.
struct PrismStyleUVE final {
    // Sizing
    PrismLengthUVE width = PrismLengthUVE::Auto();
    PrismLengthUVE height = PrismLengthUVE::Auto();
    float minWidth = 0.0F;
    float minHeight = 0.0F;
    float maxWidth = 1000000.0F;
    float maxHeight = 1000000.0F;

    // Spacing
    PrismEdgesUVE padding{};
    PrismEdgesUVE margin{};
    float gap = 0.0F; // Spacing between consecutive children

    // Flex container properties
    PrismFlexDirectionUVE flexDirection = PrismFlexDirectionUVE::Column;
    PrismJustifyUVE       justifyContent = PrismJustifyUVE::Start;
    PrismAlignUVE         alignItems = PrismAlignUVE::Stretch;

    // Flex item properties
    float flexGrow = 0.0F;   // Expansion weight
    float flexShrink = 1.0F; // Shrink weight

    // Visual appearance
    PrismColorUVE      backgroundColor{0.0F, 0.0F, 0.0F, 0.0F};
    PrismColorUVE      borderColor{0.0F, 0.0F, 0.0F, 0.0F};
    float               borderWidth = 0.0F;
    PrismCornerRadiiUVE cornerRadii{};

    // Scissor clipping
    bool clipChildren = false;

    [[nodiscard]] bool operator==(const PrismStyleUVE&) const noexcept = default;
};

} // namespace UVE::UI::Prism
