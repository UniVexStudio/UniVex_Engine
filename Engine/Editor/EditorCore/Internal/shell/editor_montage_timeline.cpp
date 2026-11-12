// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_montage_timeline.h"

#include "editor.h"
#include "editor_content_browser.h"
#include "editor_timeline_draw.h"

#include "animation_montage_editing.h"

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

namespace UVE::Editor {
namespace {

constexpr float kHeaderUVE = 96.0F;
constexpr float kRulerUVE = 22.0F;
constexpr float kSectionsUVE = 24.0F;
constexpr float kSlotUVE = 34.0F;
constexpr float kNotifyUVE = 22.0F;
constexpr float kCurvesUVE = 48.0F;

using SelectionUVE = EditorMontageEditorUVE::SelectionUVE;
using KindUVE = SelectionUVE::KindUVE;

[[nodiscard]] prism::Color SectionColorUVE(const std::size_t index) {
    static const prism::Color kColors[] = {prism::Color(86, 156, 214), prism::Color(197, 134, 192), prism::Color(78, 201, 176),
                                           prism::Color(220, 170, 90), prism::Color(214, 112, 112), prism::Color(150, 190, 90)};
    return kColors[index % std::size(kColors)];
}

[[nodiscard]] std::string SecondsTextUVE(const double t) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", t);
    return text;
}

} // namespace

std::size_t MontageTimelineUVE::NotifyTracksUVE() const noexcept {
    const EditorUVE::MontageEditSessionUVE* const session = m_window.EditorRefUVE().MontageEditSessionPtrUVE();
    if (session == nullptr) {
        return 1U;
    }
    std::size_t tracks = std::max<std::size_t>(1U, session->montage.notifyTracks);
    for (const Asset::MontageNotifyUVE& n : session->montage.notifies) {
        tracks = std::max<std::size_t>(tracks, n.track + 1U);
    }
    for (const Asset::MontageNotifyStateUVE& s : session->montage.notifyStates) {
        tracks = std::max<std::size_t>(tracks, s.track + 1U);
    }
    return tracks;
}

prism::Vec2 MontageTimelineUVE::measure(const prism::Vec2 avail) {
    const float height = kRulerUVE + kSectionsUVE + kSlotUVE + kNotifyUVE * static_cast<float>(NotifyTracksUVE()) + kCurvesUVE;
    return m_desired = constrain({avail.x, height});
}

float MontageTimelineUVE::XOfUVE(const double time) const noexcept {
    return m_rect.x + kHeaderUVE + static_cast<float>((time - m_scroll) * m_pixelsPerSecond);
}

double MontageTimelineUVE::TimeAtUVE(const float x) const noexcept {
    return std::max(0.0, m_scroll + static_cast<double>(x - m_rect.x - kHeaderUVE) / m_pixelsPerSecond);
}

prism::Rect MontageTimelineUVE::RowRectUVE(const RowUVE row, const std::size_t track) const noexcept {
    float y = m_rect.y;
    const auto rect = [&](const float h) { return prism::Rect{m_rect.x, y, m_rect.w, h}; };
    if (row == RowUVE::Ruler) {
        return rect(kRulerUVE);
    }
    y += kRulerUVE;
    if (row == RowUVE::Sections) {
        return rect(kSectionsUVE);
    }
    y += kSectionsUVE;
    if (row == RowUVE::Slot) {
        return rect(kSlotUVE);
    }
    y += kSlotUVE;
    if (row == RowUVE::Notify) {
        y += kNotifyUVE * static_cast<float>(track);
        return rect(kNotifyUVE);
    }
    y += kNotifyUVE * static_cast<float>(NotifyTracksUVE());
    return rect(kCurvesUVE);
}

void MontageTimelineUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, t.panel);
    EditorUVE::MontageEditSessionUVE* const session = m_window.EditorRefUVE().MontageEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    const Asset::AnimationMontageAssetUVE& montage = session->montage;
    const double length = Asset::MontageLengthUVE(montage);
    const std::size_t tracks = NotifyTracksUVE();
    const SelectionUVE picked = m_window.selection;

    // Track headers.
    const auto header = [&](const prism::Rect row, const std::string& label, const bool alt) {
        c.fillRect(prism::Rect{row.x, row.y, row.w, row.h}, alt ? t.window : t.panel);
        c.fillRect(prism::Rect{row.x, row.y, kHeaderUVE, row.h}, t.header);
        c.textIn(*t.small, prism::Rect{row.x + 6.0F, row.y, kHeaderUVE - 8.0F, row.h}, label, t.textDim);
        c.fillRect(prism::Rect{row.x, row.bottom() - 1.0F, row.w, 1.0F}, t.border);
    };
    header(RowRectUVE(RowUVE::Ruler), "Time", false);
    header(RowRectUVE(RowUVE::Sections), "Sections", true);
    header(RowRectUVE(RowUVE::Slot), montage.slot, false);
    for (std::size_t i = 0U; i < tracks; ++i) {
        header(RowRectUVE(RowUVE::Notify, i), "Notify " + std::to_string(i + 1U), i % 2U == 0U);
    }
    header(RowRectUVE(RowUVE::Curves), "Curves", false);

    const prism::Rect content{m_rect.x + kHeaderUVE, m_rect.y, m_rect.w - kHeaderUVE, m_rect.h};
    c.pushClip(content);
    // The ruler, its grid and the shade past the end: the look every editor timeline shares.
    const TimelineScaleUVE scale{m_rect.x + kHeaderUVE, m_scroll, static_cast<double>(m_pixelsPerSecond)};
    const prism::Rect ruler = RowRectUVE(RowUVE::Ruler);
    DrawTimelineRulerUVE(c, prism::Rect{content.x, ruler.y, content.w, ruler.h}, scale, 30.0, length, m_rect.bottom(), false);
    DrawTimelineOutOfRangeUVE(c, prism::Rect{content.x, ruler.bottom(), content.w, m_rect.bottom() - ruler.bottom()}, scale, length);

    // Sections: a marker and a label each; the line runs down through the tracks.
    const prism::Rect sectionsRow = RowRectUVE(RowUVE::Sections);
    for (std::size_t i = 0U; i < montage.sections.size(); ++i) {
        const Asset::MontageSectionUVE& section = montage.sections[i];
        const float x = XOfUVE(section.time);
        const prism::Color color = SectionColorUVE(i);
        const bool selected = picked.kind == KindUVE::Section && picked.index == i;
        c.fillRect(prism::Rect{x, sectionsRow.y, selected ? 3.0F : 2.0F, m_rect.bottom() - sectionsRow.y},
                   prism::Color(color.r, color.g, color.b, selected ? 220 : 110));
        const std::string label = section.name + (section.next.empty() ? "" : section.next == section.name ? "  (loop)" : "  > " + section.next);
        const float w = std::min(220.0F, t.small->width(label) + 12.0F);
        const prism::Rect tag{x, sectionsRow.y + 3.0F, w, sectionsRow.h - 6.0F};
        c.fillRoundRect(tag, 3.0F, selected ? color : prism::Color(color.r, color.g, color.b, 170));
        c.textIn(*t.small, prism::Rect{tag.x + 5.0F, tag.y, tag.w - 6.0F, tag.h}, label, t.textBright);
    }

    // The slot track: the clips end to end.
    const prism::Rect slotRow = RowRectUVE(RowUVE::Slot);
    for (std::size_t i = 0U; i < montage.segments.size(); ++i) {
        const double start = Asset::MontageSegmentStartUVE(montage, i);
        const double duration = Asset::MontageSegmentDurationUVE(montage.segments[i]);
        const prism::Rect block{XOfUVE(start) + 1.0F, slotRow.y + 4.0F, std::max(4.0F, static_cast<float>(duration * m_pixelsPerSecond) - 2.0F),
                                slotRow.h - 8.0F};
        const bool selected = picked.kind == KindUVE::Segment && picked.index == i;
        c.fillRoundRect(block, 3.0F, selected ? prism::Color(64, 150, 140) : prism::Color(48, 110, 104));
        if (selected) {
            c.strokeRoundRect(block, 3.0F, t.accent, 1);
        }
        std::string label = m_window.ClipNameUVE(montage.segments[i].clip);
        if (montage.segments[i].loops > 1U) {
            label += "  x" + std::to_string(montage.segments[i].loops);
        }
        c.textIn(*t.small, prism::Rect{block.x + 6.0F, block.y, block.w - 8.0F, block.h}, label, t.textBright);
    }
    if (montage.segments.empty()) {
        c.textIn(*t.small, prism::Rect{content.x + 8.0F, slotRow.y, content.w - 16.0F, slotRow.h},
                 "Drop a clip here from the Content Browser or the Asset Browser, or right-click > Add Clip", t.textDim);
    }

    // Notifies (diamonds) and notify states (bars), each on its track.
    for (std::size_t i = 0U; i < montage.notifyStates.size(); ++i) {
        const Asset::MontageNotifyStateUVE& state = montage.notifyStates[i];
        const prism::Rect row = RowRectUVE(RowUVE::Notify, state.track);
        const float x0 = XOfUVE(state.begin);
        const float x1 = std::max(x0 + 4.0F, XOfUVE(state.end));
        const bool selected = picked.kind == KindUVE::State && picked.index == i;
        const prism::Rect bar{x0, row.y + 4.0F, x1 - x0, row.h - 8.0F};
        c.fillRoundRect(bar, 3.0F, selected ? prism::Color(200, 120, 60) : prism::Color(150, 90, 50));
        c.textIn(*t.small, prism::Rect{bar.x + 5.0F, bar.y, bar.w - 6.0F, bar.h}, state.name, t.textBright);
    }
    for (std::size_t i = 0U; i < montage.notifies.size(); ++i) {
        const Asset::MontageNotifyUVE& notify = montage.notifies[i];
        const prism::Rect row = RowRectUVE(RowUVE::Notify, notify.track);
        const float x = XOfUVE(notify.time);
        const float cy = row.y + row.h * 0.5F;
        const bool selected = picked.kind == KindUVE::Notify && picked.index == i;
        const prism::Vec2 diamond[] = {{x, cy - 6.0F}, {x + 6.0F, cy}, {x, cy + 6.0F}, {x - 6.0F, cy}};
        c.fillPolygon(diamond, selected ? prism::Color(255, 210, 90) : prism::Color(230, 180, 60));
        c.textIn(*t.small, prism::Rect{x + 9.0F, row.y, 160.0F, row.h}, notify.name, t.text);
    }

    // Curves: each a line over the row, its keys as dots.
    const prism::Rect curvesRow = RowRectUVE(RowUVE::Curves);
    for (std::size_t ci = 0U; ci < montage.curves.size(); ++ci) {
        const Asset::MontageCurveUVE& curve = montage.curves[ci];
        if (curve.keys.empty()) {
            continue;
        }
        float lo = curve.keys.front().value;
        float hi = lo;
        for (const Asset::MontageCurveKeyUVE& key : curve.keys) {
            lo = std::min(lo, key.value);
            hi = std::max(hi, key.value);
        }
        const float span = hi - lo > 1e-6F ? hi - lo : 1.0F;
        const auto yOf = [&](const float v) { return curvesRow.bottom() - 6.0F - (v - lo) / span * (curvesRow.h - 12.0F); };
        const prism::Color color = SectionColorUVE(ci + 2U);
        for (std::size_t k = 1U; k < curve.keys.size(); ++k) {
            c.line({XOfUVE(curve.keys[k - 1U].time), yOf(curve.keys[k - 1U].value)}, {XOfUVE(curve.keys[k].time), yOf(curve.keys[k].value)},
                   color, 1.5F);
        }
        for (std::size_t k = 0U; k < curve.keys.size(); ++k) {
            const bool selected = picked.kind == KindUVE::CurveKey && picked.index == ci && picked.key == k;
            c.fillCircle({XOfUVE(curve.keys[k].time), yOf(curve.keys[k].value)}, selected ? 4.5F : 3.0F,
                         selected ? t.textBright : color);
        }
        c.textIn(*t.small, prism::Rect{XOfUVE(curve.keys.front().time) + 6.0F, curvesRow.y + 2.0F, 120.0F, 14.0F}, curve.name, color);
    }

    // What is being dragged, where it would land.
    if (m_drag.has_value()) {
        c.fillRect(prism::Rect{m_mouse.x, m_rect.y + kRulerUVE, 1.0F, m_rect.h - kRulerUVE}, t.accent);
    }
    c.popClip();
    // The playhead, with its time on the ruler.
    DrawTimelinePlayheadUVE(c, content, kRulerUVE, scale, session->playhead, SecondsTextUVE(session->playhead));
    if (m_dropHot) {
        c.strokeRoundRect(slotRow, 3.0F, t.accent, 2);
    }
}

MontageTimelineUVE::HitUVE MontageTimelineUVE::HitTestUVE(const prism::Vec2 p) const {
    HitUVE hit;
    const EditorUVE::MontageEditSessionUVE* const session = m_window.EditorRefUVE().MontageEditSessionPtrUVE();
    if (session == nullptr || !m_rect.contains(p)) {
        return hit;
    }
    const Asset::AnimationMontageAssetUVE& montage = session->montage;
    if (RowRectUVE(RowUVE::Ruler).contains(p)) {
        hit.row = RowUVE::Ruler;
        return hit;
    }
    if (RowRectUVE(RowUVE::Sections).contains(p)) {
        hit.row = RowUVE::Sections;
        for (std::size_t i = montage.sections.size(); i-- > 0U;) {
            const float x = XOfUVE(montage.sections[i].time);
            if (p.x >= x - 3.0F && p.x <= x + 90.0F) {
                hit.item = SelectionUVE{KindUVE::Section, i, 0U};
                break;
            }
        }
        return hit;
    }
    if (RowRectUVE(RowUVE::Slot).contains(p)) {
        hit.row = RowUVE::Slot;
        const double time = TimeAtUVE(p.x);
        for (std::size_t i = 0U; i < montage.segments.size(); ++i) {
            const double start = Asset::MontageSegmentStartUVE(montage, i);
            if (time >= start && time < start + Asset::MontageSegmentDurationUVE(montage.segments[i])) {
                hit.item = SelectionUVE{KindUVE::Segment, i, 0U};
                break;
            }
        }
        return hit;
    }
    for (std::size_t track = 0U; track < NotifyTracksUVE(); ++track) {
        if (!RowRectUVE(RowUVE::Notify, track).contains(p)) {
            continue;
        }
        hit.row = RowUVE::Notify;
        hit.track = track;
        for (std::size_t i = 0U; i < montage.notifies.size(); ++i) {
            if (montage.notifies[i].track == track && std::abs(XOfUVE(montage.notifies[i].time) - p.x) <= 7.0F) {
                hit.item = SelectionUVE{KindUVE::Notify, i, 0U};
                return hit;
            }
        }
        for (std::size_t i = 0U; i < montage.notifyStates.size(); ++i) {
            const Asset::MontageNotifyStateUVE& state = montage.notifyStates[i];
            const float x0 = XOfUVE(state.begin);
            const float x1 = std::max(x0 + 4.0F, XOfUVE(state.end));
            if (state.track == track && p.x >= x0 - 4.0F && p.x <= x1 + 4.0F) {
                hit.item = SelectionUVE{KindUVE::State, i, 0U};
                hit.edge = p.x <= x0 + 5.0F ? -1 : p.x >= x1 - 5.0F ? 1 : 0;
                return hit;
            }
        }
        return hit;
    }
    if (RowRectUVE(RowUVE::Curves).contains(p)) {
        hit.row = RowUVE::Curves;
        const prism::Rect row = RowRectUVE(RowUVE::Curves);
        for (std::size_t ci = 0U; ci < montage.curves.size(); ++ci) {
            const Asset::MontageCurveUVE& curve = montage.curves[ci];
            if (curve.keys.empty()) {
                continue;
            }
            float lo = curve.keys.front().value;
            float hi = lo;
            for (const Asset::MontageCurveKeyUVE& key : curve.keys) {
                lo = std::min(lo, key.value);
                hi = std::max(hi, key.value);
            }
            const float span = hi - lo > 1e-6F ? hi - lo : 1.0F;
            for (std::size_t k = 0U; k < curve.keys.size(); ++k) {
                const float x = XOfUVE(curve.keys[k].time);
                const float y = row.bottom() - 6.0F - (curve.keys[k].value - lo) / span * (row.h - 12.0F);
                if (std::abs(x - p.x) <= 6.0F && std::abs(y - p.y) <= 6.0F) {
                    hit.item = SelectionUVE{KindUVE::CurveKey, ci, k};
                    return hit;
                }
            }
        }
    }
    return hit;
}

bool MontageTimelineUVE::onMouseDown(const prism::MouseEvent& e) {
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    EditorUVE& editor = m_window.EditorRefUVE();
    if (editor.MontageEditSessionPtrUVE() == nullptr || e.pos.x < m_rect.x + kHeaderUVE) {
        return true;
    }
    const HitUVE hit = HitTestUVE(e.pos);
    if (e.button == prism::MouseButton::Right) {
        if (hit.item.kind != KindUVE::None) {
            m_window.selection = hit.item;
        }
        ShowContextMenuUVE(hit, e.pos);
        invalidate();
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    m_mouse = e.pos;
    if (hit.item.kind != KindUVE::None) {
        m_window.selection = hit.item;
        m_drag = hit;
        m_dragFrom = e.pos;
    } else {
        if (hit.row != RowUVE::Ruler) {
            m_window.selection = SelectionUVE{};
        }
        m_scrubbing = true;
        editor.PauseMontagePreviewUVE();
        editor.SetMontagePlayheadUVE(TimeAtUVE(e.pos.x));
        editor.TickMontageEditorUVE(0.0);
    }
    invalidate();
    return true;
}

bool MontageTimelineUVE::onMouseMove(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    if (m_scrubbing) {
        m_window.EditorRefUVE().SetMontagePlayheadUVE(TimeAtUVE(e.pos.x));
        m_window.EditorRefUVE().TickMontageEditorUVE(0.0);
        invalidate();
    } else if (m_drag.has_value()) {
        invalidate();
    }
    return true;
}

bool MontageTimelineUVE::onMouseUp(const prism::MouseEvent& e) {
    if (m_drag.has_value() && std::abs(e.pos.x - m_dragFrom.x) > 3.0F) {
        CommitDragUVE(e.pos);
    }
    m_drag.reset();
    m_scrubbing = false;
    invalidate();
    return true;
}

void MontageTimelineUVE::CommitDragUVE(const prism::Vec2 p) {
    const HitUVE drag = *m_drag;
    EditorUVE& editor = m_window.EditorRefUVE();
    const double time = TimeAtUVE(p.x);
    const double moved = TimeAtUVE(p.x) - TimeAtUVE(m_dragFrom.x);
    switch (drag.item.kind) {
        case KindUVE::Section:
            editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
                if (drag.item.index < m.sections.size()) {
                    const std::string name = m.sections[drag.item.index].name;
                    static_cast<void>(Asset::MoveMontageSectionUVE(m, name, time));
                }
            });
            m_window.selection = SelectionUVE{};
            break;
        case KindUVE::Segment: {
            // Dropped over another clip: it takes that clip's place.
            const auto* session = editor.MontageEditSessionPtrUVE();
            std::size_t to = drag.item.index;
            for (std::size_t i = 0U; session != nullptr && i < session->montage.segments.size(); ++i) {
                const double start = Asset::MontageSegmentStartUVE(session->montage, i);
                if (time >= start && time < start + Asset::MontageSegmentDurationUVE(session->montage.segments[i])) {
                    to = i;
                }
            }
            editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
                static_cast<void>(Asset::MoveMontageSegmentUVE(m, drag.item.index, to));
            });
            m_window.selection = SelectionUVE{KindUVE::Segment, to, 0U};
            break;
        }
        case KindUVE::Notify:
            editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
                if (drag.item.index < m.notifies.size()) {
                    m.notifies[drag.item.index].time = std::clamp(time, 0.0, Asset::MontageLengthUVE(m));
                }
            });
            break;
        case KindUVE::State:
            editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
                if (drag.item.index >= m.notifyStates.size()) {
                    return;
                }
                Asset::MontageNotifyStateUVE& state = m.notifyStates[drag.item.index];
                const double length = Asset::MontageLengthUVE(m);
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
        case KindUVE::CurveKey:
            editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
                if (drag.item.index < m.curves.size() && drag.item.key < m.curves[drag.item.index].keys.size()) {
                    auto& keys = m.curves[drag.item.index].keys;
                    keys[drag.item.key].time = std::max(0.0, time);
                    std::sort(keys.begin(), keys.end(),
                              [](const Asset::MontageCurveKeyUVE& a, const Asset::MontageCurveKeyUVE& b) { return a.time < b.time; });
                }
            });
            m_window.selection = SelectionUVE{};
            break;
        case KindUVE::None:
            break;
    }
}

bool MontageTimelineUVE::onMouseWheel(const prism::MouseEvent& e) {
    if ((e.mods & prism::ModShift) != 0U) {
        m_scroll = std::max(0.0, m_scroll - static_cast<double>(e.wheel.y) * 60.0 / m_pixelsPerSecond);
    } else {
        const double anchor = TimeAtUVE(e.pos.x);
        m_pixelsPerSecond = std::clamp(m_pixelsPerSecond * (e.wheel.y > 0.0F ? 1.2F : 1.0F / 1.2F), 20.0F, 4000.0F);
        m_scroll = std::max(0.0, anchor - static_cast<double>(e.pos.x - m_rect.x - kHeaderUVE) / m_pixelsPerSecond);
    }
    invalidate();
    return true;
}

bool MontageTimelineUVE::onKeyDown(const prism::KeyEvent& e) {
    EditorUVE& editor = m_window.EditorRefUVE();
    if (e.key == prism::Key::Delete || e.key == prism::Key::Backspace) {
        DeleteSelectionUVE();
        return true;
    }
    if (e.key == prism::Key::Space) {
        const auto* session = editor.MontageEditSessionPtrUVE();
        if (session != nullptr && session->playing) {
            editor.PauseMontagePreviewUVE();
        } else {
            editor.PlayMontagePreviewUVE();
        }
        return true;
    }
    return false;
}

void MontageTimelineUVE::DeleteSelectionUVE() {
    const SelectionUVE picked = m_window.selection;
    EditorUVE& editor = m_window.EditorRefUVE();
    editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
        switch (picked.kind) {
            case KindUVE::Segment: static_cast<void>(Asset::RemoveMontageSegmentUVE(m, picked.index)); break;
            case KindUVE::Section:
                if (picked.index < m.sections.size()) {
                    const std::string name = m.sections[picked.index].name;
                    static_cast<void>(Asset::RemoveMontageSectionUVE(m, name));
                }
                break;
            case KindUVE::Notify:
                if (picked.index < m.notifies.size()) {
                    m.notifies.erase(m.notifies.begin() + static_cast<std::ptrdiff_t>(picked.index));
                }
                break;
            case KindUVE::State:
                if (picked.index < m.notifyStates.size()) {
                    m.notifyStates.erase(m.notifyStates.begin() + static_cast<std::ptrdiff_t>(picked.index));
                }
                break;
            case KindUVE::CurveKey:
                if (picked.index < m.curves.size() && picked.key < m.curves[picked.index].keys.size()) {
                    auto& keys = m.curves[picked.index].keys;
                    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(picked.key));
                    if (keys.empty()) {
                        m.curves.erase(m.curves.begin() + static_cast<std::ptrdiff_t>(picked.index));
                    }
                }
                break;
            case KindUVE::None: break;
        }
    });
    m_window.selection = SelectionUVE{};
    invalidate();
}

void MontageTimelineUVE::ShowContextMenuUVE(const HitUVE& hit, const prism::Vec2 at) {
    EditorUVE& editor = m_window.EditorRefUVE();
    const double time = TimeAtUVE(at.x);
    std::vector<prism::MenuItem> items;
    const auto deleteItem = [this] { DeleteSelectionUVE(); };
    switch (hit.row) {
        case RowUVE::Sections:
        case RowUVE::Ruler:
            items.push_back(prism::MenuItem::Item("Add Section Here...", [this, time] {
                m_window.AskNameUVE("New Section", "Attack", [this, time](const std::string& name) {
                    m_window.EditorRefUVE().EditMontageUVE(
                        [&](Asset::AnimationMontageAssetUVE& m) { static_cast<void>(Asset::AddMontageSectionUVE(m, name, time)); });
                });
            }, "", prism::Icon::Plus));
            if (hit.item.kind == KindUVE::Section) {
                const auto* session = editor.MontageEditSessionPtrUVE();
                const std::string name = session != nullptr && hit.item.index < session->montage.sections.size()
                                             ? session->montage.sections[hit.item.index].name
                                             : std::string();
                items.push_back(prism::MenuItem::Item("Rename...", [this, name] {
                    m_window.AskNameUVE("Rename Section", name, [this, name](const std::string& to) {
                        m_window.EditorRefUVE().EditMontageUVE(
                            [&](Asset::AnimationMontageAssetUVE& m) { static_cast<void>(Asset::RenameMontageSectionUVE(m, name, to)); });
                    });
                }));
                items.push_back(prism::MenuItem::Item("Preview From Here", [&editor, name] { editor.PlayMontagePreviewUVE(name); }, "",
                                                      prism::Icon::Play));
                items.push_back(prism::MenuItem::Sep());
                items.push_back(prism::MenuItem::Item("Delete Section", deleteItem, "Del", prism::Icon::Trash));
            }
            break;
        case RowUVE::Slot: {
            std::size_t index = static_cast<std::size_t>(-1);
            if (hit.item.kind == KindUVE::Segment) {
                index = hit.item.index + 1U;
            }
            std::vector<prism::MenuItem> clips;
            for (const std::filesystem::path& clip : m_window.ProjectClipsUVE()) {
                clips.push_back(prism::MenuItem::Item(clip.stem().string(), [this, clip, index] { m_window.AddClipUVE(clip, index); }));
            }
            if (clips.empty()) {
                clips.push_back(prism::MenuItem::Item("No clips in this project yet", {}).disabled());
            }
            items.push_back(prism::MenuItem::Sub("Add Clip", std::move(clips), prism::Icon::Plus));
            if (hit.item.kind == KindUVE::Segment) {
                items.push_back(prism::MenuItem::Sep());
                items.push_back(prism::MenuItem::Item("Remove Clip", deleteItem, "Del", prism::Icon::Trash));
            }
            break;
        }
        case RowUVE::Notify: {
            const std::uint32_t track = static_cast<std::uint32_t>(hit.track);
            items.push_back(prism::MenuItem::Item("Add Notify Here...", [this, time, track] {
                m_window.AskNameUVE("New Notify", "hit", [this, time, track](const std::string& name) {
                    m_window.EditorRefUVE().EditMontageUVE(
                        [&](Asset::AnimationMontageAssetUVE& m) { static_cast<void>(Asset::AddMontageNotifyUVE(m, name, time, track)); });
                });
            }, "", prism::Icon::Plus));
            items.push_back(prism::MenuItem::Item("Add Notify State Here...", [this, time, track] {
                m_window.AskNameUVE("New Notify State", "hit_window", [this, time, track](const std::string& name) {
                    m_window.EditorRefUVE().EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
                        static_cast<void>(Asset::AddMontageNotifyStateUVE(m, name, time, time + 0.3, track));
                    });
                });
            }));
            items.push_back(prism::MenuItem::Item("Add Notify Track", [&editor] {
                editor.EditMontageUVE([](Asset::AnimationMontageAssetUVE& m) { m.notifyTracks = std::min(16U, m.notifyTracks + 1U); });
            }));
            if (hit.item.kind == KindUVE::Notify || hit.item.kind == KindUVE::State) {
                const bool isNotify = hit.item.kind == KindUVE::Notify;
                const std::size_t index = hit.item.index;
                items.push_back(prism::MenuItem::Item("Rename...", [this, isNotify, index] {
                    const auto* session = m_window.EditorRefUVE().MontageEditSessionPtrUVE();
                    if (session == nullptr) {
                        return;
                    }
                    const std::string current = isNotify ? (index < session->montage.notifies.size() ? session->montage.notifies[index].name : "")
                                                         : (index < session->montage.notifyStates.size() ? session->montage.notifyStates[index].name : "");
                    m_window.AskNameUVE("Rename", current, [this, isNotify, index](const std::string& to) {
                        m_window.EditorRefUVE().EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
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
        case RowUVE::Curves: {
            const prism::Rect row = RowRectUVE(RowUVE::Curves);
            const float value = std::clamp((row.bottom() - at.y) / std::max(1.0F, row.h), 0.0F, 1.0F);
            items.push_back(prism::MenuItem::Item("Add Key Here...", [this, time, value] {
                const auto* session = m_window.EditorRefUVE().MontageEditSessionPtrUVE();
                const std::string current =
                    session != nullptr && !session->montage.curves.empty() ? session->montage.curves.front().name : std::string("Curve");
                m_window.AskNameUVE("Curve", current, [this, time, value](const std::string& name) {
                    m_window.EditorRefUVE().EditMontageUVE(
                        [&](Asset::AnimationMontageAssetUVE& m) { Asset::SetMontageCurveKeyUVE(m, name, time, value); });
                });
            }, "", prism::Icon::Plus));
            if (hit.item.kind == KindUVE::CurveKey) {
                items.push_back(prism::MenuItem::Item("Delete Key", deleteItem, "Del", prism::Icon::Trash));
            }
            break;
        }
        case RowUVE::None:
            break;
    }
    if (!items.empty()) {
        prism::showContextMenu(std::move(items), at);
    }
}

bool MontageTimelineUVE::onDragEnter(const prism::DragDropEvent& e) {
    return onDragOver(e);
}

bool MontageTimelineUVE::onDragOver(const prism::DragDropEvent& e) {
    const std::string* path = e.operation && e.operation->tag() == EditorContentBrowserUVE::kDragTagUVE
                                  ? e.operation->getPayload<std::string>()
                                  : nullptr;
    const bool accepts = path != nullptr && std::filesystem::path{*path}.extension() == ".uvanim";
    if (accepts != m_dropHot) {
        m_dropHot = accepts;
        invalidate();
    }
    return accepts;
}

void MontageTimelineUVE::onDragLeave() {
    m_dropHot = false;
    invalidate();
}

bool MontageTimelineUVE::onDrop(const prism::DragDropEvent& e) {
    m_dropHot = false;
    invalidate();
    if (!onDragOver(e)) {
        return false;
    }
    m_dropHot = false;
    const std::string* path = e.operation->getPayload<std::string>();
    // Dropped on a clip: after it; elsewhere: at the end.
    const HitUVE hit = HitTestUVE(e.pos);
    const std::size_t index = hit.item.kind == KindUVE::Segment ? hit.item.index + 1U : static_cast<std::size_t>(-1);
    m_window.AddClipUVE(m_window.ContentRootUVE() / std::filesystem::path{*path}, index);
    return true;
}

} // namespace UVE::Editor
