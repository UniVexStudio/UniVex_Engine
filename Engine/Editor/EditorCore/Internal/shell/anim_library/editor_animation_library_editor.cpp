// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_animation_library_editor.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_asset_table.h"
#include "editor_host.h"

#include "animation_clip_asset.h"
#include "animation_library_asset.h"
#include "i_asset_database.h"
#include "i_project_file_index.h"

#include "prism/Application.h"
#include "prism/Canvas.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/DragDrop.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr int kWindowWidth = 980;
constexpr int kWindowHeight = 640;
constexpr float kTileW = 112.0F;
constexpr float kTileH = 150.0F;
constexpr float kGap = 10.0F;
constexpr float kPad = 14.0F;

[[nodiscard]] prism::Slot Centre() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string Lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

// ------------------------------------------------------------------------------------------ grid

void AnimationLibraryGrid::SetTiles(std::vector<Tile> tiles) {
    m_tiles = std::move(tiles);
    m_hover = kNone;
    relayout();
    invalidate();
}

int AnimationLibraryGrid::Columns() const {
    return std::max(1, static_cast<int>((rect().w - 2.0F * kPad + kGap) / (kTileW + kGap)));
}

prism::Vec2 AnimationLibraryGrid::measure(const prism::Vec2 available) {
    const int columns = std::max(1, static_cast<int>((std::max(available.x, 200.0F) - 2.0F * kPad + kGap) / (kTileW + kGap)));
    const int rows = (static_cast<int>(m_tiles.size()) + columns - 1) / columns;
    const float height = 2.0F * kPad + static_cast<float>(std::max(rows, 1)) * (kTileH + kGap);
    return constrain(prism::Vec2{std::max(available.x, 200.0F), std::max(height, available.y)});
}

prism::Rect AnimationLibraryGrid::TileRect(const std::size_t index) const {
    const int columns = Columns();
    const int column = static_cast<int>(index) % columns;
    const int row = static_cast<int>(index) / columns;
    return prism::Rect{rect().x + kPad + static_cast<float>(column) * (kTileW + kGap),
                       rect().y + kPad + static_cast<float>(row) * (kTileH + kGap), kTileW, kTileH};
}

std::size_t AnimationLibraryGrid::Hit(const prism::Vec2 p) const {
    for (std::size_t i = 0U; i < m_tiles.size(); ++i) {
        if (TileRect(i).contains(p)) {
            return i;
        }
    }
    return kNone;
}

void AnimationLibraryGrid::paint(prism::Canvas& c) {
    const prism::Theme& t = prism::theme();
    c.fillRect(rect(), t.window);
    if (m_tiles.empty()) {
        c.textIn(*t.ui, prism::Rect{rect().x, rect().y + rect().h * 0.4F, rect().w, 22.0F},
                 "No clips yet: + Add Animation, or drop .uvanim files here from the Asset Table.", t.textDim,
                 prism::HAlign::Center, true);
    }
    for (std::size_t i = 0U; i < m_tiles.size(); ++i) {
        const Tile& tile = m_tiles[i];
        const prism::Rect r = TileRect(i);
        const bool picked = tile.entry == m_selected;
        const bool hovered = i == m_hover;
        c.shadow(r, 6.0F, 4, prism::Color(0, 0, 0, 90));
        c.fillRoundRect(r, 6.0F, hovered ? t.controlHover : t.panel);
        // The icon on a deep backdrop, as a thumbnail is shown in the Asset Table.
        // The picture edge to edge across the tile, in the icon's own shape.
        const prism::Rect art{r.x + 1.0F, r.y + 1.0F, r.w - 2.0F, std::floor((r.w - 2.0F) * 128.0F / 152.0F)};
        c.fillRect(art, prism::Color(18, 19, 23));
        if (tile.icon != nullptr && tile.iconSize > 0) {
            c.blitBlend(tile.icon->data(), tile.iconSize, tile.iconHeight, tile.iconSize, art);
        } else {
            prism::drawIcon(c, prism::Icon::AnimClip, prism::Rect{art.x + art.w * 0.3F, art.y + art.h * 0.3F, art.w * 0.4F, art.h * 0.4F},
                            t.textDim, 1.5F);
        }
        // Its place in the library, and a warning when its file is gone.
        char number[16];
        std::snprintf(number, sizeof(number), "%zu", tile.entry + 1U);
        const prism::Rect badge{art.x + 4.0F, art.y + 4.0F, 22.0F, 16.0F};
        c.fillRoundRect(badge, 3.0F, prism::Color(0, 0, 0, 150));
        c.textIn(*t.small, badge, number, t.textDim, prism::HAlign::Center, false);
        if (tile.missing) {
            const prism::Rect warn{art.right() - 62.0F, art.y + 4.0F, 58.0F, 16.0F};
            c.fillRoundRect(warn, 3.0F, prism::Color(200, 70, 70));
            c.textIn(*t.small, warn, "Missing", t.textBright, prism::HAlign::Center, false);
        }
        // The type stripe, as the Asset Table's tiles.
        c.fillRect(prism::Rect{r.x, art.bottom() + 6.0F, r.w, 2.0F}, prism::Color(78, 201, 176));
        c.textIn(*t.semibold, prism::Rect{r.x + 9.0F, art.bottom() + 10.0F, r.w - 16.0F, 18.0F}, tile.name,
                 tile.missing ? t.textDim : t.textBright, prism::HAlign::Left, true);
        c.textIn(*t.small, prism::Rect{r.x + 9.0F, art.bottom() + 28.0F, r.w - 16.0F, 15.0F}, tile.detail, t.textDim,
                 prism::HAlign::Left, true);
        if (picked) {
            c.strokeRoundRect(r, 6.0F, t.highlight, 2);
        } else if (hovered) {
            c.strokeRoundRect(r, 6.0F, t.borderStrong, 1);
        }
    }
    // Where a dragged tile would go.
    if (m_dragging && m_hover != kNone && m_hover != m_pressed) {
        const prism::Rect r = TileRect(m_hover);
        const float x = m_hover < m_pressed ? r.x - kGap * 0.5F - 1.5F : r.right() + kGap * 0.5F - 1.5F;
        c.fillRoundRect(prism::Rect{x, r.y, 3.0F, r.h}, 1.5F, t.highlight);
    }
    if (m_dropHot) {
        c.strokeRoundRect(prism::Rect{rect().x + 3.0F, rect().y + 3.0F, rect().w - 6.0F, rect().h - 6.0F}, 6.0F, t.highlight, 2);
    }
}

bool AnimationLibraryGrid::onMouseDown(const prism::MouseEvent& e) {
    const std::size_t hit = Hit(e.pos);
    if (e.button == prism::MouseButton::Right) {
        if (hit != kNone) {
            m_selected = m_tiles[hit].entry;
            if (onPicked) {
                onPicked(m_selected);
            }
            if (onContext) {
                onContext(m_selected, e.pos);
            }
        }
        invalidate();
        return true;
    }
    if (e.button != prism::MouseButton::Left) {
        return false;
    }
    if (hit == kNone) {
        m_selected = kNone;
        invalidate();
        return true;
    }
    m_selected = m_tiles[hit].entry;
    if (onPicked) {
        onPicked(m_selected);
    }
    if (e.clicks >= 2) {
        if (onActivated) {
            onActivated(m_selected);
        }
        return true;
    }
    m_pressed = hit;
    m_pressAt = e.pos;
    m_dragging = false;
    invalidate();
    return true;
}

bool AnimationLibraryGrid::onMouseMove(const prism::MouseEvent& e) {
    const std::size_t hover = Hit(e.pos);
    if (m_pressed != kNone && !m_dragging && (e.pos - m_pressAt).length() > 6.0F) {
        m_dragging = true;
    }
    if (hover != m_hover || m_dragging) {
        m_hover = hover;
        invalidate();
    }
    return m_dragging;
}

bool AnimationLibraryGrid::onMouseUp(const prism::MouseEvent& e) {
    if (m_dragging && m_pressed != kNone) {
        const std::size_t target = Hit(e.pos);
        if (target != kNone && target != m_pressed && onReorder) {
            onReorder(m_tiles[m_pressed].entry, m_tiles[target].entry);
        }
    }
    m_pressed = kNone;
    m_dragging = false;
    invalidate();
    return true;
}

void AnimationLibraryGrid::onMouseLeave() {
    if (m_hover != kNone && !m_dragging) {
        m_hover = kNone;
        invalidate();
    }
}

bool AnimationLibraryGrid::onDragEnter(const prism::DragDropEvent& e) {
    return onDragOver(e);
}

bool AnimationLibraryGrid::onDragOver(const prism::DragDropEvent& e) {
    const std::string* path = e.operation && e.operation->tag() == EditorAssetTable::kDragTag
                                  ? e.operation->getPayload<std::string>()
                                  : nullptr;
    const bool accepts = path != nullptr && std::filesystem::path{*path}.extension() == ".uvanim";
    if (accepts != m_dropHot) {
        m_dropHot = accepts;
        invalidate();
    }
    return accepts;
}

void AnimationLibraryGrid::onDragLeave() {
    m_dropHot = false;
    invalidate();
}

bool AnimationLibraryGrid::onDrop(const prism::DragDropEvent& e) {
    const bool accepts = onDragOver(e);
    m_dropHot = false;
    invalidate();
    if (!accepts) {
        return false;
    }
    if (onDropped) {
        onDropped(*e.operation->getPayload<std::string>());
    }
    return true;
}

prism::Cursor AnimationLibraryGrid::cursor() const {
    return m_dragging ? prism::Cursor::SizeAll : (m_hover != kNone ? prism::Cursor::Hand : prism::Cursor::Arrow);
}

// ---------------------------------------------------------------------------------------- window

EditorAnimationLibraryWindow::EditorAnimationLibraryWindow(Editor& editor, EditorHost& host) : m_editor(editor), m_host(host) {}

EditorAnimationLibraryWindow::~EditorAnimationLibraryWindow() {
    if (m_window != 0 && m_host.IsReady()) {
        m_host.CloseWindow(m_window);
    }
}

void EditorAnimationLibraryWindow::Sync() {
    if (!m_host.IsReady()) {
        return;
    }
    const bool open = m_editor.m_animationLibraryWindow.has_value();
    const std::filesystem::path path = open ? m_editor.m_animationLibraryWindow->assetPath : std::filesystem::path{};
    if (m_window != 0 && (!open || path != m_openPath)) {
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_openPath.clear();
        m_grid.reset();
        m_filter.reset();
        m_count.reset();
        m_status.reset();
        m_tab.reset();
        m_addButton.reset();
        m_shownSignature.clear();
    }
    if (!open) {
        return;
    }
    if (m_window == 0) {
        m_openPath = path;
        m_window = m_host.OpenWindow(Build(), "Animation Library - " + path.filename().string(), kWindowWidth, kWindowHeight);
        if (m_window == 0) {
            m_editor.CloseAnimationLibraryWindow();
            return;
        }
        m_host.FocusWindow(m_window);
    }
    AnimationLibraryWindowState& state = *m_editor.m_animationLibraryWindow;
    if (state.focusPending) {
        state.focusPending = false;
        m_host.FocusWindow(m_window);
    }
    if (m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        m_editor.CloseAnimationLibraryWindow();   // every edit is on disk already
        return;
    }
    m_host.App().runInWindow(m_window, [this] { Refresh(); });
}

prism::Ref<prism::Widget> EditorAnimationLibraryWindow::Build() {
    auto window = prism::vbox();
    window->add(BuildTitleBar());
    m_grid = prism::make<AnimationLibraryGrid>();
    m_grid->onActivated = [this](const std::size_t entry) { Rename(entry); };
    m_grid->onContext = [this](const std::size_t entry, const prism::Vec2 at) { ShowEntryMenu(entry, at); };
    m_grid->onReorder = [this](const std::size_t from, const std::size_t to) { Move(from, to); };
    m_grid->onDropped = [this](const std::string& relative) {
        AddClips({m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot / relative});
    };
    auto scroll = prism::make<prism::ScrollBox>();
    scroll->setContent(m_grid);
    window->add(scroll, prism::Slot{1.0F});
    // Footer: what the last edit did.
    auto footer = prism::hbox(8.0F);
    footer->setPadding(prism::Margin(10.0F, 4.0F, 10.0F, 4.0F));
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    footer->add(m_status, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    auto hint = prism::make<prism::Label>("Double-click renames  -  drag a tile to reorder  -  right-click for more", prism::FontRole::Small);
    hint->setColor(prism::ThemeColor(&prism::Theme::textDisabled));
    footer->add(hint, Centre());
    auto footerBar = prism::make<prism::Border>();
    footerBar->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    footerBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    footerBar->setChild(footer);
    window->add(footerBar);
    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    return frame;
}

prism::Ref<prism::Widget> EditorAnimationLibraryWindow::BuildTitleBar() {
    m_tab = prism::make<DocumentTab>();
    m_tab->setFallbackIcon(prism::Icon::AnimLibrary);
    m_tab->setName(m_openPath.stem().string());

    EditorHeaderDesc desc;
    desc.documentTab = m_tab;
    desc.toolbarRow = BuildToolbar();
    desc.onClose = [this] { m_editor.CloseAnimationLibraryWindow(); };
    desc.menus.push_back({"File", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Add Animation...", [this] { ShowAddMenu(m_addButton ? m_addButton->rect() : prism::Rect{}); }, "",
                                              prism::Icon::Plus));
        items.push_back(prism::MenuItem::Item("Show in Asset Table", [this] {
            std::error_code error;
            const std::filesystem::path relative =
                std::filesystem::relative(m_openPath, m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot, error);
            if (!error) {
                m_editor.m_assetTableDirectory = relative.parent_path();
            }
        }, "", prism::Icon::Folder));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Close", [this] { m_editor.CloseAnimationLibraryWindow(); }, "", prism::Icon::Close));
        return items;
    }});
    desc.menus.push_back({"Edit", [this] {
        const std::size_t picked = m_grid ? m_grid->Selected() : AnimationLibraryGrid::kNone;
        const std::size_t count = m_editor.m_animationLibraryWindow.has_value() ? m_editor.m_animationLibraryWindow->library.entries.size() : 0U;
        const bool has = picked < count;
        prism::MenuItem rename = prism::MenuItem::Item("Rename...", [this, picked] { Rename(picked); }, "F2");
        rename.enabled = has;
        prism::MenuItem remove = prism::MenuItem::Item("Remove", [this, picked] { Remove(picked); }, "Del", prism::Icon::Trash);
        remove.enabled = has;
        return std::vector<prism::MenuItem>{std::move(rename), std::move(remove)};
    }});
    desc.menus.push_back({"Help", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("A library is a list of clips an Timeline can use by name.", {}).disabled(),
            prism::MenuItem::Item("Every edit is saved at once.", {}).disabled()};
    }});
    return BuildEditorHeader(std::move(desc));
}

prism::Ref<prism::Widget> EditorAnimationLibraryWindow::BuildToolbar() {
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 1.0F, 6.0F, 1.0F));
    auto add = prism::make<prism::Button>("Add Animation", prism::Icon::Plus, prism::ButtonStyle::Primary);
    add->setTooltip("Add clips of the project that are not in the library yet");
    std::weak_ptr<prism::Button> weakAdd = add;
    add->onClick = [this, weakAdd] {
        if (auto b = weakAdd.lock()) {
            ShowAddMenu(b->rect());
        }
    };
    m_addButton = add;
    row->add(add, Centre());
    auto rename = ToolButton(prism::Icon::NodeText, "Rename the picked clip's entry", "Rename");
    rename->onClick = [this] {
        if (m_grid) {
            Rename(m_grid->Selected());
        }
    };
    auto remove = ToolButton(prism::Icon::Trash, "Take the picked clip out of the library (the clip itself stays)", "Remove");
    remove->onClick = [this] {
        if (m_grid) {
            Remove(m_grid->Selected());
        }
    };
    row->add(rename, Centre());
    row->add(remove, Centre());
    row->add(HSeparator(), Centre());
    m_filter = prism::make<prism::TextInput>("", "Search clips");
    auto filterBox = prism::make<prism::SizeBox>();
    filterBox->setWidthOverride(220.0F);
    filterBox->setContent(m_filter);
    row->add(filterBox, Centre());
    m_count = prism::make<prism::Label>("", prism::FontRole::Small);
    m_count->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_count, prism::Slot{1.0F, prism::Margin(10.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

std::filesystem::path EditorAnimationLibraryWindow::ClipPath(const std::size_t entry) const {
    if (!m_editor.m_animationLibraryWindow.has_value() || entry >= m_editor.m_animationLibraryWindow->library.entries.size()) {
        return {};
    }
    std::filesystem::path path = m_editor.m_services->GetAssetDatabase().Resolve(m_editor.m_animationLibraryWindow->library.entries[entry].clip);
    if (!path.empty() && path.is_relative()) {
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            path = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot.parent_path() / path;
        }
    }
    return path;
}

void EditorAnimationLibraryWindow::Refresh() {
    if (!m_editor.m_animationLibraryWindow.has_value()) {
        return;
    }
    const AnimationLibraryWindowState& state = *m_editor.m_animationLibraryWindow;
    const std::string filter = m_filter ? Lower(m_filter->text()) : std::string{};
    // The tiles, rebuilt when the list, the filter or an icon changed.
    std::vector<AnimationLibraryGrid::Tile> tiles;
    std::string signature = filter + "|";
    for (std::size_t i = 0U; i < state.library.entries.size(); ++i) {
        const Asset::AnimationLibraryEntry& entry = state.library.entries[i];
        if (!filter.empty() && Lower(entry.name).find(filter) == std::string::npos) {
            continue;
        }
        AnimationLibraryGrid::Tile tile;
        tile.entry = i;
        tile.name = entry.name;
        const std::filesystem::path path = ClipPath(i);
        std::error_code error;
        tile.missing = path.empty() || !std::filesystem::is_regular_file(path, error);
        const Editor::AnimationIcon* icon = tile.missing ? nullptr : m_editor.GetAnimationIcon(path);
        if (icon != nullptr) {
            tile.icon = &icon->pixels;
            tile.iconSize = icon->width;
            tile.iconHeight = icon->height;
            char detail[96];
            std::snprintf(detail, sizeof(detail), "%.2f s  -  %zu frames", icon->durationSeconds, icon->frames);
            tile.detail = detail;
        } else {
            tile.detail = tile.missing ? "The clip's file is gone" : "Not a clip";
        }
        signature += entry.name + ":" + std::to_string(entry.clip.value) + ":" + std::to_string(icon != nullptr ? icon->serial : 0U) + ";";
        tiles.push_back(std::move(tile));
    }
    if (signature != m_shownSignature && m_grid) {
        m_shownSignature = signature;
        m_grid->SetTiles(std::move(tiles));
    }
    if (m_count) {
        const std::size_t count = state.library.entries.size();
        m_count->setText(std::to_string(count) + (count == 1U ? " clip" : " clips"));
    }
    if (m_status) {
        m_status->setText(state.status);
        m_status->setColor(state.statusIsError ? prism::ThemeColor(&prism::Theme::error) : prism::ThemeColor(&prism::Theme::textDim));
    }
}

bool EditorAnimationLibraryWindow::Save(const std::string& done) {
    AnimationLibraryWindowState& state = *m_editor.m_animationLibraryWindow;
    if (Asset::SaveAnimationLibraryAsset(state.library, state.assetPath)) {
        state.status = done;
        state.statusIsError = false;
        return true;
    }
    // The list never shows what is not on disk.
    state.status = "Could not write " + state.assetPath.filename().string() + ".";
    state.statusIsError = true;
    Asset::AnimationLibraryAsset rollback;
    if (Asset::LoadAnimationLibraryAsset(state.assetPath, rollback)) {
        state.library = std::move(rollback);
    }
    return false;
}

void EditorAnimationLibraryWindow::AddClips(const std::vector<std::filesystem::path>& absolute) {
    if (!m_editor.m_animationLibraryWindow.has_value()) {
        return;
    }
    const std::filesystem::path library = m_editor.m_animationLibraryWindow->assetPath;
    const std::size_t added = m_editor.AppendClipsToAnimationLibrary(library, absolute);
    m_editor.RefreshAnimationLibraryWindow(library);
    if (m_editor.m_animationLibraryWindow.has_value()) {
        m_editor.m_animationLibraryWindow->status = added > 0U ? (added == 1U ? "Added 1 clip." : "Added " + std::to_string(added) + " clips.")
                                                               : "Nothing new to add: only .uvanim clips not already listed join.";
        m_editor.m_animationLibraryWindow->statusIsError = false;
    }
}

void EditorAnimationLibraryWindow::ShowAddMenu(const prism::Rect anchor) {
    if (!m_editor.m_animationLibraryWindow.has_value()) {
        return;
    }
    // The project's clips the library does not have yet.
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    Asset::IAssetDatabase& database = m_editor.m_services->GetAssetDatabase();
    const Asset::AnimationLibraryAsset& library = m_editor.m_animationLibraryWindow->library;
    std::vector<std::filesystem::path> fresh;
    for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKind::Directory || entry.relativePath.extension() != ".uvanim") {
            continue;
        }
        const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
        const Asset::AssetGuid guid = database.Register(absolute);
        if (std::none_of(library.entries.begin(), library.entries.end(), [guid](const Asset::AnimationLibraryEntry& e) { return e.clip == guid; })) {
            fresh.push_back(absolute);
        }
    }
    std::sort(fresh.begin(), fresh.end());
    std::vector<prism::MenuItem> items;
    for (const std::filesystem::path& clip : fresh) {
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(clip, snapshot.contentRoot, error);
        items.push_back(prism::MenuItem::Item(error ? clip.stem().string() : relative.generic_string(), [this, clip] { AddClips({clip}); }, "",
                                              prism::Icon::AnimClip));
    }
    if (fresh.size() > 1U) {
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Add All (" + std::to_string(fresh.size()) + ")", [this, fresh] { AddClips(fresh); }, "",
                                              prism::Icon::Plus));
    }
    if (items.empty()) {
        items.push_back(prism::MenuItem::Item("Every clip of the project is in the library", {}).disabled());
    }
    prism::showMenu(std::move(items), anchor, prism::PopupPlacement::Below);
}

void EditorAnimationLibraryWindow::ShowEntryMenu(const std::size_t entry, const prism::Vec2 at) {
    if (!m_editor.m_animationLibraryWindow.has_value() || entry >= m_editor.m_animationLibraryWindow->library.entries.size()) {
        return;
    }
    const std::size_t count = m_editor.m_animationLibraryWindow->library.entries.size();
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item("Rename...", [this, entry] { Rename(entry); }, "", prism::Icon::NodeText));
    prism::MenuItem first = prism::MenuItem::Item("Move to Start", [this, entry] { Move(entry, 0U); });
    first.enabled = entry > 0U;
    prism::MenuItem last = prism::MenuItem::Item("Move to End", [this, entry, count] { Move(entry, count - 1U); });
    last.enabled = entry + 1U < count;
    items.push_back(std::move(first));
    items.push_back(std::move(last));
    items.push_back(prism::MenuItem::Item("Show Clip in Asset Table", [this, entry] {
        std::error_code error;
        const std::filesystem::path relative =
            std::filesystem::relative(ClipPath(entry), m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot, error);
        if (!error) {
            m_editor.m_assetTableDirectory = relative.parent_path();
        }
    }, "", prism::Icon::Folder));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Remove from Library", [this, entry] { Remove(entry); }, "Del", prism::Icon::Trash));
    prism::showContextMenu(std::move(items), at);
}

void EditorAnimationLibraryWindow::Rename(const std::size_t entry) {
    if (m_window == 0 || !m_editor.m_animationLibraryWindow.has_value() || entry >= m_editor.m_animationLibraryWindow->library.entries.size()) {
        return;
    }
    const std::string current = m_editor.m_animationLibraryWindow->library.entries[entry].name;
    m_host.App().runInWindow(m_window, [this, entry, current] {
        auto field = prism::make<prism::TextInput>(current, "Name");
        std::weak_ptr<prism::TextInput> weak = field;
        auto applied = std::make_shared<bool>(false);
        const auto apply = [this, entry, weak, applied] {
            const auto f = weak.lock();
            if (*applied || !f || !m_editor.m_animationLibraryWindow.has_value()) {
                return;
            }
            *applied = true;
            AnimationLibraryWindowState& state = *m_editor.m_animationLibraryWindow;
            std::string name = f->text();
            if (name.empty() || entry >= state.library.entries.size()) {
                state.status = name.empty() ? "Give the entry a name." : "That entry is gone; nothing was renamed.";
                state.statusIsError = true;
                return;
            }
            if (name.size() > Asset::kMaximumAnimationLibraryIdentifierBytes) {
                name.resize(Asset::kMaximumAnimationLibraryIdentifierBytes);
            }
            state.library.entries[entry].name = name;
            static_cast<void>(Save("Renamed to '" + name + "'."));
        };
        field->onCommit = [apply](const std::string&) {
            apply();
            if (prism::Application::exists()) {
                prism::Application::get().closeAllPopups();
            }
        };
        prism::showDialog("Rename Entry", field, {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"Rename", true, apply}}, 360.0F);
        if (prism::Application::exists()) {
            prism::Application::get().setFocus(field.get());
        }
    });
}

void EditorAnimationLibraryWindow::Remove(const std::size_t entry) {
    if (!m_editor.m_animationLibraryWindow.has_value() || entry >= m_editor.m_animationLibraryWindow->library.entries.size()) {
        return;
    }
    AnimationLibraryWindowState& state = *m_editor.m_animationLibraryWindow;
    const std::string name = state.library.entries[entry].name;
    state.library.entries.erase(state.library.entries.begin() + static_cast<std::ptrdiff_t>(entry));
    static_cast<void>(Save("Removed '" + name + "'."));
    if (m_grid) {
        m_grid->SetSelected(AnimationLibraryGrid::kNone);
    }
}

void EditorAnimationLibraryWindow::Move(const std::size_t from, const std::size_t to) {
    if (!m_editor.m_animationLibraryWindow.has_value()) {
        return;
    }
    std::vector<Asset::AnimationLibraryEntry>& entries = m_editor.m_animationLibraryWindow->library.entries;
    if (from >= entries.size() || to >= entries.size() || from == to) {
        return;
    }
    Asset::AnimationLibraryEntry moving = std::move(entries[from]);
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(from));
    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
    static_cast<void>(Save("Moved to place " + std::to_string(to + 1U) + "."));
    if (m_grid) {
        m_grid->SetSelected(to);
    }
}

void EditorAnimationLibraryWindow::Render() {
    if (m_window == 0) {
        return;
    }
    m_host.RenderWindow(m_window, {});
}

} // namespace UVE::Editor
