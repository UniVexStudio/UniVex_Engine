// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_metrics_overlay_uve.h"

namespace UVE::UI::Prism {

PrismMetricsOverlayUVE::PrismMetricsOverlayUVE()
    : PrismWidgetUVE("PrismMetricsOverlay") {
    const auto& theme = PrismThemeUVE::Dark();

    m_style.width = PrismLengthUVE::Px(240.0F);
    m_style.height = PrismLengthUVE::Px(110.0F);
    m_style.backgroundColor = theme.bgPanel;
    m_style.borderColor = theme.borderSubtle;
    m_style.borderWidth = 1.0F;
    m_style.cornerRadii = theme.radiusMedium;
    m_style.padding = PrismEdgesUVE{12.0F, 10.0F};
    m_style.flexDirection = PrismFlexDirectionUVE::Column;
    m_style.gap = theme.gapSmall;

    // Toggle minimize button
    m_toggleButton = std::make_shared<PrismButtonUVE>("HUD");
    m_toggleButton->GetStyle().width = PrismLengthUVE::Px(48.0F);
    m_toggleButton->GetStyle().height = PrismLengthUVE::Px(22.0F);
    m_toggleButton->SetOnClick([this]() {
        ToggleMinimized();
    });

    AddChild(m_toggleButton);
}

void PrismMetricsOverlayUVE::ToggleMinimized() noexcept {
    m_minimized = !m_minimized;
    if (m_minimized) {
        m_style.height = PrismLengthUVE::Px(36.0F);
    } else {
        m_style.height = PrismLengthUVE::Px(110.0F);
    }
}

void PrismMetricsOverlayUVE::UpdateMetricsUVE(const Core::FrameStatsUVE& stats,
                                              std::uint32_t drawCallsRecorded,
                                              std::size_t memoryUsageBytes) {
    m_fps = stats.fps;
    m_frameTimeMs = stats.frameTimeSeconds * 1000.0;
    m_drawCalls = drawCallsRecorded;
    m_memoryBytes = memoryUsageBytes;
}

void PrismMetricsOverlayUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    const auto& theme = PrismThemeUVE::Dark();

    // Subtle drop shadow behind overlay window
    drawList.AddDropShadow(m_layoutRect, 10.0F, 2.0F, PrismColorUVE{0.0F, 0.0F, 0.0F, 0.4F}, {0.0F, 4.0F}, m_style.cornerRadii);

    // Background panel
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    // Performance bar indicator (Green/Cyan if > 50 FPS, Yellow if 30-50, Red if < 30)
    PrismColorUVE indicatorColor = theme.accentPrimary;
    if (m_fps > 0.0 && m_fps < 30.0) {
        indicatorColor = PrismColorUVE{0.95F, 0.25F, 0.25F, 1.0F};
    } else if (m_fps >= 30.0 && m_fps < 50.0) {
        indicatorColor = PrismColorUVE{0.95F, 0.80F, 0.25F, 1.0F};
    }

    const Math::RectUVE headerBar{
        m_layoutRect.position,
        Math::Vector2UVE{m_layoutRect.size.x, 3.0F}
    };
    drawList.AddRectFilled(headerBar, indicatorColor, PrismCornerRadiiUVE{m_style.cornerRadii.topLeft, m_style.cornerRadii.topRight, 0.0F, 0.0F});
}

} // namespace UVE::UI::Prism
