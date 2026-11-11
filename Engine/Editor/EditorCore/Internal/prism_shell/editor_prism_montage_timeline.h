// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_prism_montage_editor.h"

#include "prism/Widget.h"

#include <cstddef>
#include <optional>

namespace UVE::Editor {

/// The Montage Editor's timeline, drawn with PRISM's canvas:
///
///   [header ][ ruler with the playhead                                   ]
///   [Sections][ |Start        |Attack1          |Attack2       |End       ]
///   [Slot    ][ [clip A        ][ clip B x2              ][ clip C         ]
///   [Notify 1][      <>hit            <>hit                               ]
///   [Notify 2][          [=== hit_window ===]                             ]
///   [Curves  ][ ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~  ]
///
/// Click or drag empty time to scrub; drag a section marker, a clip (to reorder), a notify, a
/// notify state (or its ends) or a curve key to move it; right-click for Add / Rename / Delete;
/// the wheel zooms (Shift pans); Delete removes what is picked. A clip dropped from the Content
/// Browser lands where it is dropped.
class MontageTimelineUVE final : public prism::Widget {
public:
    explicit MontageTimelineUVE(EditorPrismMontageEditorUVE& window) : m_window(window) {}

    prism::Vec2 measure(prism::Vec2 avail) override;
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

private:
    enum class RowUVE : std::uint8_t { Ruler, Sections, Slot, Notify, Curves, None };
    struct HitUVE final {
        RowUVE row = RowUVE::None;
        std::size_t track = 0U;   ///< Notify: which notify track
        EditorPrismMontageEditorUVE::SelectionUVE item;
        int edge = 0;             ///< State: -1 begin, 1 end, 0 body
    };
    [[nodiscard]] float XOfUVE(double time) const noexcept;
    [[nodiscard]] double TimeAtUVE(float x) const noexcept;
    [[nodiscard]] prism::Rect RowRectUVE(RowUVE row, std::size_t track = 0U) const noexcept;
    [[nodiscard]] HitUVE HitTestUVE(prism::Vec2 p) const;
    [[nodiscard]] std::size_t NotifyTracksUVE() const noexcept;
    void ShowContextMenuUVE(const HitUVE& hit, prism::Vec2 at);
    void DeleteSelectionUVE();
    void CommitDragUVE(prism::Vec2 p);

    EditorPrismMontageEditorUVE& m_window;
    double m_scroll = 0.0;          ///< seconds at the left edge
    float m_pixelsPerSecond = 220.0F;
    bool m_scrubbing = false;
    std::optional<HitUVE> m_drag;   ///< what is being dragged
    prism::Vec2 m_dragFrom{};
    prism::Vec2 m_mouse{};
    bool m_dropHot = false;
};

} // namespace UVE::Editor
