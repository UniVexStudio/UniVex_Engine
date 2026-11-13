// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_graph_component.h"

#include "prism/Widget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Editor {

class EditorAnimGraphPanelUVE;

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
    explicit BlendSpaceViewUVE(EditorAnimGraphPanelUVE& panel) : m_panel(panel) {}

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
    enum class DragUVE : std::uint8_t { None, Point, Cross, Clip };

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
    void ShowEmptyMenuUVE(prism::Vec2 at, Math::Vector2UVE position);
    void AddPointUVE(Math::Vector2UVE position, Asset::AssetGuidUVE clip);
    /// The animations list along the right: its area, a row's area, and the row under a point.
    [[nodiscard]] prism::Rect StripUVE() const noexcept;
    [[nodiscard]] prism::Rect StripRowUVE(std::size_t row) const noexcept;
    [[nodiscard]] int StripRowAtUVE(prism::Vec2 screen) const;
    void PaintStripUVE(prism::Canvas& c);
    /// The grid step of an axis (the area over its divisions).
    [[nodiscard]] float GridStepUVE(const Scene::AnimationGraphNodeUVE& space, bool y) const;

    EditorAnimGraphPanelUVE& m_panel;
    DragUVE m_drag = DragUVE::None;
    int m_dragPoint = -1;
    bool m_snap = true;
    prism::Vec2 m_mouse{};
    std::vector<Asset::AssetGuidUVE> m_clips;
    float m_stripScroll = 0.0F;
    int m_stripHover = -1;
    Asset::AssetGuidUVE m_dragClip{};
    bool m_dropHot = false;
    mutable std::string m_tip;
};

} // namespace UVE::Editor
