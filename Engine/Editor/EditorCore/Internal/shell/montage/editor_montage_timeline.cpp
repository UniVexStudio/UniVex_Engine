// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_montage_timeline.h"

#include "editor.h"
#include "editor_asset_table.h"
#include "editor_timeline_draw.h"

#include "motion_montage_editing.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Menu.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace UVE::Editor {
namespace {

constexpr float kHeader = 96.0F;
constexpr float kRuler = 22.0F;
constexpr float kSections = 24.0F;
constexpr float kSlot = 34.0F;
constexpr float kNotify = 22.0F;
constexpr float kCurves = 48.0F;

using Selection = EditorMontageEditor::Selection;
using Kind = Selection::Kind;

[[nodiscard]] prism::Color SectionColor(const std::size_t index) {
    static const prism::Color kColors[] = {prism::Color(86, 156, 214), prism::Color(197, 134, 192), prism::Color(78, 201, 176),
                                           prism::Color(220, 170, 90), prism::Color(214, 112, 112), prism::Color(150, 190, 90)};
    return kColors[index % std::size(kColors)];
}

[[nodiscard]] std::string SecondsText(const double t) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", t);
    return text;
}

/// The value range a curve is drawn over: its keys' range, always taking in 0..1, so a key's height
/// means the same when drawn, picked, added and dragged.
struct CurveRange final {
    float lo = 0.0F;
    float span = 1.0F;
};
[[nodiscard]] CurveRange CurveRangeOf(const Asset::MontageCurve& curve) {
    float lo = 0.0F;
    float hi = 1.0F;
    for (const Asset::MontageCurveKey& key : curve.keys) {
        lo = std::min(lo, key.value);
        hi = std::max(hi, key.value);
    }
    return CurveRange{lo, std::max(hi - lo, 1e-6F)};
}

[[nodiscard]] prism::Color NotifyColour(const std::uint32_t colour, const bool selected) {
    if (colour == 0U) {
        return selected ? prism::Color(255, 210, 90) : prism::Color(230, 180, 60);
    }
    const prism::Color c(static_cast<std::uint8_t>(colour >> 16U), static_cast<std::uint8_t>(colour >> 8U), static_cast<std::uint8_t>(colour));
    return selected ? prism::Color(static_cast<std::uint8_t>(std::min(255, c.r + 40)), static_cast<std::uint8_t>(std::min(255, c.g + 40)),
                                   static_cast<std::uint8_t>(std::min(255, c.b + 40)))
                    : c;
}

} // namespace

std::size_t MontageTimeline::NotifyTracks() const noexcept {
    const Editor::MontageEditSession* const session = m_window.EditorRef().MontageEditSessionPtr();
    if (session == nullptr) {
        return 1U;
    }
    std::size_t tracks = std::max<std::size_t>(1U, session->montage.notifyTracks);
    for (const Asset::MontageNotify& n : session->montage.notifies) {
        tracks = std::max<std::size_t>(tracks, n.track + 1U);
    }
    for (const Asset::MontageNotifyState& s : session->montage.notifyStates) {
        tracks = std::max<std::size_t>(tracks, s.track + 1U);
    }
    return tracks;
}

prism::Vec2 MontageTimeline::measure(const prism::Vec2 avail) {
    const float height = kRuler + kSections + kSlot + kNotify * static_cast<float>(NotifyTracks()) + kCurves;
    return m_desired = constrain({avail.x, height});
}

float MontageTimeline::XOf(const double time) const noexcept {
    return m_rect.x + kHeader + static_cast<float>((time - m_scroll) * m_pixelsPerSecond);
}

double MontageTimeline::TimeAt(const float x) const noexcept {
    return std::max(0.0, m_scroll + static_cast<double>(x - m_rect.x - kHeader) / m_pixelsPerSecond);
}

prism::Rect MontageTimeline::RowRect(const Row row, const std::size_t track) const noexcept {
    float y = m_rect.y;
    const auto rect = [&](const float h) { return prism::Rect{m_rect.x, y, m_rect.w, h}; };
    if (row == Row::Ruler) {
        return rect(kRuler);
    }
    y += kRuler;
    if (row == Row::Sections) {
        return rect(kSections);
    }
    y += kSections;
    if (row == Row::Slot) {
        return rect(kSlot);
    }
    y += kSlot;
    if (row == Row::Notify) {
        y += kNotify * static_cast<float>(track);
        return rect(kNotify);
    }
    y += kNotify * static_cast<float>(NotifyTracks());
    return rect(kCurves);
}

void MontageTimeline::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, t.panel);
    Editor::MontageEditSession* const session = m_window.EditorRef().MontageEditSessionPtr();
    if (session == nullptr) {
        return;
    }
    const Asset::MotionMontageAsset& montage = session->montage;
    const double length = Asset::MontageLength(montage);
    const std::size_t tracks = NotifyTracks();
    const Selection picked = m_window.selection;

    // Track headers.
    const auto header = [&](const prism::Rect row, const std::string& label, const bool alt) {
        c.fillRect(prism::Rect{row.x, row.y, row.w, row.h}, alt ? t.window : t.panel);
        c.fillRect(prism::Rect{row.x, row.y, kHeader, row.h}, t.header);
        c.textIn(*t.small, prism::Rect{row.x + 6.0F, row.y, kHeader - 8.0F, row.h}, label, t.textDim);
        c.fillRect(prism::Rect{row.x, row.bottom() - 1.0F, row.w, 1.0F}, t.border);
    };
    header(RowRect(Row::Ruler), "Time", false);
    header(RowRect(Row::Sections), "Sections", true);
    header(RowRect(Row::Slot), montage.slot, false);
    for (std::size_t i = 0U; i < tracks; ++i) {
        header(RowRect(Row::Notify, i), Asset::MontageNotifyTrackName(montage, static_cast<std::uint32_t>(i)), i % 2U == 0U);
    }
    header(RowRect(Row::Curves),
           m_window.activeCurve < montage.curves.size() ? "Curve: " + montage.curves[m_window.activeCurve].name : std::string("Curves"), false);

    const prism::Rect content{m_rect.x + kHeader, m_rect.y, m_rect.w - kHeader, m_rect.h};
    c.pushClip(content);
    // The ruler, its grid and the shade past the end: the look every editor timeline shares.
    const TimelineScale scale{m_rect.x + kHeader, m_scroll, static_cast<double>(m_pixelsPerSecond)};
    const prism::Rect ruler = RowRect(Row::Ruler);
    DrawTimelineRuler(c, prism::Rect{content.x, ruler.y, content.w, ruler.h}, scale, 30.0, length, m_rect.bottom(), false);
    DrawTimelineOutOfRange(c, prism::Rect{content.x, ruler.bottom(), content.w, m_rect.bottom() - ruler.bottom()}, scale, length);

    // Sections: a marker and a label each; the line runs down through the tracks.
    const prism::Rect sectionsRow = RowRect(Row::Sections);
    for (std::size_t i = 0U; i < montage.sections.size(); ++i) {
        const Asset::MontageSection& section = montage.sections[i];
        const float x = XOf(section.time);
        const prism::Color color = SectionColor(i);
        const bool selected = picked.kind == Kind::Section && picked.index == i;
        c.fillRect(prism::Rect{x, sectionsRow.y, selected ? 3.0F : 2.0F, m_rect.bottom() - sectionsRow.y},
                   prism::Color(color.r, color.g, color.b, selected ? 220 : 110));
        const std::string label = section.name + (section.next.empty() ? "" : section.next == section.name ? "  (loop)" : "  > " + section.next);
        const float w = std::min(220.0F, t.small->width(label) + 12.0F);
        const prism::Rect tag{x, sectionsRow.y + 3.0F, w, sectionsRow.h - 6.0F};
        c.fillRoundRect(tag, 3.0F, selected ? color : prism::Color(color.r, color.g, color.b, 170));
        c.textIn(*t.small, prism::Rect{tag.x + 5.0F, tag.y, tag.w - 6.0F, tag.h}, label, t.textBright);
    }

    // The slot track: the clips end to end.
    const prism::Rect slotRow = RowRect(Row::Slot);
    for (std::size_t i = 0U; i < montage.segments.size(); ++i) {
        const double start = Asset::MontageSegmentStart(montage, i);
        const double duration = Asset::MontageSegmentDuration(montage.segments[i]);
        const prism::Rect block{XOf(start) + 1.0F, slotRow.y + 4.0F, std::max(4.0F, static_cast<float>(duration * m_pixelsPerSecond) - 2.0F),
                                slotRow.h - 8.0F};
        const bool selected = picked.kind == Kind::Segment && picked.index == i;
        c.fillRoundRect(block, 3.0F, selected ? prism::Color(64, 150, 140) : prism::Color(48, 110, 104));
        if (selected) {
            c.strokeRoundRect(block, 3.0F, t.highlight, 1);
        }
        std::string label = m_window.ClipName(montage.segments[i].clip);
        if (montage.segments[i].loops > 1U) {
            label += "  x" + std::to_string(montage.segments[i].loops);
        }
        c.textIn(*t.small, prism::Rect{block.x + 6.0F, block.y, block.w - 8.0F, block.h}, label, t.textBright);
    }
    if (montage.segments.empty()) {
        c.textIn(*t.small, prism::Rect{content.x + 8.0F, slotRow.y, content.w - 16.0F, slotRow.h},
                 "Drop a clip here from the Asset Table or the Asset Browser, or right-click > Add Clip", t.textDim);
    }

    // Notifies (diamonds) and notify states (bars), each on its track.
    for (std::size_t i = 0U; i < montage.notifyStates.size(); ++i) {
        const Asset::MontageNotifyState& state = montage.notifyStates[i];
        const prism::Rect row = RowRect(Row::Notify, state.track);
        const float x0 = XOf(state.begin);
        const float x1 = std::max(x0 + 4.0F, XOf(state.end));
        const bool selected = picked.kind == Kind::State && picked.index == i;
        const prism::Rect bar{x0, row.y + 4.0F, x1 - x0, row.h - 8.0F};
        c.fillRoundRect(bar, 3.0F, selected ? prism::Color(200, 120, 60) : prism::Color(150, 90, 50));
        if (selected) {
            c.strokeRoundRect(bar, 3.0F, t.highlight, 1);
        }
        c.textIn(*t.small, prism::Rect{bar.x + 5.0F, bar.y, bar.w - 6.0F, bar.h}, state.name, t.textBright);
    }
    for (std::size_t i = 0U; i < montage.notifies.size(); ++i) {
        const Asset::MontageNotify& notify = montage.notifies[i];
        const prism::Rect row = RowRect(Row::Notify, notify.track);
        const float x = XOf(notify.time);
        const float cy = row.y + row.h * 0.5F;
        const bool selected = picked.kind == Kind::Notify && picked.index == i;
        const prism::Vec2 diamond[] = {{x, cy - 6.0F}, {x + 6.0F, cy}, {x, cy + 6.0F}, {x - 6.0F, cy}};
        c.fillPolygon(diamond, NotifyColour(notify.colour, selected));
        if (selected) {
            c.polyline(diamond, t.textBright, 1.0F, true);
        }
        c.textIn(*t.small, prism::Rect{x + 9.0F, row.y, 160.0F, row.h}, notify.name, t.text);
    }

    // Curves: the one being edited bold, with its keys; the others faint behind it.
    const prism::Rect curvesRow = RowRect(Row::Curves);
    for (std::size_t pass = 0U; pass < 2U; ++pass) {
        for (std::size_t ci = 0U; ci < montage.curves.size(); ++ci) {
            const bool active = ci == m_window.activeCurve;
            if ((pass == 0U) == active) {
                continue;   // the active one last, on top
            }
            const Asset::MontageCurve& curve = montage.curves[ci];
            if (curve.keys.empty()) {
                continue;
            }
            const CurveRange range = CurveRangeOf(curve);
            const auto yOf = [&](const float v) { return curvesRow.bottom() - 6.0F - (v - range.lo) / range.span * (curvesRow.h - 12.0F); };
            const prism::Color base = SectionColor(ci + 2U);
            const prism::Color color = active ? base : prism::Color(base.r, base.g, base.b, 80);
            for (std::size_t k = 1U; k < curve.keys.size(); ++k) {
                c.line({XOf(curve.keys[k - 1U].time), yOf(curve.keys[k - 1U].value)}, {XOf(curve.keys[k].time), yOf(curve.keys[k].value)},
                       color, active ? 1.8F : 1.0F);
            }
            if (!active) {
                continue;
            }
            for (std::size_t k = 0U; k < curve.keys.size(); ++k) {
                const bool selected = picked.kind == Kind::CurveKey && picked.index == ci && picked.key == k;
                const prism::Vec2 at{XOf(curve.keys[k].time), yOf(curve.keys[k].value)};
                c.fillCircle(at, selected ? 5.0F : 3.5F, selected ? t.textBright : color);
                c.strokeCircle(at, selected ? 5.0F : 3.5F, prism::Color(0, 0, 0, 160), 1.0F);
            }
        }
    }
    if (montage.curves.empty()) {
        c.textIn(*t.small, prism::Rect{content.x + 8.0F, curvesRow.y, content.w - 16.0F, curvesRow.h},
                 "Right-click > Add Key Here, or + Curve in the toolbar", t.textDim);
    }

    // What is being dragged, where it would land.
    if (m_drag.has_value()) {
        c.fillRect(prism::Rect{m_mouse.x, m_rect.y + kRuler, 1.0F, m_rect.h - kRuler}, t.highlight);
    }
    c.popClip();
    // The playhead, with its time on the ruler.
    DrawTimelinePlayhead(c, content, kRuler, scale, session->playhead, SecondsText(session->playhead));
    if (m_dropHot) {
        c.strokeRoundRect(slotRow, 3.0F, t.highlight, 2);
    }
}

MontageTimeline::Hit MontageTimeline::HitTest(const prism::Vec2 p) const {
    Hit hit;
    const Editor::MontageEditSession* const session = m_window.EditorRef().MontageEditSessionPtr();
    if (session == nullptr || !m_rect.contains(p)) {
        return hit;
    }
    const Asset::MotionMontageAsset& montage = session->montage;
    if (RowRect(Row::Ruler).contains(p)) {
        hit.row = Row::Ruler;
        return hit;
    }
    if (RowRect(Row::Sections).contains(p)) {
        hit.row = Row::Sections;
        for (std::size_t i = montage.sections.size(); i-- > 0U;) {
            const float x = XOf(montage.sections[i].time);
            if (p.x >= x - 3.0F && p.x <= x + 90.0F) {
                hit.item = Selection{Kind::Section, i, 0U};
                break;
            }
        }
        return hit;
    }
    if (RowRect(Row::Slot).contains(p)) {
        hit.row = Row::Slot;
        const double time = TimeAt(p.x);
        for (std::size_t i = 0U; i < montage.segments.size(); ++i) {
            const double start = Asset::MontageSegmentStart(montage, i);
            if (time >= start && time < start + Asset::MontageSegmentDuration(montage.segments[i])) {
                hit.item = Selection{Kind::Segment, i, 0U};
                break;
            }
        }
        return hit;
    }
    for (std::size_t track = 0U; track < NotifyTracks(); ++track) {
        if (!RowRect(Row::Notify, track).contains(p)) {
            continue;
        }
        hit.row = Row::Notify;
        hit.track = track;
        for (std::size_t i = 0U; i < montage.notifies.size(); ++i) {
            if (montage.notifies[i].track == track && std::abs(XOf(montage.notifies[i].time) - p.x) <= 7.0F) {
                hit.item = Selection{Kind::Notify, i, 0U};
                return hit;
            }
        }
        for (std::size_t i = 0U; i < montage.notifyStates.size(); ++i) {
            const Asset::MontageNotifyState& state = montage.notifyStates[i];
            const float x0 = XOf(state.begin);
            const float x1 = std::max(x0 + 4.0F, XOf(state.end));
            if (state.track == track && p.x >= x0 - 4.0F && p.x <= x1 + 4.0F) {
                hit.item = Selection{Kind::State, i, 0U};
                hit.edge = p.x <= x0 + 5.0F ? -1 : p.x >= x1 - 5.0F ? 1 : 0;
                return hit;
            }
        }
        return hit;
    }
    if (RowRect(Row::Curves).contains(p)) {
        hit.row = Row::Curves;
        const prism::Rect row = RowRect(Row::Curves);
        const std::size_t ci = m_window.activeCurve;
        if (ci < montage.curves.size()) {
            const Asset::MontageCurve& curve = montage.curves[ci];
            const CurveRange range = CurveRangeOf(curve);
            for (std::size_t k = 0U; k < curve.keys.size(); ++k) {
                const float x = XOf(curve.keys[k].time);
                const float y = row.bottom() - 6.0F - (curve.keys[k].value - range.lo) / range.span * (row.h - 12.0F);
                if (std::abs(x - p.x) <= 6.0F && std::abs(y - p.y) <= 6.0F) {
                    hit.item = Selection{Kind::CurveKey, ci, k};
                    return hit;
                }
            }
        }
    }
    return hit;
}

bool MontageTimeline::onMouseDown(const prism::MouseEvent& e) {
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    Editor& editor = m_window.EditorRef();
    if (editor.MontageEditSessionPtr() == nullptr) {
        return true;
    }
    if (e.pos.x < m_rect.x + kHeader) {
        // A track's header: its own menu (add, rename, remove a track; the curves).
        if (e.button == prism::MouseButton::Right) {
            ShowHeaderMenu(HitTest(prism::Vec2{m_rect.x + kHeader + 1.0F, e.pos.y}), e.pos);
        }
        return true;
    }
    const Hit hit = HitTest(e.pos);
    if (e.button == prism::MouseButton::Right) {
        if (hit.item.kind != Kind::None) {
            m_window.selection = hit.item;
        }
        ShowContextMenu(hit, e.pos);
        invalidate();
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    m_mouse = e.pos;
    if (hit.item.kind != Kind::None) {
        m_window.selection = hit.item;
        m_drag = hit;
        m_dragFrom = e.pos;
    } else {
        if (hit.row != Row::Ruler) {
            m_window.selection = Selection{};
        }
        m_scrubbing = true;
        editor.PauseMontagePreview();
        editor.SetMontagePlayhead(TimeAt(e.pos.x));
        editor.TickMontageEditor(0.0);
    }
    invalidate();
    return true;
}

bool MontageTimeline::onMouseMove(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    if (m_scrubbing) {
        m_window.EditorRef().SetMontagePlayhead(TimeAt(e.pos.x));
        m_window.EditorRef().TickMontageEditor(0.0);
        invalidate();
    } else if (m_drag.has_value()) {
        invalidate();
    }
    return true;
}

bool MontageTimeline::onMouseUp(const prism::MouseEvent& e) {
    if (m_drag.has_value() && std::abs(e.pos.x - m_dragFrom.x) > 3.0F) {
        CommitDrag(e.pos);
    }
    m_drag.reset();
    m_scrubbing = false;
    invalidate();
    return true;
}

void MontageTimeline::CommitDrag(const prism::Vec2 p) {
    const Hit drag = *m_drag;
    Editor& editor = m_window.EditorRef();
    const double time = TimeAt(p.x);
    const double moved = TimeAt(p.x) - TimeAt(m_dragFrom.x);
    switch (drag.item.kind) {
        case Kind::Section:
            editor.EditMontage([&](Asset::MotionMontageAsset& m) {
                if (drag.item.index < m.sections.size()) {
                    const std::string name = m.sections[drag.item.index].name;
                    static_cast<void>(Asset::MoveMontageSection(m, name, time));
                }
            });
            m_window.selection = Selection{};
            break;
        case Kind::Segment: {
            // Dropped over another clip: it takes that clip's place.
            const auto* session = editor.MontageEditSessionPtr();
            std::size_t to = drag.item.index;
            for (std::size_t i = 0U; session != nullptr && i < session->montage.segments.size(); ++i) {
                const double start = Asset::MontageSegmentStart(session->montage, i);
                if (time >= start && time < start + Asset::MontageSegmentDuration(session->montage.segments[i])) {
                    to = i;
                }
            }
            editor.EditMontage([&](Asset::MotionMontageAsset& m) {
                static_cast<void>(Asset::MoveMontageSegment(m, drag.item.index, to));
            });
            m_window.selection = Selection{Kind::Segment, to, 0U};
            break;
        }
        case Kind::Notify:
            editor.EditMontage([&](Asset::MotionMontageAsset& m) {
                if (drag.item.index < m.notifies.size()) {
                    m.notifies[drag.item.index].time = std::clamp(time, 0.0, std::max(Asset::kMontageEmptyTimelineSeconds, Asset::MontageLength(m)));
                    if (const std::optional<std::uint32_t> track = TrackAt(p.y)) {
                        m.notifies[drag.item.index].track = *track;   // dropped on another track: it moves there
                    }
                }
            });
            break;
        case Kind::State:
            editor.EditMontage([&](Asset::MotionMontageAsset& m) {
                if (drag.item.index >= m.notifyStates.size()) {
                    return;
                }
                Asset::MontageNotifyState& state = m.notifyStates[drag.item.index];
                const double length = std::max(Asset::kMontageEmptyTimelineSeconds, Asset::MontageLength(m));
                if (const std::optional<std::uint32_t> track = TrackAt(p.y); track.has_value() && drag.edge == 0) {
                    state.track = *track;
                }
                if (drag.edge < 0) {
                    state.begin = std::clamp(time, 0.0, state.end);
                } else if (drag.edge > 0) {
                    state.end = std::clamp(time, state.begin, length);
                } else {
                    const double span = state.end - state.begin;
                    state.begin = std::clamp(state.begin + moved, 0.0, std::max(0.0, length - span));
                    state.end = state.begin + span;
                }
            });
            break;
        case Kind::CurveKey:
            editor.EditMontage([&](Asset::MotionMontageAsset& m) {
                if (drag.item.index < m.curves.size() && drag.item.key < m.curves[drag.item.index].keys.size()) {
                    auto& keys = m.curves[drag.item.index].keys;
                    // Time and value both follow the mouse, on the scale the curve is drawn on.
                    const CurveRange range = CurveRangeOf(m.curves[drag.item.index]);
                    const prism::Rect row = RowRect(Row::Curves);
                    const float fraction = std::clamp((row.bottom() - 6.0F - p.y) / std::max(1.0F, row.h - 12.0F), 0.0F, 1.0F);
                    keys[drag.item.key].time = std::max(0.0, time);
                    keys[drag.item.key].value = range.lo + fraction * range.span;
                    std::sort(keys.begin(), keys.end(),
                              [](const Asset::MontageCurveKey& a, const Asset::MontageCurveKey& b) { return a.time < b.time; });
                }
            });
            m_window.selection = Selection{};
            break;
        case Kind::None:
            break;
    }
}

bool MontageTimeline::onMouseWheel(const prism::MouseEvent& e) {
    if ((e.mods & prism::ModShift) != 0U) {
        m_scroll = std::max(0.0, m_scroll - static_cast<double>(e.wheel.y) * 60.0 / m_pixelsPerSecond);
    } else {
        const double anchor = TimeAt(e.pos.x);
        m_pixelsPerSecond = std::clamp(m_pixelsPerSecond * (e.wheel.y > 0.0F ? 1.2F : 1.0F / 1.2F), 20.0F, 4000.0F);
        m_scroll = std::max(0.0, anchor - static_cast<double>(e.pos.x - m_rect.x - kHeader) / m_pixelsPerSecond);
    }
    invalidate();
    return true;
}

bool MontageTimeline::onKeyDown(const prism::KeyEvent& e) {
    Editor& editor = m_window.EditorRef();
    const bool ctrl = (e.mods & prism::ModCtrl) != 0U;
    if (ctrl && e.key == prism::Key::S) {
        static_cast<void>(editor.SaveMontageEditor());
        return true;
    }
    if (ctrl && (e.key == prism::Key::Y || (e.key == prism::Key::Z && (e.mods & prism::ModShift) != 0U))) {
        static_cast<void>(editor.RedoMontageEdit());
        return true;
    }
    if (ctrl && e.key == prism::Key::Z) {
        static_cast<void>(editor.UndoMontageEdit());
        return true;
    }
    if (e.key == prism::Key::Delete || e.key == prism::Key::Backspace) {
        DeleteSelection();
        return true;
    }
    if (e.key == prism::Key::Space) {
        const auto* session = editor.MontageEditSessionPtr();
        if (session != nullptr && session->playing) {
            editor.PauseMontagePreview();
        } else {
            editor.PlayMontagePreview();
        }
        return true;
    }
    return false;
}

void MontageTimeline::DeleteSelection() {
    const Selection picked = m_window.selection;
    Editor& editor = m_window.EditorRef();
    editor.EditMontage([&](Asset::MotionMontageAsset& m) {
        switch (picked.kind) {
            case Kind::Segment: static_cast<void>(Asset::RemoveMontageSegment(m, picked.index)); break;
            case Kind::Section:
                if (picked.index < m.sections.size()) {
                    const std::string name = m.sections[picked.index].name;
                    static_cast<void>(Asset::RemoveMontageSection(m, name));
                }
                break;
            case Kind::Notify:
                if (picked.index < m.notifies.size()) {
                    m.notifies.erase(m.notifies.begin() + static_cast<std::ptrdiff_t>(picked.index));
                }
                break;
            case Kind::State:
                if (picked.index < m.notifyStates.size()) {
                    m.notifyStates.erase(m.notifyStates.begin() + static_cast<std::ptrdiff_t>(picked.index));
                }
                break;
            case Kind::CurveKey:
                if (picked.index < m.curves.size() && picked.key < m.curves[picked.index].keys.size()) {
                    auto& keys = m.curves[picked.index].keys;
                    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(picked.key));
                    if (keys.empty()) {
                        m.curves.erase(m.curves.begin() + static_cast<std::ptrdiff_t>(picked.index));
                    }
                }
                break;
            case Kind::None: break;
        }
    });
    m_window.selection = Selection{};
    invalidate();
}

void MontageTimeline::ShowContextMenu(const Hit& hit, const prism::Vec2 at) {
    Editor& editor = m_window.EditorRef();
    const double time = TimeAt(at.x);
    std::vector<prism::MenuItem> items;
    const auto deleteItem = [this] { DeleteSelection(); };
    switch (hit.row) {
        case Row::Sections:
        case Row::Ruler:
            items.push_back(prism::MenuItem::Item("Add Section Here...", [this, time] {
                m_window.AskName("New Section", "Attack", [this, time](const std::string& name) {
                    m_window.EditorRef().EditMontage(
                        [&](Asset::MotionMontageAsset& m) { static_cast<void>(Asset::AddMontageSection(m, name, time)); });
                });
            }, "", prism::Icon::Plus));
            if (hit.item.kind == Kind::Section) {
                const auto* session = editor.MontageEditSessionPtr();
                const std::string name = session != nullptr && hit.item.index < session->montage.sections.size()
                                             ? session->montage.sections[hit.item.index].name
                                             : std::string();
                items.push_back(prism::MenuItem::Item("Rename...", [this, name] {
                    m_window.AskName("Rename Section", name, [this, name](const std::string& to) {
                        m_window.EditorRef().EditMontage(
                            [&](Asset::MotionMontageAsset& m) { static_cast<void>(Asset::RenameMontageSection(m, name, to)); });
                    });
                }));
                items.push_back(prism::MenuItem::Item("Preview From Here", [&editor, name] { editor.PlayMontagePreview(name); }, "",
                                                      prism::Icon::Play));
                items.push_back(prism::MenuItem::Sep());
                items.push_back(prism::MenuItem::Item("Delete Section", deleteItem, "Del", prism::Icon::Trash));
            }
            break;
        case Row::Slot: {
            std::size_t index = static_cast<std::size_t>(-1);
            if (hit.item.kind == Kind::Segment) {
                index = hit.item.index + 1U;
            }
            std::vector<prism::MenuItem> clips;
            for (const std::filesystem::path& clip : m_window.ProjectClips()) {
                clips.push_back(prism::MenuItem::Item(clip.stem().string(), [this, clip, index] { m_window.AddClip(clip, index); }));
            }
            if (clips.empty()) {
                clips.push_back(prism::MenuItem::Item("No clips in this project yet", {}).disabled());
            }
            items.push_back(prism::MenuItem::Sub("Add Clip", std::move(clips), prism::Icon::Plus));
            if (hit.item.kind == Kind::Segment) {
                items.push_back(prism::MenuItem::Sep());
                items.push_back(prism::MenuItem::Item("Remove Clip", deleteItem, "Del", prism::Icon::Trash));
            }
            break;
        }
        case Row::Notify: {
            const std::uint32_t track = static_cast<std::uint32_t>(hit.track);
            items.push_back(prism::MenuItem::Item("Add Notify Here...", [this, time, track] {
                m_window.AskName("New Notify", "hit", [this, time, track](const std::string& name) {
                    m_window.EditorRef().EditMontage(
                        [&](Asset::MotionMontageAsset& m) { static_cast<void>(Asset::AddMontageNotify(m, name, time, track)); });
                });
            }, "", prism::Icon::Plus));
            items.push_back(prism::MenuItem::Item("Add Notify State Here...", [this, time, track] {
                m_window.AskName("New Notify State", "hit_window", [this, time, track](const std::string& name) {
                    m_window.EditorRef().EditMontage([&](Asset::MotionMontageAsset& m) {
                        static_cast<void>(Asset::AddMontageNotifyState(m, name, time, time + 0.3, track));
                    });
                });
            }));
            items.push_back(prism::MenuItem::Item("Add Notify Track", [&editor] {
                editor.EditMontage([](Asset::MotionMontageAsset& m) { m.notifyTracks = std::min(64U, m.notifyTracks + 1U); });
            }));
            if (hit.item.kind == Kind::Notify || hit.item.kind == Kind::State) {
                const bool isNotify = hit.item.kind == Kind::Notify;
                const std::size_t index = hit.item.index;
                items.push_back(prism::MenuItem::Item("Rename...", [this, isNotify, index] {
                    const auto* session = m_window.EditorRef().MontageEditSessionPtr();
                    if (session == nullptr) {
                        return;
                    }
                    const std::string current = isNotify ? (index < session->montage.notifies.size() ? session->montage.notifies[index].name : "")
                                                         : (index < session->montage.notifyStates.size() ? session->montage.notifyStates[index].name : "");
                    m_window.AskName("Rename", current, [this, isNotify, index](const std::string& to) {
                        m_window.EditorRef().EditMontage([&](Asset::MotionMontageAsset& m) {
                            if (isNotify && index < m.notifies.size()) {
                                m.notifies[index].name = to;
                            } else if (!isNotify && index < m.notifyStates.size()) {
                                m.notifyStates[index].name = to;
                            }
                        });
                    });
                }));
                items.push_back(prism::MenuItem::Sep());
                items.push_back(prism::MenuItem::Item("Delete", deleteItem, "Del", prism::Icon::Trash));
            }
            break;
        }
        case Row::Curves: {
            // The key goes on the curve being edited, at the height clicked (on its own scale).
            const prism::Rect row = RowRect(Row::Curves);
            const float fraction = std::clamp((row.bottom() - 6.0F - at.y) / std::max(1.0F, row.h - 12.0F), 0.0F, 1.0F);
            const auto* session = editor.MontageEditSessionPtr();
            const bool haveCurve = session != nullptr && m_window.activeCurve < session->montage.curves.size();
            items.push_back(prism::MenuItem::Item(haveCurve ? "Add Key Here" : "Add Curve and Key Here", [this, time, fraction] {
                m_window.EditorRef().EditMontage([&](Asset::MotionMontageAsset& m) {
                    if (m_window.activeCurve >= m.curves.size()) {
                        m.curves.push_back(Asset::MontageCurve{m_window.UniqueCurveName(m, "Curve"), {}});
                        m_window.activeCurve = m.curves.size() - 1U;
                    }
                    Asset::MontageCurve& curve = m.curves[m_window.activeCurve];
                    const CurveRange range = CurveRangeOf(curve);
                    Asset::SetMontageCurveKey(m, curve.name, time, range.lo + fraction * range.span);
                });
            }, "", prism::Icon::Plus));
            if (hit.item.kind == Kind::CurveKey) {
                items.push_back(prism::MenuItem::Item("Delete Key", deleteItem, "Del", prism::Icon::Trash));
            }
            break;
        }
        case Row::None:
            break;
    }
    if (!items.empty()) {
        prism::showContextMenu(std::move(items), at);
    }
}

std::optional<std::uint32_t> MontageTimeline::TrackAt(const float y) const noexcept {
    for (std::size_t track = 0U; track < NotifyTracks(); ++track) {
        const prism::Rect row = RowRect(Row::Notify, track);
        if (y >= row.y && y < row.bottom()) {
            return static_cast<std::uint32_t>(track);
        }
    }
    return std::nullopt;
}

void MontageTimeline::ShowHeaderMenu(const Hit& hit, const prism::Vec2 at) {
    Editor& editor = m_window.EditorRef();
    const auto* session = editor.MontageEditSessionPtr();
    if (session == nullptr) {
        return;
    }
    std::vector<prism::MenuItem> items;
    if (hit.row == Row::Notify) {
        const auto track = static_cast<std::uint32_t>(hit.track);
        items.push_back(prism::MenuItem::Item("Add Notify Track", [&editor] {
            editor.EditMontage([](Asset::MotionMontageAsset& m) { m.notifyTracks = std::min(64U, m.notifyTracks + 1U); });
        }, "", prism::Icon::Plus));
        items.push_back(prism::MenuItem::Item("Rename Track...", [this, track] {
            const auto* s = m_window.EditorRef().MontageEditSessionPtr();
            const std::string current = s != nullptr ? Asset::MontageNotifyTrackName(s->montage, track) : std::string();
            m_window.AskName("Rename Track", current, [this, track](const std::string& name) {
                m_window.EditorRef().EditMontage([&](Asset::MotionMontageAsset& m) { Asset::SetMontageNotifyTrackName(m, track, name); });
            });
        }));
        prism::MenuItem remove = prism::MenuItem::Item("Remove Track", [&editor, track] {
            editor.EditMontage([track](Asset::MotionMontageAsset& m) { static_cast<void>(Asset::RemoveMontageNotifyTrack(m, track)); });
        }, "", prism::Icon::Trash);
        remove.enabled = NotifyTracks() > 1U;
        items.push_back(prism::MenuItem::Sep());
        items.push_back(std::move(remove));
    } else if (hit.row == Row::Curves) {
        for (std::size_t ci = 0U; ci < session->montage.curves.size(); ++ci) {
            items.push_back(prism::MenuItem::Check(session->montage.curves[ci].name, ci == m_window.activeCurve,
                                                   [this, ci] { m_window.activeCurve = ci; invalidate(); }));
        }
        if (!items.empty()) {
            items.push_back(prism::MenuItem::Sep());
        }
        items.push_back(prism::MenuItem::Item("Add Curve", [this] { m_window.AddAtPlayhead(EditorMontageEditor::AddKind::Curve); }, "",
                                              prism::Icon::Plus));
    } else if (hit.row == Row::Sections) {
        items.push_back(prism::MenuItem::Item("Add Section at Playhead", [this] { m_window.AddAtPlayhead(EditorMontageEditor::AddKind::Section); },
                                              "", prism::Icon::Plus));
    }
    if (!items.empty()) {
        prism::showContextMenu(std::move(items), at);
    }
}

bool MontageTimeline::onDragEnter(const prism::DragDropEvent& e) {
    return onDragOver(e);
}

bool MontageTimeline::onDragOver(const prism::DragDropEvent& e) {
    const std::string* path = e.operation && e.operation->tag() == EditorAssetTable::kDragTag
                                  ? e.operation->getPayload<std::string>()
                                  : nullptr;
    const bool accepts = path != nullptr && std::filesystem::path{*path}.extension() == ".uvanim";
    if (accepts != m_dropHot) {
        m_dropHot = accepts;
        invalidate();
    }
    return accepts;
}

void MontageTimeline::onDragLeave() {
    m_dropHot = false;
    invalidate();
}

bool MontageTimeline::onDrop(const prism::DragDropEvent& e) {
    m_dropHot = false;
    invalidate();
    if (!onDragOver(e)) {
        return false;
    }
    m_dropHot = false;
    const std::string* path = e.operation->getPayload<std::string>();
    // Dropped on a clip: after it; elsewhere: at the end.
    const Hit hit = HitTest(e.pos);
    const std::size_t index = hit.item.kind == Kind::Segment ? hit.item.index + 1U : static_cast<std::size_t>(-1);
    m_window.AddClip(m_window.ContentRoot() / std::filesystem::path{*path}, index);
    return true;
}

} // namespace UVE::Editor
