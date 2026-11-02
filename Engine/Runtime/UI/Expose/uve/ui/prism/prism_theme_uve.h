// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/math/color_uve.h"
#include "uve/ui/prism/prism_geometry_uve.h"

namespace UVE::UI::Prism {

/// Central design tokens for Prism UI: colors, typography sizes, radii, and spacing.
/// Follows modern AAA engine design systems (Unreal Slate / Figma / Linear).
struct PrismThemeUVE final {
    // Background tones (Dark theme)
    PrismColorUVE bgApp{0.07F, 0.08F, 0.09F, 1.0F};       // Main background (#121417)
    PrismColorUVE bgPanel{0.10F, 0.11F, 0.13F, 1.0F};     // Panel surface (#1a1c21)
    PrismColorUVE bgWidget{0.14F, 0.15F, 0.18F, 1.0F};    // Button/Input background (#24262e)
    PrismColorUVE bgWidgetHover{0.18F, 0.20F, 0.24F, 1.0F};
    PrismColorUVE bgWidgetActive{0.22F, 0.25F, 0.30F, 1.0F};

    // Accent / Brand (Prism Cyan/Blue)
    PrismColorUVE accentPrimary{0.16F, 0.52F, 0.95F, 1.0F}; // #2985f2
    PrismColorUVE accentHover{0.25F, 0.60F, 1.00F, 1.0F};
    PrismColorUVE accentFocusRing{0.30F, 0.65F, 1.00F, 0.6F};

    // Text & Foreground
    PrismColorUVE textPrimary{0.95F, 0.96F, 0.98F, 1.0F};
    PrismColorUVE textSecondary{0.65F, 0.68F, 0.74F, 1.0F};
    PrismColorUVE textMuted{0.45F, 0.48F, 0.53F, 1.0F};
    PrismColorUVE textDisabled{0.30F, 0.32F, 0.36F, 1.0F};

    // Borders & Dividers
    PrismColorUVE borderSubtle{0.18F, 0.20F, 0.23F, 1.0F};
    PrismColorUVE borderProminent{0.28F, 0.30F, 0.35F, 1.0F};

    // Corner Radii
    PrismCornerRadiiUVE radiusSmall{4.0F};
    PrismCornerRadiiUVE radiusMedium{6.0F};
    PrismCornerRadiiUVE radiusLarge{10.0F};

    // Default Spacing Metrics
    float gapSmall = 4.0F;
    float gapMedium = 8.0F;
    float gapLarge = 16.0F;

    [[nodiscard]] static const PrismThemeUVE& Dark() noexcept {
        static const PrismThemeUVE s_darkTheme{};
        return s_darkTheme;
    }

    [[nodiscard]] bool operator==(const PrismThemeUVE&) const noexcept = default;
};

} // namespace UVE::UI::Prism
