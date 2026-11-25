// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_loading_screen.h"

#include "prism/Canvas.h"
#include "prism/Icons.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace UVE::Editor {
namespace {

constexpr double kStartupFadeSeconds = 0.25;

[[nodiscard]] float SmoothStep(const float x) noexcept {
    const float t = std::clamp(x, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

} // namespace

void EditorLoadingScreen::SyncRender(const bool shown, const prism::Rect view, std::string text, std::string detail,
                                           const float progress) {
    m_renderShown = shown;
    m_renderView = view;
    m_renderText = std::move(text);
    m_renderDetail = std::move(detail);
    m_renderProgress += (std::clamp(progress, 0.0F, 1.0F) - m_renderProgress) * 0.2F;
    if (!shown) {
        m_renderProgress = 0.0F;
    }
}

void EditorLoadingScreen::Sync(const Editor::EditorLoadingState& state, const std::string& project) {
    const auto now = std::chrono::steady_clock::now();
    if (state.startup) {
        m_wasStartup = true;
        m_startupAlpha = 1.0F;
        m_startupFadeBegan = {};
        if (!state.step.empty()) {
            m_lastStartupStep = state.step;
        }
        m_lastStartupDetail = state.detail;
        m_lastStartupProgress = std::clamp(state.progress, 0.0F, 1.0F);
    } else if (m_wasStartup) {
        if (m_startupFadeBegan == std::chrono::steady_clock::time_point{}) {
            m_startupFadeBegan = now;
            m_lastStartupProgress = 1.0F;
        }
        const double elapsed = std::chrono::duration<double>(now - m_startupFadeBegan).count();
        const float t = static_cast<float>(std::clamp(elapsed / kStartupFadeSeconds, 0.0, 1.0));
        m_startupAlpha = 1.0F - SmoothStep(t);
        if (t >= 1.0F) {
            m_wasStartup = false;
            m_startupAlpha = 0.0F;
        }
    }

    const bool startupVisible = state.startup || m_startupAlpha > 0.005F;
    const bool shown = startupVisible || state.busy || m_renderShown;
    if (!shown) {
        m_shown = 0.0F;
        m_frames = 0;
        m_cancelButtonRect = {};
        m_cancelHovered = false;
        if (visible()) {
            setVisible(false);
        }
        m_state = state;
        return;
    }
    if (!visible()) {
        setVisible(true);
    }
    ++m_frames;
    const float targetProgress = state.startup ? state.progress : (startupVisible ? 1.0F : state.progress);
    m_shown += (std::clamp(targetProgress, 0.0F, 1.0F) - m_shown) * 0.25F;
    if (targetProgress < m_shown && !startupVisible) {
        m_shown = targetProgress; // a new batch starts again from its own beginning
    }
    if (!state.busy || !state.cancellable) {
        m_cancelButtonRect = {};
        m_cancelHovered = false;
    }
    m_state = state;
    m_project = project;
    invalidate();
}

prism::Vec2 EditorLoadingScreen::measure(const prism::Vec2 avail) {
    return m_desired = constrain(avail);
}

prism::Rect EditorLoadingScreen::CornerPanelRect(const prism::Rect bounds) const {
    const float w = std::min(344.0F, std::max(240.0F, bounds.w - 28.0F));
    const float h = 72.0F;
    return prism::Rect{std::floor(bounds.right() - w - 16.0F), std::floor(bounds.bottom() - h - 34.0F), w, h};
}

prism::Rect EditorLoadingScreen::CancelButtonRect(const prism::Rect panel) const {
    constexpr float btnW = 58.0F;
    constexpr float btnH = 20.0F;
    return prism::Rect{std::floor(panel.right() - btnW - 12.0F), std::floor(panel.y + 28.0F), btnW, btnH};
}

prism::Widget* EditorLoadingScreen::hitTest(const prism::Vec2 p) {
    if (!visible()) {
        return nullptr;
    }
    if (m_state.startup) {
        return rect().contains(p) ? this : nullptr;
    }
    if (m_startupAlpha <= 0.005F && m_state.busy && m_state.cancellable) {
        const prism::Rect cancelRect = CancelButtonRect(CornerPanelRect(rect()));
        if (cancelRect.contains(p)) {
            return this;
        }
    }
    return nullptr;
}

bool EditorLoadingScreen::onMouseDown(const prism::MouseEvent& e) {
    if (m_state.startup) {
        // Held a while: a click lets the rest go on behind the editor.
        if (m_frames > 180 && onDismiss) {
            onDismiss();
        }
        return true;
    }
    if (m_startupAlpha <= 0.005F && m_state.busy && m_state.cancellable) {
        const prism::Rect cancelRect = CancelButtonRect(CornerPanelRect(rect()));
        if (cancelRect.contains(e.pos)) {
            if (onCancel) {
                onCancel();
            }
            return true;
        }
    }
    return false;
}

bool EditorLoadingScreen::onMouseMove(const prism::MouseEvent& e) {
    if (!m_state.startup && m_startupAlpha <= 0.005F && m_state.busy && m_state.cancellable) {
        const bool over = CancelButtonRect(CornerPanelRect(rect())).contains(e.pos);
        if (over != m_cancelHovered) {
            m_cancelHovered = over;
            invalidate();
        }
        return over;
    }
    return false;
}

void EditorLoadingScreen::onMouseLeave() {
    if (m_cancelHovered) {
        m_cancelHovered = false;
        invalidate();
    }
}

prism::Cursor EditorLoadingScreen::cursor() const {
    return m_cancelHovered ? prism::Cursor::Hand : prism::Cursor::Arrow;
}

void EditorLoadingScreen::PaintMark(prism::Canvas& c, const prism::Rect mark, const float alpha) const {
    if (prism::hasBrandMark()) {
        prism::drawBrandMark(c, mark, alpha);
        return;
    }
    // Minimal fallback only when running without PRISM brand image assets (e.g. unit tests).
    const prism::Theme& t = prism::theme();
    c.fillRoundRect(mark, mark.w * 0.22F, t.accent.alpha(alpha * 0.22F));
    c.strokeRoundRect(mark, mark.w * 0.22F, t.accent.alpha(alpha), 2);
}

void EditorLoadingScreen::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    const prism::Color accent = t.accent;
    const bool startupVisible = m_state.startup || m_startupAlpha > 0.005F;

    if (!startupVisible && m_renderShown && m_renderView.w > 260.0F && m_renderView.h > 80.0F) {
        // Over the level view, at its top: the render loading, and what it is drawing now.
        const float height = m_renderDetail.empty() ? 44.0F : 62.0F;
        const prism::Rect panel{std::floor(m_renderView.x + (m_renderView.w - 268.0F) * 0.5F), m_renderView.y + 14.0F, 268.0F, height};
        c.fillRoundRect(panel, 6.0F, prism::Color(20, 22, 27, 232));
        c.strokeRoundRect(panel, 6.0F, t.border, 1);
        c.textIn(*t.small, prism::Rect{panel.x + 12.0F, panel.y + 6.0F, panel.w - 70.0F, 16.0F}, m_renderText, t.textBright);
        const int percent = static_cast<int>(std::round(m_renderProgress * 100.0F));
        c.textIn(*t.small, prism::Rect{panel.x + 12.0F, panel.y + 6.0F, panel.w - 24.0F, 16.0F}, std::to_string(percent) + "%", t.text,
                 prism::HAlign::Right);
        const prism::Rect bar{panel.x + 12.0F, panel.y + 28.0F, panel.w - 24.0F, 5.0F};
        c.fillRoundRect(bar, 2.5F, t.window);
        c.fillRoundRect(prism::Rect{bar.x, bar.y, std::max(5.0F, bar.w * m_renderProgress), bar.h}, 2.5F, accent);
        if (!m_renderDetail.empty()) {
            c.textIn(*t.small, prism::Rect{panel.x + 12.0F, panel.y + 40.0F, panel.w - 24.0F, 16.0F}, m_renderDetail, t.textDim);
        }
    }

    if (!startupVisible && !m_state.busy) {
        return;
    }

    if (!startupVisible) {
        // Non-blocking bottom-right progress card for background copies/imports/animations.
        const prism::Rect panel = CornerPanelRect(r);
        c.shadow(panel, 6.0F, 10, prism::Color(0, 0, 0, 140));
        c.fillRoundRect(panel, 6.0F, t.header.withAlpha(246));
        c.strokeRoundRect(panel, 6.0F, t.border, 1);

        // Header row: status dot + phase title on left, count + percentage on right.
        c.fillCircle(prism::Vec2{panel.x + 16.0F, panel.y + 16.0F}, 3.5F, accent);
        const int percent = static_cast<int>(std::round(std::clamp(m_shown, 0.0F, 1.0F) * 100.0F));
        std::string rightMeta = std::to_string(percent) + "%";
        if (!m_state.detail.empty()) {
            rightMeta = m_state.detail + "  ·  " + rightMeta;
        }
        c.textIn(*t.small, prism::Rect{panel.x + 26.0F, panel.y + 8.0F, panel.w - 150.0F, 16.0F},
                 m_state.step.empty() ? "Importing assets" : m_state.step, t.textBright);
        c.textIn(*t.small, prism::Rect{panel.x + 12.0F, panel.y + 8.0F, panel.w - 24.0F, 16.0F}, rightMeta, t.textDim,
                 prism::HAlign::Right);

        // Middle row: active filename on left + clickable Cancel button on right.
        const float itemRightPad = m_state.cancellable ? 78.0F : 12.0F;
        const std::string itemLabel = !m_state.currentItem.empty() ? m_state.currentItem : "Working in background...";
        c.textIn(*t.small, prism::Rect{panel.x + 12.0F, panel.y + 30.0F, panel.w - 12.0F - itemRightPad, 16.0F},
                 itemLabel, t.textDim);

        if (m_state.cancellable) {
            m_cancelButtonRect = CancelButtonRect(panel);
            c.fillRoundRect(m_cancelButtonRect, 4.0F, m_cancelHovered ? t.controlHover : t.control);
            c.strokeRoundRect(m_cancelButtonRect, 4.0F, m_cancelHovered ? t.borderStrong : t.border, 1);
            c.textIn(*t.small, m_cancelButtonRect, "Cancel", m_cancelHovered ? t.textBright : t.text, prism::HAlign::Center);
        } else {
            m_cancelButtonRect = {};
        }

        // Bottom row: smooth progress track.
        const prism::Rect bar{panel.x + 12.0F, panel.y + 56.0F, panel.w - 24.0F, 5.0F};
        c.fillRoundRect(bar, 2.5F, t.window);
        const float fillW = std::clamp(bar.w * m_shown, 6.0F, bar.w);
        c.fillRoundRect(prism::Rect{bar.x, bar.y, fillW, bar.h}, 2.5F, accent);
        return;
    }

    // Full-screen branded UniVex Engine startup intro with smooth alpha fade-out.
    const float a = std::clamp(m_startupAlpha, 0.0F, 1.0F);
    c.gradientV(r, prism::Color(17, 19, 24).alpha(a), prism::Color(11, 12, 16).alpha(a));
    c.fillRect(prism::Rect{r.x, r.y, r.w, 2.0F}, accent.alpha(a * 0.85F));

    const bool compact = r.h < 460.0F || r.w < 540.0F;
    const float logoSide = compact ? 80.0F : 112.0F;
    const float barWidth = std::min(340.0F, std::max(180.0F, r.w - 56.0F));
    const float blockHeight = logoSide + (compact ? 116.0F : 138.0F);
    const float topY = std::floor(r.y + (r.h - blockHeight) * 0.5F);
    const float centerX = r.x + r.w * 0.5F;

    // Real univex_logo.png centered cleanly above title & progress.
    const prism::Rect markRect{std::floor(centerX - logoSide * 0.5F), topY, logoSide, logoSide};
    PaintMark(c, markRect, a);

    float y = markRect.bottom() + (compact ? 14.0F : 20.0F);
    c.textIn(*t.semibold, prism::Rect{r.x + 24.0F, y, r.w - 48.0F, 24.0F}, "UniVex Engine",
             t.textBright.alpha(a), prism::HAlign::Center);
    y += 24.0F;

    std::string subtitle;
    if (!m_version.empty() && !m_project.empty()) {
        subtitle = m_version + "  ·  " + m_project;
    } else if (!m_project.empty()) {
        subtitle = m_project;
    } else {
        subtitle = m_version;
    }
    if (!subtitle.empty()) {
        c.textIn(*t.small, prism::Rect{r.x + 24.0F, y, r.w - 48.0F, 18.0F}, subtitle,
                 t.textDim.alpha(a), prism::HAlign::Center);
        y += 24.0F;
    } else {
        y += 14.0F;
    }

    const float progress = m_state.startup ? m_shown : m_lastStartupProgress;
    const prism::Rect bar{std::floor(centerX - barWidth * 0.5F), y, barWidth, 5.0F};
    c.fillRoundRect(bar, 2.5F, prism::Color(30, 33, 42).alpha(a));
    c.fillRoundRect(prism::Rect{bar.x, bar.y, std::clamp(bar.w * progress, 6.0F, bar.w), bar.h}, 2.5F, accent.alpha(a));
    y += 14.0F;

    const std::string& stepText = m_state.startup ? m_state.step : m_lastStartupStep;
    const std::string& detailText = m_state.startup ? m_state.detail : m_lastStartupDetail;
    const int percent = static_cast<int>(std::round(std::clamp(progress, 0.0F, 1.0F) * 100.0F));
    c.textIn(*t.small, prism::Rect{bar.x, y, bar.w - 42.0F, 16.0F},
             stepText.empty() ? "Starting UniVex Engine" : stepText, t.text.alpha(a), prism::HAlign::Left);
    c.textIn(*t.small, prism::Rect{bar.x, y, bar.w, 16.0F}, std::to_string(percent) + "%",
             t.textDim.alpha(a), prism::HAlign::Right);
    if (!detailText.empty()) {
        c.textIn(*t.small, prism::Rect{bar.x, y + 18.0F, bar.w, 16.0F}, detailText,
                 t.textDim.alpha(a * 0.85F), prism::HAlign::Left);
    }

    if (m_state.startup && m_frames > 180) {
        c.textIn(*t.small, prism::Rect{r.x + 24.0F, r.bottom() - 32.0F, r.w - 48.0F, 16.0F},
                 "Click anywhere to continue loading in background", t.textDim.alpha(a * 0.7F), prism::HAlign::Center);
    }
}

} // namespace UVE::Editor
