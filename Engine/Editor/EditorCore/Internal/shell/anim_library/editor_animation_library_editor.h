// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace prism {
class Label;
class TextInput;
} // namespace prism

namespace UVE::Editor {

class Editor;
class EditorHost;
class DocumentTab;

/// The clips of an Animation Library as tiles: each with its icon (the skeleton half-way through
/// the clip), its name, length and frames. Click picks, double-click renames, right-click for
/// the rest; drag a tile onto another to reorder; drop clips from the Asset Table to add them.
class AnimationLibraryGrid final : public prism::Widget {
public:
    struct Tile {
        std::string name;
        std::string detail;
        bool missing = false;
        /// The clip's icon (ARGB, `iconSize` square), or none.
        const std::vector<std::uint32_t>* icon = nullptr;
        int iconSize = 0;
        int iconHeight = 0;
        /// Its index in the library (the tiles may be filtered).
        std::size_t entry = 0U;
    };
    void SetTiles(std::vector<Tile> tiles);
    void SetSelected(std::size_t entry) { m_selected = entry; invalidate(); }
    [[nodiscard]] std::size_t Selected() const noexcept { return m_selected; }

    std::function<void(std::size_t entry)> onPicked;
    std::function<void(std::size_t entry)> onActivated;
    std::function<void(std::size_t entry, prism::Vec2 at)> onContext;
    std::function<void(std::size_t from, std::size_t to)> onReorder;
    std::function<void(const std::string& contentRelative)> onDropped;

    prism::Vec2 measure(prism::Vec2 available) override;
    void paint(prism::Canvas& c) override;
    bool onMouseDown(const prism::MouseEvent& e) override;
    bool onMouseMove(const prism::MouseEvent& e) override;
    bool onMouseUp(const prism::MouseEvent& e) override;
    void onMouseLeave() override;
    bool onDragEnter(const prism::DragDropEvent& e) override;
    bool onDragOver(const prism::DragDropEvent& e) override;
    void onDragLeave() override;
    bool onDrop(const prism::DragDropEvent& e) override;
    prism::Cursor cursor() const override;

    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);

private:
    [[nodiscard]] int Columns() const;
    [[nodiscard]] prism::Rect TileRect(std::size_t index) const;
    /// The tile under `p` (its position in m_tiles), or kNone.
    [[nodiscard]] std::size_t Hit(prism::Vec2 p) const;

    std::vector<Tile> m_tiles;
    std::size_t m_selected = kNone;
    std::size_t m_hover = kNone;
    std::size_t m_pressed = kNone;
    prism::Vec2 m_pressAt{};
    bool m_dragging = false;
    bool m_dropHot = false;
};

/// The Animation Library editor, in a window of its own (as Retarget and the Motion Montage Editor):
///
///   [#1 logo] [title bar: File Edit Help | drag strip | - [] x]
///   [       ] [#2 the library's tab                          ]
///   [ + Add Animation | Remove | Rename | search | 12 clips | status ]
///   [ tiles: icon, name, length and frames                          ]
///
/// Every edit is written to the `.uvanimlib` at once (a failed write puts the list back), so
/// there is nothing to save and nothing to lose on close.
class EditorAnimationLibraryWindow final {
public:
    EditorAnimationLibraryWindow(Editor& editor, EditorHost& host);
    ~EditorAnimationLibraryWindow();
    EditorAnimationLibraryWindow(const EditorAnimationLibraryWindow&) = delete;
    EditorAnimationLibraryWindow& operator=(const EditorAnimationLibraryWindow&) = delete;

    void Sync();
    void Render();
    [[nodiscard]] int WindowId() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> Build();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBar();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbar();
    void Refresh();
    /// Writes the library; on failure the list goes back to the file. `done` is the status line.
    bool Save(const std::string& done);
    void ShowAddMenu(prism::Rect anchor);
    void ShowEntryMenu(std::size_t entry, prism::Vec2 at);
    void Rename(std::size_t entry);
    void Remove(std::size_t entry);
    void Move(std::size_t from, std::size_t to);
    void AddClips(const std::vector<std::filesystem::path>& absolute);
    [[nodiscard]] std::filesystem::path ClipPath(std::size_t entry) const;

    Editor& m_editor;
    EditorHost& m_host;
    int m_window = 0;
    std::filesystem::path m_openPath;
    std::string m_shownSignature;

    std::shared_ptr<DocumentTab> m_tab;
    std::shared_ptr<AnimationLibraryGrid> m_grid;
    std::shared_ptr<prism::TextInput> m_filter;
    std::shared_ptr<prism::Label> m_count;
    std::shared_ptr<prism::Label> m_status;
    std::shared_ptr<prism::Widget> m_addButton;
};

} // namespace UVE::Editor
