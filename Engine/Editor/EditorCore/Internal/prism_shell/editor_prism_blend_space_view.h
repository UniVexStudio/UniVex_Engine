// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_graph_component.h"

#include "prism/Widget.h"

#include <cstdint>
#include <optional>

namespace UVE::Editor {

class EditorPrismAnimGraphPanelUVE;

/// A Blend Space node opened from the Animation Graph: its area with the axes and grid, the points
/// (each an animation, sized by how much it plays now), the triangles the 2D space blends across
/// with the lit one, and the position cross the parameters move.
///
/// The tool (side panel) decides what a click does: Select moves a point (or the cross, on empty
/// space), Add puts a point and asks for its animation, Remove takes one away. Snapping follows the
/// step set beside it (Shift turns it off for one drag). Right-click a point for its animation and
/// Remove; Esc goes back to the tree.
class BlendSpaceViewUVE final : public prism::Widget {
public:
    explicit BlendSpaceViewUVE(EditorPrismAnimGraphPanelUVE& panel) : m_panel(panel) {}

    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool onKeyDown(const prism::KeyEvent& e) override;
    bool focusable() const override { return true; }

private:
    enum class DragUVE : std::uint8_t { None, Point, Cross };

    [[nodiscard]] const Scene::AnimationGraphNodeUVE* SpaceUVE() const;
    [[nodiscard]] std::size_t SpaceIndexUVE() const;
    [[nodiscard]] prism::Rect PlotUVE() const noexcept;
    [[nodiscard]] prism::Vec2 ToScreenUVE(const Scene::AnimationGraphNodeUVE& space, Math::Vector2UVE at) const;
    [[nodiscard]] Math::Vector2UVE ToSpaceUVE(const Scene::AnimationGraphNodeUVE& space, prism::Vec2 screen, bool snap) const;
    [[nodiscard]] int PointAtUVE(const Scene::AnimationGraphNodeUVE& space, prism::Vec2 screen) const;
    /// Where the cross is: the driving parameters' values, else the node's own.
    [[nodiscard]] Math::Vector2UVE CrossUVE(const Scene::AnimationGraphNodeUVE& space) const;
    void SetCrossUVE(const Scene::AnimationGraphNodeUVE& space, Math::Vector2UVE at);
    void ShowPointMenuUVE(prism::Vec2 at, int point);
    void AskClipUVE(prism::Vec2 at, Math::Vector2UVE position);

    EditorPrismAnimGraphPanelUVE& m_panel;
    DragUVE m_drag = DragUVE::None;
    int m_dragPoint = -1;
    bool m_snap = true;
    prism::Vec2 m_mouse{};
};

} // namespace UVE::Editor
