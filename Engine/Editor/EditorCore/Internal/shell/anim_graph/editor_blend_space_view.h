// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_graph_component.h"

#include "prism/Widget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Editor {

class EditorAnimGraphPanel;

/// A Blend Space node opened from the Animation Graph: its area with the axes and grid, the points
/// (each an animation, sized by how much it plays now), the triangles the 2D space blends across
/// with the lit one, and the position cross the parameters move.
///
/// The tool (side panel) decides what a click does: Select moves a point (or the cross, on empty
/// space), Add puts a point and asks for its animation, Remove takes one away. Snapping follows the
/// step set beside it (Shift turns it off for one drag). Right-click a point for its animation and
/// Remove; Esc goes back to the tree.
class BlendSpaceView final : public prism::Widget {
public:
    explicit BlendSpaceView(EditorAnimGraphPanel& panel) : m_panel(panel) {}

    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool onKeyDown(const prism::KeyEvent& e) override;
    bool onMouseWheel(const prism::MouseEvent& e) override;
    void onMouseLeave() override;
    bool onDragOver(const prism::DragDropEvent& e) override;
    void onDragLeave() override;
    bool onDrop(const prism::DragDropEvent& e) override;
    const std::string& tooltipAt(prism::Vec2 p) const override;
    prism::Cursor cursor() const override;
    bool focusable() const override { return true; }

private:
    enum class Drag : std::uint8_t { None, Point, Cross, Clip };

    [[nodiscard]] const Scene::AnimationGraphNode* Space() const;
    [[nodiscard]] std::size_t SpaceIndex() const;
    [[nodiscard]] prism::Rect Plot() const noexcept;
    [[nodiscard]] prism::Vec2 ToScreen(const Scene::AnimationGraphNode& space, Math::Vector2 at) const;
    [[nodiscard]] Math::Vector2 ToSpace(const Scene::AnimationGraphNode& space, prism::Vec2 screen, bool snap) const;
    [[nodiscard]] int PointAt(const Scene::AnimationGraphNode& space, prism::Vec2 screen) const;
    /// Where the cross is: the driving parameters' values, else the node's own.
    [[nodiscard]] Math::Vector2 Cross(const Scene::AnimationGraphNode& space) const;
    void SetCross(const Scene::AnimationGraphNode& space, Math::Vector2 at);
    void ShowPointMenu(prism::Vec2 at, int point);
    void AskClip(prism::Vec2 at, Math::Vector2 position);
    void ShowEmptyMenu(prism::Vec2 at, Math::Vector2 position);
    void AddPoint(Math::Vector2 position, Asset::AssetGuid clip);
    /// The animations list along the right: its area, a row's area, and the row under a point.
    [[nodiscard]] prism::Rect Strip() const noexcept;
    [[nodiscard]] prism::Rect StripRow(std::size_t row) const noexcept;
    [[nodiscard]] int StripRowAt(prism::Vec2 screen) const;
    void PaintStrip(prism::Canvas& c);
    /// The grid step of an axis (the area over its divisions).
    [[nodiscard]] float GridStep(const Scene::AnimationGraphNode& space, bool y) const;

    EditorAnimGraphPanel& m_panel;
    Drag m_drag = Drag::None;
    int m_dragPoint = -1;
    bool m_snap = true;
    prism::Vec2 m_mouse{};
    std::vector<Asset::AssetGuid> m_clips;
    float m_stripScroll = 0.0F;
    int m_stripHover = -1;
    Asset::AssetGuid m_dragClip{};
    bool m_dropHot = false;
    mutable std::string m_tip;
};

} // namespace UVE::Editor
