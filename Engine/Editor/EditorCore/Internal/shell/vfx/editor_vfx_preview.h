// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "particle_system_asset.h"

#include "prism/Widget.h"

#include <cstddef>
#include <functional>
#include <string>

namespace UVE::Editor {

class Editor;

/// Formats the live preview statistics overlay text: per-emitter live particle count,
/// CPU simulation milliseconds per emitter, and total instances drawn.
[[nodiscard]] std::string FormatVfxPreviewStatsText(const Editor& editor);

/// Studio-grade timeline widget for the VFX Editor, positioned directly below the 3D preview:
/// - Full-width ruler across `[0, max(0.1, duration, warmUpSeconds)]` with adaptive major/minor ticks
/// - Shaded warm-up region `[0, warmUpSeconds]`
/// - Elapsed playback progress rail + burst diamond keyframe lane
/// - Hover scrub guide + high-contrast draggable playhead with clamped time pill
class VfxTimelineWidget final : public prism::Widget {
public:
    explicit VfxTimelineWidget(Editor& editor);

    std::function<void(double)> onScrub;

    prism::Vec2 measure(prism::Vec2 avail) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    void onMouseLeave() override;
    prism::Cursor cursor() const override { return prism::Cursor::SizeWE; }
    const std::string& tooltipAt(prism::Vec2 p) const override;
    bool focusable() const override { return true; }

private:
    [[nodiscard]] double EffectiveDuration() const noexcept;
    [[nodiscard]] float TimeToScreenX(double seconds) const noexcept;
    [[nodiscard]] double ScreenXToTime(float x) const noexcept;
    void ScrubAtScreenX(float x);
    void UpdateHoverTooltip(float x);

    Editor& m_editor;
    bool m_scrubbing = false;
    bool m_hoverValid = false;
    double m_hoverTime = 0.0;
    std::string m_hoverTooltip;
};

} // namespace UVE::Editor
