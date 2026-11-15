// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "particle_system_asset.h"

#include "prism/Widget.h"

#include <cstddef>
#include <functional>
#include <string>

namespace UVE::Editor {

class EditorUVE;

/// Formats the live preview statistics overlay text: per-emitter live particle count,
/// CPU simulation milliseconds per emitter, and total instances drawn.
[[nodiscard]] std::string FormatVfxPreviewStatsTextUVE(const EditorUVE& editor);

/// Timeline widget for the VFX Editor:
/// - Ruler across `[0, max(0.1, duration)]`
/// - Shaded warm-up region `[0, warmUpSeconds]`
/// - Burst diamond markers for every `SpawnBurst` module across enabled emitters
/// - Draggable playhead with time badge
class VfxTimelineWidgetUVE final : public prism::Widget {
public:
    explicit VfxTimelineWidgetUVE(EditorUVE& editor);

    std::function<void(double)> onScrub;

    prism::Vec2 measure(prism::Vec2 avail) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool focusable() const override { return true; }

private:
    [[nodiscard]] double EffectiveDurationUVE() const noexcept;
    [[nodiscard]] float TimeToScreenXUVE(double seconds) const noexcept;
    [[nodiscard]] double ScreenXToTimeUVE(float x) const noexcept;
    void ScrubAtScreenXUVE(float x);

    EditorUVE& m_editor;
    bool m_scrubbing = false;
};

} // namespace UVE::Editor
