// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace UVE::Editor {

/// The Retarget window's source box: a skeleton drawn flat, from the front, as one connected
/// figure - each bone a tapered blade from its joint to the next, every joint a ringed dot in the
/// colour of how the character answers it. The numbers it draws are the skeleton's own rest
/// positions (projected); nothing is moved or recomputed to make it look right.
///
/// The mouse wheel zooms at the pointer, a right or middle drag pans, a double click fits it
/// again; hovering a joint names it, clicking picks it (onPicked).
class RetargetSkeletonDiagramUVE final : public prism::Widget {
public:
    struct JointUVE {
        std::string name;
        /// A second line when hovered or picked (what answers it, how it was found).
        std::string detail;
        /// Index of the joint it hangs from in the same list, or -1.
        int parent = -1;
        /// Rest position seen from the front: x to the figure's left on screen-right, y up.
        float x = 0.0F;
        float y = 0.0F;
        std::array<float, 3> colour{0.6F, 0.6F, 0.62F};
        /// Drawn faint (made by the retarget, or missing).
        bool faint = false;
        /// Not drawn at all (an optional bone the character does not have); what hangs from it
        /// hangs from the nearest joint above that is drawn.
        bool hidden = false;
        /// A helper, not a part of the body (the root on the ground, IK targets, sockets): a small
        /// open ring, with no blade to or from it, so the body reads as one figure.
        bool helper = false;
    };

    RetargetSkeletonDiagramUVE();

    /// A new skeleton (the view fits it again when `refit`).
    void SetJointsUVE(std::vector<JointUVE> joints, bool refit);
    [[nodiscard]] const std::vector<JointUVE>& JointsUVE() const noexcept { return m_joints; }
    void SetSelectedUVE(int joint);
    [[nodiscard]] int SelectedUVE() const noexcept { return m_selected; }
    /// A line under the figure when there is nothing to draw.
    void SetEmptyTextUVE(std::string text);
    /// The key along the bottom: a colour and what it means (with its count).
    void SetLegendUVE(std::vector<std::pair<std::array<float, 3>, std::string>> legend);

    std::function<void(int)> onPicked;

    prism::Vec2 measure(prism::Vec2 available) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool onMouseWheel(const prism::MouseEvent& e) override;
    void onMouseLeave() override;
    prism::Cursor cursor() const override;

private:
    /// The figure's area inside the box (above the legend).
    [[nodiscard]] prism::Rect FigureRectUVE() const;
    /// Where joint `index` is drawn, in window pixels.
    [[nodiscard]] prism::Vec2 ToViewUVE(std::size_t index) const;
    [[nodiscard]] float FitScaleUVE() const;
    [[nodiscard]] int HitUVE(prism::Vec2 p) const;
    /// The nearest drawn joint above `index` (-1: none).
    [[nodiscard]] int DrawnParentUVE(std::size_t index) const;

    std::vector<JointUVE> m_joints;
    std::string m_empty;
    std::vector<std::pair<std::array<float, 3>, std::string>> m_legend;
    /// Bounds of the drawn joints, in skeleton units.
    float m_minX = 0.0F;
    float m_maxX = 1.0F;
    float m_minY = 0.0F;
    float m_maxY = 1.0F;
    float m_zoom = 1.0F;
    prism::Vec2 m_pan{0.0F, 0.0F};
    int m_selected = -1;
    int m_hover = -1;
    bool m_panning = false;
    prism::Vec2 m_panLast{};
};

} // namespace UVE::Editor
