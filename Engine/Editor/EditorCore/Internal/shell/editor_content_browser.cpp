// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_content_browser.h"
#include <iterator>
#include <fstream>
#include "blueprint_asset.h"

#include "editor_asset_thumbnail.h"

#include "editor.h"
#include "editor_content_browser_model.h"
#include "editor_content_catalogue.h"
#include "mesh_component.h"

#include "animation_clip_asset.h"
#include "animation_montage_asset.h"
#include "animation_montage_editing.h"
#include "i_asset_database.h"
#include "i_project_file_index.h"
#include "material_asset.h"
#include "montage_library_asset.h"

#include "prism/Application.h"

#include <GL/gl.h>
#include "prism/AssetBrowser.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Icons.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/SearchMenu.h"
#include "prism/TableView.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <system_error>

namespace UVE::Editor {
namespace {

void HashUVE(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void HashUVE(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        HashUVE(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] std::string LowerExtensionUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

[[nodiscard]] bool IsEntityAssetUVE(const std::filesystem::path& path) {
    const std::string extension = LowerExtensionUVE(path);
    return extension == ".uventity" || extension == ".uvprefab";
}

[[nodiscard]] std::string FormatSizeUVE(const std::uintmax_t bytes) {
    char buffer[32];
    if (bytes >= 1024U * 1024U) {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    } else if (bytes >= 1024U) {
        std::snprintf(buffer, sizeof(buffer), "%.1f KB", static_cast<double>(bytes) / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%ju B", bytes);
    }
    return buffer;
}

[[nodiscard]] std::string FormatTimeUVE(const std::int64_t seconds) {
    if (seconds <= 0) {
        return "-";
    }
    const std::time_t t = static_cast<std::time_t>(seconds);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

[[nodiscard]] std::int64_t StartOfTodayUVE() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    return static_cast<std::int64_t>(std::mktime(&local));
}

/// Data columns of the Details table, and the sort key each one stands for.
/// The narrowest the sidebar gets side by side: its create row ("+ Add | Save All | Import") fits.
constexpr float kSidebarMinWidthUVE = 262.0F;

constexpr std::array<std::pair<const char*, ContentSortKeyUVE>, 5> kDetailsColumnsUVE{{
    {"Name", ContentSortKeyUVE::Name},
    {"Type", ContentSortKeyUVE::Type},
    {"Size", ContentSortKeyUVE::Size},
    {"Modified", ContentSortKeyUVE::Modified},
    {"Folder", ContentSortKeyUVE::Folder},
}};

/// The tile grid: tiles fill each row edge to edge and re-flow with the width. A narrow area still
/// fits at least kMinColumnsUVE tiles (they shrink, never below kMinTileUVE); a wide one fits as
/// many of the chosen size as there is room for. List rows keep their height and fill columns.
class TileGridUVE final : public prism::Widget {
public:
    static constexpr int kMinColumnsUVE = 4;
    static constexpr float kMinTileUVE = 56.0F;
    static constexpr float kGapUVE = 6.0F;

    TileGridUVE(const prism::Vec2 baseSize, const bool rows) : m_base(baseSize), m_rows(rows) {}
    void add(prism::Ref<prism::Widget> w) { addChild(std::move(w)); }

    /// Columns and tile width for a row `width` wide.
    [[nodiscard]] static std::pair<int, float> Fit(const float width, const float baseWidth, const bool rows) {
        const float gap = rows ? 2.0F : kGapUVE;
        int columns = std::max(1, static_cast<int>(std::floor((width + gap) / (baseWidth + gap))));
        if (!rows) {
            columns = std::max(columns, kMinColumnsUVE);
            const int fitMin = std::max(1, static_cast<int>(std::floor((width + gap) / (kMinTileUVE + gap))));
            columns = std::min(columns, std::max(1, fitMin));
        }
        const float tile = std::max(rows ? 120.0F : kMinTileUVE, (width - gap * static_cast<float>(columns - 1)) /
                                                                       static_cast<float>(columns));
        return {columns, tile};
    }

    prism::Vec2 measure(const prism::Vec2 avail) override {
        const float width = std::max(avail.x, 1.0F);
        const auto [columns, tile] = Fit(width, m_base.x, m_rows);
        const float tileH = m_rows ? m_base.y : std::floor(tile * (m_base.y / m_base.x));
        const int count = static_cast<int>(children().size());
        const int lines = (count + columns - 1) / std::max(1, columns);
        const float gap = m_rows ? 1.0F : kGapUVE;
        for (const auto& child : children()) {
            child->measure(prism::Vec2(tile, tileH));
        }
        return m_desired = constrain(prism::Vec2(width, lines > 0 ? lines * tileH + (lines - 1) * gap : 0.0F));
    }
    void arrange(const prism::Rect r) override {
        prism::Widget::arrange(r);
        const auto [columns, tile] = Fit(r.w, m_base.x, m_rows);
        const float tileH = m_rows ? m_base.y : std::floor(tile * (m_base.y / m_base.x));
        const float gapX = m_rows ? 2.0F : kGapUVE;
        const float gapY = m_rows ? 1.0F : kGapUVE;
        int i = 0;
        for (const auto& child : children()) {
            const int col = i % columns;
            const int line = i / columns;
            child->arrange(prism::Rect{std::floor(r.x + col * (tile + gapX)), std::floor(r.y + line * (tileH + gapY)),
                                       std::floor(tile), tileH});
            ++i;
        }
    }

private:
    prism::Vec2 m_base;
    bool m_rows = false;
};

} // namespace

prism::Icon EditorContentBrowserUVE::IconForUVE(const ItemTypeUVE type) {
    switch (type) {
        case ItemTypeUVE::Folder: return prism::Icon::Folder;
        case ItemTypeUVE::Scene: return prism::Icon::SceneAsset;
        case ItemTypeUVE::Prefab: return prism::Icon::Prefab;
        case ItemTypeUVE::Entity: return prism::Icon::EntityAsset;
        case ItemTypeUVE::Bundle: return prism::Icon::Bundle;
        case ItemTypeUVE::Mesh: return prism::Icon::MeshAsset;
        case ItemTypeUVE::Model: return prism::Icon::Model;
        case ItemTypeUVE::Texture: return prism::Icon::NodeTexture;
        case ItemTypeUVE::Shader: return prism::Icon::Shader;
        case ItemTypeUVE::Material: return prism::Icon::Material;
        case ItemTypeUVE::Save: return prism::Icon::Save;
        case ItemTypeUVE::Animation: return prism::Icon::AnimClip;
        case ItemTypeUVE::AnimationLibrary: return prism::Icon::AnimLibrary;
        case ItemTypeUVE::AnimationMontage: return prism::Icon::AnimMontage;
        case ItemTypeUVE::MontageLibrary: return prism::Icon::MontageLibrary;
        case ItemTypeUVE::Blueprint: return prism::Icon::BlueprintClass;
        case ItemTypeUVE::WidgetBlueprint: return prism::Icon::BlueprintClass;
        case ItemTypeUVE::Script: return prism::Icon::Script;
        case ItemTypeUVE::Audio: return prism::Icon::NodeSound;
        case ItemTypeUVE::Font: return prism::Icon::NodeText;
        case ItemTypeUVE::File: return prism::Icon::File;
    }
    return prism::Icon::File;
}

std::pair<std::string, std::string> EditorContentBrowserUVE::DescribeUVE(const ItemTypeUVE type) {
    switch (type) {
        case ItemTypeUVE::Folder: return {"Folder", "DIR"};
        case ItemTypeUVE::Scene: return {"World Level", "LVL"};
        case ItemTypeUVE::Prefab: return {"Prefab", "PFB"};
        case ItemTypeUVE::Entity: return {"Entity", "ENT"};
        case ItemTypeUVE::Bundle: return {"Bundle", "BND"};
        case ItemTypeUVE::Mesh: return {"Static Mesh", "SM"};
        case ItemTypeUVE::Model: return {"Skeletal Mesh", "SK"};
        case ItemTypeUVE::Texture: return {"Texture 2D", "T2D"};
        case ItemTypeUVE::Shader: return {"Shader", "SH"};
        case ItemTypeUVE::Material: return {"Material", "MAT"};
        case ItemTypeUVE::Save: return {"Save", "SAV"};
        case ItemTypeUVE::Animation: return {"Animation", "ANM"};
        case ItemTypeUVE::AnimationLibrary: return {"Animation Library", "LIB"};
        case ItemTypeUVE::AnimationMontage: return {"Animation Montage", "MONT"};
        case ItemTypeUVE::MontageLibrary: return {"Montage Library", "MLIB"};
        case ItemTypeUVE::Blueprint: return {"Blueprint Class", "BP"};
        case ItemTypeUVE::WidgetBlueprint: return {"Widget Blueprint", "WBP"};
        case ItemTypeUVE::Script: return {"Script", "UVS"};
        case ItemTypeUVE::Audio: return {"Audio", "AUD"};
        case ItemTypeUVE::Font: return {"Font", "FNT"};
        case ItemTypeUVE::File: return {"File", "FILE"};
    }
    return {"File", "FILE"};
}

prism::Color EditorContentBrowserUVE::TintForUVE(const ItemTypeUVE type) {
    switch (type) {
        case ItemTypeUVE::Folder: return prism::Color(214, 176, 92);
        case ItemTypeUVE::Scene: return prism::Color(120, 170, 235);
        case ItemTypeUVE::Entity:
        case ItemTypeUVE::Prefab: return prism::Color(110, 200, 160);
        case ItemTypeUVE::Mesh:
        case ItemTypeUVE::Model: return prism::Color(160, 200, 230);
        case ItemTypeUVE::Texture: return prism::Color(220, 140, 110);
        case ItemTypeUVE::Material: return prism::Color(200, 160, 110);
        case ItemTypeUVE::Script:
        case ItemTypeUVE::Shader: return prism::Color(170, 150, 230);
        case ItemTypeUVE::Animation:
        case ItemTypeUVE::AnimationLibrary:
        case ItemTypeUVE::AnimationMontage:
        case ItemTypeUVE::MontageLibrary: return prism::Color(90, 200, 200);
        case ItemTypeUVE::Audio: return prism::Color(230, 190, 90);
        case ItemTypeUVE::Blueprint: return prism::Color(80, 150, 240);
        case ItemTypeUVE::WidgetBlueprint: return prism::Color(70, 190, 140);
        default: return prism::Color(170, 176, 186);
    }
}



/// One item in the tile area. Assets are PRISM's asset tile around the engine's preview; folders
/// are a large folder glyph with the name under it (no card), the way a content search shows them.
/// A right click opens the item's menu; dragging it starts a content drag (to the Outliner, ...).
class EditorContentBrowserUVE::TileUVE final : public prism::Widget {
public:
    TileUVE(EditorContentBrowserUVE& owner, const std::size_t entryIndex, std::string name, const ItemTypeUVE type,
            const bool selected, const std::uint32_t* preview, const int previewW, const int previewH, const float scale,
            const prism::Vec2 size, const bool row)
        : m_owner(owner), m_entryIndex(entryIndex), m_name(std::move(name)), m_type(type), m_selected(selected),
          m_preview(preview), m_previewW(previewW), m_previewH(previewH), m_previewScale(scale), m_size(size),
          m_row(row) {}

    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain(m_size); }
    void setSelected(const bool selected) {
        if (selected != m_selected) {
            m_selected = selected;
            invalidate();
        }
    }

    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        const prism::Rect r = rect();
        if (m_row) {
            // List: icon and name on one line.
            if (m_selected || m_hot) {
                c.fillRoundRect(r, 3.0F, m_selected ? t.rowSelected : t.rowHover);
            }
            const EditorIconsUVE::ImageUVE icon = m_owner.TypeIconUVE(m_type, 16);
            const prism::Rect box{r.x + 6.0F, std::floor(r.y + (r.h - 16.0F) * 0.5F), 16.0F, 16.0F};
            if (icon) {
                c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, box);
            }
            c.textIn(*t.ui, prism::Rect{r.x + 28.0F, r.y, r.w - 32.0F, r.h}, m_name, t.text, prism::HAlign::Left, true);
            return;
        }
        if (m_type == ItemTypeUVE::Folder) {
            if (m_selected || m_hot) {
                c.fillRoundRect(r, 4.0F, m_selected ? t.rowSelected : t.rowHover);
            }
            const float labelH = 30.0F;
            const float side = std::min(r.w - 12.0F, r.h - labelH - 6.0F);
            const prism::Rect box{std::floor(r.x + (r.w - side) * 0.5F), r.y + 6.0F, side, side};
            PaintFolderUVE(c, box);
            c.textIn(*t.ui, prism::Rect{r.x + 2.0F, r.bottom() - labelH, r.w - 4.0F, labelH}, m_name, t.textBright,
                     prism::HAlign::Center, true);
            return;
        }
        // PRISM's content tile and thumbnail, fed with the engine's own classification of the file.
        prism::AssetRecord record{};
        record.name = m_name;
        record.path = m_name;
        record.icon = IconForUVE(m_type);
        record.badgeColor = TintForUVE(m_type);
        const auto [typeName, badge] = DescribeUVE(m_type);
        record.type = typeName;
        record.badgeText = badge;
        record.thumbnail = m_preview;
        record.thumbnailW = m_previewW;
        record.thumbnailH = m_previewH;
        record.thumbnailScale = m_previewScale;
        prism::paintAssetTile(c, r, record, m_selected, m_hot);
        if (m_type == ItemTypeUVE::Blueprint || m_type == ItemTypeUVE::WidgetBlueprint) {
            // A Blueprint's well is the word, not a glyph.
            const prism::Rect well{r.x + 3.0F, r.y + 3.0F, r.w - 6.0F, 86.0F - 17.0F};
            c.fillRect(well, m_hot && !m_selected ? prism::Color(40, 41, 45) : prism::Color(30, 31, 34));
            c.textIn(*t.medium, well, m_type == ItemTypeUVE::Blueprint ? "Blueprint" : "Widget Blueprint", prism::Color(214, 220, 230),
                     prism::HAlign::Center, false);
        }
    }

    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button == prism::MouseButton::Left) {
            m_owner.SelectEntryUVE(m_entryIndex, (e.mods & prism::ModCtrl) != 0, (e.mods & prism::ModShift) != 0);
            if (e.clicks >= 2) {
                m_pressed = false;
                m_owner.OpenEntryUVE(m_entryIndex);
                return true;
            }
            m_pressed = true;
            m_pressPos = e.pos;
            return true;
        }
        if (e.button == prism::MouseButton::Right) {
            if (!m_selected) {
                m_owner.SelectEntryUVE(m_entryIndex, false, false);
            }
            m_owner.ShowItemMenuUVE(m_entryIndex, e.pos);
            return true;
        }
        return false;
    }
    bool onMouseMove(const prism::MouseEvent& e) override {
        if (m_pressed && (e.pos - m_pressPos).length() > 6.0F) {
            m_pressed = false;
            m_owner.BeginDragUVE(m_entryIndex);
            return true;
        }
        if (!m_hot) {
            m_hot = true;
            invalidate();
        }
        return false;
    }
    bool onMouseUp(const prism::MouseEvent&) override {
        m_pressed = false;
        return true;
    }
    void onMouseLeave() override {
        m_hot = false;
        invalidate();
    }

private:
    /// A filled folder, shaded: the tab and back in the darker gold-brown, the front lighter with a
    /// highlight along its top edge and a soft shadow under it.
    static void PaintFolderUVE(prism::Canvas& c, const prism::Rect b) {
        const prism::Color back(138, 96, 34);
        const prism::Color front(196, 142, 58);
        const prism::Color frontLow(170, 118, 44);
        const float w = b.w;
        const float h = b.h * 0.78F;
        const float x = b.x;
        const float y = b.y + (b.h - h) * 0.5F;
        c.fillRoundRect(prism::Rect{x + 3.0F, y + h * 0.18F + 4.0F, w - 4.0F, h * 0.82F}, 6.0F, prism::Color(0, 0, 0, 70));
        // Back and tab.
        c.fillRoundRect(prism::Rect{x, y, w * 0.42F, h * 0.24F}, 4.0F, back);
        c.fillRoundRect(prism::Rect{x, y + h * 0.10F, w, h * 0.90F}, 5.0F, back);
        // Front, a shade lighter, with a vertical fall-off and a highlight on its top edge.
        const prism::Rect frontRect{x, y + h * 0.26F, w, h * 0.74F};
        c.fillRoundRect(frontRect, 5.0F, front);
        c.gradientV(prism::Rect{frontRect.x + 2.0F, frontRect.y + frontRect.h * 0.45F, frontRect.w - 4.0F,
                                frontRect.h * 0.55F - 3.0F},
                    front, frontLow);
        c.fillRect(prism::Rect{frontRect.x + 4.0F, frontRect.y + 1.0F, frontRect.w - 8.0F, 1.5F}, prism::Color(240, 196, 120, 200));
    }

    EditorContentBrowserUVE& m_owner;
    std::size_t m_entryIndex = 0;
    std::string m_name;
    ItemTypeUVE m_type = ItemTypeUVE::File;
    bool m_selected = false;
    bool m_hot = false;
    bool m_pressed = false;
    prism::Vec2 m_pressPos{0.0F, 0.0F};
    const std::uint32_t* m_preview = nullptr;
    int m_previewW = 0;
    int m_previewH = 0;
    float m_previewScale = 1.0F;
    prism::Vec2 m_size{108.0F, 136.0F};
    bool m_row = false;
};

/// The item area: a click on empty space clears the pick, a right click opens the create menu.
class EditorContentBrowserUVE::ItemsAreaUVE final : public prism::Border {
public:
    explicit ItemsAreaUVE(EditorContentBrowserUVE& owner) : m_owner(owner) {}
    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button == prism::MouseButton::Right) {
            m_owner.ShowEmptyMenuUVE(e.pos);
            return true;
        }
        if (e.button == prism::MouseButton::Left) {
            m_owner.m_editor.m_contentSelection.ClearUVE();
            m_owner.m_editor.m_selectedProjectFile.reset();
            m_owner.m_editor.m_selectedAsset.reset();
            m_owner.ChangedUVE();
            return true;
        }
        return false;
    }

private:
    EditorContentBrowserUVE& m_owner;
};

EditorContentBrowserUVE::EditorContentBrowserUVE(EditorUVE& editor) : m_editor(editor) {}

EditorContentBrowserUVE::~EditorContentBrowserUVE() = default;

prism::Ref<prism::Widget> EditorContentBrowserUVE::SectionHeaderUVE(const std::string& title, const bool open,
                                                                      std::shared_ptr<prism::Widget> body,
                                                                      std::shared_ptr<prism::Widget> extra) {
    auto row = prism::hbox(2.0F);
    row->setPadding(prism::Margin(2.0F, 1.0F, 4.0F, 1.0F));
    auto toggle = prism::make<prism::Button>(title, open ? prism::Icon::TriDown : prism::Icon::TriRight,
                                             prism::ButtonStyle::Flat);
    std::weak_ptr<prism::Button> weakToggle = toggle;
    std::weak_ptr<prism::Widget> weakBody = body;
    toggle->onClick = [weakToggle, weakBody] {
        auto t = weakToggle.lock();
        auto b = weakBody.lock();
        if (!t || !b) {
            return;
        }
        b->setVisible(!b->visible());
        t->setIcon(b->visible() ? prism::Icon::TriDown : prism::Icon::TriRight);
    };
    row->add(toggle, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    if (extra) {
        row->add(extra, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    }
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

prism::Ref<prism::Widget> EditorContentBrowserUVE::BuildUVE() {
    // ---- sidebar: Create (catalogue with icons), Favorites, Content (folders), Shelves.
    auto left = prism::vbox(0.0F);
    const auto sized = [](prism::Ref<prism::Widget> w, const float h) {
        auto box = prism::make<prism::SizeBox>();
        box->setHeightOverride(h);
        box->setContent(std::move(w));
        return box;
    };

    // The create row: "+ Add" opens the whole catalogue as a menu (the same menu as a right click
    // on empty space), with Save All and Import beside it.
    auto addButton = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Success);
    addButton->setTooltip("Create in this folder: folders, scenes, entities, scripts, materials ...");
    std::weak_ptr<prism::Button> weakAdd = addButton;
    addButton->onClick = [this, weakAdd] {
        const auto anchor = weakAdd.lock();
        prism::showSearchMenu(CatalogueMenuUVE(), anchor ? anchor->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                              "Start typing to search", true);
    };
    auto saveAllButton = prism::make<prism::Button>("Save All", prism::Icon::Save, prism::ButtonStyle::Toolbar);
    saveAllButton->setTooltip("Save everything (Ctrl+Shift+S)");
    saveAllButton->onClick = [this] { static_cast<void>(m_editor.RunEditorCommandUVE("file.saveAll")); };
    auto createRow = prism::hbox(4.0F);
    createRow->setPadding(prism::Margin(4.0F, 3.0F, 4.0F, 3.0F));
    createRow->add(addButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    createRow->add(saveAllButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    createRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto createBar = prism::make<prism::Border>();
    createBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    createBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    createBar->setChild(createRow);


    m_favorites = prism::make<prism::TreeView>();
    m_favorites->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (m_syncingFolders || nodes.empty()) {
            return;
        }
        const int id = nodes.front()->id;
        if (id > 0 && static_cast<std::size_t>(id) <= m_favoriteIds.size()) {
            RevealUVE(m_favoriteIds[static_cast<std::size_t>(id - 1)]);
        }
    };
    m_favorites->onContext = [this](prism::TreeNode* node, const prism::Vec2 at) {
        if (node == nullptr || node->id <= 0 || static_cast<std::size_t>(node->id) > m_favoriteIds.size()) {
            return;
        }
        const std::filesystem::path path = m_favoriteIds[static_cast<std::size_t>(node->id - 1)];
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Show in Folder", [this, path] { RevealUVE(path); }));
        items.push_back(prism::MenuItem::Item("Unpin", [this, path] {
            m_editor.ToggleProjectPathFavoriteUVE(path);
            m_favoriteSignature = 0;
        }));
        prism::showMenu(std::move(items), prism::Rect{at.x, at.y, 1.0F, 1.0F}, prism::PopupPlacement::Below);
    };
    auto favoritesSection = prism::vbox(0.0F);
    favoritesSection->add(SectionHeaderUVE("Favorites", true, m_favorites, nullptr));
    favoritesSection->add(m_favorites, prism::Slot{1.0F});

    m_folderSearch = prism::make<prism::TextInput>("", "Search folders");
    m_folderSearch->onChanged = [this](const std::string& text) {
        m_editor.m_contentTreeFilter = text;
        m_folderSignature = 0;
    };
    m_folderSearch->setVisible(false);
    auto searchToggle = prism::make<prism::Button>("", prism::Icon::Search, prism::ButtonStyle::Flat);
    searchToggle->setTooltip("Search folders");
    searchToggle->onClick = [this] {
        m_folderSearch->setVisible(!m_folderSearch->visible());
        if (!m_folderSearch->visible()) {
            m_folderSearch->setText("", true);
        } else if (prism::Application::exists()) {
            prism::Application::get().setFocus(m_folderSearch.get());
        }
    };
    m_folders = prism::make<prism::TreeView>();
    m_folders->setShowGuides(true);
    m_folders->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (m_syncingFolders || nodes.empty()) {
            return;
        }
        const int id = nodes.front()->id;
        if (id > 0 && static_cast<std::size_t>(id) <= m_folderIds.size()) {
            GoToFolderUVE(std::filesystem::path(m_folderIds[static_cast<std::size_t>(id - 1)]));
        }
    };
    auto contentSection = prism::vbox(0.0F);
    contentSection->add(SectionHeaderUVE("Content", true, m_folders, searchToggle));
    contentSection->add(m_folderSearch, prism::Slot{0.0F, prism::Margin(4.0F, 2.0F, 4.0F, 2.0F)});
    contentSection->add(m_folders, prism::Slot{1.0F});

    m_shelves = prism::make<prism::TreeView>();
    m_shelves->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (m_syncingFolders || nodes.empty()) {
            return;
        }
        const int id = nodes.front()->id;
        if (id > 0 && static_cast<std::size_t>(id) <= m_shelfNames.size() &&
            !m_shelfNames[static_cast<std::size_t>(id - 1)].empty()) {
            m_editor.m_contentBrowserShelf = m_shelfNames[static_cast<std::size_t>(id - 1)];
            m_editor.m_contentSelection.ClearUVE();
            ChangedUVE();
        }
    };
    auto shelvesBody = sized(m_shelves, 96.0F);
    shelvesBody->setVisible(false);
    auto shelvesSection = prism::vbox(0.0F);
    shelvesSection->add(SectionHeaderUVE("Shelves", false, shelvesBody, nullptr));
    shelvesSection->add(shelvesBody);
    // The sections share the sidebar through dividers, so each can be made taller or shorter.
    auto sections = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    sections->add(favoritesSection, prism::Splitter::Pane{66.0F, 24.0F, 400.0F, false});
    sections->add(contentSection, prism::Splitter::Pane{160.0F, 60.0F, 1e9F, true});
    left->add(createBar);
    left->add(sections, prism::Slot{1.0F});
    left->add(shelvesSection);
    auto sidebar = prism::make<prism::Border>();
    sidebar->setBackground(prism::ThemeColor(&prism::Theme::panel));
    sidebar->setChild(left);
    m_sidebar = sidebar;

    // ---- items: View | Split | Filter | search, path, the mode's view, status.
    auto right = prism::vbox(4.0F);
    right->setPadding(prism::Margin(4.0F));
    auto bar = prism::hbox(4.0F);
    m_viewButton = prism::make<prism::Button>("View", prism::Icon::Settings, prism::ButtonStyle::Toolbar);
    m_viewButton->setTooltip("How items are shown: Tiles, Columns, Details, Recent, Board; size and sort");
    m_viewButton->onClick = [this] { ShowViewMenuUVE(); };
    m_splitButton = prism::make<prism::Button>("Split", prism::Icon::Frame, prism::ButtonStyle::Toolbar);
    m_splitButton->setTooltip("How the sidebar and the items share the Content Browser");
    m_splitButton->onClick = [this] { ShowSplitMenuUVE(); };
    m_filterButton = prism::make<prism::Button>("Filter", prism::Icon::Filter, prism::ButtonStyle::Toolbar);
    m_filterButton->onClick = [this] { ShowFilterMenuUVE(); };
    m_itemSearch = prism::make<prism::TextInput>("", "Search items");
    m_itemSearch->setLeadingIcon(prism::Icon::Search);
    m_itemSearch->onChanged = [this](const std::string& text) {
        m_editor.m_assetFilter = text;
        ChangedUVE();
    };
    bar->add(m_viewButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_splitButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_filterButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_itemSearch, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    right->add(bar);
    m_path = prism::make<prism::Label>("Content", prism::FontRole::Small);
    m_path->setColor(prism::ThemeColor(&prism::Theme::textDim));
    right->add(m_path);
    m_itemsHost = prism::make<ItemsAreaUVE>(*this);
    right->add(m_itemsHost, prism::Slot{1.0F});
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    right->add(m_status);
    m_items = right;

    m_root = prism::make<prism::Border>();
    m_splitMode = m_editor.m_contentBrowserSplitModeUVE ? SplitModeUVE::SideBySide : SplitModeUVE::ItemsOnly;
    ApplySplitModeUVE();
    return m_root;
}

void EditorContentBrowserUVE::ApplySplitModeUVE() {
    if (!m_root || !m_sidebar || !m_items) {
        return;
    }
    // Take both panes out of whatever holds them now, then lay them out for the mode.
    if (m_split) {
        m_split->removeChild(m_sidebar.get());
        m_split->removeChild(m_items.get());
        m_split.reset();
    }
    m_root->clearChildren();
    m_sidebar->setVisible(true);
    m_items->setVisible(true);
    const float ratio = std::clamp(m_editor.m_contentBrowserSplitRatio, 0.12F, 0.7F);
    switch (m_splitMode) {
        case SplitModeUVE::SideBySide:
        case SplitModeUVE::Stacked: {
            const bool stacked = m_splitMode == SplitModeUVE::Stacked;
            m_split = prism::make<prism::Splitter>(stacked ? prism::Orientation::Vertical : prism::Orientation::Horizontal);
            // Wide enough for the "+ Add | Save All | Import" row.
            m_split->add(m_sidebar, prism::Splitter::Pane{stacked ? 220.0F : std::max(kSidebarMinWidthUVE, ratio * 640.0F),
                                                          stacked ? 120.0F : kSidebarMinWidthUVE, stacked ? 600.0F : 700.0F, false});
            m_split->add(m_items, prism::Splitter::Pane{600.0F, 160.0F, 1e9F, true});
            m_root->setChild(m_split);
            break;
        }
        case SplitModeUVE::ItemsOnly: m_root->setChild(m_items); break;
        case SplitModeUVE::SidebarOnly: m_root->setChild(m_sidebar); break;
    }
    m_editor.m_contentBrowserSplitModeUVE = m_splitMode != SplitModeUVE::ItemsOnly;
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorContentBrowserUVE::RefreshUVE() {
    RebuildFoldersUVE();
    RebuildFavoritesUVE();
    RebuildItemsUVE();
    if (m_status && m_status->text() != m_editor.m_contentStatusMessage && !m_editor.m_contentStatusMessage.empty()) {
        m_status->setText(m_editor.m_contentStatusMessage);
    }
}

void EditorContentBrowserUVE::ChangedUVE() {
    m_tileSignature = 0;
}

void EditorContentBrowserUVE::GoToFolderUVE(const std::filesystem::path& directory) {
    m_editor.m_contentBrowserDirectory = directory;
    m_editor.m_contentBrowserShelf.clear();
    m_editor.m_selectedProjectFile.reset();
    m_editor.m_selectedAsset.reset();
    m_editor.m_contentSelection.ClearUVE();
    ChangedUVE();
}

void EditorContentBrowserUVE::RevealUVE(const std::filesystem::path& relativePath) {
    // Go to where it lives and pick it there (a folder opens itself).
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
        if (entry.relativePath != relativePath) {
            continue;
        }
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
            GoToFolderUVE(entry.relativePath);
            return;
        }
        GoToFolderUVE(entry.relativePath.parent_path());
        m_editor.m_selectedProjectFile = entry;
        m_editor.m_assetFilter.clear();
        if (m_itemSearch) {
            m_itemSearch->setText("");
        }
        return;
    }
}

void EditorContentBrowserUVE::RevealFileUVE(const std::filesystem::path& absolutePath) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, snapshot.contentRoot, error);
    if (!error && !relative.empty()) {
        RevealUVE(relative);
    }
}

std::string EditorContentBrowserUVE::DisplayNameUVE(const Asset::ProjectFileEntryUVE& entry) const {
    return entry.relativePath.filename().string();
}

EditorIconsUVE::ImageUVE EditorContentBrowserUVE::TypeIconUVE(const ItemTypeUVE type, const int size) {
    return m_icons.ContentTypeUVE(EditorUVE::GetContentBrowserItemTypeLabelUVE(type), size, IconForUVE(type));
}

void EditorContentBrowserUVE::RebuildFavoritesUVE() {
    if (!m_favorites) {
        return;
    }
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::uint64_t h = snapshot.refreshGeneration;
    for (const std::filesystem::path& p : m_editor.m_favoriteProjectPaths) {
        HashUVE(h, p.generic_string());
    }
    if (h == m_favoriteSignature) {
        return;
    }
    m_favoriteSignature = h;
    m_syncingFolders = true;
    m_favorites->clear();
    m_favoriteIds.clear();
    // Favorites that exist in this snapshot (one not yet scanned stays stored, just not shown).
    for (const std::filesystem::path& favorite : m_editor.m_favoriteProjectPaths) {
        const auto it = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                                     [&](const Asset::ProjectFileEntryUVE& e) { return e.relativePath == favorite; });
        if (it == snapshot.entries.end()) {
            continue;
        }
        m_favoriteIds.push_back(favorite);
        const ItemTypeUVE type = EditorUVE::ClassifyContentBrowserEntryUVE(*it);
        prism::TreeNode* node = m_favorites->addNode(nullptr, favorite.filename().string(), IconForUVE(type),
                                                     static_cast<int>(m_favoriteIds.size()));
        node->toggles = false;
        if (const EditorIconsUVE::ImageUVE icon = TypeIconUVE(type, 16); icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
    }
    if (m_favoriteIds.empty()) {
        prism::TreeNode* hint = m_favorites->addNode(nullptr, "Right-click an item > Pin", prism::Icon::Info, 0);
        hint->toggles = false;
        hint->hasTint = true;
        hint->tint = prism::theme().textDim;
    }
    m_favorites->refresh();
    m_syncingFolders = false;
}

void EditorContentBrowserUVE::RebuildFoldersUVE() {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::uint64_t h = snapshot.refreshGeneration;
    HashUVE(h, m_editor.m_contentTreeFilter);
    HashUVE(h, m_editor.m_contentBrowserDirectory.generic_string());
    if (h == m_folderSignature || !m_folders) {
        return;
    }
    m_folderSignature = h;
    m_syncingFolders = true;
    if (m_shelves) {
        m_shelves->clear();
        m_shelfNames.clear();
        for (const ContentShelfUVE& shelf : m_editor.m_contentShelves.GetAllUVE()) {
            m_shelfNames.push_back(shelf.name);
            m_shelves->addNode(nullptr, shelf.name, prism::Icon::Layers, static_cast<int>(m_shelfNames.size()))->toggles =
                false;
        }
        if (m_shelfNames.empty()) {
            m_shelfNames.emplace_back();
            m_shelves->addNode(nullptr, "No shelves yet", prism::Icon::Info, 1)->toggles = false;
        }
        m_shelves->refresh();
    }
    m_folders->clear();
    m_folderIds.clear();
    std::vector<std::string> folders = CollectVisibleContentFoldersUVE(snapshot.entries, m_editor.m_contentTreeFilter);
    // The content root itself.
    m_folderIds.emplace_back("");
    prism::TreeNode* root = m_folders->addNode(nullptr, "Content", prism::Icon::FolderOpen, 1);
    root->toggles = false;
    if (const EditorIconsUVE::ImageUVE icon = m_icons.ContentTypeUVE("folder_open", 16); icon) {
        root->image = icon.pixels;
        root->imageSize = icon.size;
    }
    std::map<std::string, prism::TreeNode*> nodes{{"", root}};
    std::sort(folders.begin(), folders.end());
    int selectedId = m_editor.m_contentBrowserDirectory.empty() ? 1 : 0;
    for (const std::string& folder : folders) {
        const std::filesystem::path p(folder);
        const std::string parent = p.parent_path().generic_string();
        prism::TreeNode* parentNode = nodes.count(parent) > 0 ? nodes[parent] : root;
        m_folderIds.push_back(folder);
        const int id = static_cast<int>(m_folderIds.size());
        prism::TreeNode* node = m_folders->addNode(parentNode, p.filename().string(), prism::Icon::Folder, id);
        node->toggles = false;
        if (const EditorIconsUVE::ImageUVE icon = m_icons.ContentTypeUVE("Folder", 16); icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        nodes[folder] = node;
        if (m_editor.m_contentBrowserDirectory.generic_string() == folder) {
            selectedId = id;
        }
    }
    if (selectedId > 0) {
        m_folders->setSelection({selectedId});
    }
    m_folders->refresh();
    m_syncingFolders = false;
}


void EditorContentBrowserUVE::RebuildItemsUVE() {
    if (!m_itemsHost) {
        return;
    }
    Asset::IProjectFileIndexUVE& index = m_editor.m_services->GetProjectFileIndexUVE();
    const Asset::ProjectFileSnapshotUVE snapshot = index.GetSnapshotUVE();
    m_editor.ReconcileContentBrowserDirectoryUVE(snapshot);
    m_contentRoot = snapshot.contentRoot;
    std::uint64_t h = snapshot.refreshGeneration;
    // Clip icons drawn in the background: the tiles take the new ones a few times a second at most.
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_editor.m_animationIconGeneration != m_iconGenerationShown && now - m_iconGenerationAt > std::chrono::milliseconds(300)) {
            m_iconGenerationShown = m_editor.m_animationIconGeneration;
            m_iconGenerationAt = now;
        }
        HashUVE(h, m_iconGenerationShown);
    }
    HashUVE(h, m_editor.m_contentBrowserDirectory.generic_string());
    HashUVE(h, m_editor.m_assetFilter);
    HashUVE(h, m_editor.m_contentBrowserShelf);
    HashUVE(h, static_cast<std::uint64_t>(m_editor.m_contentBrowserTypeFocus));
    HashUVE(h, static_cast<std::uint64_t>(m_editor.m_contentBrowserMode));
    HashUVE(h, static_cast<std::uint64_t>(m_editor.m_contentBrowserViewMode));
    HashUVE(h, static_cast<std::uint64_t>(m_editor.m_contentSortKey) * 2U + (m_editor.m_contentSortAscending ? 1U : 0U));
    // The pick is tracked apart: picking an item restyles the shown items instead of rebuilding them
    // (a rebuild would drop the tile being pressed, and with it a drag about to start).
    std::uint64_t pick = 0;
    for (const std::string& item : m_editor.m_contentSelection.ItemsUVE()) {
        HashUVE(pick, item);
    }
    if (m_editor.m_selectedProjectFile.has_value()) {
        HashUVE(pick, m_editor.m_selectedProjectFile->relativePath.generic_string());
    }
    if (m_editor.m_contentBrowserMode == ContentBrowserModeUVE::Columns) {
        HashUVE(h, pick); // Columns shows the picked file's preview, so it follows the pick
    }
    if (h == m_tileSignature) {
        if (pick != m_pickSignature) {
            m_pickSignature = pick;
            UpdatePickUVE(snapshot);
        }
        return;
    }
    m_tileSignature = h;
    m_pickSignature = pick;

    // What the mode lists: one folder (or a shelf) for Tiles / Columns / Details; everything below
    // the folder for Recent and Board.
    const ContentBrowserModeUVE mode = m_editor.m_contentBrowserMode;
    const std::filesystem::path directory = m_editor.m_contentBrowserDirectory;
    const ContentShelfUVE* const shelf =
        m_editor.m_contentBrowserShelf.empty() ? nullptr : m_editor.m_contentShelves.FindUVE(m_editor.m_contentBrowserShelf);
    const bool wholeTree = mode == ContentBrowserModeUVE::Recent || mode == ContentBrowserModeUVE::Board;
    std::vector<std::size_t> listed =
        shelf != nullptr ? ListContentShelfUVE(snapshot.entries, *shelf, m_editor.m_assetFilter)
        : wholeTree      ? ListContentTreeUVE(snapshot.entries, directory, m_editor.m_assetFilter)
                         : ListContentFolderUVE(snapshot.entries, directory, m_editor.m_assetFilter);
    std::vector<std::size_t> candidates;
    for (const std::size_t i : listed) {
        if (m_editor.DoesContentBrowserEntryMatchFocusUVE(snapshot.entries[i])) {
            candidates.push_back(i);
        }
    }
    // The engine's facts and sort (folders stay ahead of files).
    std::vector<ContentItemFactsUVE> facts;
    facts.reserve(candidates.size());
    for (const std::size_t i : candidates) {
        const Asset::ProjectFileEntryUVE& entry = snapshot.entries[i];
        ContentItemFactsUVE f;
        f.name = entry.relativePath.filename().string();
        f.type = EditorUVE::GetContentBrowserItemTypeLabelUVE(EditorUVE::ClassifyContentBrowserEntryUVE(entry));
        f.folder = entry.relativePath.parent_path().generic_string();
        f.isFolder = entry.kind == Asset::ProjectFileEntryKindUVE::Directory;
        if (!f.isFolder) {
            const ContentFileFactsUVE disk =
                m_editor.GetContentFileFactsUVE(snapshot.contentRoot, entry, snapshot.refreshGeneration);
            f.size = disk.size;
            f.modified = disk.modified;
        }
        facts.push_back(std::move(f));
    }
    std::vector<std::size_t> order(candidates.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    if (mode == ContentBrowserModeUVE::Recent) {
        SortContentFactsUVE(order, facts, ContentSortKeyUVE::Modified, false);
    } else {
        SortContentFactsUVE(order, facts, m_editor.m_contentSortKey, m_editor.m_contentSortAscending);
    }
    m_shown.clear();
    m_facts.clear();
    std::vector<std::string> shownOrder;
    for (const std::size_t o : order) {
        if (mode == ContentBrowserModeUVE::Recent && facts[o].isFolder) {
            continue; // Recent lists what changed, which is files
        }
        m_shown.push_back(candidates[o]);
        m_facts.push_back(facts[o]);
        shownOrder.push_back(snapshot.entries[candidates[o]].relativePath.generic_string());
    }
    m_editor.m_contentShownOrderPrevious = shownOrder;

    switch (mode) {
        case ContentBrowserModeUVE::Tiles: BuildTilesUVE(snapshot); break;
        case ContentBrowserModeUVE::Columns: BuildColumnsUVE(snapshot); break;
        case ContentBrowserModeUVE::Details: BuildTableUVE(snapshot, false); break;
        case ContentBrowserModeUVE::Recent: BuildTableUVE(snapshot, true); break;
        case ContentBrowserModeUVE::Board: BuildBoardUVE(snapshot); break;
    }

    if (m_path) {
        static constexpr std::array<const char*, 5> kModeNames{"Tiles", "Columns", "Details", "Recent", "Board"};
        const std::string where = shelf != nullptr ? "Shelf / " + shelf->name
                                  : directory.empty() ? std::string("Content")
                                                      : "Content / " + directory.generic_string();
        m_path->setText(where + "   -   " + kModeNames[static_cast<std::size_t>(mode)]);
    }
    if (m_status && m_editor.m_contentStatusMessage.empty()) {
        m_status->setText(m_shown.empty() ? (snapshot.contentRootExists ? "Nothing here. Right-click to create something."
                                                                         : "No Content folder in this project.")
                                          : std::to_string(m_shown.size()) + " items");
    }
}

void EditorContentBrowserUVE::BuildTilesUVE(const Asset::ProjectFileSnapshotUVE& snapshot) {
    using ViewMode = EditorUVE::ContentBrowserViewModeUVE;
    const ViewMode view = m_editor.m_contentBrowserViewMode;
    const bool rows = view == ViewMode::List;
    const prism::Vec2 size = rows ? prism::Vec2(240.0F, 24.0F)
                             : view == ViewMode::LargeTiles ? prism::Vec2(140.0F, 172.0F)
                                                            : prism::Vec2(108.0F, 136.0F);
    auto tiles = prism::make<TileGridUVE>(size, rows);
    m_tileWidgets.clear();
    m_editor.BeginAnimationIconBatchUVE();   // this folder's clips are drawn first, top-down
    for (std::size_t n = 0; n < m_shown.size(); ++n) {
        const std::size_t i = m_shown[n];
        const Asset::ProjectFileEntryUVE& entry = snapshot.entries[i];
        const std::string key = entry.relativePath.generic_string();
        const bool selected = m_editor.m_contentSelection.ContainsUVE(key) ||
                              (m_editor.m_contentSelection.IsEmptyUVE() && m_editor.m_selectedProjectFile.has_value() &&
                               m_editor.m_selectedProjectFile->relativePath == entry.relativePath);
        const ItemTypeUVE type = EditorUVE::ClassifyContentBrowserEntryUVE(entry);
        // The engine's own preview (real mesh, PBR material, texture) when it has one, else the
        // engine's icon for the type; PRISM only draws the tile around it. Folders have no preview.
        const std::uint32_t* pixels = nullptr;
        int pw = 0;
        int ph = 0;
        float scale = 1.0F;
        if (!rows && type != ItemTypeUVE::Folder) {
            if (const PreviewUVE* preview = PreviewForUVE(entry, type); preview != nullptr) {
                pixels = preview->pixels.data();
                pw = preview->width;
                ph = preview->height;
            } else if (const EditorIconsUVE::ImageUVE icon = TypeIconUVE(type, 64); icon) {
                pixels = icon.pixels;
                pw = icon.size;
                ph = icon.size;
                scale = 0.62F;
            }
        }
        auto tile = prism::make<TileUVE>(*this, i, DisplayNameUVE(entry), type, selected, pixels, pw, ph, scale, size, rows);
        tile->setTooltip(entry.relativePath.filename().string() + "\nType: " + m_facts[n].type);
        m_tileWidgets.emplace_back(key, tile);
        tiles->add(tile);
    }
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(tiles);
    m_table.reset();
    m_itemsHost->setChild(scroll);
}

void EditorContentBrowserUVE::UpdatePickUVE(const Asset::ProjectFileSnapshotUVE& snapshot) {
    const auto picked = [this](const std::filesystem::path& relative) {
        return m_editor.m_contentSelection.ContainsUVE(relative.generic_string()) ||
               (m_editor.m_contentSelection.IsEmptyUVE() && m_editor.m_selectedProjectFile.has_value() &&
                m_editor.m_selectedProjectFile->relativePath == relative);
    };
    for (const auto& [key, weakTile] : m_tileWidgets) {
        if (const std::shared_ptr<TileUVE> tile = weakTile.lock()) {
            tile->setSelected(picked(std::filesystem::path(key)));
        }
    }
    if (m_table) {
        std::vector<int> rows;
        for (std::size_t n = 0; n < m_shown.size(); ++n) {
            if (m_shown[n] < snapshot.entries.size() && picked(snapshot.entries[m_shown[n]].relativePath)) {
                rows.push_back(static_cast<int>(n));
            }
        }
        m_syncingFolders = true;
        m_table->select(rows);
        m_syncingFolders = false;
    }
}

void EditorContentBrowserUVE::BuildTableUVE(const Asset::ProjectFileSnapshotUVE& snapshot, const bool recent) {
    m_tileWidgets.clear();
    // Details: a table to sort by name, type, size, date or folder. Recent: what changed lately
    // across everything below, newest first, bucketed by the engine's age labels.
    auto table = prism::make<prism::TableView>();
    const std::int64_t today = StartOfTodayUVE();
    if (recent) {
        for (const auto& [title, width] : std::array<std::pair<const char*, float>, 5>{
                 {{"Name", 240.0F}, {"Changed", 100.0F}, {"Modified", 140.0F}, {"Type", 120.0F}, {"Folder", 200.0F}}}) {
            prism::TableColumn column;
            column.id = title;
            column.title = title;
            column.width = width;
            column.sortable = false;
            table->addColumn(column);
        }
    } else {
        for (std::size_t c = 0; c < kDetailsColumnsUVE.size(); ++c) {
            prism::TableColumn column;
            column.id = kDetailsColumnsUVE[c].first;
            column.title = kDetailsColumnsUVE[c].first;
            column.width = c == 0 ? 240.0F : c == 4 ? 200.0F : 110.0F;
            table->addColumn(column);
        }
    }
    table->cellText = [this, recent, today](const int row, const int col) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= m_facts.size()) {
            return {};
        }
        const ContentItemFactsUVE& f = m_facts[static_cast<std::size_t>(row)];
        if (recent) {
            switch (col) {
                case 0: return f.name;
                case 1: return GetContentAgeLabelUVE(ClassifyContentAgeUVE(f.modified, today));
                case 2: return FormatTimeUVE(f.modified);
                case 3: return f.type;
                default: return f.folder.empty() ? std::string("Content") : f.folder;
            }
        }
        switch (col) {
            case 0: return f.name;
            case 1: return f.type;
            case 2: return f.isFolder ? std::string("-") : FormatSizeUVE(f.size);
            case 3: return f.isFolder ? std::string("-") : FormatTimeUVE(f.modified);
            default: return f.folder.empty() ? std::string("Content") : f.folder;
        }
    };
    table->paintCell = [this](prism::Canvas& c, const int row, const int col, const prism::Rect r, bool) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_facts.size() || row >= static_cast<int>(m_shown.size())) {
            return;
        }
        const prism::Theme& t = prism::theme();
        const ContentItemFactsUVE& f = m_facts[static_cast<std::size_t>(row)];
        const Asset::ProjectFileSnapshotUVE snap = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        std::string text = m_table && m_table->cellText ? m_table->cellText(row, col) : f.name;
        float x = r.x + 6.0F;
        if (col == 0 && static_cast<std::size_t>(m_shown[static_cast<std::size_t>(row)]) < snap.entries.size()) {
            const ItemTypeUVE type =
                EditorUVE::ClassifyContentBrowserEntryUVE(snap.entries[m_shown[static_cast<std::size_t>(row)]]);
            if (const EditorIconsUVE::ImageUVE icon = TypeIconUVE(type, 16); icon) {
                c.blitBlend(icon.pixels, icon.size, icon.size, icon.size,
                            prism::Rect{x, std::floor(r.y + (r.h - 16.0F) * 0.5F), 16.0F, 16.0F});
            }
            x += 22.0F;
        }
        c.textIn(*t.ui, prism::Rect{x, r.y, r.right() - x - 4.0F, r.h}, text, col == 0 ? t.text : t.textDim,
                 prism::HAlign::Left, true);
    };
    // The order is the engine's (SortContentFactsUVE); a header click changes the engine's sort key.
    table->lessThan = [](const int a, const int b, int) { return a < b; };
    table->onSort = [this, recent](const int col, const prism::SortState state) {
        if (recent || m_syncingFolders || col < 0 || static_cast<std::size_t>(col) >= kDetailsColumnsUVE.size()) {
            return;
        }
        m_editor.m_contentSortKey = kDetailsColumnsUVE[static_cast<std::size_t>(col)].second;
        m_editor.m_contentSortAscending = state != prism::SortState::Descending;
        ChangedUVE();
    };
    table->onSelect = [this](const std::vector<int>& rows) {
        if (m_syncingFolders || rows.empty()) {
            return;
        }
        const int row = rows.back();
        if (row >= 0 && static_cast<std::size_t>(row) < m_shown.size()) {
            SelectEntryUVE(m_shown[static_cast<std::size_t>(row)], false, false);
        }
    };
    table->onActivate = [this](const int row) {
        if (row >= 0 && static_cast<std::size_t>(row) < m_shown.size()) {
            OpenEntryUVE(m_shown[static_cast<std::size_t>(row)]);
        }
    };
    table->onContext = [this](const int row, const prism::Vec2 at) {
        if (row >= 0 && static_cast<std::size_t>(row) < m_shown.size()) {
            ShowItemMenuUVE(m_shown[static_cast<std::size_t>(row)], at);
        } else {
            ShowEmptyMenuUVE(at);
        }
    };
    table->setRowCount(static_cast<int>(m_shown.size()));
    m_table = table;
    // Show the engine's sort on the header without sorting again.
    if (!recent) {
        int sortColumn = 0;
        for (std::size_t c = 0; c < kDetailsColumnsUVE.size(); ++c) {
            if (kDetailsColumnsUVE[c].second == m_editor.m_contentSortKey) {
                sortColumn = static_cast<int>(c);
            }
        }
        m_syncingFolders = true;
        table->sortBy(sortColumn, prism::SortState::Ascending);
        m_syncingFolders = false;
    }
    std::vector<int> picked;
    for (std::size_t n = 0; n < m_shown.size(); ++n) {
        const Asset::ProjectFileEntryUVE& entry = snapshot.entries[m_shown[n]];
        if (m_editor.m_selectedProjectFile.has_value() && m_editor.m_selectedProjectFile->relativePath == entry.relativePath) {
            picked.push_back(static_cast<int>(n));
        }
    }
    m_syncingFolders = true;
    table->select(picked);
    m_syncingFolders = false;
    m_itemsHost->setChild(table);
}

void EditorContentBrowserUVE::BuildColumnsUVE(const Asset::ProjectFileSnapshotUVE& snapshot) {
    m_tileWidgets.clear();
    // Columns: one list per folder level from Content down to the open folder, then a preview of
    // the file picked. Picking a folder in any column opens it.
    auto row = prism::hbox(1.0F);
    std::vector<std::filesystem::path> levels{std::filesystem::path{}};
    for (const auto& part : m_editor.m_contentBrowserDirectory) {
        levels.push_back(levels.back() / part);
    }
    for (std::size_t level = 0; level < levels.size(); ++level) {
        const std::filesystem::path& dir = levels[level];
        const std::vector<std::size_t> entries =
            level + 1 == levels.size() ? m_shown : ListContentFolderUVE(snapshot.entries, dir, "");
        auto list = prism::make<prism::TreeView>();
        std::vector<std::size_t> ids;
        int selectedId = 0;
        for (const std::size_t i : entries) {
            const Asset::ProjectFileEntryUVE& entry = snapshot.entries[i];
            const ItemTypeUVE type = EditorUVE::ClassifyContentBrowserEntryUVE(entry);
            ids.push_back(i);
            prism::TreeNode* node =
                list->addNode(nullptr, DisplayNameUVE(entry), IconForUVE(type), static_cast<int>(ids.size()));
            node->toggles = false;
            if (const EditorIconsUVE::ImageUVE icon = TypeIconUVE(type, 16); icon) {
                node->image = icon.pixels;
                node->imageSize = icon.size;
            }
            const bool onPath = level + 1 < levels.size() && entry.relativePath == levels[level + 1];
            const bool picked = m_editor.m_selectedProjectFile.has_value() &&
                                m_editor.m_selectedProjectFile->relativePath == entry.relativePath;
            if (onPath || picked) {
                selectedId = static_cast<int>(ids.size());
            }
        }
        if (selectedId > 0) {
            list->setSelection({selectedId});
        }
        list->onSelect = [this, ids, snapshotEntries = snapshot.entries](const std::vector<prism::TreeNode*>& nodes) {
            if (m_syncingFolders || nodes.empty() || nodes.front()->id <= 0) {
                return;
            }
            const std::size_t i = ids[static_cast<std::size_t>(nodes.front()->id - 1)];
            if (i < snapshotEntries.size() && snapshotEntries[i].kind == Asset::ProjectFileEntryKindUVE::Directory) {
                GoToFolderUVE(snapshotEntries[i].relativePath);
            } else {
                SelectEntryUVE(i, false, false);
                ChangedUVE();
            }
        };
        list->onActivate = [this, ids](prism::TreeNode* node) {
            if (node != nullptr && node->id > 0) {
                OpenEntryUVE(ids[static_cast<std::size_t>(node->id - 1)]);
            }
        };
        list->onContext = [this, ids](prism::TreeNode* node, const prism::Vec2 at) {
            if (node != nullptr && node->id > 0) {
                ShowItemMenuUVE(ids[static_cast<std::size_t>(node->id - 1)], at);
            } else {
                ShowEmptyMenuUVE(at);
            }
        };
        list->refresh();
        auto column = prism::make<prism::Border>();
        column->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 1.0F, 0.0F));
        column->setChild(list);
        auto sizedColumn = prism::make<prism::SizeBox>();
        sizedColumn->setWidthOverride(190.0F);
        sizedColumn->setContent(column);
        row->add(sizedColumn, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    }
    // The picked file: its preview and facts.
    auto preview = prism::vbox(4.0F);
    preview->setPadding(prism::Margin(10.0F));
    if (m_editor.m_selectedProjectFile.has_value() &&
        m_editor.m_selectedProjectFile->kind != Asset::ProjectFileEntryKindUVE::Directory) {
        const Asset::ProjectFileEntryUVE entry = *m_editor.m_selectedProjectFile;
        const ItemTypeUVE type = EditorUVE::ClassifyContentBrowserEntryUVE(entry);
        const std::uint32_t* pixels = nullptr;
        int pw = 0;
        int ph = 0;
        float scale = 1.0F;
        if (const PreviewUVE* p = PreviewForUVE(entry, type); p != nullptr) {
            pixels = p->pixels.data();
            pw = p->width;
            ph = p->height;
        } else if (const EditorIconsUVE::ImageUVE icon = TypeIconUVE(type, 64); icon) {
            pixels = icon.pixels;
            pw = icon.size;
            ph = icon.size;
            scale = 0.62F;
        }
        const auto found = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                                        [&](const Asset::ProjectFileEntryUVE& e) { return e.relativePath == entry.relativePath; });
        const std::size_t index = found == snapshot.entries.end() ? 0U
                                                                  : static_cast<std::size_t>(found - snapshot.entries.begin());
        preview->add(prism::make<TileUVE>(*this, index, DisplayNameUVE(entry), type, true, pixels, pw, ph, scale,
                                          prism::Vec2(160.0F, 196.0F), false),
                     prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
        const ContentFileFactsUVE disk = m_editor.GetContentFileFactsUVE(snapshot.contentRoot, entry, snapshot.refreshGeneration);
        for (const std::string& line : {std::string("Type: ") + EditorUVE::GetContentBrowserItemTypeLabelUVE(type),
                                        std::string("Size: ") + FormatSizeUVE(disk.size),
                                        std::string("Modified: ") + FormatTimeUVE(disk.modified)}) {
            auto label = prism::make<prism::Label>(line, prism::FontRole::Small);
            label->setColor(prism::ThemeColor(&prism::Theme::textDim));
            preview->add(label);
        }
    }
    row->add(preview, prism::Slot{1.0F});
    m_table.reset();
    m_itemsHost->setChild(row);
}

void EditorContentBrowserUVE::BuildBoardUVE(const Asset::ProjectFileSnapshotUVE& snapshot) {
    m_tileWidgets.clear();
    // Board: everything below the folder in lanes by kind (the engine's GroupContentByTypeUVE).
    std::vector<std::size_t> order(m_facts.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    const auto lanes = GroupContentByTypeUVE(order, m_facts);
    auto row = prism::hbox(6.0F);
    row->setPadding(prism::Margin(2.0F));
    for (const auto& [typeName, members] : lanes) {
        auto lane = prism::vbox(2.0F);
        auto header = prism::make<prism::Label>(typeName + "  (" + std::to_string(members.size()) + ")",
                                                prism::FontRole::Small);
        header->setColor(prism::ThemeColor(&prism::Theme::textBright));
        lane->add(header);
        auto list = prism::make<prism::TreeView>();
        std::vector<std::size_t> ids;
        for (const std::size_t m : members) {
            const std::size_t i = m_shown[m];
            const Asset::ProjectFileEntryUVE& entry = snapshot.entries[i];
            const ItemTypeUVE type = EditorUVE::ClassifyContentBrowserEntryUVE(entry);
            ids.push_back(i);
            prism::TreeNode* node = list->addNode(nullptr, DisplayNameUVE(entry), IconForUVE(type), static_cast<int>(ids.size()));
            node->toggles = false;
            if (const EditorIconsUVE::ImageUVE icon = TypeIconUVE(type, 16); icon) {
                node->image = icon.pixels;
                node->imageSize = icon.size;
            }
        }
        list->onSelect = [this, ids](const std::vector<prism::TreeNode*>& nodes) {
            if (!m_syncingFolders && !nodes.empty() && nodes.front()->id > 0) {
                SelectEntryUVE(ids[static_cast<std::size_t>(nodes.front()->id - 1)], false, false);
            }
        };
        list->onActivate = [this, ids](prism::TreeNode* node) {
            if (node != nullptr && node->id > 0) {
                OpenEntryUVE(ids[static_cast<std::size_t>(node->id - 1)]);
            }
        };
        list->onContext = [this, ids](prism::TreeNode* node, const prism::Vec2 at) {
            if (node != nullptr && node->id > 0) {
                ShowItemMenuUVE(ids[static_cast<std::size_t>(node->id - 1)], at);
            }
        };
        list->refresh();
        lane->add(list, prism::Slot{1.0F});
        auto card = prism::make<prism::Border>();
        card->setBackground(prism::ThemeColor(&prism::Theme::header));
        card->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
        card->setPadding(prism::Margin(4.0F));
        card->setChild(lane);
        auto sizedLane = prism::make<prism::SizeBox>();
        sizedLane->setWidthOverride(200.0F);
        sizedLane->setContent(card);
        row->add(sizedLane, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    }
    if (lanes.empty()) {
        auto empty = prism::make<prism::Label>("No files below this folder.", prism::FontRole::Small);
        empty->setColor(prism::ThemeColor(&prism::Theme::textDim));
        row->add(empty);
    }
    m_table.reset();
    m_itemsHost->setChild(row);
}

// ------------------------------------------------------------------------------------- menus

void EditorContentBrowserUVE::ShowViewMenuUVE() {
    // The view mode: the engine's five Content Browser modes, the tile size and the sort.
    using ViewMode = EditorUVE::ContentBrowserViewModeUVE;
    std::vector<prism::MenuItem> items;
    static constexpr std::array<std::pair<ContentBrowserModeUVE, const char*>, 5> kModes{{
        {ContentBrowserModeUVE::Tiles, "Tiles"},
        {ContentBrowserModeUVE::Columns, "Columns"},
        {ContentBrowserModeUVE::Details, "Details"},
        {ContentBrowserModeUVE::Recent, "Recent"},
        {ContentBrowserModeUVE::Board, "Board"},
    }};
    for (const auto& [mode, label] : kModes) {
        items.push_back(prism::MenuItem::Check(label, m_editor.m_contentBrowserMode == mode, [this, m = mode] {
            m_editor.m_contentBrowserMode = m;
            ChangedUVE();
        }));
    }
    items.push_back(prism::MenuItem::Sep());
    std::vector<prism::MenuItem> sizes;
    for (const auto& [view, label] : std::array<std::pair<ViewMode, const char*>, 3>{
             {{ViewMode::LargeTiles, "Large Tiles"}, {ViewMode::SmallTiles, "Small Tiles"}, {ViewMode::List, "List"}}}) {
        sizes.push_back(prism::MenuItem::Check(label, m_editor.m_contentBrowserViewMode == view, [this, v = view] {
            m_editor.m_contentBrowserViewMode = v;
            m_editor.m_contentBrowserMode = ContentBrowserModeUVE::Tiles;
            ChangedUVE();
        }));
    }
    items.push_back(prism::MenuItem::Sub("Tile Size", std::move(sizes), prism::Icon::Grid));
    std::vector<prism::MenuItem> sort;
    for (std::size_t c = 0; c < kDetailsColumnsUVE.size(); ++c) {
        const ContentSortKeyUVE key = kDetailsColumnsUVE[c].second;
        sort.push_back(prism::MenuItem::Check(kDetailsColumnsUVE[c].first, m_editor.m_contentSortKey == key, [this, key] {
            m_editor.m_contentSortKey = key;
            ChangedUVE();
        }));
    }
    sort.push_back(prism::MenuItem::Sep());
    sort.push_back(prism::MenuItem::Check("Ascending", m_editor.m_contentSortAscending, [this] {
        m_editor.m_contentSortAscending = !m_editor.m_contentSortAscending;
        ChangedUVE();
    }));
    items.push_back(prism::MenuItem::Sub("Sort By", std::move(sort), prism::Icon::Filter));
    prism::showMenu(std::move(items), m_viewButton ? m_viewButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

void EditorContentBrowserUVE::ShowSplitMenuUVE() {
    // The split mode: how the Content Browser's own sidebar and items share its space.
    std::vector<prism::MenuItem> items;
    static constexpr std::array<std::pair<SplitModeUVE, const char*>, 4> kSplits{{
        {SplitModeUVE::SideBySide, "Side by Side"},
        {SplitModeUVE::Stacked, "Stacked (sidebar above)"},
        {SplitModeUVE::ItemsOnly, "Items Only"},
        {SplitModeUVE::SidebarOnly, "Sidebar Only"},
    }};
    for (const auto& [mode, label] : kSplits) {
        items.push_back(prism::MenuItem::Check(label, m_splitMode == mode, [this, m = mode] {
            m_splitMode = m;
            ApplySplitModeUVE();
        }));
    }
    prism::showMenu(std::move(items), m_splitButton ? m_splitButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

std::vector<prism::MenuItem> EditorContentBrowserUVE::CatalogueMenuUVE() {
    // Laid out as Unreal's Content Browser Add: GET (import), CREATE (a folder, the assets made most,
    // then every kind by category, each opening beside the menu). Only what the engine makes from
    // nothing is here: objects (lights, meshes ...) are placed with the Outliner's '+', and textures,
    // meshes and sounds come in through Import.
    std::vector<prism::MenuItem> items;
    const auto item = [this](const char* label, const std::string& id, const prism::Icon icon, const char* typeLabel) {
        prism::MenuItem entry = prism::MenuItem::Item(label, [this, id] { CreateCatalogueItemUVE(id); }, "", icon);
        if (typeLabel != nullptr) {
            if (const EditorIconsUVE::ImageUVE image = m_icons.ContentTypeUVE(typeLabel, 16, icon); image) {
                entry.withImage(image.pixels, image.size);
            }
        }
        return entry;
    };
    const auto entityClass = [this] {
        prism::MenuItem entry = prism::MenuItem::Item("Entity Class", [this] {
            const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            m_editor.OpenEntityClassPickerUVE(snapshot.contentRoot / m_editor.m_contentBrowserDirectory);
        }, "", prism::Icon::EntityAsset);
        if (const EditorIconsUVE::ImageUVE image = m_icons.ContentTypeUVE("Entity", 16, prism::Icon::EntityAsset); image) {
            entry.withImage(image.pixels, image.size);
        }
        return entry;
    };
    // A material of a given kind, made in the current folder.
    const auto material = [this](const char* label, const char* stem, const Asset::MaterialKindUVE kind) {
        prism::MenuItem entry = prism::MenuItem::Item(label, [this, stem, kind] { CreateMaterialUVE(stem, kind); }, "", prism::Icon::Material);
        if (const EditorIconsUVE::ImageUVE image = m_icons.ContentTypeUVE("Material", 16, prism::Icon::Material); image) {
            entry.withImage(image.pixels, image.size);
        }
        return entry;
    };

    items.push_back(prism::MenuItem::Header("GET"));
    items.push_back(prism::MenuItem::Item("Import to Current Folder", [this] { CreateCatalogueItemUVE(""); }, "Ctrl+I", prism::Icon::Open));
    items.push_back(prism::MenuItem::Header("CREATE"));
    items.push_back(item("New Folder", "folder", prism::Icon::Folder, "Folder"));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(entityClass());
    items.push_back(item("Level", "viewport_3d", prism::Icon::SceneAsset, "Viewport3D"));
    items.push_back(item("Material", "material", prism::Icon::Material, "Material"));
    items.push_back(item("Animation Montage", "animation-montage", prism::Icon::AnimMontage, "Animation Montage"));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Sub("Animation", {
        item("Animation Library", "animation-library", prism::Icon::AnimLibrary, "Animation Library"),
        item("Animation Montage", "animation-montage", prism::Icon::AnimMontage, "Animation Montage"),
        item("Montage Library", "montage-library", prism::Icon::MontageLibrary, "Montage Library"),
    }));
    // A Blueprint Class: gameplay as nodes (what an object's Script slot runs).
    prism::MenuItem blueprintClass =
        prism::MenuItem::Item("Blueprint Class", [this] { CreateBlueprintUVE(); }, "", prism::Icon::BlueprintClass);
    items.push_back(prism::MenuItem::Sub("Blueprint", {blueprintClass, entityClass()}));
    items.push_back(prism::MenuItem::Sub("Gameplay", {blueprintClass}));
    // A Widget Blueprint: a screen of UI (text, buttons, bars ...) with its own graph.
    prism::MenuItem widgetBlueprint =
        prism::MenuItem::Item("Widget Blueprint", [this] { CreateWidgetBlueprintUVE(); }, "", prism::Icon::BlueprintClass);
    items.push_back(prism::MenuItem::Sub("User Interface", {widgetBlueprint}));
    items.push_back(prism::MenuItem::Sub("Material", {
        material("Material", "M_Standard", Asset::MaterialKindUVE::Standard),
        material("ORM Material", "M_ORM", Asset::MaterialKindUVE::Orm),
        material("Shader Material", "M_Shader", Asset::MaterialKindUVE::Shader),
        item("Shader", "shader_script", prism::Icon::Shader, "Shader"),
    }));
    items.push_back(prism::MenuItem::Sub("World", {
        item("Level", "viewport_3d", prism::Icon::SceneAsset, "Viewport3D"),
        item("Level 2D", "viewport_2d", prism::Icon::SceneAsset, "Viewport3D"),
    }));
    return items;
}

void EditorContentBrowserUVE::CreateMaterialUVE(const std::string& stem, const Asset::MaterialKindUVE kind) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path folder = snapshot.contentRoot / m_editor.m_contentBrowserDirectory;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path path = EditorUVE::MakeUniqueContentPathUVE(folder, stem, ".uvmaterial");
    Asset::MaterialAssetUVE material{};
    material.kind = kind;
    if (kind == Asset::MaterialKindUVE::Orm) {
        material.metallic = 1.0F;
        material.occlusionStrength = 1.0F;
    } else if (kind == Asset::MaterialKindUVE::Shader) {
        material.useCustomShaderCode = true;
    }
    if (!Asset::SaveMaterialAssetUVE(material, path)) {
        m_editor.m_contentStatusMessage = "Could not create a material here.";
        return;
    }
    m_editor.m_contentStatusMessage.clear();
    m_editor.RefreshProjectFileIndexUVE();
    m_folderSignature = 0;
    ChangedUVE();
}

void EditorContentBrowserUVE::CreateBlueprintUVE() {
    // A Blueprint Class with Unreal's starting graph (Event Ready, Event Tick); an object's Script
    // slot picks it, or the Entity Editor's Blueprint button makes one for an object directly.
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path folder = snapshot.contentRoot / m_editor.m_contentBrowserDirectory;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path path = EditorUVE::MakeUniqueContentPathUVE(folder, "BP_NewBlueprint", ".uvbp");
    Asset::BlueprintAssetUVE blueprint;
    blueprint.parentKind = "Object3D";
    Asset::BlueprintNodeUVE ready;
    ready.id = 1U;
    ready.type = "event_ready";
    Asset::BlueprintNodeUVE tick;
    tick.id = 2U;
    tick.type = "event_tick";
    tick.y = 200.0F;
    blueprint.nodes = {ready, tick};
    if (!Asset::SaveBlueprintAssetUVE(blueprint, path)) {
        m_editor.m_contentStatusMessage = "Could not create a Blueprint here.";
        return;
    }
    m_editor.m_contentStatusMessage.clear();
    m_editor.RefreshProjectFileIndexUVE();
    m_folderSignature = 0;
    ChangedUVE();
}

void EditorContentBrowserUVE::CreateWidgetBlueprintUVE() {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::optional<std::filesystem::path> made =
        m_editor.CreateWidgetBlueprintAssetUVE(snapshot.contentRoot / m_editor.m_contentBrowserDirectory);
    if (!made.has_value()) {
        m_editor.m_contentStatusMessage = "Could not create a Widget Blueprint here.";
        return;
    }
    m_editor.m_contentStatusMessage.clear();
    m_editor.RefreshProjectFileIndexUVE();
    m_folderSignature = 0;
    ChangedUVE();
}

void EditorContentBrowserUVE::OpenBlueprintUVE(const std::filesystem::path& full) {
    // A Blueprint is edited on its object: open the entity whose object runs it, then the graph.
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path relative = full.lexically_relative(snapshot.contentRoot);
    const std::string name = full.filename().string();
    for (const Asset::ProjectFileEntryUVE& other : snapshot.entries) {
        if (other.kind != Asset::ProjectFileEntryKindUVE::File || LowerExtensionUVE(other.relativePath) != ".uventity") {
            continue;
        }
        const std::filesystem::path entityPath = snapshot.contentRoot / other.relativePath;
        std::ifstream in(entityPath, std::ios::binary);
        const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::size_t at = text.find(relative.generic_string() + "\"");
        if (at == std::string::npos) {
            continue;
        }
        // The path as the Script slot stores it (everything from its opening quote).
        const std::size_t open = text.rfind('"', at - 1U);
        const std::string stored = open == std::string::npos ? relative.generic_string() : text.substr(open + 1U, at + relative.generic_string().size() - open - 1U);
        if (!m_editor.OpenEntityEditorUVE(entityPath)) {
            break;
        }
        m_editor.m_pendingBlueprintOpenUVE = stored;
        return;
    }
    m_editor.m_contentStatusMessage = name + " is not on any object yet: pick it in an object's Script slot, or use the Entity Editor's Blueprint button.";
}

void EditorContentBrowserUVE::CreateCatalogueItemUVE(const std::string& id) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path here = snapshot.contentRoot / m_editor.m_contentBrowserDirectory;
    if (id.empty()) {
        static_cast<void>(m_editor.PromptImportFilesUVE(here));
    } else if (!m_editor.CreateContentCatalogueItemUVE(id, here).has_value()) {
        m_editor.m_contentStatusMessage = "Could not create that here.";
    } else {
        m_editor.m_contentStatusMessage.clear();
    }
    m_editor.RefreshProjectFileIndexUVE();
    m_folderSignature = 0;
    ChangedUVE();
}

void EditorContentBrowserUVE::ShowEmptyMenuUVE(const prism::Vec2 at) {
    // Right-click on empty space: the same Add menu, and Refresh.
    std::vector<prism::MenuItem> items = CatalogueMenuUVE();
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Refresh", [this] {
        m_editor.RefreshProjectFileIndexUVE();
        m_folderSignature = 0;
        ChangedUVE();
    }, "", prism::Icon::Refresh));
    prism::showSearchMenu(std::move(items), prism::Rect{at.x, at.y, 1.0F, 1.0F}, prism::PopupPlacement::AtPoint,
                          "Start typing to search", true);
}

void EditorContentBrowserUVE::ShowItemMenuUVE(const std::size_t entryIndex, const prism::Vec2 at) {
    // A file's or folder's menu: the same actions the previous Content Browser offered.
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntryUVE entry = snapshot.entries[entryIndex];
    const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
    const std::string name = entry.relativePath.filename().string();
    const bool directory = entry.kind == Asset::ProjectFileEntryKindUVE::Directory;
    const bool allowed = m_editor.IsAuthoringCommandAllowedUVE();
    const ItemTypeUVE type = EditorUVE::ClassifyContentBrowserEntryUVE(entry);
    const std::string extension = LowerExtensionUVE(entry.relativePath);
    std::vector<prism::MenuItem> items;
    const auto add = [&items](prism::MenuItem item, const bool enabled = true) {
        if (!enabled) {
            item.disabled();
        }
        items.push_back(std::move(item));
    };

    if (directory) {
        add(prism::MenuItem::Item("Open", [this, entryIndex] { OpenEntryUVE(entryIndex); }, "", prism::Icon::FolderOpen));
        add(prism::MenuItem::Item("Optimize Animations in Folder", [this, rel = entry.relativePath] {
            m_editor.StartOptimizeAnimationsUVE(m_editor.CollectAnimationClipsUVE(rel));
        }, "", prism::Icon::AnimClip), allowed && !m_editor.IsOptimizingAnimationsUVE());
    }
    if (!directory && IsEntityAssetUVE(entry.relativePath)) {
        // Opens the entity in the Entity Editor's own window; the level stays open beside it.
        add(prism::MenuItem::Item("Open Entity", [this, absolute, name] {
            if (!m_editor.OpenEntityEditorUVE(absolute)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Entity Editor.";
            }
        }, "", prism::Icon::Open), allowed);
        add(prism::MenuItem::Item("Place in Scene", [this, absolute, name] {
            if (m_editor.PlaceEntityAssetUVE(absolute) == Scene::kInvalidEntityUVE) {
                m_editor.m_contentStatusMessage = "Could not place " + name + ".";
            }
        }, "", prism::Icon::Plus), allowed);
        if (extension == ".uventity") {
            const bool isDefault = m_editor.GetDefaultPlayerEntityUVE() == entry.relativePath.generic_string();
            add(prism::MenuItem::Check("Default Player", isDefault, [this, isDefault, rel = entry.relativePath] {
                if (!m_editor.SetDefaultPlayerEntityUVE(isDefault ? std::filesystem::path{} : rel)) {
                    m_editor.m_contentStatusMessage = "Could not save the project settings.";
                }
            }));
        }
    }
    if (type == ItemTypeUVE::Scene) {
        add(prism::MenuItem::Item("Open Scene", [this, entryIndex] { OpenEntryUVE(entryIndex); }, "", prism::Icon::Globe));
    }
    if (extension == ".uvs") {
        add(prism::MenuItem::Item("Edit Script", [this, absolute] { static_cast<void>(m_editor.OpenUVScriptPathUVE(absolute)); },
                                  "", prism::Icon::Script), allowed);
    }
    if (extension == ".uvshader") {
        add(prism::MenuItem::Item("Edit Shader", [this, entry] {
            m_editor.m_selectedProjectFile = entry;
            m_editor.m_selectedAsset.reset();
        }, "", prism::Icon::Script));
    }
    if (type == ItemTypeUVE::AnimationLibrary) {
        add(prism::MenuItem::Item("Open Library", [this, entryIndex] { OpenEntryUVE(entryIndex); }, "", prism::Icon::AnimLibrary));
    }
    // A clip becomes a montage: a .uvmontage beside it with the clip on its slot track, opened.
    if (extension == ".uvanim") {
        add(prism::MenuItem::Item("Optimize Animation", [this, absolute] { m_editor.StartOptimizeAnimationsUVE({absolute}); }, "",
                                  prism::Icon::AnimClip), allowed);
        add(prism::MenuItem::Item("Create Montage", [this, absolute] { CreateMontageFromClipUVE(absolute); }, "", prism::Icon::AnimMontage),
            allowed);
    }
    // A montage joins a montage library, the way a clip joins an animation library.
    if (type == ItemTypeUVE::AnimationMontage) {
        std::vector<prism::MenuItem> libraries;
        for (const Asset::ProjectFileEntryUVE& other : snapshot.entries) {
            if (other.kind != Asset::ProjectFileEntryKindUVE::File || LowerExtensionUVE(other.relativePath) != ".uvmontagelib") {
                continue;
            }
            const std::filesystem::path library = snapshot.contentRoot / other.relativePath;
            libraries.push_back(prism::MenuItem::Item(other.relativePath.stem().string(),
                                                      [this, absolute, library] { AddMontageToLibraryUVE(absolute, library); }));
        }
        if (!libraries.empty()) {
            libraries.push_back(prism::MenuItem::Sep());
        }
        libraries.push_back(prism::MenuItem::Item("New Montage Library", [this, absolute] {
            const std::filesystem::path library = absolute.parent_path() / "Montage Library.uvmontagelib";
            AddMontageToLibraryUVE(absolute, library);
        }, "", prism::Icon::MontageLibrary));
        add(prism::MenuItem::Sub("Add to Montage Library", std::move(libraries), prism::Icon::MontageLibrary), allowed);
    }
    if (type == ItemTypeUVE::MontageLibrary) {
        add(prism::MenuItem::Item("Open Library", [this, entryIndex] { OpenEntryUVE(entryIndex); }, "", prism::Icon::MontageLibrary));
    }
    // Model sources and imported models: place, use as the selected object's mesh, retarget.
    const bool modelSource = EditorUVE::IsModelSourcePathUVE(entry.relativePath);
    const std::filesystem::path importedModel =
        modelSource ? m_editor.GetImportedModelPathUVE(entry.relativePath) : std::filesystem::path{};
    std::error_code ec;
    const bool modelReady = !importedModel.empty() && std::filesystem::is_regular_file(importedModel, ec);
    const bool uvmodel = entry.registeredAssetGuid.has_value() && extension == ".uvmodel";
    const EditorModelSourceInfoUVE* const info = modelSource ? m_editor.FindModelSourceInfoUVE(entry.relativePath) : nullptr;
    if (modelReady || (info != nullptr && info->hasSkeleton)) {
        add(prism::MenuItem::Item("Place in Scene", [this, rel = entry.relativePath, name] {
            if (m_editor.PlaceModelSourceUVE(rel) == Scene::kInvalidEntityUVE && m_editor.m_contentStatusMessage.empty()) {
                m_editor.m_contentStatusMessage = "Could not place " + name + ".";
            }
        }, "", prism::Icon::Plus), allowed);
    }
    if (modelReady || uvmodel) {
        Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
        const bool canAssign = m_editor.IsDocumentEntityUVE(m_editor.m_selectedEntity) && allowed &&
                               em.HasComponentUVE<Scene::MeshComponentUVE>(m_editor.m_selectedEntity);
        add(prism::MenuItem::Item("Use as Mesh on Selected Object", [this, modelReady, importedModel, entry] {
            Scene::IEntityManagerUVE& entities = m_editor.m_services->GetEntityManagerUVE();
            if (!entities.HasComponentUVE<Scene::MeshComponentUVE>(m_editor.m_selectedEntity)) {
                return;
            }
            Scene::MeshComponentUVE mesh = entities.GetComponentUVE<Scene::MeshComponentUVE>(m_editor.m_selectedEntity);
            mesh.meshGuid = modelReady ? m_editor.RegisterImportedModelArtifactsUVE(importedModel) : *entry.registeredAssetGuid;
            static_cast<void>(m_editor.SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Mesh, mesh));
        }), canAssign);
    }
    const bool canRetarget = extension == ".uvanim" || uvmodel || (info != nullptr && (info->rigged || info->hasSkeleton));
    if (canRetarget) {
        add(prism::MenuItem::Item("Retarget...", [this, extension, absolute, rel = entry.relativePath] {
            if (extension == ".uvanim") {
                m_editor.OpenRetargetWindowUVE({absolute}, {});
            } else {
                m_editor.OpenRetargetWindowUVE({}, rel);
            }
        }), allowed);
    }
    if (!items.empty()) {
        items.push_back(prism::MenuItem::Sep());
    }
    // Favorites and shelves.
    const bool pinned = m_editor.IsProjectPathFavoritedUVE(entry.relativePath);
    add(prism::MenuItem::Item(pinned ? "Unpin from Favorites" : "Pin to Favorites", [this, rel = entry.relativePath] {
        m_editor.ToggleProjectPathFavoriteUVE(rel);
        m_favoriteSignature = 0;
    }, "", prism::Icon::NodeFlag));
    std::vector<prism::MenuItem> shelves;
    for (const ContentShelfUVE& shelf : m_editor.m_contentShelves.GetAllUVE()) {
        const bool onShelf = m_editor.m_contentShelves.ContainsUVE(shelf.name, entry.relativePath);
        shelves.push_back(prism::MenuItem::Check(shelf.name, onShelf, [this, shelfName = shelf.name, onShelf, rel = entry.relativePath] {
            if (onShelf) {
                static_cast<void>(m_editor.m_contentShelves.RemoveItemUVE(shelfName, rel));
            } else {
                static_cast<void>(m_editor.m_contentShelves.AddItemUVE(shelfName, rel));
            }
            static_cast<void>(m_editor.SaveSharedShelvesUVE());
            m_folderSignature = 0;
            ChangedUVE();
        }));
    }
    if (!shelves.empty()) {
        shelves.push_back(prism::MenuItem::Sep());
    }
    shelves.push_back(prism::MenuItem::Item("New Shelf with This", [this, rel = entry.relativePath] {
        const std::string shelfName = m_editor.m_contentShelves.CreateUVE("Shelf");
        if (!shelfName.empty()) {
            static_cast<void>(m_editor.m_contentShelves.AddItemUVE(shelfName, rel));
            static_cast<void>(m_editor.SaveSharedShelvesUVE());
        }
        m_folderSignature = 0;
    }));
    items.push_back(prism::MenuItem::Sub("Shelves", std::move(shelves), prism::Icon::Layers));
    if (!m_editor.m_contentBrowserShelf.empty() &&
        m_editor.m_contentShelves.ContainsUVE(m_editor.m_contentBrowserShelf, entry.relativePath)) {
        add(prism::MenuItem::Item("Remove from " + m_editor.m_contentBrowserShelf, [this, rel = entry.relativePath] {
            static_cast<void>(m_editor.m_contentShelves.RemoveItemUVE(m_editor.m_contentBrowserShelf, rel));
            static_cast<void>(m_editor.SaveSharedShelvesUVE());
            ChangedUVE();
        }));
    }
    add(prism::MenuItem::Item("Show in Folder", [this, rel = entry.relativePath] { RevealUVE(rel); }, "", prism::Icon::FolderOpen));
    add(prism::MenuItem::Item("Copy Path", [rel = entry.relativePath] {
        if (prism::Application::exists()) {
            prism::Application::get().platform().setClipboard(rel.generic_string());
        }
    }, "", prism::Icon::Copy));
    items.push_back(prism::MenuItem::Sep());
    add(prism::MenuItem::Item("Rename", [this, entryIndex] { ShowRenameUVE(entryIndex); }, "F2"), allowed);
    add(prism::MenuItem::Item("Duplicate", [this, absolute, name] {
        if (!EditorUVE::DuplicateContentFileUVE(absolute).has_value()) {
            m_editor.m_contentStatusMessage = "Could not duplicate " + name + ".";
        }
        m_editor.RefreshProjectFileIndexUVE();
        m_folderSignature = 0;
        ChangedUVE();
    }, "", prism::Icon::Duplicate), allowed);
    // Deleting cannot be undone, so it asks once more in a submenu instead of acting on one click.
    std::vector<prism::MenuItem> confirm;
    confirm.push_back(prism::MenuItem::Item(directory ? "Delete folder and everything in it" : "Delete permanently",
                                            [this, absolute, name, rel = entry.relativePath] {
        std::error_code error;
        std::filesystem::remove_all(absolute, error);
        if (error) {
            m_editor.m_contentStatusMessage = "Could not delete " + name + ".";
            return;
        }
        if (m_editor.GetDefaultPlayerEntityUVE() == rel.generic_string()) {
            static_cast<void>(m_editor.SetDefaultPlayerEntityUVE({}));
        }
        if (m_editor.m_selectedProjectFile.has_value() && m_editor.m_selectedProjectFile->relativePath == rel) {
            m_editor.m_selectedProjectFile.reset();
            m_editor.m_selectedAsset.reset();
        }
        m_editor.RefreshProjectFileIndexUVE();
        m_folderSignature = 0;
        ChangedUVE();
    }, "", prism::Icon::Trash));
    prism::MenuItem del = prism::MenuItem::Sub("Delete", std::move(confirm), prism::Icon::Trash);
    if (!allowed) {
        del.disabled();
    }
    items.push_back(std::move(del));
    prism::showMenu(std::move(items), prism::Rect{at.x, at.y, 1.0F, 1.0F}, prism::PopupPlacement::Below);
}

void EditorContentBrowserUVE::ShowRenameUVE(const std::size_t entryIndex) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntryUVE entry = snapshot.entries[entryIndex];
    const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
    auto field = prism::make<prism::TextInput>(entry.relativePath.stem().string(), "New name");
    field->selectAll();
    std::weak_ptr<prism::TextInput> weakField = field;
    const auto apply = [this, weakField, absolute] {
        auto f = weakField.lock();
        if (!f || f->text().empty()) {
            return;
        }
        if (!m_editor.RenameContentAssetUVE(absolute, f->text()).has_value()) {
            m_editor.m_contentStatusMessage = "Could not rename to \"" + f->text() + "\" (empty, taken or not allowed).";
        }
        m_editor.RefreshProjectFileIndexUVE();
        m_folderSignature = 0;
        ChangedUVE();
    };
    field->onCommit = [apply](const std::string&) {
        apply();
        if (prism::Application::exists()) {
            prism::Application::get().closeAllPopups();
        }
    };
    prism::showDialog("Rename " + entry.relativePath.filename().string(), field,
                      {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"Rename", true, apply}}, 360.0F);
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(field.get());
    }
}

void EditorContentBrowserUVE::BeginDragUVE(const std::size_t entryIndex) {
    // A content drag: the payload is the item's content-relative path (the Outliner places entity
    // assets and models dropped on it).
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    if (entryIndex >= snapshot.entries.size() || !prism::Application::exists()) {
        return;
    }
    const Asset::ProjectFileEntryUVE& entry = snapshot.entries[entryIndex];
    auto op = std::make_shared<prism::DragDropOperation>(kDragTagUVE);
    op->setPayload(entry.relativePath.generic_string());
    const std::string name = entry.relativePath.filename().string();
    const EditorIconsUVE::ImageUVE icon = TypeIconUVE(EditorUVE::ClassifyContentBrowserEntryUVE(entry), 16);
    op->setDecorator([name, icon](prism::Canvas& c, const prism::Vec2 mouse) {
        const prism::Theme& t = prism::theme();
        const prism::Rect card{mouse.x + 12.0F, mouse.y + 12.0F, std::min(260.0F, t.ui->width(name) + 40.0F), 26.0F};
        c.shadow(card, 3, 6, prism::Color(0, 0, 0, 140));
        c.fillRoundRect(card, 4.0F, prism::Color(35, 45, 60, 240));
        c.strokeRoundRect(card, 4.0F, t.accent, 1);
        if (icon) {
            c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, prism::Rect{card.x + 6.0F, card.y + 5.0F, 16.0F, 16.0F});
        }
        c.textIn(*t.ui, prism::Rect{card.x + 28.0F, card.y, card.w - 32.0F, card.h}, name, t.textBright, prism::HAlign::Left);
    });
    prism::Application::get().startDragDrop(op);
}

const EditorContentBrowserUVE::PreviewUVE* EditorContentBrowserUVE::PreviewForUVE(
    const Asset::ProjectFileEntryUVE& entry, const ItemTypeUVE type) {
    std::uintptr_t texture = 0U;
    bool renderTarget = false;
    switch (type) {
        case ItemTypeUVE::Texture: texture = m_editor.GetTextureThumbnailUVE(entry.relativePath); break;
        case ItemTypeUVE::Material:
            texture = m_editor.GetMaterialThumbnailUVE(entry.relativePath);
            renderTarget = true;
            break;
        case ItemTypeUVE::Animation: {
            // A clip's icon is drawn on the CPU already: copied as it is (again when it is redrawn).
            // The content root kept from the last listing: a snapshot copy per tile would cost a
            // whole project listing for each of thousands of clips.
            const std::filesystem::path absolute = m_contentRoot / entry.relativePath;
            const EditorUVE::AnimationIconUVE* const icon = m_editor.GetAnimationIconUVE(absolute);
            if (icon == nullptr) {
                return nullptr;
            }
            PreviewUVE& preview = m_previews[entry.relativePath.generic_string()];
            const std::uint64_t generation = icon->serial | (std::uint64_t{1} << 63U);
            if (preview.generation != generation || preview.pixels.empty()) {
                preview.pixels = icon->pixels;
                preview.width = icon->width;
                preview.height = icon->height;
                preview.texture = 0U;
                preview.generation = generation;
            }
            return &preview;
        }
        case ItemTypeUVE::Entity:
        case ItemTypeUVE::Prefab:
            // An entity's own look: its models and primitives through the thumbnail's camera.
        case ItemTypeUVE::Mesh:
        case ItemTypeUVE::Model:
            // The mesh preview is drawn upright already (its projection is flipped for top-down
            // images), so it is read as it is; reading it bottom-up stood the model on its head.
            texture = m_editor.GetMeshThumbnailUVE(entry.relativePath);
            renderTarget = false;
            break;
        default: return nullptr;
    }
    if (texture == 0U) {
        return nullptr;
    }
    PreviewUVE& preview = m_previews[entry.relativePath.generic_string()];
    const std::uint64_t generation = m_editor.m_meshThumbnailGeneration;
    if (preview.texture == texture && preview.generation == generation && !preview.pixels.empty()) {
        return &preview;
    }
    // Read the engine's GL texture back once; PRISM draws CPU-side pixels.
    if (!ReadTexturePixelsUVE(texture, renderTarget, preview.width, preview.height, preview.pixels)) {
        return nullptr;
    }
    preview.texture = texture;
    preview.generation = generation;
    return &preview;
}

void EditorContentBrowserUVE::SelectEntryUVE(const std::size_t entryIndex, const bool additive, const bool range) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntryUVE& entry = snapshot.entries[entryIndex];
    m_editor.m_contentSelection.ClickUVE(m_editor.m_contentShownOrderPrevious, entry.relativePath.generic_string(),
                                         additive, range);
    m_editor.m_selectedProjectFile = entry;
    if (entry.registeredAssetGuid.has_value()) {
        m_editor.m_selectedAsset = Asset::AssetRecordUVE{*entry.registeredAssetGuid, snapshot.contentRoot / entry.relativePath};
    } else {
        m_editor.m_selectedAsset.reset();
    }
}

void EditorContentBrowserUVE::OpenEntryUVE(const std::size_t entryIndex) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntryUVE entry = snapshot.entries[entryIndex];
    const std::string name = entry.relativePath.filename().string();
    const std::filesystem::path full = snapshot.contentRoot / entry.relativePath;
    // The same actions the previous Content Browser ran on a double click.
    if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
        GoToFolderUVE(entry.relativePath);
        return;
    }
    switch (EditorUVE::ClassifyContentBrowserEntryUVE(entry)) {
        case ItemTypeUVE::Entity:
            if (!m_editor.OpenEntityEditorUVE(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Entity Editor.";
            }
            break;
        case ItemTypeUVE::Scene:
            if (!m_editor.OpenSceneAssetUVE(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " as a scene.";
            }
            break;
        case ItemTypeUVE::AnimationMontage:
            if (!m_editor.OpenMontageEditorUVE(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Montage Editor.";
            }
            break;
        case ItemTypeUVE::MontageLibrary:
            ShowMontageLibraryUVE(full);
            break;
        case ItemTypeUVE::AnimationLibrary:
            if (!m_editor.OpenAnimationLibraryUVE(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " as an animation library.";
            }
            break;
        case ItemTypeUVE::Blueprint:
            OpenBlueprintUVE(full);
            break;
        case ItemTypeUVE::WidgetBlueprint:
            if (!m_editor.OpenEntityEditorUVE(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Widget Editor.";
            }
            break;
        case ItemTypeUVE::Script:
            if (!m_editor.OpenUVScriptPathUVE(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Script Editor.";
            }
            break;
        default:
            break;
    }
}

void EditorContentBrowserUVE::ShowFilterMenuUVE() {
    static constexpr std::array<std::pair<FocusUVE, const char*>, 12> kFocusesUVE{{
        {FocusUVE::All, "All"},
        {FocusUVE::Folders, "Folders"},
        {FocusUVE::Scene, "Scenes"},
        {FocusUVE::Prefab, "Prefabs"},
        {FocusUVE::Bundle, "Bundles"},
        {FocusUVE::Mesh, "Meshes"},
        {FocusUVE::Texture, "Textures"},
        {FocusUVE::Shader, "Shaders"},
        {FocusUVE::Material, "Materials"},
        {FocusUVE::Save, "Saves"},
        {FocusUVE::Registered, "Registered"},
        {FocusUVE::OtherFiles, "Other files"},
    }};
    std::vector<prism::MenuItem> items;
    for (const auto& [focus, label] : kFocusesUVE) {
        items.push_back(prism::MenuItem::Check(label, m_editor.m_contentBrowserTypeFocus == focus, [this, f = focus] {
            m_editor.m_contentBrowserTypeFocus = f;
            m_tileSignature = 0;
        }));
    }
    prism::showMenu(std::move(items), m_filterButton ? m_filterButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

// ---------------------------------------------------------------------------------- montages

void EditorContentBrowserUVE::CreateMontageFromClipUVE(const std::filesystem::path& clipPath) {
    Asset::AnimationClipAssetUVE clip;
    if (!Asset::LoadAnimationClipAssetUVE(clipPath, clip)) {
        m_editor.m_contentStatusMessage = "Could not read " + clipPath.filename().string();
        return;
    }
    Asset::IAssetDatabaseUVE& database = m_editor.m_services->GetAssetDatabaseUVE();
    const Asset::AssetGuidUVE guid = database.RegisterUVE(clipPath);
    Asset::AnimationMontageAssetUVE montage;
    static_cast<void>(Asset::AddMontageSegmentUVE(montage, guid, clip.durationSeconds));
    // AM_<clip>, or AM_<clip>_2 ... beside the clip.
    std::filesystem::path path = clipPath.parent_path() / ("AM_" + clipPath.stem().string() + ".uvmontage");
    std::error_code error;
    for (int n = 2; std::filesystem::exists(path, error) && n < 1000; ++n) {
        path = clipPath.parent_path() / ("AM_" + clipPath.stem().string() + "_" + std::to_string(n) + ".uvmontage");
    }
    if (!Asset::SaveAnimationMontageAssetUVE(montage, path)) {
        m_editor.m_contentStatusMessage = "Could not write " + path.filename().string();
        return;
    }
    static_cast<void>(database.RegisterUVE(path));
    static_cast<void>(m_editor.m_services->GetProjectFileIndexUVE().RefreshUVE(m_editor.m_services->GetAssetDatabaseUVE()));
    m_editor.m_contentStatusMessage = "Created " + path.filename().string();
    static_cast<void>(m_editor.OpenMontageEditorUVE(path));
}

void EditorContentBrowserUVE::AddMontageToLibraryUVE(const std::filesystem::path& montagePath, const std::filesystem::path& libraryPath) {
    Asset::MontageLibraryAssetUVE library;
    std::error_code error;
    if (std::filesystem::exists(libraryPath, error)) {
        if (!Asset::LoadMontageLibraryAssetUVE(libraryPath, library)) {
            m_editor.m_contentStatusMessage = "Not a montage library: " + libraryPath.filename().string();
            return;
        }
    } else {
        library.libraryId = libraryPath.stem().string();
    }
    Asset::IAssetDatabaseUVE& database = m_editor.m_services->GetAssetDatabaseUVE();
    const Asset::AssetGuidUVE guid = database.RegisterUVE(montagePath);
    if (std::ranges::any_of(library.entries, [guid](const Asset::MontageLibraryEntryUVE& e) { return e.montage == guid; })) {
        m_editor.m_contentStatusMessage = montagePath.stem().string() + " is already in " + libraryPath.stem().string();
        return;
    }
    Asset::MontageLibraryEntryUVE entry;
    entry.montage = guid;
    entry.name = montagePath.stem().string();
    library.entries.push_back(std::move(entry));
    if (!Asset::SaveMontageLibraryAssetUVE(library, libraryPath)) {
        m_editor.m_contentStatusMessage = "Could not write " + libraryPath.filename().string();
        return;
    }
    static_cast<void>(database.RegisterUVE(libraryPath));
    static_cast<void>(m_editor.m_services->GetProjectFileIndexUVE().RefreshUVE(m_editor.m_services->GetAssetDatabaseUVE()));
    m_editor.m_contentStatusMessage = "Added " + montagePath.stem().string() + " to " + libraryPath.stem().string();
}

void EditorContentBrowserUVE::ShowMontageLibraryUVE(const std::filesystem::path& libraryPath) {
    Asset::MontageLibraryAssetUVE library;
    if (!Asset::LoadMontageLibraryAssetUVE(libraryPath, library)) {
        m_editor.m_contentStatusMessage = "Could not read " + libraryPath.filename().string();
        return;
    }
    // Its montages, each with the name a script plays it by, opened or taken off the list.
    Asset::IAssetDatabaseUVE& database = m_editor.m_services->GetAssetDatabaseUVE();
    auto list = prism::vbox(4.0F);
    list->setPadding(prism::Margin(4.0F));
    if (library.entries.empty()) {
        auto hint = prism::make<prism::Label>("Empty. Right-click a montage > Add to Montage Library.", prism::FontRole::Small);
        hint->setColor(prism::ThemeColor(&prism::Theme::textDim));
        list->add(hint);
    }
    for (const Asset::MontageLibraryEntryUVE& entry : library.entries) {
        const std::filesystem::path montage = database.ResolveUVE(entry.montage);
        auto row = prism::hbox(6.0F);
        auto label = prism::make<prism::Label>(entry.name + (montage.empty() ? "   (missing)" : "   " + montage.filename().string()));
        row->add(label, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        auto open = prism::make<prism::Button>("Open", prism::Icon::Open, prism::ButtonStyle::Flat);
        open->setEnabled(!montage.empty());
        open->onClick = [this, montage] {
            if (prism::Application::exists()) {
                prism::Application::get().closeAllPopups();
            }
            static_cast<void>(m_editor.OpenMontageEditorUVE(montage));
        };
        auto remove = prism::make<prism::Button>("Remove", prism::Icon::Trash, prism::ButtonStyle::Flat);
        remove->onClick = [this, libraryPath, guid = entry.montage] {
            Asset::MontageLibraryAssetUVE current;
            if (Asset::LoadMontageLibraryAssetUVE(libraryPath, current)) {
                std::erase_if(current.entries, [guid](const Asset::MontageLibraryEntryUVE& e) { return e.montage == guid; });
                static_cast<void>(Asset::SaveMontageLibraryAssetUVE(current, libraryPath));
            }
            if (prism::Application::exists()) {
                prism::Application::get().closeAllPopups();
            }
            ShowMontageLibraryUVE(libraryPath);
        };
        row->add(open);
        row->add(remove);
        list->add(row);
    }
    prism::showDialog("Montage Library - " + libraryPath.stem().string(), list, {prism::DialogButton{"Close", true, {}}}, 480.0F);
}

} // namespace UVE::Editor
