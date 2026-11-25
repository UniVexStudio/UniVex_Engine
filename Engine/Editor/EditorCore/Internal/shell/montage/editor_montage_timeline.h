// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_montage_editor.h"

#include "prism/Widget.h"

#include <cstddef>
#include <cstdint>
#include <optional>

namespace UVE::Editor {

/// The Motion Montage Editor's timeline, drawn with PRISM's canvas:
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
class MontageTimeline final : public prism::Widget {
public:
    explicit MontageTimeline(EditorMontageEditor& window) : m_window(window) {}

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
    enum class Row : std::uint8_t { Ruler, Sections, Slot, Notify, Curves, None };
    struct Hit final {
        Row row = Row::None;
        std::size_t track = 0U;   ///< Notify: which notify track
        EditorMontageEditor::Selection item;
        int edge = 0;             ///< State: -1 begin, 1 end, 0 body
    };
    [[nodiscard]] float XOf(double time) const noexcept;
    [[nodiscard]] double TimeAt(float x) const noexcept;
    [[nodiscard]] prism::Rect RowRect(Row row, std::size_t track = 0U) const noexcept;
    [[nodiscard]] Hit HitTest(prism::Vec2 p) const;
    [[nodiscard]] std::size_t NotifyTracks() const noexcept;
    void ShowContextMenu(const Hit& hit, prism::Vec2 at);
    void ShowHeaderMenu(const Hit& hit, prism::Vec2 at);
    /// The notify track under window height `y`, if any.
    [[nodiscard]] std::optional<std::uint32_t> TrackAt(float y) const noexcept;
    void DeleteSelection();
    void CommitDrag(prism::Vec2 p);

    EditorMontageEditor& m_window;
    double m_scroll = 0.0;          ///< seconds at the left edge
    float m_pixelsPerSecond = 220.0F;
    bool m_scrubbing = false;
    std::optional<Hit> m_drag;   ///< what is being dragged
    prism::Vec2 m_dragFrom{};
    prism::Vec2 m_mouse{};
    bool m_dropHot = false;
};

} // namespace UVE::Editor
