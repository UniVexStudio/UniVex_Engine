// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Canvas.h"
#include "prism/Theme.h"

#include <string>

namespace UVE::Editor {

/// The look every timeline in the editor shares - the Animation Sequencer's dope sheet and the
/// Motion Montage Editor's tracks: the ruler with its ticks and grid, the playhead with its frame tag, the
/// shade past the end, the row headers and the key diamonds, in one set of colours.
struct TimelineScale final {
    float left = 0.0F;              ///< screen x of time `scrollSeconds`
    double scrollSeconds = 0.0;
    double pixelsPerSecond = 100.0;
    [[nodiscard]] float X(const double seconds) const noexcept {
        return left + static_cast<float>((seconds - scrollSeconds) * pixelsPerSecond);
    }
    [[nodiscard]] double Seconds(const float x) const noexcept {
        return scrollSeconds + static_cast<double>(x - left) / pixelsPerSecond;
    }
};

/// Position orange, Rotation green, Scale violet; events blue, blend shapes pink, the playhead red.
[[nodiscard]] prism::Color TimelineChannelColor(int channel) noexcept;
[[nodiscard]] prism::Color TimelineEventColor() noexcept;
[[nodiscard]] prism::Color TimelineMorphColor() noexcept;
[[nodiscard]] prism::Color TimelinePlayheadColor() noexcept;
[[nodiscard]] const char* TimelineChannelName(int channel) noexcept;

/// The ruler across `ruler` (ticks, labels in frames below a second's spacing - else seconds -
/// and grid lines down to `gridBottom`), up to `endSeconds`.
void DrawTimelineRuler(prism::Canvas& c, prism::Rect ruler, const TimelineScale& scale, double frameRate,
                          double endSeconds, float gridBottom, bool frameLabels);
/// Shade over the time past `endSeconds` inside `area`.
void DrawTimelineOutOfRange(prism::Canvas& c, prism::Rect area, const TimelineScale& scale, double endSeconds);
/// The playhead at `seconds`: a line down `area` and a tag on the ruler (`rulerHeight` tall) with
/// `label` (the frame) in it.
void DrawTimelinePlayhead(prism::Canvas& c, prism::Rect area, float rulerHeight, const TimelineScale& scale,
                             double seconds, const std::string& label);
/// A row header: the header colour, the label dimmed, a line under it.
void DrawTimelineRowHeader(prism::Canvas& c, prism::Rect header, const std::string& label, prism::Color text);
/// A key: a diamond of half-size `half`, outlined in white when selected.
void DrawTimelineKey(prism::Canvas& c, prism::Vec2 centre, float half, prism::Color color, bool selected);

} // namespace UVE::Editor
