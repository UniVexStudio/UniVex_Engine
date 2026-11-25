// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"

#include "prism/Widget.h"

#include <chrono>
#include <functional>
#include <string>

namespace UVE::Editor {

/// The editor's loading screen. While the editor starts it covers the window with the branded
/// UniVex Engine intro (`univex_logo.png`, version, project name, live step, sleek progress bar)
/// and fades out smoothly once ready. After startup, background asset work (batch file copies,
/// model conversions, animation extraction) appears as a non-blocking bottom-right card with a
/// clickable Cancel button while the rest of the editor stays 100% interactive.
class EditorLoadingScreen final : public prism::Widget {
public:
    /// Called every frame with the editor's loading state.
    void Sync(const Editor::EditorLoadingState& state, const std::string& project);
    /// Sets the engine version string shown on the startup intro (e.g. "v0.1.1.0").
    void SetVersion(std::string version) { m_version = std::move(version); }
    /// The level view's own loading, drawn over the view (`view`): the render warming up and the
    /// assets it is still loading, after the editor has appeared.
    void SyncRender(bool shown, prism::Rect view, std::string text, std::string detail, float progress);
    std::function<void()> onDismiss;
    std::function<void()> onCancel;

    prism::Vec2 measure(prism::Vec2 avail) override;
    void paint(prism::Canvas& c) override;
    prism::Widget* hitTest(prism::Vec2 p) override;
    bool hitSelf() const override { return m_state.startup; }
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    void onMouseLeave() override;
    prism::Cursor cursor() const override;

private:
    void PaintMark(prism::Canvas& c, prism::Rect mark, float alpha) const;
    [[nodiscard]] prism::Rect CornerPanelRect(prism::Rect bounds) const;
    [[nodiscard]] prism::Rect CancelButtonRect(prism::Rect panel) const;

    Editor::EditorLoadingState m_state;
    std::string m_project;
    std::string m_version;
    bool m_renderShown = false;
    prism::Rect m_renderView{};
    std::string m_renderText;
    std::string m_renderDetail;
    float m_renderProgress = 0.0F;
    /// The bar moves toward its value instead of jumping.
    float m_shown = 0.0F;
    int m_frames = 0;
    /// Smooth 0.25 s fade-out after startup completes.
    bool m_wasStartup = false;
    float m_startupAlpha = 0.0F;
    std::chrono::steady_clock::time_point m_startupFadeBegan{};
    std::string m_lastStartupStep;
    std::string m_lastStartupDetail;
    float m_lastStartupProgress = 1.0F;
    prism::Rect m_cancelButtonRect{};
    bool m_cancelHovered = false;
};

} // namespace UVE::Editor
