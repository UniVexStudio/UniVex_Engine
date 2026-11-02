// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <memory>
#include <string>
#include "uve/core/frame_stats_uve.h"
#include "uve/ui/prism/prism_button_uve.h"
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

/// Pilot floating HUD window displaying live engine metrics:
/// FPS, Frame time (ms), and draw calls, styled with Prism modern tokens.
class PrismMetricsOverlayUVE final : public PrismWidgetUVE {
public:
    PrismMetricsOverlayUVE();

    /// Updates live frame statistics.
    void UpdateMetricsUVE(const Core::FrameStatsUVE& stats, std::uint32_t drawCallsRecorded, std::size_t memoryUsageBytes);

    [[nodiscard]] bool IsMinimized() const noexcept { return m_minimized; }
    void ToggleMinimized() noexcept;

protected:
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    bool                        m_minimized = false;
    std::shared_ptr<PrismButtonUVE> m_toggleButton;

    // Metrics cache
    double                      m_fps = 0.0;
    double                      m_frameTimeMs = 0.0;
    std::uint32_t               m_drawCalls = 0;
    std::size_t                 m_memoryBytes = 0;
};

} // namespace UVE::UI::Prism
