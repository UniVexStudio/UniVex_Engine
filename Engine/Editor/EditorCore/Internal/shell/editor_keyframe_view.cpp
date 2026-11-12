// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_keyframe_view.h"

#include "editor.h"
#include "editor_content_browser.h"
#include "editor_sequencer_panel.h"
#include "editor_timeline_tracks.h"

#include "animation_clip_editing.h"
#include "i_project_file_index.h"
#include "skeleton_3d.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/DragDrop.h"
#include "prism/Font.h"
#include "prism/Menu.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr float kTrackListUVE = 230.0F;
constexpr float kRulerUVE = 26.0F;
constexpr float kRowUVE = 22.0F;
constexpr float kKeyMarginUVE = 10.0F;   ///< right margin of the key area

[[nodiscard]] std::string FrameTextUVE(const double seconds, const double rate) {
    return std::to_string(std::lround(seconds * rate));
}

[[nodiscard]] prism::Color WithAlphaUVE(const prism::Color c, const int alpha) {
    return prism::Color(c.r, c.g, c.b, static_cast<std::uint8_t>(std::clamp(alpha, 0, 255)));
}

/// X lighter, Y the channel's colour, Z darker: three lines of one channel read as one family.
[[nodiscard]] prism::Color ShadeUVE(const prism::Color c, const int axis) {
    const float factor = axis == 0 ? 1.3F : axis == 1 ? 1.0F : 0.68F;
    const auto part = [factor](const std::uint8_t v) {
        return static_cast<std::uint8_t>(std::clamp(static_cast<float>(v) * factor, 0.0F, 255.0F));
    };
    return prism::Color(part(c.r), part(c.g), part(c.b));
}

} // namespace

// ------------------------------------------------------------------------------------- geometry

std::vector<KeyframeViewUVE::RowUVE> KeyframeViewUVE::RowsUVE() const {
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ViewUVE();
    std::vector<RowUVE> rows;
    if (view.clip == nullptr) {
        return rows;
    }
    EditorUVE& editor = m_panel.EditorRefUVE();
    const Scene::IEntityManagerUVE& em = editor.m_services->GetEntityManagerUVE();
    TimelineSkeletonUVE skeleton;
    if (view.skeleton != Scene::kInvalidEntityUVE && em.IsAliveUVE(view.skeleton) &&
        em.HasComponentUVE<Scene::Skeleton3DComponentUVE>(view.skeleton)) {
        const auto& bones = em.GetComponentUVE<Scene::Skeleton3DComponentUVE>(view.skeleton).bones;
        skeleton.present = true;
        std::vector<int> depth(bones.size(), 0);
        for (std::size_t i = 0U; i < bones.size(); ++i) {
            if (bones[i].parentIndex >= 0 && static_cast<std::size_t>(bones[i].parentIndex) < i) {
                depth[i] = depth[static_cast<std::size_t>(bones[i].parentIndex)] + 1;
            }
            skeleton.bones.push_back(TimelineSkeletonUVE::BoneUVE{bones[i].name, depth[i]});
        }
    }
    const Asset::AnimationClipAssetUVE& clip = *view.clip;
    for (const TimelineRowUVE& spec :
         BuildTimelineRowsUVE(clip, skeleton, editor.m_timeline.filter, editor.m_timeline.expandedTracks)) {
        RowUVE row;
        row.label = spec.label;
        row.depth = spec.depth;
        row.channel = spec.channel;
        row.track = spec.track;
        row.events = spec.kind == TimelineRowKindUVE::Events;
        switch (spec.kind) {
            case TimelineRowKindUVE::Transform:
            case TimelineRowKindUVE::TransformChannel: row.samples = &clip.samples; break;
            case TimelineRowKindUVE::Bone:
            case TimelineRowKindUVE::BoneChannel: row.samples = &clip.bones[spec.trackIndex].samples; break;
            case TimelineRowKindUVE::Morph: row.morph = &clip.morphWeights[spec.trackIndex]; break;
            case TimelineRowKindUVE::Events: break;
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

prism::Rect KeyframeViewUVE::KeysRectUVE() const noexcept {
    return prism::Rect{m_rect.x + kTrackListUVE, m_rect.y + kRulerUVE, std::max(40.0F, m_rect.w - kTrackListUVE - kKeyMarginUVE),
                       std::max(0.0F, m_rect.h - kRulerUVE)};
}

TimelineScaleUVE KeyframeViewUVE::ScaleUVE() const {
    const EditorUVE::AnimationTimelineStateUVE& state = m_panel.EditorRefUVE().m_timeline;
    const double duration = m_panel.ViewUVE().durationSeconds;
    const prism::Rect keys = KeysRectUVE();
    const double fit = static_cast<double>(keys.w) / std::max(1.0e-3, duration);
    TimelineScaleUVE scale;
    scale.left = keys.x;
    scale.pixelsPerSecond = state.pixelsPerSecond > 0.0F ? static_cast<double>(state.pixelsPerSecond) : fit;
    const double visible = static_cast<double>(keys.w) / scale.pixelsPerSecond;
    scale.scrollSeconds = state.pixelsPerSecond > 0.0F ? std::clamp(state.scrollSeconds, 0.0, std::max(0.0, duration - visible)) : 0.0;
    return scale;
}

double KeyframeViewUVE::SnapUVE(const double seconds) const {
    const double rate = m_panel.ViewUVE().frameRate;
    return std::round(seconds * rate) / rate;
}

float KeyframeViewUVE::RowTopUVE(const int row) const noexcept {
    return m_rect.y + kRulerUVE + static_cast<float>(row) * kRowUVE - m_scrollY;
}

int KeyframeViewUVE::RowAtUVE(const float y) const noexcept {
    if (y < m_rect.y + kRulerUVE) {
        return -1;
    }
    return static_cast<int>(std::floor((y - m_rect.y - kRulerUVE + m_scrollY) / kRowUVE));
}

bool KeyframeViewUVE::IsSelectedUVE(const std::string& track, const double seconds) const {
    const auto& keys = m_panel.EditorRefUVE().m_timeline.selectedKeys;
    return std::any_of(keys.begin(), keys.end(), [&](const ClipKeyUVE& key) {
        return key.track == track && std::abs(key.timeSeconds - seconds) < kClipKeyTimeToleranceUVE;
    });
}

double KeyframeViewUVE::DragDeltaUVE() const {
    if (m_drag != DragUVE::Keys && m_drag != DragUVE::Event) {
        return 0.0;
    }
    return SnapUVE(static_cast<double>(m_mouse.x - m_dragFrom.x) / ScaleUVE().pixelsPerSecond);
}

void KeyframeViewUVE::ClampScrollUVE(const std::size_t rowCount) {
    const float content = static_cast<float>(rowCount) * kRowUVE;
    const float room = std::max(0.0F, m_rect.h - kRulerUVE);
    m_scrollY = std::clamp(m_scrollY, 0.0F, std::max(0.0F, content - room));
}

void KeyframeViewUVE::FitUVE() {
    EditorUVE& editor = m_panel.EditorRefUVE();
    editor.m_timeline.pixelsPerSecond = 0.0F;
    editor.m_timeline.scrollSeconds = 0.0;
    invalidate();
}

// ---------------------------------------------------------------------------------------- paint

void KeyframeViewUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, t.panel);
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ResyncUVE();
    EditorUVE& editor = m_panel.EditorRefUVE();
    // Nothing to show: a quiet icon in the middle; what to do is its tooltip.
    const auto emptyIcon = [&](const prism::Icon icon, const prism::Rect area, std::string tip) {
        prism::drawIcon(c, icon, prism::Rect{area.x + area.w * 0.5F - 12.0F, area.y + area.h * 0.5F - 12.0F, 24.0F, 24.0F}, t.textDisabled);
        setTooltip(std::move(tip));
    };
    if (view.player == Scene::kInvalidEntityUVE) {
        emptyIcon(prism::Icon::AnimSequencer, m_rect, "Add an AnimationSequencer to the entity to animate it");
        return;
    }
    if (view.clip == nullptr) {
        const bool broken = !editor.m_timeline.loadError.empty();
        emptyIcon(broken ? prism::Icon::Warning : prism::Icon::AnimClip, m_rect,
                  broken ? editor.m_timeline.loadError : "New Animation or Add from Project in the animation menu, or drop a .uvanim here");
        if (m_dropHot) {
            c.strokeRoundRect(m_rect, 4.0F, t.accent, 2);
        }
        return;
    }
    setTooltip({});
    const std::vector<RowUVE> rows = RowsUVE();
    ClampScrollUVE(rows.size());

    // The track list's column and its header.
    const prism::Rect list{m_rect.x, m_rect.y, kTrackListUVE, m_rect.h};
    c.fillRect(list, t.window);
    c.fillRect(prism::Rect{list.right() - 1.0F, m_rect.y, 1.0F, m_rect.h}, t.border);
    PaintRowsUVE(c, rows);
    const prism::Rect corner{m_rect.x, m_rect.y, kTrackListUVE, kRulerUVE};
    DrawTimelineRowHeaderUVE(c, corner,
                             std::to_string(view.clip->bones.size()) + " tracks  -  " + FrameTextUVE(view.durationSeconds, view.frameRate) +
                                 " frames",
                             t.textDim);

    // Ruler, then the curves over the key area when that view is on.
    const TimelineScaleUVE scale = ScaleUVE();
    const prism::Rect ruler{m_rect.x + kTrackListUVE, m_rect.y, m_rect.w - kTrackListUVE, kRulerUVE};
    DrawTimelineRulerUVE(c, ruler, scale, view.frameRate, view.durationSeconds, m_rect.bottom(), true);
    if (editor.m_timeline.curves) {
        PaintCurvesUVE(c);
    }
    const prism::Rect area{ruler.x, m_rect.y, ruler.w, m_rect.h};
    DrawTimelineOutOfRangeUVE(c, prism::Rect{area.x, area.y + kRulerUVE, area.w, area.h - kRulerUVE}, scale, view.durationSeconds);

    // A selection box being drawn.
    if (m_drag == DragUVE::Box) {
        const prism::Rect box{std::min(m_dragFrom.x, m_mouse.x), std::min(m_dragFrom.y, m_mouse.y), std::abs(m_mouse.x - m_dragFrom.x),
                              std::abs(m_mouse.y - m_dragFrom.y)};
        c.fillRect(box, WithAlphaUVE(t.accent, 40));
        c.strokeRect(box, WithAlphaUVE(t.accent, 200));
    }
    DrawTimelinePlayheadUVE(c, area, kRulerUVE, scale, editor.m_timeline.timeSeconds,
                            FrameTextUVE(editor.m_timeline.timeSeconds, view.frameRate));
    if (m_dropHot) {
        c.strokeRoundRect(m_rect, 4.0F, t.accent, 2);
    }
    if (focused()) {
        c.fillRect(prism::Rect{m_rect.x, m_rect.y, m_rect.w, 1.0F}, WithAlphaUVE(t.accent, 160));
    }
}

void KeyframeViewUVE::PaintRowsUVE(prism::Canvas& c, const std::vector<RowUVE>& rows) {
    const prism::Theme& t = prism::theme();
    EditorUVE& editor = m_panel.EditorRefUVE();
    const EditorUVE::AnimationTimelineStateUVE& state = editor.m_timeline;
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ViewUVE();
    const Asset::AnimationClipAssetUVE& clip = *view.clip;
    const TimelineScaleUVE scale = ScaleUVE();
    const prism::Rect keys = KeysRectUVE();
    const prism::Rect body{m_rect.x, keys.y, m_rect.w, keys.h};
    const double dragDelta = DragDeltaUVE();
    c.pushClip(body);
    const int first = std::max(0, RowAtUVE(keys.y));
    const int last = std::min(static_cast<int>(rows.size()) - 1, RowAtUVE(keys.bottom()));
    for (int index = first; index <= last; ++index) {
        const RowUVE& row = rows[static_cast<std::size_t>(index)];
        const float y = RowTopUVE(index);
        const prism::Rect line{m_rect.x, y, m_rect.w, kRowUVE};
        const bool selected = !row.events && row.morph == nullptr && row.track == editor.m_selectedSkeletonBone &&
                              row.channel == state.curveChannel;
        c.fillRect(line, selected ? t.rowSelected : (index % 2 == 0 ? t.panel : t.window));
        // The list side: the arrow (tracks), a swatch (channels and blend shapes), the name.
        const prism::Rect name{m_rect.x, y, kTrackListUVE - 1.0F, kRowUVE};
        c.fillRect(name, selected ? t.rowSelected : t.window);
        const float indent = 8.0F + static_cast<float>(std::min(row.depth, 20)) * 11.0F;
        if (row.events) {
            prism::drawIcon(c, prism::Icon::AnimEvent, prism::Rect{name.x + indent, y + 3.0F, 16.0F, 16.0F}, TimelineEventColorUVE());
            c.textIn(*t.ui, prism::Rect{name.x + indent + 20.0F, y, name.w - indent - 22.0F, kRowUVE}, row.label, TimelineEventColorUVE());
        } else if (row.morph != nullptr) {
            c.fillRoundRect(prism::Rect{name.x + indent + 3.0F, y + 7.0F, 8.0F, 8.0F}, 2.0F, TimelineMorphColorUVE());
            c.textIn(*t.ui, prism::Rect{name.x + indent + 16.0F, y, name.w - indent - 18.0F, kRowUVE}, row.label, t.text);
        } else if (row.channel < 0) {
            const bool open = std::ranges::find(state.expandedTracks, row.track) != state.expandedTracks.end();
            prism::drawIcon(c, open ? prism::Icon::TriDown : prism::Icon::TriRight, prism::Rect{name.x + indent - 2.0F, y + 5.0F, 12.0F, 12.0F},
                            t.textDim);
            prism::drawIcon(c, row.track.empty() ? prism::Icon::TransformTrack : prism::Icon::BoneTrack,
                            prism::Rect{name.x + indent + 11.0F, y + 4.0F, 14.0F, 14.0F}, selected ? t.textBright : t.textDim);
            c.textIn(*t.ui, prism::Rect{name.x + indent + 29.0F, y, name.w - indent - 31.0F, kRowUVE}, row.label,
                     selected ? t.textBright : t.text);
        } else {
            c.fillRoundRect(prism::Rect{name.x + indent + 13.0F, y + 7.0F, 8.0F, 8.0F}, 2.0F, TimelineChannelColorUVE(row.channel));
            c.textIn(*t.small, prism::Rect{name.x + indent + 26.0F, y, name.w - indent - 28.0F, kRowUVE}, row.label, t.textDim);
        }
        if (state.curves) {
            continue;   // the curves view draws over the key area
        }

        // The key side.
        c.pushClip(prism::Rect{keys.x - 8.0F, y, keys.w + 16.0F, kRowUVE});
        const float mid = y + kRowUVE * 0.5F;
        if (row.events) {
            for (std::size_t e = 0U; e < clip.events.size(); ++e) {
                const bool chosen = static_cast<int>(e) == state.selectedEvent;
                double at = clip.events[e].timeSeconds;
                if (chosen && m_drag == DragUVE::Event) {
                    at = std::clamp(at + dragDelta, 0.0, view.durationSeconds);
                }
                const float x = scale.XUVE(at);
                const prism::Vec2 flag[] = {{x, mid - 7.0F}, {x + 6.0F, mid + 5.0F}, {x - 6.0F, mid + 5.0F}};
                c.fillPolygon(flag, chosen ? prism::Color(255, 255, 255) : TimelineEventColorUVE());
                c.textIn(*t.small, prism::Rect{x + 8.0F, y, 160.0F, kRowUVE}, clip.events[e].eventId,
                         chosen ? t.textBright : TimelineEventColorUVE());
            }
        } else if (row.samples != nullptr && !row.samples->empty()) {
            const auto& samples = *row.samples;
            const double spacing = samples.size() > 1U
                                       ? (samples.back().timeSeconds - samples.front().timeSeconds) / static_cast<double>(samples.size() - 1U)
                                       : view.durationSeconds;
            const bool stacked = row.channel < 0;
            const bool dense = spacing * scale.pixelsPerSecond < (stacked ? 18.0 : 9.0);
            for (int channel = stacked ? 0 : row.channel; channel <= (stacked ? 2 : row.channel); ++channel) {
                const float laneH = stacked ? (kRowUVE - 4.0F) / 3.0F : kRowUVE - 6.0F;
                const float laneTop = stacked ? y + 2.0F + laneH * static_cast<float>(channel) : y + 3.0F;
                const float laneMid = laneTop + laneH * 0.5F;
                const float half = std::max(2.5F, laneH * 0.5F - (stacked ? 0.3F : 1.5F));
                const prism::Color color = TimelineChannelColorUVE(channel);
                const auto moves = [&](const std::size_t i) { return ShowsTimelineKeyUVE(samples, i, channel); };
                if (dense) {
                    std::size_t i = 0U;
                    while (i < samples.size()) {
                        if (!moves(i)) {
                            ++i;
                            continue;
                        }
                        std::size_t j = i;
                        while (j + 1U < samples.size() && moves(j + 1U)) {
                            ++j;
                        }
                        const float x0 = scale.XUVE(samples[i].timeSeconds);
                        const float x1 = std::max(scale.XUVE(samples[j].timeSeconds), x0 + 2.0F);
                        c.fillRoundRect(prism::Rect{x0, laneMid - half * 0.6F, x1 - x0, half * 1.2F}, 1.5F,
                                        WithAlphaUVE(color, stacked ? 170 : 130));
                        i = j + 1U;
                    }
                } else {
                    for (std::size_t i = 0U; i < samples.size(); ++i) {
                        const float x = scale.XUVE(samples[i].timeSeconds);
                        if (x < keys.x - 8.0F || x > keys.right() + 8.0F || !moves(i)) {
                            continue;
                        }
                        DrawTimelineKeyUVE(c, prism::Vec2{x, laneMid}, half, color, false);
                    }
                }
            }
            // The selected keys, where a drag would put them.
            for (std::size_t i = 0U; i < samples.size(); ++i) {
                if (!IsSelectedUVE(row.track, samples[i].timeSeconds) || !ShowsTimelineKeyUVE(samples, i, row.channel)) {
                    continue;
                }
                const double shown = std::clamp(samples[i].timeSeconds + dragDelta, 0.0, view.durationSeconds);
                DrawTimelineKeyUVE(c, prism::Vec2{scale.XUVE(shown), mid}, stacked ? 5.0F : 5.5F,
                                   row.channel < 0 ? t.textBright : TimelineChannelColorUVE(row.channel), true);
            }
        } else if (row.morph != nullptr && !row.morph->samples.empty()) {
            const auto& samples = row.morph->samples;
            const double spacing = samples.size() > 1U
                                       ? (samples.back().timeSeconds - samples.front().timeSeconds) / static_cast<double>(samples.size() - 1U)
                                       : view.durationSeconds;
            const bool dense = spacing * scale.pixelsPerSecond < 9.0;
            const float half = kRowUVE * 0.5F - 4.5F;
            const auto shows = [&](const std::size_t i) { return ShowsMorphKeyUVE(*row.morph, i); };
            if (dense) {
                std::size_t i = 0U;
                while (i < samples.size()) {
                    if (!shows(i)) {
                        ++i;
                        continue;
                    }
                    std::size_t j = i;
                    while (j + 1U < samples.size() && shows(j + 1U)) {
                        ++j;
                    }
                    const float x0 = scale.XUVE(samples[i].timeSeconds);
                    const float x1 = std::max(scale.XUVE(samples[j].timeSeconds), x0 + 2.0F);
                    c.fillRoundRect(prism::Rect{x0, mid - half * 0.6F, x1 - x0, half * 1.2F}, 1.5F, WithAlphaUVE(TimelineMorphColorUVE(), 130));
                    i = j + 1U;
                }
            } else {
                for (std::size_t i = 0U; i < samples.size(); ++i) {
                    if (shows(i)) {
                        DrawTimelineKeyUVE(c, prism::Vec2{scale.XUVE(samples[i].timeSeconds), mid}, half, TimelineMorphColorUVE(), false);
                    }
                }
            }
        }
        c.popClip();
    }
    c.popClip();
}

// --------------------------------------------------------------------------------------- curves

float KeyframeViewUVE::CurvePlotUVE::YUVE(const float value, const int channel) const noexcept {
    return plot.bottom() - (value - low[channel]) / (high[channel] - low[channel]) * plot.h;
}

float KeyframeViewUVE::CurvePlotUVE::ValueUVE(const float y, const int channel) const noexcept {
    return low[channel] + (plot.bottom() - y) / std::max(1.0F, plot.h) * (high[channel] - low[channel]);
}

KeyframeViewUVE::CurvePlotUVE KeyframeViewUVE::BuildCurvePlotUVE() const {
    CurvePlotUVE plot;
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ViewUVE();
    EditorUVE& editor = m_panel.EditorRefUVE();
    const prism::Rect keys = KeysRectUVE();
    plot.plot = prism::Rect{keys.x, keys.y + 24.0F, keys.w, std::max(10.0F, keys.h - 34.0F)};
    if (view.clip == nullptr) {
        return plot;
    }
    plot.track = editor.m_selectedSkeletonBone;
    plot.samples = FindClipTrackSamplesUVE(*view.clip, plot.track);
    if (plot.samples == nullptr && !view.clip->samples.empty()) {
        plot.track.clear();
        plot.samples = &view.clip->samples;
    }
    if (plot.samples == nullptr || plot.samples->empty()) {
        plot.samples = nullptr;
        return plot;
    }
    const auto& samples = *plot.samples;
    // The channel picked on the left, else every channel that moves.
    std::vector<int> channels;
    for (int channel = 0; channel < 3; ++channel) {
        bool moves = false;
        for (std::size_t i = 1U; i < samples.size() && !moves; ++i) {
            moves = TimelineChannelChangesUVE(samples[i - 1U].pose, samples[i].pose, channel);
        }
        if (editor.m_timeline.curveChannel == channel || (editor.m_timeline.curveChannel < 0 && moves)) {
            channels.push_back(channel);
        }
    }
    if (channels.empty()) {
        channels.push_back(editor.m_timeline.curveChannel >= 0 ? editor.m_timeline.curveChannel : 0);
    }
    for (int channel = 0; channel < 3; ++channel) {
        plot.low[channel] = std::numeric_limits<float>::max();
        plot.high[channel] = std::numeric_limits<float>::lowest();
    }
    for (const int channel : channels) {
        for (int axis = 0; axis < 3; ++axis) {
            CurveLineUVE line{channel, axis, {}, {}};
            float offset = 0.0F;
            for (std::size_t i = 0U; i < samples.size(); ++i) {
                const float raw = GetClipPoseComponentUVE(samples[i].pose, channel, axis);
                // Rotation angles are unwrapped, so a turn past 180 degrees stays one line.
                if (channel == 1 && i > 0U) {
                    while (raw + offset - line.values.back() > 180.0F) {
                        offset -= 360.0F;
                    }
                    while (raw + offset - line.values.back() < -180.0F) {
                        offset += 360.0F;
                    }
                }
                line.values.push_back(raw + offset);
                line.unwrap.push_back(offset);
                plot.low[channel] = std::min(plot.low[channel], raw + offset);
                plot.high[channel] = std::max(plot.high[channel], raw + offset);
            }
            plot.lines.push_back(std::move(line));
        }
    }
    for (const int channel : channels) {
        if (plot.high[channel] - plot.low[channel] < 1.0e-3F) {
            plot.low[channel] -= 1.0F;
            plot.high[channel] += 1.0F;
        }
        const float pad = (plot.high[channel] - plot.low[channel]) * 0.08F;
        plot.low[channel] -= pad;
        plot.high[channel] += pad;
    }
    // A drag keeps the range it started with, so the graph does not rescale under the mouse.
    if (m_drag == DragUVE::Curve) {
        plot.low[editor.m_timeline.curveComponent / 3] = editor.m_timeline.curveRangeMin;
        plot.high[editor.m_timeline.curveComponent / 3] = editor.m_timeline.curveRangeMax;
    }
    plot.normalized = channels.size() > 1U;
    return plot;
}

void KeyframeViewUVE::PaintCurvesUVE(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    EditorUVE& editor = m_panel.EditorRefUVE();
    const prism::Rect keys = KeysRectUVE();
    c.pushClip(prism::Rect{keys.x - 2.0F, keys.y, keys.w + 12.0F, keys.h});
    c.fillRect(keys, t.logBg);
    const CurvePlotUVE plot = BuildCurvePlotUVE();
    if (plot.samples == nullptr) {
        prism::drawIcon(c, prism::Icon::BoneTrack, prism::Rect{keys.x + keys.w * 0.5F - 12.0F, keys.y + keys.h * 0.5F - 12.0F, 24.0F, 24.0F}, t.textDisabled);
        c.popClip();
        return;
    }
    const TimelineScaleUVE scale = ScaleUVE();
    const auto& samples = *plot.samples;
    // Value grid: labelled for a single channel, plain guides when channels are scaled to fit.
    if (plot.normalized) {
        for (int i = 0; i <= 4; ++i) {
            c.fillRect(prism::Rect{keys.x, plot.plot.y + plot.plot.h * static_cast<float>(i) / 4.0F, keys.w, 1.0F},
                       prism::Color(255, 255, 255, 14));
        }
    } else {
        const int only = plot.lines.front().channel;
        const float rawStep = (plot.high[only] - plot.low[only]) / 4.0F;
        const float magnitude = std::pow(10.0F, std::floor(std::log10(std::max(rawStep, 1.0e-6F))));
        float step = magnitude;
        for (const float factor : {1.0F, 2.0F, 5.0F, 10.0F}) {
            if (magnitude * factor >= rawStep) {
                step = magnitude * factor;
                break;
            }
        }
        for (float value = std::ceil(plot.low[only] / step) * step; value <= plot.high[only]; value += step) {
            const float y = plot.YUVE(value, only);
            c.fillRect(prism::Rect{keys.x, y, keys.w, 1.0F},
                       std::abs(value) < step * 0.01F ? prism::Color(255, 255, 255, 40) : prism::Color(255, 255, 255, 14));
            char label[32];
            std::snprintf(label, sizeof(label), step >= 1.0F ? "%.0f" : (step >= 0.1F ? "%.1f" : "%.2f"), static_cast<double>(value));
            c.textIn(*t.small, prism::Rect{keys.x + 4.0F, y - 14.0F, 60.0F, 13.0F}, label, t.textDim);
        }
    }
    const bool showKeys = samples.size() < 2U || (samples[1].timeSeconds - samples[0].timeSeconds) * scale.pixelsPerSecond >= 6.0;
    std::vector<prism::Vec2> points;
    for (const CurveLineUVE& line : plot.lines) {
        const prism::Color color = ShadeUVE(TimelineChannelColorUVE(line.channel), line.axis);
        points.clear();
        for (std::size_t i = 0U; i < samples.size(); ++i) {
            points.push_back(prism::Vec2{scale.XUVE(samples[i].timeSeconds), plot.YUVE(line.values[i], line.channel)});
        }
        c.polyline(points, color, 1.6F);
        if (showKeys) {
            for (const prism::Vec2 point : points) {
                c.fillCircle(point, 2.6F, color);
            }
        }
    }
    // Legend: which channels and axes, and whose curves.
    float x = keys.x + 8.0F;
    std::vector<int> shown;
    for (const CurveLineUVE& line : plot.lines) {
        if (std::ranges::find(shown, line.channel) == shown.end()) {
            shown.push_back(line.channel);
        }
    }
    for (const int channel : shown) {
        const std::string name = TimelineChannelNameUVE(channel);
        c.textIn(*t.small, prism::Rect{x, keys.y + 4.0F, 80.0F, 16.0F}, name, TimelineChannelColorUVE(channel));
        x += t.small->width(name) + 6.0F;
        for (int axis = 0; axis < 3; ++axis) {
            const char* axisName = axis == 0 ? "X" : axis == 1 ? "Y" : "Z";
            c.textIn(*t.small, prism::Rect{x, keys.y + 4.0F, 12.0F, 16.0F}, axisName, ShadeUVE(TimelineChannelColorUVE(channel), axis));
            x += 11.0F;
        }
        x += 14.0F;
    }
    c.textIn(*t.small, prism::Rect{x, keys.y + 4.0F, keys.right() - x - 8.0F, 16.0F},
             plot.track.empty() ? std::string{"Transform"} : plot.track,
             t.textDim);
    // The point being dragged, with its value.
    if (m_drag == DragUVE::Curve) {
        const int channel = editor.m_timeline.curveComponent / 3;
        const int axis = editor.m_timeline.curveComponent % 3;
        const float value = plot.ValueUVE(std::clamp(m_mouse.y, plot.plot.y, plot.plot.bottom()), channel);
        const prism::Vec2 at{scale.XUVE(editor.m_timeline.curveKeySeconds), plot.YUVE(value, channel)};
        c.strokeCircle(at, 5.5F, prism::Color(255, 255, 255), 1.5F);
        char label[64];
        std::snprintf(label, sizeof(label), "%s %s  %.3f", TimelineChannelNameUVE(channel), axis == 0 ? "X" : axis == 1 ? "Y" : "Z",
                      static_cast<double>(value));
        c.textIn(*t.small, prism::Rect{at.x + 9.0F, at.y - 16.0F, 180.0F, 14.0F}, label, t.textBright);
    }
    c.popClip();
}

// ---------------------------------------------------------------------------------------- input

bool KeyframeViewUVE::onMouseDown(const prism::MouseEvent& e) {
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(this);
    }
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ResyncUVE();
    EditorUVE& editor = m_panel.EditorRefUVE();
    EditorUVE::AnimationTimelineStateUVE& state = editor.m_timeline;
    if (view.clip == nullptr) {
        return true;
    }
    m_mouse = e.pos;
    m_dragFrom = e.pos;
    const std::vector<RowUVE> rows = RowsUVE();
    const int rowIndex = RowAtUVE(e.pos.y);
    const RowUVE* const row = rowIndex >= 0 && rowIndex < static_cast<int>(rows.size()) ? &rows[static_cast<std::size_t>(rowIndex)] : nullptr;
    const TimelineScaleUVE scale = ScaleUVE();
    const bool inList = e.pos.x < m_rect.x + kTrackListUVE;
    const bool inRuler = e.pos.y < m_rect.y + kRulerUVE;

    if (e.button == prism::MouseButton::Right) {
        if (!inList && !inRuler) {
            ShowMenuUVE(e.pos, row);
        }
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }

    // The track list: the arrow opens a track into its channels, the name picks the bone.
    if (inList) {
        if (row != nullptr && !row->events && row->morph == nullptr) {
            const float indent = 8.0F + static_cast<float>(std::min(row->depth, 20)) * 11.0F;
            if (row->channel < 0 && e.pos.x < m_rect.x + indent + 10.0F) {
                const auto open = std::ranges::find(state.expandedTracks, row->track);
                if (open != state.expandedTracks.end()) {
                    state.expandedTracks.erase(open);
                } else {
                    state.expandedTracks.push_back(row->track);
                }
            } else {
                editor.m_selectedSkeletonBone = row->track;
                state.curveChannel = row->channel;
            }
        }
        invalidate();
        return true;
    }
    // The ruler scrubs, snapped to frames.
    if (inRuler) {
        m_drag = DragUVE::Scrub;
        editor.SetAnimationTimelineTimeUVE(SnapUVE(scale.SecondsUVE(e.pos.x)));
        invalidate();
        return true;
    }
    // The curves view: grab the nearest point to drag its value.
    if (state.curves) {
        const CurvePlotUVE plot = BuildCurvePlotUVE();
        if (plot.samples != nullptr) {
            float best = 9.0F;
            for (const CurveLineUVE& line : plot.lines) {
                for (std::size_t i = 0U; i < plot.samples->size(); ++i) {
                    const float dx = scale.XUVE((*plot.samples)[i].timeSeconds) - e.pos.x;
                    const float dy = plot.YUVE(line.values[i], line.channel) - e.pos.y;
                    const float distance = std::sqrt(dx * dx + dy * dy);
                    if (distance < best) {
                        best = distance;
                        m_drag = DragUVE::Curve;
                        state.curveComponent = line.channel * 3 + line.axis;
                        state.curveKeySeconds = (*plot.samples)[i].timeSeconds;
                        state.curveUnwrap = line.unwrap[i];
                    }
                }
            }
            if (m_drag == DragUVE::Curve) {
                state.curveRangeMin = plot.low[state.curveComponent / 3];
                state.curveRangeMax = plot.high[state.curveComponent / 3];
                editor.SetAnimationTimelineTimeUVE(state.curveKeySeconds);
            }
        }
        invalidate();
        return true;
    }
    if (row == nullptr) {
        state.selectedKeys.clear();
        state.selectedEvent = -1;
        m_drag = DragUVE::Box;
        invalidate();
        return true;
    }
    // Events: click picks one (and drags it); double-click names it, or adds one on empty time.
    if (row->events) {
        int hit = -1;
        float best = 8.0F;
        for (std::size_t i = 0U; i < view.clip->events.size(); ++i) {
            const float distance = std::abs(scale.XUVE(view.clip->events[i].timeSeconds) - e.pos.x);
            if (distance < best) {
                best = distance;
                hit = static_cast<int>(i);
            }
        }
        if (e.clicks >= 2) {
            if (hit < 0) {
                AddEventUVE(scale.SecondsUVE(e.pos.x));
            } else {
                RenameEventUVE(hit);
            }
            invalidate();
            return true;
        }
        state.selectedEvent = hit;
        state.selectedKeys.clear();
        if (hit >= 0) {
            m_drag = DragUVE::Event;
        }
        invalidate();
        return true;
    }
    state.selectedEvent = -1;
    if (row->morph != nullptr) {
        return true;   // a blend-shape key is shown, not edited
    }
    // A key: select it (Shift adds, Ctrl toggles) and drag the selection; empty time: a box.
    const bool shift = (e.mods & prism::ModShift) != 0U;
    const bool ctrl = (e.mods & prism::ModCtrl) != 0U;
    std::optional<double> hit;
    float best = 7.0F;
    if (row->samples != nullptr) {
        for (std::size_t i = 0U; i < row->samples->size(); ++i) {
            const float distance = std::abs(scale.XUVE((*row->samples)[i].timeSeconds) - e.pos.x);
            if (distance < best && ShowsTimelineKeyUVE(*row->samples, i, row->channel)) {
                best = distance;
                hit = (*row->samples)[i].timeSeconds;
            }
        }
    }
    if (hit.has_value()) {
        const ClipKeyUVE key{row->track, *hit};
        const bool already = IsSelectedUVE(key.track, key.timeSeconds);
        if (ctrl && already) {
            std::erase(state.selectedKeys, key);
        } else {
            if (!shift && !ctrl && !already) {
                state.selectedKeys.clear();
            }
            if (!already) {
                state.selectedKeys.push_back(key);
            }
            m_drag = DragUVE::Keys;
        }
        editor.m_selectedSkeletonBone = row->track;
    } else {
        if (!shift && !ctrl) {
            state.selectedKeys.clear();
        }
        m_drag = DragUVE::Box;
    }
    invalidate();
    return true;
}

bool KeyframeViewUVE::onMouseMove(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    if (m_drag == DragUVE::Scrub) {
        m_panel.EditorRefUVE().SetAnimationTimelineTimeUVE(SnapUVE(ScaleUVE().SecondsUVE(e.pos.x)));
    }
    if (m_drag != DragUVE::None) {
        invalidate();
    }
    return true;
}

bool KeyframeViewUVE::onMouseUp(const prism::MouseEvent& e) {
    m_mouse = e.pos;
    if (m_drag != DragUVE::None && m_drag != DragUVE::Scrub) {
        m_panel.ResyncUVE();
        CommitDragUVE();
    }
    m_drag = DragUVE::None;
    invalidate();
    return true;
}

void KeyframeViewUVE::CommitDragUVE() {
    EditorUVE& editor = m_panel.EditorRefUVE();
    EditorUVE::AnimationTimelineStateUVE& state = editor.m_timeline;
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ViewUVE();
    if (view.clip == nullptr) {
        return;
    }
    const double rate = view.frameRate;
    switch (m_drag) {
        case DragUVE::Keys: {
            const double delta = DragDeltaUVE();
            if (std::abs(delta) > kClipKeyTimeToleranceUVE && !state.selectedKeys.empty()) {
                std::vector<ClipKeyUVE> landed;
                const std::vector<ClipKeyUVE> moving = state.selectedKeys;
                editor.EditAnimationTimelineClipUVE(
                    [&](Asset::AnimationClipAssetUVE& next) { landed = MoveClipKeysUVE(next, moving, delta, rate); });
                state.selectedKeys = std::move(landed);
            }
            break;
        }
        case DragUVE::Event: {
            const double delta = DragDeltaUVE();
            const int chosen = state.selectedEvent;
            if (chosen >= 0 && static_cast<std::size_t>(chosen) < view.clip->events.size() && std::abs(delta) > kClipKeyTimeToleranceUVE) {
                const double to = view.clip->events[static_cast<std::size_t>(chosen)].timeSeconds + delta;
                std::optional<std::size_t> landed;
                editor.EditAnimationTimelineClipUVE(
                    [&](Asset::AnimationClipAssetUVE& next) { landed = MoveClipEventUVE(next, static_cast<std::size_t>(chosen), to, rate); });
                state.selectedEvent = landed.has_value() ? static_cast<int>(*landed) : -1;
            }
            break;
        }
        case DragUVE::Box: {
            const std::vector<RowUVE> rows = RowsUVE();
            const TimelineScaleUVE scale = ScaleUVE();
            const float left = std::min(m_dragFrom.x, m_mouse.x);
            const float right = std::max(m_dragFrom.x, m_mouse.x);
            const float top = std::min(m_dragFrom.y, m_mouse.y);
            const float bottom = std::max(m_dragFrom.y, m_mouse.y);
            for (std::size_t index = 0U; index < rows.size(); ++index) {
                const RowUVE& row = rows[index];
                const float rowTop = RowTopUVE(static_cast<int>(index));
                if (row.events || row.samples == nullptr || rowTop + kRowUVE < top || rowTop > bottom) {
                    continue;
                }
                for (std::size_t i = 0U; i < row.samples->size(); ++i) {
                    const double s = (*row.samples)[i].timeSeconds;
                    const float x = scale.XUVE(s);
                    if (x >= left && x <= right && ShowsTimelineKeyUVE(*row.samples, i, row.channel) && !IsSelectedUVE(row.track, s)) {
                        state.selectedKeys.push_back(ClipKeyUVE{row.track, s});
                    }
                }
            }
            break;
        }
        case DragUVE::Curve: {
            const CurvePlotUVE plot = BuildCurvePlotUVE();
            const int channel = state.curveComponent / 3;
            const int axis = state.curveComponent % 3;
            const float value = plot.ValueUVE(std::clamp(m_mouse.y, plot.plot.y, plot.plot.bottom()), channel);
            const float stored = value - state.curveUnwrap;
            const double keySeconds = state.curveKeySeconds;
            const std::string track = plot.track;
            if (std::abs(m_mouse.y - m_dragFrom.y) > 1.0F) {
                editor.EditAnimationTimelineClipUVE([&](Asset::AnimationClipAssetUVE& next) {
                    static_cast<void>(SetClipKeyComponentUVE(next, track, keySeconds, channel, axis, stored));
                });
            }
            break;
        }
        case DragUVE::None:
        case DragUVE::Scrub: break;
    }
}

bool KeyframeViewUVE::onMouseWheel(const prism::MouseEvent& e) {
    EditorUVE& editor = m_panel.EditorRefUVE();
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ResyncUVE();
    if (view.clip == nullptr) {
        return false;
    }
    const TimelineScaleUVE scale = ScaleUVE();
    const prism::Rect keys = KeysRectUVE();
    if ((e.mods & prism::ModCtrl) != 0U) {
        // Zoom around the mouse.
        const double fit = static_cast<double>(keys.w) / std::max(1.0e-3, view.durationSeconds);
        const double anchor = scale.SecondsUVE(e.pos.x);
        const double next = std::clamp(scale.pixelsPerSecond * (e.wheel.y > 0.0F ? 1.25 : 0.8), fit, 4000.0);
        editor.m_timeline.pixelsPerSecond = next <= fit * 1.001 ? 0.0F : static_cast<float>(next);
        editor.m_timeline.scrollSeconds = anchor - static_cast<double>(e.pos.x - keys.x) / next;
    } else if ((e.mods & prism::ModShift) != 0U) {
        const double visible = static_cast<double>(keys.w) / scale.pixelsPerSecond;
        editor.m_timeline.scrollSeconds = scale.scrollSeconds - static_cast<double>(e.wheel.y) * visible * 0.1;
    } else {
        m_scrollY -= e.wheel.y * kRowUVE * 3.0F;
        ClampScrollUVE(RowsUVE().size());
    }
    invalidate();
    return true;
}

bool KeyframeViewUVE::onKeyDown(const prism::KeyEvent& e) {
    EditorUVE& editor = m_panel.EditorRefUVE();
    EditorUVE::AnimationTimelineStateUVE& state = editor.m_timeline;
    const EditorUVE::AnimationTimelineViewUVE& view = m_panel.ResyncUVE();
    if (view.clip == nullptr) {
        return false;
    }
    const bool ctrl = (e.mods & prism::ModCtrl) != 0U;
    const bool shift = (e.mods & prism::ModShift) != 0U;
    bool handled = true;
    switch (e.key) {
        case prism::Key::Space: editor.ToggleAnimationTimelinePlayUVE(); break;
        case prism::Key::Left: editor.StepAnimationTimelineFrameUVE(-1); break;
        case prism::Key::Right: editor.StepAnimationTimelineFrameUVE(1); break;
        case prism::Key::Home: editor.SetAnimationTimelineTimeUVE(0.0); break;
        case prism::Key::End: editor.SetAnimationTimelineTimeUVE(view.durationSeconds); break;
        case prism::Key::Escape:
            state.selectedKeys.clear();
            state.selectedEvent = -1;
            break;
        case prism::Key::Delete:
        case prism::Key::Backspace: DeleteSelectionUVE(); break;
        case prism::Key::K:
            if (!ctrl) {
                editor.InsertAnimationTimelineKeyUVE();
            } else {
                handled = false;
            }
            break;
        case prism::Key::C:
            if (ctrl) {
                CopyUVE();
            } else {
                handled = false;
            }
            break;
        case prism::Key::V:
            if (ctrl) {
                PasteUVE();
            } else {
                handled = false;
            }
            break;
        case prism::Key::Z:
            if (ctrl && shift) {
                editor.RedoAnimationTimelineUVE();
            } else if (ctrl) {
                editor.UndoAnimationTimelineUVE();
            } else {
                handled = false;
            }
            break;
        case prism::Key::Y:
            if (ctrl) {
                editor.RedoAnimationTimelineUVE();
            } else {
                handled = false;
            }
            break;
        case prism::Key::S:
            if (ctrl) {
                editor.SaveAnimationTimelineClipUVE();
            } else {
                handled = false;
            }
            break;
        case prism::Key::F: FitUVE(); break;
        default: handled = false; break;
    }
    if (handled) {
        invalidate();
    }
    return handled;
}

// -------------------------------------------------------------------------------------- editing

void KeyframeViewUVE::DeleteSelectionUVE() {
    EditorUVE& editor = m_panel.EditorRefUVE();
    EditorUVE::AnimationTimelineStateUVE& state = editor.m_timeline;
    if (state.selectedEvent >= 0) {
        const auto chosen = static_cast<std::size_t>(state.selectedEvent);
        editor.EditAnimationTimelineClipUVE(
            [&](Asset::AnimationClipAssetUVE& next) { static_cast<void>(RemoveClipEventUVE(next, chosen)); });
        state.selectedEvent = -1;
        state.status = "Deleted an event";
        return;
    }
    if (state.selectedKeys.empty()) {
        return;
    }
    std::size_t removed = 0U;
    const std::vector<ClipKeyUVE> keys = state.selectedKeys;
    editor.EditAnimationTimelineClipUVE([&](Asset::AnimationClipAssetUVE& next) { removed = DeleteClipKeysUVE(next, keys); });
    state.status = "Deleted " + std::to_string(removed) + " key" + (removed == 1U ? "" : "s") +
                   (removed < keys.size() ? " (a track keeps its last key)" : "");
    state.selectedKeys.clear();
}

void KeyframeViewUVE::CopyUVE() {
    EditorUVE::AnimationTimelineStateUVE& state = m_panel.EditorRefUVE().m_timeline;
    if (state.clip == nullptr || state.selectedKeys.empty()) {
        return;
    }
    state.clipboard = CopyClipKeysUVE(*state.clip, state.selectedKeys);
    state.status = "Copied " + std::to_string(state.clipboard.entries.size()) + " keys";
}

void KeyframeViewUVE::PasteUVE() {
    EditorUVE& editor = m_panel.EditorRefUVE();
    EditorUVE::AnimationTimelineStateUVE& state = editor.m_timeline;
    if (state.clipboard.IsEmptyUVE()) {
        return;
    }
    std::vector<ClipKeyUVE> pasted;
    const double at = state.timeSeconds;
    const double rate = m_panel.ViewUVE().frameRate;
    editor.EditAnimationTimelineClipUVE(
        [&](Asset::AnimationClipAssetUVE& next) { pasted = PasteClipKeysUVE(next, state.clipboard, at, rate); });
    state.selectedKeys = std::move(pasted);
}

void KeyframeViewUVE::AddEventUVE(const double seconds) {
    EditorUVE& editor = m_panel.EditorRefUVE();
    std::size_t added = 0U;
    const double rate = m_panel.ViewUVE().frameRate;
    const double at = std::clamp(seconds, 0.0, m_panel.ViewUVE().durationSeconds);
    if (editor.EditAnimationTimelineClipUVE([&](Asset::AnimationClipAssetUVE& next) { added = AddClipEventUVE(next, at, rate); })) {
        editor.m_timeline.selectedEvent = static_cast<int>(added);
        RenameEventUVE(static_cast<int>(added));
    }
}

void KeyframeViewUVE::RenameEventUVE(const int index) {
    EditorUVE& editor = m_panel.EditorRefUVE();
    const auto& clip = editor.m_timeline.clip;
    if (clip == nullptr || index < 0 || static_cast<std::size_t>(index) >= clip->events.size()) {
        return;
    }
    editor.m_timeline.selectedEvent = index;
    EditorSequencerPanelUVE::AskNameUVE(
        "Event Name", clip->events[static_cast<std::size_t>(index)].eventId,
        [&editor, index](const std::string& name) {
            editor.EditAnimationTimelineClipUVE([&](Asset::AnimationClipAssetUVE& next) {
                static_cast<void>(RenameClipEventUVE(next, static_cast<std::size_t>(index), name));
            });
        });
}

void KeyframeViewUVE::ShowMenuUVE(const prism::Vec2 at, const RowUVE* row) {
    EditorUVE& editor = m_panel.EditorRefUVE();
    const EditorUVE::AnimationTimelineStateUVE& state = editor.m_timeline;
    const double seconds = SnapUVE(ScaleUVE().SecondsUVE(at.x));
    std::vector<prism::MenuItem> items;
    if (row != nullptr && row->events) {
        items.push_back(prism::MenuItem::Item("Add Event Here...", [this, seconds] { AddEventUVE(seconds); }, "", prism::Icon::AnimEvent));
        if (state.selectedEvent >= 0) {
            const int chosen = state.selectedEvent;
            items.push_back(prism::MenuItem::Item("Rename Event...", [this, chosen] { RenameEventUVE(chosen); }));
            items.push_back(prism::MenuItem::Item("Delete Event", [this] { DeleteSelectionUVE(); }, "Del", prism::Icon::Trash));
        }
    } else {
        if (row != nullptr && row->morph == nullptr) {
            const std::string track = row->track;
            items.push_back(prism::MenuItem::Item(
                "Key " + (track.empty() ? std::string{"Transform"} : track) + " Here",
                [this, &editor, track, seconds] {
                    editor.m_selectedSkeletonBone = track;
                    editor.SetAnimationTimelineTimeUVE(seconds);
                    editor.InsertAnimationTimelineKeyUVE();
                    invalidate();
                },
                "K", prism::Icon::Key));
        }
        items.push_back(prism::MenuItem::Item("Copy Keys", [this] { CopyUVE(); }, "Ctrl+C", prism::Icon::Copy));
        items.back().enabled = !state.selectedKeys.empty();
        items.push_back(prism::MenuItem::Item("Paste at Playhead", [this] { PasteUVE(); invalidate(); }, "Ctrl+V", prism::Icon::Paste));
        items.back().enabled = !state.clipboard.IsEmptyUVE();
        items.push_back(prism::MenuItem::Item("Delete Keys", [this] { DeleteSelectionUVE(); invalidate(); }, "Del", prism::Icon::Trash));
        items.back().enabled = !state.selectedKeys.empty();
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Add Event Here...", [this, seconds] { AddEventUVE(seconds); }, "", prism::Icon::AnimEvent));
    }
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Fit the Whole Clip", [this] { FitUVE(); }, "F", prism::Icon::Frame));
    prism::showContextMenu(std::move(items), at);
}

// ---------------------------------------------------------------------------------- drag & drop

bool KeyframeViewUVE::onDragEnter(const prism::DragDropEvent& e) {
    return onDragOver(e);
}

bool KeyframeViewUVE::onDragOver(const prism::DragDropEvent& e) {
    const std::string* path = e.operation && e.operation->tag() == EditorContentBrowserUVE::kDragTagUVE
                                  ? e.operation->getPayload<std::string>()
                                  : nullptr;
    const bool accepts = path != nullptr && std::filesystem::path{*path}.extension() == ".uvanim" &&
                         m_panel.ViewUVE().player != Scene::kInvalidEntityUVE;
    if (accepts != m_dropHot) {
        m_dropHot = accepts;
        invalidate();
    }
    return accepts;
}

void KeyframeViewUVE::onDragLeave() {
    m_dropHot = false;
    invalidate();
}

bool KeyframeViewUVE::onDrop(const prism::DragDropEvent& e) {
    const bool accepts = onDragOver(e);
    m_dropHot = false;
    invalidate();
    if (!accepts) {
        return false;
    }
    EditorUVE& editor = m_panel.EditorRefUVE();
    const std::filesystem::path root = editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    static_cast<void>(editor.AddClipToAnimationSequencerUVE(m_panel.ViewUVE().player, root / std::filesystem::path{*e.operation->getPayload<std::string>()}));
    return true;
}

} // namespace UVE::Editor
