// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Canvas.h"
#include "prism/Theme.h"

#include <string>

namespace UVE::Editor {

/// The look every timeline in the editor shares - the Animation Sequencer's dope sheet and the
/// Montage Editor's tracks: the ruler with its ticks and grid, the playhead with its frame tag, the
/// shade past the end, the row headers and the key diamonds, in one set of colours.
struct TimelineScaleUVE final {
    float left = 0.0F;              ///< screen x of time `scrollSeconds`
    double scrollSeconds = 0.0;
    double pixelsPerSecond = 100.0;
    [[nodiscard]] float XUVE(const double seconds) const noexcept {
        return left + static_cast<float>((seconds - scrollSeconds) * pixelsPerSecond);
    }
    [[nodiscard]] double SecondsUVE(const float x) const noexcept {
        return scrollSeconds + static_cast<double>(x - left) / pixelsPerSecond;
    }
};

/// Position orange, Rotation green, Scale violet; events blue, blend shapes pink, the playhead red.
[[nodiscard]] prism::Color TimelineChannelColorUVE(int channel) noexcept;
[[nodiscard]] prism::Color TimelineEventColorUVE() noexcept;
[[nodiscard]] prism::Color TimelineMorphColorUVE() noexcept;
[[nodiscard]] prism::Color TimelinePlayheadColorUVE() noexcept;
[[nodiscard]] const char* TimelineChannelNameUVE(int channel) noexcept;

/// The ruler across `ruler` (ticks, labels in frames below a second's spacing - else seconds -
/// and grid lines down to `gridBottom`), up to `endSeconds`.
void DrawTimelineRulerUVE(prism::Canvas& c, prism::Rect ruler, const TimelineScaleUVE& scale, double frameRate,
                          double endSeconds, float gridBottom, bool frameLabels);
/// Shade over the time past `endSeconds` inside `area`.
void DrawTimelineOutOfRangeUVE(prism::Canvas& c, prism::Rect area, const TimelineScaleUVE& scale, double endSeconds);
/// The playhead at `seconds`: a line down `area` and a tag on the ruler (`rulerHeight` tall) with
/// `label` (the frame) in it.
void DrawTimelinePlayheadUVE(prism::Canvas& c, prism::Rect area, float rulerHeight, const TimelineScaleUVE& scale,
                             double seconds, const std::string& label);
/// A row header: the header colour, the label dimmed, a line under it.
void DrawTimelineRowHeaderUVE(prism::Canvas& c, prism::Rect header, const std::string& label, prism::Color text);
/// A key: a diamond of half-size `half`, outlined in white when selected.
void DrawTimelineKeyUVE(prism::Canvas& c, prism::Vec2 centre, float half, prism::Color color, bool selected);

} // namespace UVE::Editor
