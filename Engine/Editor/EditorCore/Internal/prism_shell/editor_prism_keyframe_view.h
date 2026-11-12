// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "animation_clip_asset.h"
#include "editor_prism_timeline_draw.h"

#include "prism/Widget.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Editor {

class EditorPrismSequencerPanelUVE;

/// The Animation Sequencer's keys, drawn with PRISM's canvas: the track list on the left (Events,
/// the clip's Transform, the bones in skeleton order - each opening into Position / Rotation /
/// Scale - and the blend shapes), the ruler with the playhead, and per row the keys as diamonds in
/// their channel's colour, merged into held spans where they crowd. The Curves view draws the
/// selected track's values over time instead.
///
/// Click the ruler to scrub; click a key to select it (Shift adds, Ctrl toggles), drag to move the
/// selection (snapped to frames), drag on empty space for a box; double-click the Events row to add
/// or rename an event; right-click for the menu; Delete, Ctrl+C / Ctrl+V, K, Space, the arrows,
/// Home / End, Ctrl+Z / Ctrl+Y and Ctrl+S while it has focus. The wheel scrolls the rows, Ctrl +
/// wheel zooms around the mouse and Shift + wheel pans. A .uvanim dropped here joins the player.
class KeyframeViewUVE final : public prism::Widget {
public:
    explicit KeyframeViewUVE(EditorPrismSequencerPanelUVE& panel) : m_panel(panel) {}

    prism::Vec2 measure(prism::Vec2 avail) override { return m_desired = constrain(avail); }
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    bool onMouseWheel(const prism::MouseEvent& e) override;
    bool onKeyDown(const prism::KeyEvent& e) override;
    bool focusable() const override { return true; }
    bool onDragEnter(const prism::DragDropEvent& e) override;
    bool onDragOver(const prism::DragDropEvent& e) override;
    void onDragLeave() override;
    bool onDrop(const prism::DragDropEvent& e) override;

    /// Shows the whole clip.
    void FitUVE();

private:
    struct RowUVE final {
        std::string label;
        int depth = 0;
        const std::vector<Asset::AnimationAssetSampleUVE>* samples = nullptr;
        const Asset::AnimationAssetMorphTrackUVE* morph = nullptr;
        bool events = false;
        int channel = -1;   ///< -1: the track row itself, else 0 position, 1 rotation, 2 scale
        std::string track;
    };
    enum class DragUVE : std::uint8_t { None, Scrub, Keys, Event, Box, Curve };

    [[nodiscard]] std::vector<RowUVE> RowsUVE() const;
    [[nodiscard]] prism::Rect KeysRectUVE() const noexcept;   ///< the key area, under the ruler
    [[nodiscard]] TimelineScaleUVE ScaleUVE() const;
    [[nodiscard]] double SnapUVE(double seconds) const;
    [[nodiscard]] int RowAtUVE(float y) const noexcept;
    [[nodiscard]] float RowTopUVE(int row) const noexcept;
    [[nodiscard]] bool IsSelectedUVE(const std::string& track, double seconds) const;
    [[nodiscard]] double DragDeltaUVE() const;
    void ClampScrollUVE(std::size_t rowCount);

    void PaintRowsUVE(prism::Canvas& c, const std::vector<RowUVE>& rows);
    void PaintCurvesUVE(prism::Canvas& c);
    void ShowMenuUVE(prism::Vec2 at, const RowUVE* row);
    void CommitDragUVE();
    void DeleteSelectionUVE();
    void CopyUVE();
    void PasteUVE();
    void RenameEventUVE(int index);
    void AddEventUVE(double seconds);

    /// The curves view's plot: which track, its lines and their ranges (shared by paint and hits).
    struct CurveLineUVE final {
        int channel = 0;
        int axis = 0;
        std::vector<float> values;
        std::vector<float> unwrap;
    };
    struct CurvePlotUVE final {
        std::string track;
        const std::vector<Asset::AnimationAssetSampleUVE>* samples = nullptr;
        std::vector<CurveLineUVE> lines;
        float low[3] = {0.0F, 0.0F, 0.0F};
        float high[3] = {1.0F, 1.0F, 1.0F};
        bool normalized = false;
        prism::Rect plot{};
        [[nodiscard]] float YUVE(float value, int channel) const noexcept;
        [[nodiscard]] float ValueUVE(float y, int channel) const noexcept;
    };
    [[nodiscard]] CurvePlotUVE BuildCurvePlotUVE() const;

    EditorPrismSequencerPanelUVE& m_panel;
    float m_scrollY = 0.0F;
    DragUVE m_drag = DragUVE::None;
    prism::Vec2 m_dragFrom{};
    prism::Vec2 m_mouse{};
    bool m_dropHot = false;
};

} // namespace UVE::Editor
