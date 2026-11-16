// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_vfx_preview.h"

#include "editor.h"
#include "editor_timeline_draw.h"

#include "prism/Canvas.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr float kTimelineHeaderWidthUVE = 86.0F;
constexpr float kTimelineRulerHeightUVE = 22.0F;
constexpr float kTimelineBodyHeightUVE = 36.0F;
constexpr float kTimelineRightPadUVE = 14.0F;

[[nodiscard]] std::string FormatSecondsShortUVE(const double seconds) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f s", seconds);
    return buf;
}

[[nodiscard]] float ReadBurstTimeInputUVE(const Asset::ParticleModuleUVE& mod) noexcept {
    const auto it = mod.floats.find("Time");
    return it != mod.floats.end() ? it->second.constant : 0.0F;
}

} // namespace

std::string FormatVfxPreviewStatsTextUVE(const EditorUVE& editor) {
    const EditorUVE::VfxEditSessionUVE* const session =
        const_cast<EditorUVE&>(editor).VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return {};
    }

    const VFX::VfxInstanceUVE* const instance =
        session->previewWorld.TryGetInstanceUVE(session->previewHandle);
    std::size_t totalAlive = 0U;
    std::string lines;
    const std::size_t emitterCount = session->system.emitters.size();
    for (std::size_t i = 0U; i < emitterCount; ++i) {
        const Asset::ParticleEmitterAssetUVE& emitter = session->system.emitters[i];
        std::size_t alive = 0U;
        if (instance != nullptr && i < instance->GetEmitterCountUVE()) {
            alive = instance->GetEmitterStateUVE(i).liveCount;
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
    std::snprintf(summary, sizeof(summary), "Total: %zu particles  |  %zu instances drawn",
                  totalAlive, session->lastInstancesDrawn);
    if (!lines.empty()) {
        lines += "\n";
    }
    lines += summary;
    return lines;
}

VfxTimelineWidgetUVE::VfxTimelineWidgetUVE(EditorUVE& editor) : m_editor(editor) {}

prism::Vec2 VfxTimelineWidgetUVE::measure(const prism::Vec2 avail) {
    return m_desired = constrain(prism::Vec2{avail.x, kTimelineRulerHeightUVE + kTimelineBodyHeightUVE});
}

double VfxTimelineWidgetUVE::EffectiveDurationUVE() const noexcept {
    const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return 1.0;
    }
    double maxTime = std::max(0.1, static_cast<double>(session->system.duration));
    maxTime = std::max(maxTime, static_cast<double>(session->system.warmUpSeconds));
    return maxTime;
}

float VfxTimelineWidgetUVE::TimeToScreenXUVE(const double seconds) const noexcept {
    const float left = m_rect.x + kTimelineHeaderWidthUVE;
    const float usableW = std::max(1.0F, m_rect.w - kTimelineHeaderWidthUVE - kTimelineRightPadUVE);
    const double duration = EffectiveDurationUVE();
    const double normalized = std::clamp(seconds / duration, 0.0, 1.0);
    return left + static_cast<float>(normalized) * usableW;
}

double VfxTimelineWidgetUVE::ScreenXToTimeUVE(const float x) const noexcept {
    const float left = m_rect.x + kTimelineHeaderWidthUVE;
    const float usableW = std::max(1.0F, m_rect.w - kTimelineHeaderWidthUVE - kTimelineRightPadUVE);
    const double normalized = std::clamp(static_cast<double>((x - left) / usableW), 0.0, 1.0);
    return normalized * EffectiveDurationUVE();
}

void VfxTimelineWidgetUVE::ScrubAtScreenXUVE(const float x) {
    const double t = ScreenXToTimeUVE(x);
    m_editor.SetVfxPlayheadUVE(t);
    if (onScrub) {
        onScrub(t);
    }
    invalidate();
}

void VfxTimelineWidgetUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(m_rect, t.panel);

    const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }

    const prism::Rect rulerHeader{m_rect.x, m_rect.y, kTimelineHeaderWidthUVE, kTimelineRulerHeightUVE};
    const prism::Rect trackHeader{m_rect.x, m_rect.y + kTimelineRulerHeightUVE, kTimelineHeaderWidthUVE, kTimelineBodyHeightUVE};
    DrawTimelineRowHeaderUVE(c, rulerHeader, "Timeline", t.textDim);
    DrawTimelineRowHeaderUVE(c, trackHeader, "Bursts / Warm", t.textDim);

    const float left = m_rect.x + kTimelineHeaderWidthUVE;
    const float usableW = std::max(1.0F, m_rect.w - kTimelineHeaderWidthUVE - kTimelineRightPadUVE);
    const double duration = EffectiveDurationUVE();

    TimelineScaleUVE scale{};
    scale.left = left;
    scale.scrollSeconds = 0.0;
    scale.pixelsPerSecond = static_cast<double>(usableW) / duration;

    const prism::Rect contentArea{left, m_rect.y, m_rect.w - kTimelineHeaderWidthUVE, m_rect.h};
    const prism::Rect rulerRect{left, m_rect.y, m_rect.w - kTimelineHeaderWidthUVE, kTimelineRulerHeightUVE};
    const prism::Rect trackRect{left, m_rect.y + kTimelineRulerHeightUVE, m_rect.w - kTimelineHeaderWidthUVE, kTimelineBodyHeightUVE};

    c.pushClip(contentArea);

    c.fillRect(trackRect, t.window);
    DrawTimelineRulerUVE(c, rulerRect, scale, 60.0, duration, m_rect.bottom(), false);
    DrawTimelineOutOfRangeUVE(c, contentArea, scale, static_cast<double>(session->system.duration));

    // Draw warm-up shaded region [0, warmUpSeconds] when warmUpSeconds > 0.
    if (session->system.warmUpSeconds > 0.0F) {
        const float warmEndX = TimeToScreenXUVE(static_cast<double>(session->system.warmUpSeconds));
        const float warmW = std::max(0.0F, warmEndX - left);
        if (warmW > 0.5F) {
            const prism::Rect warmRect{left, trackRect.y + 2.0F, warmW, trackRect.h - 4.0F};
            c.fillRoundRect(warmRect, 3.0F, prism::Color(235, 168, 64, 52));
            c.strokeRoundRect(warmRect, 3.0F, prism::Color(235, 168, 64, 150), 1.0F);
            c.textIn(*t.small, prism::Rect{warmRect.x + 4.0F, warmRect.y, std::max(0.0F, warmRect.w - 8.0F), warmRect.h},
                     "Warm-Up", prism::Color(245, 195, 105), prism::HAlign::Left, true);
        }
    }

    // Draw diamond markers for every SpawnBurst module across enabled emitters.
    const float markerCenterY = trackRect.y + trackRect.h * 0.5F;
    for (std::size_t ei = 0U; ei < session->system.emitters.size(); ++ei) {
        const Asset::ParticleEmitterAssetUVE& emitter = session->system.emitters[ei];
        if (!emitter.enabled) {
            continue;
        }
        const bool isSelectedEmitter = (ei == session->selectedEmitter);
        for (const Asset::ParticleModuleUVE& mod : emitter.emitterUpdate) {
            if (!mod.enabled || mod.type != Asset::ParticleModuleTypeUVE::SpawnBurst) {
                continue;
            }
            const double burstTime = std::clamp(static_cast<double>(ReadBurstTimeInputUVE(mod)), 0.0, duration);
            const float bx = TimeToScreenXUVE(burstTime);
            DrawTimelineKeyUVE(c, prism::Vec2{bx, markerCenterY}, 5.5F,
                               isSelectedEmitter ? prism::Color(90, 200, 250) : prism::Color(140, 165, 195),
                               isSelectedEmitter);
        }
    }

    DrawTimelinePlayheadUVE(c, contentArea, kTimelineRulerHeightUVE, scale,
                            session->playhead, FormatSecondsShortUVE(session->playhead));

    c.popClip();
    c.strokeRect(m_rect, t.border, 1.0F);
}

bool VfxTimelineWidgetUVE::onMouseDown(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && e.pos.x >= m_rect.x + kTimelineHeaderWidthUVE) {
        m_scrubbing = true;
        ScrubAtScreenXUVE(e.pos.x);
        return true;
    }
    return false;
}

bool VfxTimelineWidgetUVE::onMouseMove(const prism::MouseEvent& e) {
    if (m_scrubbing) {
        ScrubAtScreenXUVE(e.pos.x);
        return true;
    }
    return false;
}

bool VfxTimelineWidgetUVE::onMouseUp(const prism::MouseEvent& e) {
    if (e.button == prism::MouseButton::Left && m_scrubbing) {
        m_scrubbing = false;
        return true;
    }
    return false;
}

} // namespace UVE::Editor
