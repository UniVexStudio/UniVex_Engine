// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_loading_screen.h"

#include "prism/Canvas.h"
#include "prism/Theme.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace UVE::Editor {

void EditorLoadingScreenUVE::SyncRenderUVE(const bool shown, const prism::Rect view, std::string text, std::string detail,
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

void EditorLoadingScreenUVE::SyncUVE(const EditorUVE::EditorLoadingStateUVE& state, const std::string& project) {
    const bool shown = state.startup || state.busy || m_renderShown;
    if (!shown) {
        m_shown = 0.0F;
        m_frames = 0;
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
    m_shown += (state.progress - m_shown) * 0.25F;
    if (state.progress < m_shown) {
        m_shown = state.progress; // a new batch starts again from its own beginning
    }
    m_state = state;
    m_project = project;
    invalidate();
}

prism::Vec2 EditorLoadingScreenUVE::measure(const prism::Vec2 avail) {
    return m_desired = constrain(avail);
}

bool EditorLoadingScreenUVE::onMouseDown(const prism::MouseEvent&) {
    if (!m_state.startup) {
        return false;
    }
    // Held a while: a click lets the rest go on behind the editor.
    if (m_frames > 180 && onDismiss) {
        onDismiss();
    }
    return true;
}

void EditorLoadingScreenUVE::PaintMarkUVE(prism::Canvas& c, const prism::Rect mark) const {
    const prism::Color blue(32, 112, 228);
    c.fillRoundRect(mark, mark.w * 0.16F, blue);
    c.strokeRoundRect(mark, mark.w * 0.16F, prism::Color(82, 160, 255, 220), 1);
    const float uw = mark.w * 0.46F;
    const float uh = mark.h * 0.52F;
    const float th = std::max(2.0F, std::round(mark.w * 0.12F));
    const float ux = mark.x + (mark.w - uw) * 0.5F;
    const float uy = mark.y + (mark.h - uh) * 0.5F;
    const prism::Color white(255, 255, 255);
    c.fillRect(prism::Rect{ux, uy, th, uh - uw * 0.5F}, white);
    c.fillRect(prism::Rect{ux + uw - th, uy, th, uh - uw * 0.5F}, white);
    const prism::Vec2 bowl{ux + uw * 0.5F, uy + uh - uw * 0.5F};
    c.fillCircle(bowl, uw * 0.5F, white);
    c.fillCircle(bowl, uw * 0.5F - th, blue);
    c.fillRect(prism::Rect{ux + th, uy, uw - 2.0F * th, uh - uw * 0.5F}, blue);
}

void EditorLoadingScreenUVE::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    const prism::Rect r = rect();
    const prism::Color accent(48, 132, 240);
    if (!m_state.startup && m_renderShown && m_renderView.w > 260.0F && m_renderView.h > 80.0F) {
        // Over the level view, at its top: the render loading, and what it is drawing now.
        const float height = m_renderDetail.empty() ? 44.0F : 62.0F;
        const prism::Rect panel{std::floor(m_renderView.x + (m_renderView.w - 268.0F) * 0.5F), m_renderView.y + 14.0F, 268.0F, height};
        c.fillRoundRect(panel, 6.0F, prism::Color(20, 22, 27, 230));
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
    if (!m_state.startup && !m_state.busy) {
        return;
    }
    if (!m_state.startup) {
        // The corner panel: what is running and how far along it is.
        const prism::Rect panel{r.right() - 336.0F, r.bottom() - 92.0F, 320.0F, 58.0F};
        c.shadow(panel, 6.0F, 8, prism::Color(0, 0, 0, 120));
        c.fillRoundRect(panel, 6.0F, t.header);
        c.strokeRoundRect(panel, 6.0F, t.border, 1);
        c.textIn(*t.small, prism::Rect{panel.x + 12.0F, panel.y + 8.0F, panel.w - 24.0F, 18.0F}, m_state.step, t.textBright);
        const prism::Rect bar{panel.x + 12.0F, panel.y + 36.0F, panel.w - 24.0F, 6.0F};
        c.fillRoundRect(bar, 3.0F, t.window);
        c.fillRoundRect(prism::Rect{bar.x, bar.y, std::max(6.0F, bar.w * m_shown), bar.h}, 3.0F, accent);
        return;
    }
    // The whole window: plain, with the engine mark in the middle.
    c.fillRect(r, prism::Color(18, 19, 23));
    const float side = 112.0F;
    PaintMarkUVE(c, prism::Rect{std::floor(r.x + (r.w - side) * 0.5F), std::floor(r.y + (r.h - side) * 0.5F), side, side});
}

} // namespace UVE::Editor
