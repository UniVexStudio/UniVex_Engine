// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_timeline_draw.h"

#include "editor_timeline_tracks.h"

#include "prism/Font.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace UVE::Editor {

prism::Color TimelineChannelColorUVE(const int channel) noexcept {
    switch (channel) {
        case 0: return prism::Color(236, 146, 72);    // Position
        case 1: return prism::Color(96, 200, 132);    // Rotation
        default: return prism::Color(150, 128, 236);  // Scale
    }
}

prism::Color TimelineEventColorUVE() noexcept { return prism::Color(112, 196, 255); }
prism::Color TimelineMorphColorUVE() noexcept { return prism::Color(214, 132, 206); }
prism::Color TimelinePlayheadColorUVE() noexcept { return prism::Color(236, 86, 86); }

const char* TimelineChannelNameUVE(const int channel) noexcept {
    return channel == 0 ? "Position" : channel == 1 ? "Rotation" : "Scale";
}

void DrawTimelineRulerUVE(prism::Canvas& c, const prism::Rect ruler, const TimelineScaleUVE& scale, const double frameRate,
                          const double endSeconds, const float gridBottom, const bool frameLabels) {
    const prism::Theme& t = prism::theme();
    c.fillRect(ruler, t.header);
    c.fillRect(prism::Rect{ruler.x, ruler.bottom() - 1.0F, ruler.w, 1.0F}, t.border);
    const double rate = std::max(1.0, frameRate);
    const double tick = PickTimelineTickStepUVE(scale.pixelsPerSecond, rate, 56.0F);
    const double minor = std::max(1.0 / rate, tick / 5.0);
    const double visible = static_cast<double>(ruler.w) / scale.pixelsPerSecond;
    c.pushClip(ruler);
    for (double s = std::floor(scale.scrollSeconds / minor) * minor; s <= scale.scrollSeconds + visible + minor; s += minor) {
        if (s < -1e-9 || s > endSeconds + 1e-9) {
            continue;
        }
        const float x = std::round(scale.XUVE(s));
        const bool major = std::abs(std::remainder(s, tick)) < minor * 0.25;
        c.fillRect(prism::Rect{x, ruler.y + (major ? 11.0F : 17.0F), 1.0F, ruler.h - (major ? 11.0F : 17.0F)},
                   major ? t.textDim : t.borderStrong);
        if (major) {
            char label[32];
            if (frameLabels && tick < 1.0) {
                std::snprintf(label, sizeof(label), "%ld", static_cast<long>(std::lround(s * rate)));
            } else if (tick < 1.0) {
                std::snprintf(label, sizeof(label), "%.2f", s);
            } else {
                std::snprintf(label, sizeof(label), "%.0fs", s);
            }
            c.textIn(*t.small, prism::Rect{x + 3.0F, ruler.y, 64.0F, 13.0F}, label, t.textDim);
        }
    }
    c.popClip();
    // Grid lines through the rows, on the major ticks.
    const prism::Rect rows{ruler.x, ruler.bottom(), ruler.w, std::max(0.0F, gridBottom - ruler.bottom())};
    c.pushClip(rows);
    for (double s = std::floor(scale.scrollSeconds / tick) * tick; s <= scale.scrollSeconds + visible + tick; s += tick) {
        if (s < -1e-9 || s > endSeconds + 1e-9) {
            continue;
        }
        c.fillRect(prism::Rect{std::round(scale.XUVE(s)), rows.y, 1.0F, rows.h}, prism::Color(255, 255, 255, 12));
    }
    c.popClip();
}

void DrawTimelineOutOfRangeUVE(prism::Canvas& c, const prism::Rect area, const TimelineScaleUVE& scale,
                               const double endSeconds) {
    const float endX = std::max(area.x, scale.XUVE(endSeconds));
    if (endX < area.right()) {
        c.fillRect(prism::Rect{endX, area.y, area.right() - endX, area.h}, prism::Color(0, 0, 0, 70));
        c.fillRect(prism::Rect{endX, area.y, 1.0F, area.h}, prism::Color(255, 255, 255, 40));
    }
}

void DrawTimelinePlayheadUVE(prism::Canvas& c, const prism::Rect area, const float rulerHeight, const TimelineScaleUVE& scale,
                             const double seconds, const std::string& label) {
    const float x = std::round(scale.XUVE(seconds));
    if (x < area.x - 1.0F || x > area.right() + 1.0F) {
        return;
    }
    const prism::Theme& t = prism::theme();
    const prism::Color red = TimelinePlayheadColorUVE();
    c.pushClip(prism::Rect{area.x - 24.0F, area.y, area.w + 48.0F, area.h});
    c.fillRect(prism::Rect{x, area.y + rulerHeight - 4.0F, 1.0F, area.h - rulerHeight + 4.0F}, red);
    const float w = std::max(18.0F, t.small->width(label) + 10.0F);
    const prism::Rect tag{x - w * 0.5F, area.y + 2.0F, w, rulerHeight - 8.0F};
    c.fillRoundRect(tag, 3.0F, red);
    const prism::Vec2 point[] = {{x - 4.0F, tag.bottom()}, {x + 4.0F, tag.bottom()}, {x, tag.bottom() + 4.0F}};
    c.fillPolygon(point, red);
    c.textIn(*t.small, tag, label, prism::Color(255, 255, 255), prism::HAlign::Center, false);
    c.popClip();
}

void DrawTimelineRowHeaderUVE(prism::Canvas& c, const prism::Rect header, const std::string& label, const prism::Color text) {
    const prism::Theme& t = prism::theme();
    c.fillRect(header, t.header);
    c.textIn(*t.small, prism::Rect{header.x + 6.0F, header.y, header.w - 8.0F, header.h}, label, text);
    c.fillRect(prism::Rect{header.x, header.bottom() - 1.0F, header.w, 1.0F}, t.border);
}

void DrawTimelineKeyUVE(prism::Canvas& c, const prism::Vec2 centre, const float half, const prism::Color color,
                        const bool selected) {
    if (selected) {
        const float o = half + 2.0F;
        const prism::Vec2 ring[] = {{centre.x, centre.y - o}, {centre.x + o, centre.y}, {centre.x, centre.y + o}, {centre.x - o, centre.y}};
        c.fillPolygon(ring, prism::Color(255, 255, 255));
    }
    const prism::Vec2 diamond[] = {{centre.x, centre.y - half}, {centre.x + half, centre.y}, {centre.x, centre.y + half},
                                   {centre.x - half, centre.y}};
    c.fillPolygon(diamond, selected ? prism::Color(255, 226, 120) : color);
}

} // namespace UVE::Editor
