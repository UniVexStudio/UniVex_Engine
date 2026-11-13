// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"

#include "prism/Widget.h"

#include <string>

namespace UVE::Editor {

/// The editor's loading screen. While the editor starts it covers the window with the engine mark
/// alone, and it takes the input, so nothing is clicked half-loaded (a click after a few seconds
/// lets the rest go on behind the editor). After that, work still running (a big import) shows as
/// a small panel in the lower right corner, and the level view shows "Loading render" while its
/// renderer warms up and loads what the scene draws; neither takes input.
class EditorLoadingScreenUVE final : public prism::Widget {
public:
    /// Called every frame with the editor's loading state.
    void SyncUVE(const EditorUVE::EditorLoadingStateUVE& state, const std::string& project);
    /// The level view's own loading, drawn over the view (`view`): the render warming up and the
    /// assets it is still loading, after the editor has appeared.
    void SyncRenderUVE(bool shown, prism::Rect view, std::string text, float progress);
    std::function<void()> onDismiss;

    prism::Vec2 measure(prism::Vec2 avail) override;
    void paint(prism::Canvas& c) override;
    bool hitSelf() const override { return m_state.startup; }
    bool onMouseDown(const prism::MouseEvent& e) override;

private:
    void PaintMarkUVE(prism::Canvas& c, prism::Rect mark) const;

    EditorUVE::EditorLoadingStateUVE m_state;
    std::string m_project;
    bool m_renderShown = false;
    prism::Rect m_renderView{};
    std::string m_renderText;
    float m_renderProgress = 0.0F;
    /// The bar moves toward its value instead of jumping.
    float m_shown = 0.0F;
    int m_frames = 0;
};

} // namespace UVE::Editor
