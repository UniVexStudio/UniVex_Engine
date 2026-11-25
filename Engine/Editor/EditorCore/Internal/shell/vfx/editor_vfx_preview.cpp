// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_vfx_preview.h"

#include "editor.h"
#include "editor_timeline_draw.h"

#include "prism/Canvas.h"
#include "prism/Theme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr float kTimelinePadX = 10.0F;
constexpr float kTimelineRulerHeight = 22.0F;
constexpr float kTimelineBodyHeight = 42.0F;

[[nodiscard]] std::string FormatSecondsShort(const double seconds) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2fs", seconds);
    return buf;
}

[[nodiscard]] std::string FormatTickLabel(const double seconds, const double step) {
    char buf[32];
    if (step >= 1.0) {
        std::snprintf(buf, sizeof(buf), "%.0fs", seconds);
    } else if (step >= 0.1) {
        std::snprintf(buf, sizeof(buf), "%.1fs", seconds);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2fs", seconds);
    }
    return buf;
}

[[nodiscard]] double PickMajorTickStep(const double pixelsPerSecond, const float minSpacingPx) noexcept {
    static constexpr std::array<double, 12> kSteps{
        0.05, 0.1, 0.2, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0};
    for (const double step : kSteps) {
        if (step * pixelsPerSecond >= static_cast<double>(minSpacingPx)) {
            return step;
        }
    }
    double step = 120.0;
    while (step * pixelsPerSecond < static_cast<double>(minSpacingPx) && step < 1.0e9) {
        step *= 2.0;
    }
    return step;
}

[[nodiscard]] float ReadBurstTimeInput(const Asset::ParticleModule& mod) noexcept {
    const auto it = mod.floats.find("Time");
    return it != mod.floats.end() ? it->second.constant : 0.0F;
}

} // namespace

std::string FormatVfxPreviewStatsText(const Editor& editor) {
    const Editor::VfxEditSession* const session =
        const_cast<Editor&>(editor).VfxEditSessionPtr();
    if (session == nullptr) {
        return {};
    }

    const VFX::VfxInstance* const instance =
        session->previewWorld.TryGetInstance(session->previewHandle);
    std::size_t totalAlive = 0U;
    std::string lines;
    const std::size_t emitterCount = session->system.emitters.size();
    for (std::size_t i = 0U; i < emitterCount; ++i) {
        const Asset::ParticleEmitterAsset& emitter = session->system.emitters[i];
        std::size_t alive = 0U;
        if (instance != nullptr && i < instance->GetEmitterCount()) {
            alive = instance->GetEmitterState(i).liveCount;
        }
        totalAlive += alive;
        const float cpuMs =
            i < session->lastEmitterCpuMs.size() ? session->lastEmitterCpuMs[i] : 0.0F;
        char row[160];
        std::snprintf(row, sizeof(row), "%s: %zu alive  (%.2f ms)",
                      emitter.name.empty() ? "Emitter" : emitter.name.c_str(),
                      alive,
                      static_cast<double>(cpuMs));
        if (!lines.empty()) {
            lines += "\n";
        }
        lines += row;
    }

    char summary[160];
    std::snprintf(summary, sizeof(summary), "Total: %zu particles  |  %zu drawn",
                  totalAlive, session->lastInstancesDrawn);
    if (!lines.empty()) {
        lines += "\n";
    }
    lines += summary;
    return lines;
}

VfxTimelineWidget::VfxTimelineWidget(Editor& editor) : m_editor(editor) {}

prism::Vec2 VfxTimelineWidget::measure(const prism::Vec2 avail) {
    return m_desired = constrain(prism::Vec2{avail.x, kTimelineRulerHeight + kTimelineBodyHeight});
}

double VfxTimelineWidget::EffectiveDuration() const noexcept {
    const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr) {
        return 1.0;
    }
    double maxTime = std::max(0.1, static_cast<double>(session->system.duration));
    maxTime = std::max(maxTime, static_cast<double>(session->system.warmUpSeconds));
    return maxTime;
}

float VfxTimelineWidget::TimeToScreenX(const double seconds) const noexcept {
    const float left = m_rect.x + kTimelinePadX;
    const float usableW = std::max(1.0F, m_rect.w - kTimelinePadX * 2.0F);
    const double duration = EffectiveDuration();
    const double normalized = std::clamp(seconds / duration, 0.0, 1.0);
    return left + static_cast<float>(normalized) * usableW;
}

double VfxTimelineWidget::ScreenXToTime(const float x) const noexcept {
    const float left = m_rect.x + kTimelinePadX;
    const float usableW = std::max(1.0F, m_rect.w - kTimelinePadX * 2.0F);
    const double normalized = std::clamp(static_cast<double>((x - left) / usableW), 0.0, 1.0);
    return normalized * EffectiveDuration();
}

void VfxTimelineWidget::ScrubAtScreenX(const float x) {
    const double t = ScreenXToTime(x);
    m_hoverValid = true;
    m_hoverTime = t;
    UpdateHoverTooltip(x);
    m_editor.SetVfxPlayhead(t);
    if (onScrub) {
        onScrub(t);
    }
    invalidate();
}

void VfxTimelineWidget::UpdateHoverTooltip(const float x) {
    const double t = ScreenXToTime(x);
    const double duration = EffectiveDuration();
    char buf[96];
    std::snprintf(buf, sizeof(buf), "Scrub: %.2f s / %.2f s (Click or drag)", t, duration);
    m_hoverTooltip = buf;
}

const std::string& VfxTimelineWidget::tooltipAt(const prism::Vec2) const {
    return m_hoverTooltip;
}

void VfxTimelineWidget::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, t.panel);

    const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr || m_rect.w < 4.0F || m_rect.h < 4.0F) {
        return;
    }

    const float left = m_rect.x + kTimelinePadX;
    const float usableW = std::max(1.0F, m_rect.w - kTimelinePadX * 2.0F);
    const double duration = EffectiveDuration();
    const double pixelsPerSecond = static_cast<double>(usableW) / duration;

    const prism::Rect rulerRect{m_rect.x, m_rect.y, m_rect.w, kTimelineRulerHeight};
    const prism::Rect trackRect{m_rect.x, m_rect.y + kTimelineRulerHeight, m_rect.w,
                                std::max(0.0F, m_rect.h - kTimelineRulerHeight)};

    c.pushClip(m_rect);

    // 1. Ruler background & track dark studio canvas.
    c.fillRect(rulerRect, t.header);
    c.fillRect(trackRect, t.field);
    c.hline(m_rect.x, m_rect.right(), rulerRect.bottom() - 1.0F, t.border);

    // 2. Adaptive major & minor ticks across [0, duration], plus subtle vertical grid lines.
    const double majorStep = PickMajorTickStep(pixelsPerSecond, 54.0F);
    const double minorStep = majorStep / 5.0;
    for (int idx = 0; ; ++idx) {
        const double s = static_cast<double>(idx) * minorStep;
        if (s > duration + 1.0e-6) {
            break;
        }
        const float x = std::round(TimeToScreenX(std::min(s, duration)));
        const bool isMajor = (idx % 5) == 0;
        const float tickTop = rulerRect.y + (isMajor ? 12.0F : 16.0F);
        c.fillRect(prism::Rect{x, tickTop, 1.0F, rulerRect.bottom() - 1.0F - tickTop},
                   isMajor ? t.textDim : t.borderStrong);
        if (isMajor) {
            c.fillRect(prism::Rect{x, trackRect.y, 1.0F, trackRect.h}, t.borderSoft);
            const std::string label = FormatTickLabel(s, majorStep);
            const float labelW = t.small->width(label) + 4.0F;
            const float labelX = std::clamp(x + 3.0F, m_rect.x + 3.0F, std::max(m_rect.x + 3.0F, m_rect.right() - labelW - 3.0F));
            c.textIn(*t.small, prism::Rect{labelX, rulerRect.y + 1.0F, labelW, 12.0F}, label, t.textDim,
                     prism::HAlign::Left, false);
        }
    }

    // 3. System duration end boundary (shaded out-of-range if warm-up exceeds duration).
    const double sysDuration = std::max(0.1, static_cast<double>(session->system.duration));
    const float durationEndX = std::round(TimeToScreenX(sysDuration));
    if (durationEndX < m_rect.right() - 1.0F) {
        c.fillRect(prism::Rect{durationEndX, trackRect.y, m_rect.right() - durationEndX, trackRect.h},
                   t.window.withAlpha(110));
        c.fillRect(prism::Rect{durationEndX, trackRect.y, 1.0F, trackRect.h}, t.borderStrong);
    }

    // 4. Warm-up shaded region [0, warmUpSeconds] when warmUpSeconds > 0.
    if (session->system.warmUpSeconds > 0.0F) {
        const float warmEndX = TimeToScreenX(static_cast<double>(session->system.warmUpSeconds));
        const float warmW = std::max(0.0F, warmEndX - left);
        if (warmW > 1.0F) {
            const prism::Rect warmRect{left, trackRect.y + 2.0F, warmW, trackRect.h - 4.0F};
            c.fillRoundRect(warmRect, 3.0F, t.warning.withAlpha(42));
            c.fillRect(prism::Rect{warmRect.x, warmRect.y, warmRect.w, 2.0F}, t.warning.withAlpha(200));
            c.strokeRoundRect(warmRect, 3.0F, t.warning.withAlpha(135), 1);
            if (warmW >= 46.0F) {
                c.textIn(*t.small, prism::Rect{warmRect.x + 5.0F, warmRect.y + 3.0F, warmW - 10.0F, 14.0F},
                         "Warm-Up", t.warning, prism::HAlign::Left, true);
            }
        }
    }

    // 5. Playback scrub rail & elapsed progress fill.
    const float railY = trackRect.bottom() - 13.0F;
    const float railH = 6.0F;
    const prism::Rect railRect{left, railY, usableW, railH};
    c.fillRoundRect(railRect, 3.0F, t.control);
    c.strokeRoundRect(railRect, 3.0F, t.border, 1);

    const float playheadX = std::round(TimeToScreenX(session->playhead));
    const float fillW = std::clamp(playheadX - left, 0.0F, usableW);
    if (fillW > 1.0F) {
        const prism::Color fillColor =
            session->playing ? t.info.withAlpha(220) : t.highlight.withAlpha(185);
        c.fillRoundRect(prism::Rect{left, railY, fillW, railH}, 3.0F, fillColor);
    }

    // 6. SpawnBurst keyframe markers lane with vertical stem to the rail.
    const float markerCenterY = trackRect.y + 13.0F;
    for (std::size_t ei = 0U; ei < session->system.emitters.size(); ++ei) {
        const Asset::ParticleEmitterAsset& emitter = session->system.emitters[ei];
        if (!emitter.enabled) {
            continue;
        }
        const bool isSelectedEmitter = (ei == session->selectedEmitter);
        for (const Asset::ParticleModule& mod : emitter.emitterUpdate) {
            if (!mod.enabled || mod.type != Asset::ParticleModuleType::SpawnBurst) {
                continue;
            }
            const double burstTime = std::clamp(static_cast<double>(ReadBurstTimeInput(mod)), 0.0, duration);
            const float bx = std::round(TimeToScreenX(burstTime));
            const prism::Color markerColor = isSelectedEmitter ? t.info : t.textDim;
            c.fillRect(prism::Rect{bx, markerCenterY + 5.0F, 1.0F, std::max(0.0F, railY - (markerCenterY + 5.0F))},
                       markerColor.withAlpha(isSelectedEmitter ? 130 : 85));
            DrawTimelineKey(c, prism::Vec2{bx, markerCenterY}, 5.0F, markerColor, isSelectedEmitter);
        }
    }

    // 7. Subtle hover scrub guide line (when hovering and not actively dragging).
    if (m_hoverValid && !m_scrubbing) {
        const float hx = std::round(TimeToScreenX(m_hoverTime));
        if (std::abs(hx - playheadX) > 2.0F) {
            c.fillRect(prism::Rect{hx, rulerRect.y, 1.0F, m_rect.h}, t.text.withAlpha(64));
        }
    }

    // 8. High-contrast playhead needle, rail knot, and clamped ruler time pill.
    const prism::Color headColor = TimelinePlayheadColor();
    c.fillRect(prism::Rect{playheadX, rulerRect.bottom() - 3.0F, 2.0F, trackRect.h + 3.0F}, headColor);
    c.fillCircle(prism::Vec2{playheadX + 0.5F, railY + railH * 0.5F}, 4.5F, t.textBright);
    c.fillCircle(prism::Vec2{playheadX + 0.5F, railY + railH * 0.5F}, 3.0F, headColor);

    const std::string timeLabel = FormatSecondsShort(session->playhead);
    const float tagW = std::max(34.0F, t.small->width(timeLabel) + 10.0F);
    const float tagX = std::clamp(playheadX - tagW * 0.5F, m_rect.x + 2.0F,
                                  std::max(m_rect.x + 2.0F, m_rect.right() - tagW - 2.0F));
    const prism::Rect tagRect{tagX, rulerRect.y + 2.0F, tagW, rulerRect.h - 7.0F};
    c.fillRoundRect(tagRect, 3.0F, headColor);
    const prism::Vec2 pointer[] = {
        {playheadX - 3.5F, tagRect.bottom()},
        {playheadX + 4.5F, tagRect.bottom()},
        {playheadX + 0.5F, tagRect.bottom() + 4.0F},
    };
    c.fillPolygon(pointer, headColor);
    c.textIn(*t.small, tagRect, timeLabel, t.textBright, prism::HAlign::Center, false);

    c.popClip();
    c.strokeRect(m_rect, t.border, 1);
}

bool VfxTimelineWidget::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left) {
        m_scrubbing = true;
        ScrubAtScreenX(e.pos.x);
        return true;
    }
    return false;
}

bool VfxTimelineWidget::onMouseMove(const prism::MouseEvent& e) {
    m_hoverValid = true;
    m_hoverTime = ScreenXToTime(e.pos.x);
    UpdateHoverTooltip(e.pos.x);
    if (m_scrubbing) {
        ScrubAtScreenX(e.pos.x);
        return true;
    }
    invalidate();
    return false;
}

bool VfxTimelineWidget::onMouseUp(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && m_scrubbing) {
        m_scrubbing = false;
        invalidate();
        return true;
    }
    return false;
}

void VfxTimelineWidget::onMouseLeave() {
    m_hoverValid = false;
    invalidate();
}

} // namespace UVE::Editor
