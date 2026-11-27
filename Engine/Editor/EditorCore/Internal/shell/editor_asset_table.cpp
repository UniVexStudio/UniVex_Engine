// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_asset_table.h"
#include "editor_layout_store.h"
#include <functional>
#include <iterator>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include "blueprint_asset.h"
#include "texture_procedural.h"

#include "editor_asset_thumbnail.h"

#include "editor.h"
#include "editor_asset_table_model.h"
#include "editor_content_catalogue.h"
#include "mesh_component.h"

#include "animation_clip_asset.h"
#include "motion_montage_asset.h"
#include "motion_montage_editing.h"
#include "i_asset_database.h"
#include "i_project_file_index.h"
#include "material_asset.h"
#include "vfx_templates.h"
#include "montage_library_asset.h"

#include "prism/Application.h"

#include "gl_system_headers.h"
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
#include <unordered_map>
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

void Hash(std::uint64_t& h, const std::uint64_t v) {
    h ^= v + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
}

void Hash(std::uint64_t& h, const std::string& s) {
    for (const char c : s) {
        Hash(h, static_cast<std::uint64_t>(static_cast<unsigned char>(c)));
    }
}

[[nodiscard]] std::string LowerExtension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension;
}

[[nodiscard]] bool IsEntityAsset(const std::filesystem::path& path) {
    const std::string extension = LowerExtension(path);
    return extension == ".uventity" || extension == ".uvprefab";
}

/// `path` is `folder` or lies inside it (generic content-relative paths).
[[nodiscard]] bool IsSameOrInside(const std::string& path, const std::string& folder) {
    return path == folder ||
           (!folder.empty() && path.size() > folder.size() && path.starts_with(folder) && path[folder.size()] == '/');
}

/// `paths` without repeats, the content root, or any path inside another listed folder.
[[nodiscard]] std::vector<std::filesystem::path> TopLevelContentPaths(const std::vector<std::filesystem::path>& paths) {
    std::vector<std::string> names;
    names.reserve(paths.size());
    for (const std::filesystem::path& path : paths) {
        names.push_back(path.generic_string());
    }
    std::vector<std::filesystem::path> top;
    std::vector<std::string> topNames;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        const std::string& name = names[i];
        if (name.empty() || std::ranges::find(topNames, name) != topNames.end()) {
            continue;
        }
        const bool inside = std::ranges::any_of(names, [&name](const std::string& other) {
            return other != name && IsSameOrInside(name, other);
        });
        if (!inside) {
            top.push_back(paths[i]);
            topNames.push_back(name);
        }
    }
    return top;
}

[[nodiscard]] std::string CountItems(const std::size_t count) {
    return count == 1U ? std::string("1 item") : std::to_string(count) + " items";
}

[[nodiscard]] std::string FormatSize(const std::uintmax_t bytes) {
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

[[nodiscard]] std::string FormatTime(const std::int64_t seconds) {
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

[[nodiscard]] std::int64_t StartOfToday() {
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
/// The narrowest the sidebar gets side by side: its create row ("+ Add | Save All") fits.
constexpr float kSidebarMinWidth = 196.0F;

constexpr std::array<std::pair<const char*, ContentSortKey>, 5> kDetailsColumns{{
    {"Name", ContentSortKey::Name},
    {"Type", ContentSortKey::Type},
    {"Size", ContentSortKey::Size},
    {"Modified", ContentSortKey::Modified},
    {"Folder", ContentSortKey::Folder},
}};

/// The tile grid: tiles fill each row edge to edge and re-flow with the width. A narrow area still
/// fits at least kMinColumns tiles (they shrink, never below kMinTile); a wide one fits as
/// many of the chosen size as there is room for. List rows keep their height and fill columns.
class TileGrid final : public prism::Widget {
public:
    static constexpr int kMinColumns = 4;
    static constexpr float kMinTile = 56.0F;
    static constexpr float kGap = 6.0F;

    TileGrid(const prism::Vec2 baseSize, const bool rows) : m_base(baseSize), m_rows(rows) {}
    void add(prism::Ref<prism::Widget> w) { addChild(std::move(w)); }

    /// Columns and tile width for a row `width` wide.
    [[nodiscard]] static std::pair<int, float> Fit(const float width, const float baseWidth, const bool rows) {
        const float gap = rows ? 2.0F : kGap;
        int columns = std::max(1, static_cast<int>(std::floor((width + gap) / (baseWidth + gap))));
        if (!rows) {
            columns = std::max(columns, kMinColumns);
            const int fitMin = std::max(1, static_cast<int>(std::floor((width + gap) / (kMinTile + gap))));
            columns = std::min(columns, std::max(1, fitMin));
        }
        const float tile = std::max(rows ? 120.0F : kMinTile, (width - gap * static_cast<float>(columns - 1)) /
                                                                       static_cast<float>(columns));
        return {columns, tile};
    }

    prism::Vec2 measure(const prism::Vec2 avail) override {
        const float width = std::max(avail.x, 1.0F);
        const auto [columns, tile] = Fit(width, m_base.x, m_rows);
        const float tileH = m_rows ? m_base.y : std::floor(tile * (m_base.y / m_base.x));
        const int count = static_cast<int>(children().size());
        const int lines = (count + columns - 1) / std::max(1, columns);
        const float gap = m_rows ? 1.0F : kGap;
        for (const auto& child : children()) {
            child->measure(prism::Vec2(tile, tileH));
        }
        return m_desired = constrain(prism::Vec2(width, lines > 0 ? lines * tileH + (lines - 1) * gap : 0.0F));
    }
    void arrange(const prism::Rect r) override {
        prism::Widget::arrange(r);
        const auto [columns, tile] = Fit(r.w, m_base.x, m_rows);
        const float tileH = m_rows ? m_base.y : std::floor(tile * (m_base.y / m_base.x));
        const float gapX = m_rows ? 2.0F : kGap;
        const float gapY = m_rows ? 1.0F : kGap;
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

prism::Icon EditorAssetTable::IconFor(const ItemType type) {
    switch (type) {
        case ItemType::Folder: return prism::Icon::Folder;
        case ItemType::Scene: return prism::Icon::SceneAsset;
        case ItemType::Prefab: return prism::Icon::Prefab;
        case ItemType::Entity: return prism::Icon::EntityAsset;
        case ItemType::Bundle: return prism::Icon::Bundle;
        case ItemType::Mesh: return prism::Icon::MeshAsset;
        case ItemType::Model: return prism::Icon::Model;
        case ItemType::Texture: return prism::Icon::NodeTexture;
        case ItemType::Shader: return prism::Icon::Shader;
        case ItemType::Material: return prism::Icon::Material;
        case ItemType::Save: return prism::Icon::Save;
        case ItemType::Animation: return prism::Icon::AnimClip;
        case ItemType::AnimationLibrary: return prism::Icon::AnimLibrary;
        case ItemType::MotionMontage: return prism::Icon::AnimMontage;
        case ItemType::ParticleSystem: return prism::Icon::Light;
        case ItemType::MontageLibrary: return prism::Icon::MontageLibrary;
        case ItemType::Blueprint: return prism::Icon::BlueprintClass;
        case ItemType::WidgetBlueprint: return prism::Icon::BlueprintClass;
        case ItemType::Script: return prism::Icon::Script;
        case ItemType::Audio: return prism::Icon::NodeSound;
        case ItemType::Font: return prism::Icon::NodeText;
        case ItemType::File: return prism::Icon::File;
    }
    return prism::Icon::File;
}

std::pair<std::string, std::string> EditorAssetTable::Describe(const ItemType type) {
    switch (type) {
        case ItemType::Folder: return {"Folder", "DIR"};
        case ItemType::Scene: return {"World Level", "LVL"};
        case ItemType::Prefab: return {"Prefab", "PFB"};
        case ItemType::Entity: return {"Entity", "ENT"};
        case ItemType::Bundle: return {"Bundle", "BND"};
        case ItemType::Mesh: return {"Static Mesh", "SM"};
        case ItemType::Model: return {"Skeletal Mesh", "SK"};
        case ItemType::Texture: return {"Texture 2D", "T2D"};
        case ItemType::Shader: return {"Shader", "SH"};
        case ItemType::Material: return {"Material", "MAT"};
        case ItemType::Save: return {"Save", "SAV"};
        case ItemType::Animation: return {"Animation", "ANM"};
        case ItemType::AnimationLibrary: return {"Animation Library", "LIB"};
        case ItemType::MotionMontage: return {"Motion Montage", "MM"};
        case ItemType::ParticleSystem: return {"Particle System", "VFX"};
        case ItemType::MontageLibrary: return {"Motion Montage Library", "ACL"};
        case ItemType::Blueprint: return {"Blueprint Class", "BP"};
        case ItemType::WidgetBlueprint: return {"Widget Blueprint", "WBP"};
        case ItemType::Script: return {"Script", "UVS"};
        case ItemType::Audio: return {"Audio", "AUD"};
        case ItemType::Font: return {"Font", "FNT"};
        case ItemType::File: return {"File", "FILE"};
    }
    return {"File", "FILE"};
}

prism::Color EditorAssetTable::TintFor(const ItemType type) {
    switch (type) {
        case ItemType::Folder: return prism::Color(214, 176, 92);
        case ItemType::Scene: return prism::Color(120, 170, 235);
        case ItemType::Entity:
        case ItemType::Prefab: return prism::Color(110, 200, 160);
        case ItemType::Mesh:
        case ItemType::Model: return prism::Color(160, 200, 230);
        case ItemType::Texture: return prism::Color(220, 140, 110);
        case ItemType::Material: return prism::Color(200, 160, 110);
        case ItemType::Script:
        case ItemType::Shader: return prism::Color(170, 150, 230);
        case ItemType::Animation:
        case ItemType::AnimationLibrary:
        case ItemType::MotionMontage:
        case ItemType::MontageLibrary: return prism::Color(90, 200, 200);
        case ItemType::ParticleSystem: return prism::Color(240, 130, 70);
        case ItemType::Audio: return prism::Color(230, 190, 90);
        case ItemType::Blueprint: return prism::Color(80, 150, 240);
        case ItemType::WidgetBlueprint: return prism::Color(70, 190, 140);
        default: return prism::Color(170, 176, 186);
    }
}



/// One item in the tile area. Assets are PRISM's asset tile around the engine's preview; folders
/// are a large folder glyph with the name under it (no card), the way a content search shows them.
/// A right click opens the item's menu; dragging it starts a content drag (to the Outliner, ...).
class EditorAssetTable::Tile final : public prism::Widget {
public:
    Tile(EditorAssetTable& owner, const std::size_t entryIndex, std::string name, const ItemType type,
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
            const EditorIcons::Image icon = m_owner.TypeIcon(m_type, 16);
            const prism::Rect box{r.x + 6.0F, std::floor(r.y + (r.h - 16.0F) * 0.5F), 16.0F, 16.0F};
            if (icon) {
                c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, box);
            }
            c.textIn(*t.ui, prism::Rect{r.x + 28.0F, r.y, r.w - 32.0F, r.h}, m_name, t.text, prism::HAlign::Left, true);
            return;
        }
        if (m_type == ItemType::Folder) {
            if (m_selected || m_hot) {
                c.fillRoundRect(r, 4.0F, m_selected ? t.rowSelected : t.rowHover);
            }
            const float labelH = 30.0F;
            const float side = std::min(r.w - 12.0F, r.h - labelH - 6.0F);
            const prism::Rect box{std::floor(r.x + (r.w - side) * 0.5F), r.y + 6.0F, side, side};
            PaintFolder(c, box);
            c.textIn(*t.ui, prism::Rect{r.x + 2.0F, r.bottom() - labelH, r.w - 4.0F, labelH}, m_name, t.textBright,
                     prism::HAlign::Center, true);
            return;
        }
        // PRISM's content tile and thumbnail, fed with the engine's own classification of the file.
        prism::AssetRecord record{};
        record.name = m_name;
        record.path = m_name;
        record.icon = IconFor(m_type);
        record.badgeColor = TintFor(m_type);
        const auto [typeName, badge] = Describe(m_type);
        record.type = typeName;
        record.badgeText = badge;
        record.thumbnail = m_preview;
        record.thumbnailW = m_previewW;
        record.thumbnailH = m_previewH;
        record.thumbnailScale = m_previewScale;
        prism::paintAssetTile(c, r, record, m_selected, m_hot);
        if (m_type == ItemType::Blueprint || m_type == ItemType::WidgetBlueprint) {
            // A Blueprint's well is the word, not a glyph.
            const prism::Rect well{r.x + 3.0F, r.y + 3.0F, r.w - 6.0F, prism::assetTileWellHeight(r) - 17.0F};
            c.fillRect(well, m_hot && !m_selected ? prism::Color(40, 41, 45) : prism::Color(30, 31, 34));
            c.textIn(*t.medium, well, m_type == ItemType::Blueprint ? "Blueprint" : "Widget Blueprint", prism::Color(214, 220, 230),
                     prism::HAlign::Center, false);
        }
    }

    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button == prism::MouseButton::Left) {
            m_owner.SelectEntry(m_entryIndex, (e.mods & prism::ModCtrl) != 0, (e.mods & prism::ModShift) != 0);
            if (e.clicks >= 2) {
                m_pressed = false;
                m_owner.OpenEntry(m_entryIndex);
                return true;
            }
            m_pressed = true;
            m_pressPos = e.pos;
            return true;
        }
        if (e.button == prism::MouseButton::Right) {
            if (!m_selected) {
                m_owner.SelectEntry(m_entryIndex, false, false);
            }
            m_owner.ShowItemMenu(m_entryIndex, e.pos);
            return true;
        }
        return false;
    }
    bool onMouseMove(const prism::MouseEvent& e) override {
        if (m_pressed && (e.pos - m_pressPos).length() > 6.0F) {
            m_pressed = false;
            m_owner.BeginDrag(m_entryIndex);
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
    static void PaintFolder(prism::Canvas& c, const prism::Rect b) {
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

    EditorAssetTable& m_owner;
    std::size_t m_entryIndex = 0;
    std::string m_name;
    ItemType m_type = ItemType::File;
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
class EditorAssetTable::ItemsArea final : public prism::Border {
public:
    explicit ItemsArea(EditorAssetTable& owner) : m_owner(owner) {}
    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button == prism::MouseButton::Right) {
            m_owner.ShowEmptyMenu(e.pos);
            return true;
        }
        if (e.button == prism::MouseButton::Left) {
            m_owner.m_editor.m_contentSelection.Clear();
            m_owner.m_editor.m_selectedProjectFile.reset();
            m_owner.m_editor.m_selectedAsset.reset();
            m_owner.Changed();
            return true;
        }
        return false;
    }
    // Delete here removes the picked files; it must not go on to the editor's object Delete.
    bool onKeyDown(const prism::KeyEvent& e) override {
        if (e.key != prism::Key::Delete) {
            return false;
        }
        if (!e.repeat) {
            m_owner.AskDeleteContent(m_owner.PickedContent());
        }
        return true;
    }
    bool focusable() const override { return true; }

private:
    EditorAssetTable& m_owner;
};

EditorAssetTable::EditorAssetTable(Editor& editor) : m_editor(editor) {}

EditorAssetTable::~EditorAssetTable() = default;

prism::Ref<prism::Widget> EditorAssetTable::SectionHeader(const std::string& title, const bool open,
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

prism::Ref<prism::Widget> EditorAssetTable::Build() {
    // ---- sidebar: Create (catalogue with icons), Favorites, Content (folders), Shelves.
    auto left = prism::vbox(0.0F);
    const auto sized = [](prism::Ref<prism::Widget> w, const float h) {
        auto box = prism::make<prism::SizeBox>();
        box->setHeightOverride(h);
        box->setContent(std::move(w));
        return box;
    };

    // The create row: "+ Add" opens the whole catalogue as a menu (the same menu as a right click
    // on empty space, including Import and Import Folder), with Save All beside it.
    auto addButton = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Success);
    addButton->setTooltip("Create or import in this folder: folders, scenes, entities, scripts, materials ...");
    std::weak_ptr<prism::Button> weakAdd = addButton;
    addButton->onClick = [this, weakAdd] {
        const auto anchor = weakAdd.lock();
        prism::showSearchMenu(CatalogueMenu(), anchor ? anchor->rect() : prism::Rect{}, prism::PopupPlacement::Below,
                              "Start typing to search", true);
    };
    auto saveAllButton = prism::make<prism::Button>("Save All", prism::Icon::Save, prism::ButtonStyle::Toolbar);
    saveAllButton->setTooltip("Save everything (Ctrl+Shift+S)");
    saveAllButton->onClick = [this] { static_cast<void>(m_editor.RunEditorCommand("file.saveAll")); };
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
            Reveal(m_favoriteIds[static_cast<std::size_t>(id - 1)]);
        }
    };
    m_favorites->onContext = [this](prism::TreeNode* node, const prism::Vec2 at) {
        if (node == nullptr || node->id <= 0 || static_cast<std::size_t>(node->id) > m_favoriteIds.size()) {
            return;
        }
        const std::filesystem::path path = m_favoriteIds[static_cast<std::size_t>(node->id - 1)];
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Show in Folder", [this, path] { Reveal(path); }));
        items.push_back(prism::MenuItem::Item("Unpin", [this, path] {
            m_editor.ToggleProjectPathFavorite(path);
            m_favoriteSignature = 0;
        }));
        prism::showMenu(std::move(items), prism::Rect{at.x, at.y, 1.0F, 1.0F}, prism::PopupPlacement::Below);
    };
    auto favoritesSection = prism::vbox(0.0F);
    favoritesSection->add(SectionHeader("Favorites", true, m_favorites, nullptr));
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
            GoToFolder(std::filesystem::path(m_folderIds[static_cast<std::size_t>(id - 1)]));
        }
    };
    m_folders->onContext = [this](prism::TreeNode* node, const prism::Vec2 at) {
        if (node == nullptr || node->id <= 0 || static_cast<std::size_t>(node->id) > m_folderIds.size()) {
            return;
        }
        const std::filesystem::path relFolder(m_folderIds[static_cast<std::size_t>(node->id - 1)]);
        const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        const std::string folderPrefix = relFolder.generic_string();
        std::size_t animCount = 0U;
        for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
            if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
                continue;
            }
            const std::string p = entry.relativePath.generic_string();
            const bool inside = folderPrefix.empty() || p == folderPrefix ||
                                (p.size() > folderPrefix.size() && p.compare(0U, folderPrefix.size(), folderPrefix) == 0 &&
                                 p[folderPrefix.size()] == '/');
            if (!inside) {
                continue;
            }
            const std::string ext = LowerExtension(entry.relativePath);
            if (ext == ".uvanim" || ext == ".uvmodel") {
                ++animCount;
            } else if (Editor::IsModelSourcePath(entry.relativePath)) {
                const EditorModelSourceInfo* const src = m_editor.FindModelSourceInfo(entry.relativePath);
                if (src == nullptr || src->rigged || src->hasSkeleton || src->animationCount > 0U || src->animationOnly) {
                    ++animCount;
                }
            }
        }
        const std::size_t directAnimCount = m_editor.CollectAnimationClipsInFolder(relFolder).size();
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Open", [this, relFolder] { GoToFolder(relFolder); }, "", prism::Icon::FolderOpen));
        if (directAnimCount > 0U) {
            prism::MenuItem createLib = prism::MenuItem::Item(
                "Create Animation Library (" + std::to_string(directAnimCount) + ")",
                [this, relFolder] {
                    if (m_editor.CreateAnimationLibraryFromFolder(relFolder).has_value()) {
                        Changed();
                    }
                },
                "", prism::Icon::AnimLibrary);
            if (!m_editor.IsAuthoringCommandAllowed()) {
                createLib.disabled();
            }
            items.push_back(std::move(createLib));
        }
        if (animCount > 0U) {
            prism::MenuItem retarget = prism::MenuItem::Item(
                "Retarget Folder (" + std::to_string(animCount) + ")...",
                [this, relFolder] { m_editor.OpenRetargetWindowForFolder(relFolder); });
            if (!m_editor.IsAuthoringCommandAllowed()) {
                retarget.disabled();
            }
            items.push_back(std::move(retarget));
        }
        prism::showMenu(std::move(items), prism::Rect{at.x, at.y, 1.0F, 1.0F}, prism::PopupPlacement::Below);
    };
    auto contentSection = prism::vbox(0.0F);
    contentSection->add(SectionHeader("Content", true, m_folders, searchToggle));
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
            m_editor.m_assetTableShelf = m_shelfNames[static_cast<std::size_t>(id - 1)];
            m_editor.m_contentSelection.Clear();
            Changed();
        }
    };
    auto shelvesBody = sized(m_shelves, 96.0F);
    shelvesBody->setVisible(false);
    auto shelvesSection = prism::vbox(0.0F);
    shelvesSection->add(SectionHeader("Shelves", false, shelvesBody, nullptr));
    shelvesSection->add(shelvesBody);
    // The sections share the sidebar through dividers, so each can be made taller or shorter.
    auto sections = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    sections->add(favoritesSection, prism::Splitter::Pane{66.0F, 24.0F, 400.0F, false});
    sections->add(contentSection, prism::Splitter::Pane{160.0F, 60.0F, 1e9F, true});
    EditorLayoutStore::Instance().BindSplitter("assetTable.sections", *sections);
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
    m_viewButton->onClick = [this] { ShowViewMenu(); };
    m_splitButton = prism::make<prism::Button>("Split", prism::Icon::Frame, prism::ButtonStyle::Toolbar);
    m_splitButton->setTooltip("How the sidebar and the items share the Asset Table");
    m_splitButton->onClick = [this] { ShowSplitMenu(); };
    m_filterButton = prism::make<prism::Button>("Filter", prism::Icon::Filter, prism::ButtonStyle::Toolbar);
    m_filterButton->onClick = [this] { ShowFilterMenu(); };
    m_itemSearch = prism::make<prism::TextInput>("", "Search items");
    m_itemSearch->setLeadingIcon(prism::Icon::Search);
    m_itemSearch->onChanged = [this](const std::string& text) {
        m_editor.m_assetFilter = text;
        Changed();
    };
    bar->add(m_viewButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_splitButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_filterButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    bar->add(m_itemSearch, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    right->add(bar);
    m_path = prism::make<prism::Label>("Content", prism::FontRole::Small);
    m_path->setColor(prism::ThemeColor(&prism::Theme::textDim));
    right->add(m_path);
    m_itemsHost = prism::make<ItemsArea>(*this);
    right->add(m_itemsHost, prism::Slot{1.0F});
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    right->add(m_status);
    m_items = right;

    m_root = prism::make<prism::Border>();
    m_splitMode = m_editor.m_assetTableSplitMode ? SplitMode::SideBySide : SplitMode::ItemsOnly;
    ApplySplitMode();
    return m_root;
}

void EditorAssetTable::ApplySplitMode() {
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
    const float ratio = std::clamp(m_editor.m_assetTableSplitRatio, 0.12F, 0.7F);
    switch (m_splitMode) {
        case SplitMode::SideBySide:
        case SplitMode::Stacked: {
            const bool stacked = m_splitMode == SplitMode::Stacked;
            m_split = prism::make<prism::Splitter>(stacked ? prism::Orientation::Vertical : prism::Orientation::Horizontal);
            // Wide enough for the "+ Add | Save All | Import" row.
            m_split->add(m_sidebar, prism::Splitter::Pane{stacked ? 220.0F : std::max(kSidebarMinWidth, ratio * 640.0F),
                                                          stacked ? 120.0F : kSidebarMinWidth, stacked ? 600.0F : 700.0F, false});
            m_split->add(m_items, prism::Splitter::Pane{600.0F, 160.0F, 1e9F, true});
            EditorLayoutStore::Instance().BindSplitter(stacked ? "assetTable.splitV" : "assetTable.splitH", *m_split);
            m_root->setChild(m_split);
            break;
        }
        case SplitMode::ItemsOnly: m_root->setChild(m_items); break;
        case SplitMode::SidebarOnly: m_root->setChild(m_sidebar); break;
    }
    m_editor.m_assetTableSplitMode = m_splitMode != SplitMode::ItemsOnly;
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

void EditorAssetTable::Refresh() {
    RebuildFolders();
    RebuildFavorites();
    RebuildItems();
    if (m_status && m_status->text() != m_editor.m_contentStatusMessage && !m_editor.m_contentStatusMessage.empty()) {
        m_status->setText(m_editor.m_contentStatusMessage);
    }
}

void EditorAssetTable::Changed() {
    m_tileSignature = 0;
}

void EditorAssetTable::GoToFolder(const std::filesystem::path& directory) {
    m_editor.m_assetTableDirectory = directory;
    m_editor.m_assetTableShelf.clear();
    m_editor.m_selectedProjectFile.reset();
    m_editor.m_selectedAsset.reset();
    m_editor.m_contentSelection.Clear();
    // Files copied into the folder from outside (a file manager, USB) show up and start importing on open.
    m_editor.RefreshProjectFileIndex();
    Changed();
}

void EditorAssetTable::Reveal(const std::filesystem::path& relativePath) {
    // Go to where it lives and pick it there (a folder opens itself).
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
        if (entry.relativePath != relativePath) {
            continue;
        }
        if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
            GoToFolder(entry.relativePath);
            return;
        }
        GoToFolder(entry.relativePath.parent_path());
        m_editor.m_selectedProjectFile = entry;
        m_editor.m_assetFilter.clear();
        if (m_itemSearch) {
            m_itemSearch->setText("");
        }
        return;
    }
}

void EditorAssetTable::RevealFile(const std::filesystem::path& absolutePath) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    std::error_code error;
    const std::filesystem::path relative = std::filesystem::relative(absolutePath, snapshot.contentRoot, error);
    if (!error && !relative.empty()) {
        Reveal(relative);
    }
}

std::string EditorAssetTable::DisplayName(const Asset::ProjectFileEntry& entry) const {
    return entry.relativePath.filename().string();
}

EditorIcons::Image EditorAssetTable::TypeIcon(const ItemType type, const int size) {
    return m_icons.ContentType(Editor::GetAssetTableItemTypeLabel(type), size, IconFor(type));
}

void EditorAssetTable::RebuildFavorites() {
    if (!m_favorites) {
        return;
    }
    // The project listing is copied only when the favourites have to be built again.
    std::uint64_t h = m_editor.m_services->GetProjectFileIndex().GetRefreshGeneration();
    for (const std::filesystem::path& p : m_editor.m_favoriteProjectPaths) {
        Hash(h, p.generic_string());
    }
    if (h == m_favoriteSignature) {
        return;
    }
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    m_favoriteSignature = h;
    m_syncingFolders = true;
    m_favorites->clear();
    m_favoriteIds.clear();
    // Favorites that exist in this snapshot (one not yet scanned stays stored, just not shown).
    for (const std::filesystem::path& favorite : m_editor.m_favoriteProjectPaths) {
        const auto it = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                                     [&](const Asset::ProjectFileEntry& e) { return e.relativePath == favorite; });
        if (it == snapshot.entries.end()) {
            continue;
        }
        m_favoriteIds.push_back(favorite);
        const ItemType type = m_editor.GetAssetTableEntryType(*it);
        prism::TreeNode* node = m_favorites->addNode(nullptr, favorite.filename().string(), IconFor(type),
                                                     static_cast<int>(m_favoriteIds.size()));
        node->toggles = false;
        if (const EditorIcons::Image icon = TypeIcon(type, 16); icon) {
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

void EditorAssetTable::RebuildFolders() {
    std::uint64_t h = m_editor.m_services->GetProjectFileIndex().GetRefreshGeneration();
    Hash(h, m_editor.m_contentTreeFilter);
    Hash(h, m_editor.m_assetTableDirectory.generic_string());
    if (h == m_folderSignature || !m_folders) {
        return;
    }
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    m_folderSignature = h;
    m_syncingFolders = true;
    if (m_shelves) {
        m_shelves->clear();
        m_shelfNames.clear();
        for (const ContentShelf& shelf : m_editor.m_contentShelves.GetAll()) {
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
    std::vector<std::string> folders = CollectVisibleContentFolders(snapshot.entries, m_editor.m_contentTreeFilter);
    // The content root itself.
    m_folderIds.emplace_back("");
    prism::TreeNode* root = m_folders->addNode(nullptr, "Content", prism::Icon::FolderOpen, 1);
    root->toggles = false;
    if (const EditorIcons::Image icon = m_icons.ContentType("folder_open", 16); icon) {
        root->image = icon.pixels;
        root->imageSize = icon.size;
    }
    std::map<std::string, prism::TreeNode*> nodes{{"", root}};
    std::sort(folders.begin(), folders.end());
    int selectedId = m_editor.m_assetTableDirectory.empty() ? 1 : 0;
    for (const std::string& folder : folders) {
        const std::filesystem::path p(folder);
        const std::string parent = p.parent_path().generic_string();
        prism::TreeNode* parentNode = nodes.count(parent) > 0 ? nodes[parent] : root;
        m_folderIds.push_back(folder);
        const int id = static_cast<int>(m_folderIds.size());
        prism::TreeNode* node = m_folders->addNode(parentNode, p.filename().string(), prism::Icon::Folder, id);
        node->toggles = false;
        if (const EditorIcons::Image icon = m_icons.ContentType("Folder", 16); icon) {
            node->image = icon.pixels;
            node->imageSize = icon.size;
        }
        nodes[folder] = node;
        if (m_editor.m_assetTableDirectory.generic_string() == folder) {
            selectedId = id;
        }
    }
    if (selectedId > 0) {
        m_folders->setSelection({selectedId});
    }
    m_folders->refresh();
    m_syncingFolders = false;
}


void EditorAssetTable::RebuildItems() {
    if (!m_itemsHost) {
        return;
    }
    Asset::IProjectFileIndex& index = m_editor.m_services->GetProjectFileIndex();
    // The project listing is copied only when the tiles (or the pick) have to change: an unchanged
    // folder costs a few hashes a frame, not a copy of every file the project has.
    std::uint64_t h = index.GetRefreshGeneration();
    // Clip icons drawn in the background: the tiles take the new ones a few times a second at most.
    {
        const auto now = std::chrono::steady_clock::now();
        if (m_editor.m_animationIconGeneration != m_iconGenerationShown && now - m_iconGenerationAt > std::chrono::milliseconds(300)) {
            m_iconGenerationShown = m_editor.m_animationIconGeneration;
            m_iconGenerationAt = now;
        }
        Hash(h, m_iconGenerationShown);
    }
    Hash(h, m_editor.m_materialThumbnailGeneration);
    Hash(h, m_editor.m_meshThumbnailGeneration);   // a model finishing its import redraws its tile
    Hash(h, m_editor.m_assetTableDirectory.generic_string());
    Hash(h, m_editor.m_assetFilter);
    Hash(h, m_editor.m_assetTableShelf);
    Hash(h, static_cast<std::uint64_t>(m_editor.m_assetTableTypeFocus));
    Hash(h, static_cast<std::uint64_t>(m_editor.m_assetTableMode));
    Hash(h, static_cast<std::uint64_t>(m_editor.m_assetTableViewMode));
    Hash(h, static_cast<std::uint64_t>(m_editor.m_contentSortKey) * 2U + (m_editor.m_contentSortAscending ? 1U : 0U));
    // The pick is tracked apart: picking an item restyles the shown items instead of rebuilding them
    // (a rebuild would drop the tile being pressed, and with it a drag about to start).
    std::uint64_t pick = 0;
    for (const std::string& item : m_editor.m_contentSelection.Items()) {
        Hash(pick, item);
    }
    if (m_editor.m_selectedProjectFile.has_value()) {
        Hash(pick, m_editor.m_selectedProjectFile->relativePath.generic_string());
    }
    if (m_editor.m_assetTableMode == AssetTableMode::Columns) {
        Hash(h, pick); // Columns shows the picked file's preview, so it follows the pick
    }
    if (h == m_tileSignature) {
        if (pick != m_pickSignature) {
            m_pickSignature = pick;
            UpdatePick(index.GetSnapshot());
        }
        return;
    }
    const Asset::ProjectFileSnapshot snapshot = index.GetSnapshot();
    m_editor.ReconcileAssetTableDirectory(snapshot);
    m_contentRoot = snapshot.contentRoot;
    m_tileSignature = h;
    m_pickSignature = pick;

    // What the mode lists: one folder (or a shelf) for Tiles / Columns / Details; everything below
    // the folder for Recent and Board.
    const AssetTableMode mode = m_editor.m_assetTableMode;
    const std::filesystem::path directory = m_editor.m_assetTableDirectory;
    const ContentShelf* const shelf =
        m_editor.m_assetTableShelf.empty() ? nullptr : m_editor.m_contentShelves.Find(m_editor.m_assetTableShelf);
    const bool wholeTree = mode == AssetTableMode::Recent || mode == AssetTableMode::Board;
    std::vector<std::size_t> listed =
        shelf != nullptr ? ListContentShelf(snapshot.entries, *shelf, m_editor.m_assetFilter)
        : wholeTree      ? ListContentTree(snapshot.entries, directory, m_editor.m_assetFilter)
                         : ListContentFolder(snapshot.entries, directory, m_editor.m_assetFilter);
    // "<folder>/<stem>" of every .uvanim, sorted; read only when an animation-only source is listed.
    std::vector<std::string> clipStems;
    bool clipStemsRead = false;
    const auto hasImportedClips = [&snapshot, &clipStems, &clipStemsRead](const std::filesystem::path& source) {
        if (!clipStemsRead) {
            clipStemsRead = true;
            for (const Asset::ProjectFileEntry& other : snapshot.entries) {
                if (other.kind == Asset::ProjectFileEntryKind::File && other.relativePath.extension() == ".uvanim") {
                    clipStems.push_back((other.relativePath.parent_path() / other.relativePath.stem()).generic_string());
                }
            }
            std::ranges::sort(clipStems);
        }
        // "<source>.uvanim" or "<source>_<take>.uvanim", as ImportModelAnimations names them.
        const std::string stem = (source.parent_path() / source.stem()).generic_string();
        const auto take = std::ranges::lower_bound(clipStems, stem + "_");
        return std::ranges::binary_search(clipStems, stem) || (take != clipStems.end() && take->starts_with(stem + "_"));
    };
    std::vector<std::size_t> candidates;
    for (const std::size_t i : listed) {
        const ItemType type = m_editor.GetAssetTableEntryType(snapshot.entries[i]);
        // Gameplay is written as Blueprints: loose script files are not listed.
        if (type == ItemType::Script) {
            continue;
        }
        // An image's import settings (<image>.uvimport) belong to the image, not beside it as a file.
        if (snapshot.entries[i].relativePath.extension() == ".uvimport") {
            continue;
        }
        // An animation-only file is listed as the clips imported from it, not a second time beside them.
        if (type == ItemType::Animation && Editor::IsModelSourcePath(snapshot.entries[i].relativePath) &&
            hasImportedClips(snapshot.entries[i].relativePath)) {
            continue;
        }
        if (m_editor.DoesAssetTableEntryMatchFocus(snapshot.entries[i])) {
            candidates.push_back(i);
        }
    }
    // The engine's facts and sort (folders stay ahead of files).
    std::vector<ContentItemFacts> facts;
    facts.reserve(candidates.size());
    for (const std::size_t i : candidates) {
        const Asset::ProjectFileEntry& entry = snapshot.entries[i];
        ContentItemFacts f;
        f.name = entry.relativePath.filename().string();
        f.type = Editor::GetAssetTableItemTypeLabel(m_editor.GetAssetTableEntryType(entry));
        f.folder = entry.relativePath.parent_path().generic_string();
        f.isFolder = entry.kind == Asset::ProjectFileEntryKind::Directory;
        if (!f.isFolder) {
            const ContentFileFacts disk =
                m_editor.GetContentFileFacts(snapshot.contentRoot, entry, snapshot.refreshGeneration);
            f.size = disk.size;
            f.modified = disk.modified;
        }
        facts.push_back(std::move(f));
    }
    std::vector<std::size_t> order(candidates.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    if (mode == AssetTableMode::Recent) {
        SortContentFacts(order, facts, ContentSortKey::Modified, false);
    } else {
        SortContentFacts(order, facts, m_editor.m_contentSortKey, m_editor.m_contentSortAscending);
    }
    m_shown.clear();
    m_facts.clear();
    std::vector<std::string> shownOrder;
    for (const std::size_t o : order) {
        if (mode == AssetTableMode::Recent && facts[o].isFolder) {
            continue; // Recent lists what changed, which is files
        }
        m_shown.push_back(candidates[o]);
        m_facts.push_back(facts[o]);
        shownOrder.push_back(snapshot.entries[candidates[o]].relativePath.generic_string());
    }
    m_editor.m_contentShownOrderPrevious = shownOrder;

    switch (mode) {
        case AssetTableMode::Tiles: BuildTiles(snapshot); break;
        case AssetTableMode::Columns: BuildColumns(snapshot); break;
        case AssetTableMode::Details: BuildTable(snapshot, false); break;
        case AssetTableMode::Recent: BuildTable(snapshot, true); break;
        case AssetTableMode::Board: BuildBoard(snapshot); break;
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

void EditorAssetTable::BuildTiles(const Asset::ProjectFileSnapshot& snapshot) {
    using ViewMode = Editor::AssetTableViewMode;
    const ViewMode view = m_editor.m_assetTableViewMode;
    const bool rows = view == ViewMode::List;
    const prism::Vec2 size = rows ? prism::Vec2(240.0F, 24.0F)
                             : view == ViewMode::LargeTiles ? prism::Vec2(140.0F, 178.0F)
                                                            : prism::Vec2(100.0F, 138.0F);   // square wells (see assetTileWellHeight)
    auto tiles = prism::make<TileGrid>(size, rows);
    m_tileWidgets.clear();
    m_editor.BeginAnimationIconBatch();   // this folder's clips are drawn first, top-down
    for (std::size_t n = 0; n < m_shown.size(); ++n) {
        const std::size_t i = m_shown[n];
        const Asset::ProjectFileEntry& entry = snapshot.entries[i];
        const std::string key = entry.relativePath.generic_string();
        const bool selected = m_editor.m_contentSelection.Contains(key) ||
                              (m_editor.m_contentSelection.IsEmpty() && m_editor.m_selectedProjectFile.has_value() &&
                               m_editor.m_selectedProjectFile->relativePath == entry.relativePath);
        const ItemType type = m_editor.GetAssetTableEntryType(entry);
        // The engine's own preview (real mesh, PBR material, texture) when it has one, else the
        // engine's icon for the type; PRISM only draws the tile around it. Folders have no preview.
        const std::uint32_t* pixels = nullptr;
        int pw = 0;
        int ph = 0;
        float scale = 1.0F;
        if (!rows && type != ItemType::Folder) {
            if (const Preview* preview = PreviewFor(entry, type); preview != nullptr) {
                pixels = preview->pixels.data();
                pw = preview->width;
                ph = preview->height;
            } else if (const EditorIcons::Image icon = TypeIcon(type, 64); icon) {
                pixels = icon.pixels;
                pw = icon.size;
                ph = icon.size;
                scale = 0.62F;
            }
        }
        auto tile = prism::make<Tile>(*this, i, DisplayName(entry), type, selected, pixels, pw, ph, scale, size, rows);
        tile->setTooltip(entry.relativePath.filename().string() + "\nType: " + m_facts[n].type);
        m_tileWidgets.emplace_back(key, tile);
        tiles->add(tile);
    }
    auto scroll = prism::make<prism::ScrollView>();
    scroll->setContent(tiles);
    m_table.reset();
    m_itemsHost->setChild(scroll);
}

void EditorAssetTable::UpdatePick(const Asset::ProjectFileSnapshot& snapshot) {
    const auto picked = [this](const std::filesystem::path& relative) {
        return m_editor.m_contentSelection.Contains(relative.generic_string()) ||
               (m_editor.m_contentSelection.IsEmpty() && m_editor.m_selectedProjectFile.has_value() &&
                m_editor.m_selectedProjectFile->relativePath == relative);
    };
    for (const auto& [key, weakTile] : m_tileWidgets) {
        if (const std::shared_ptr<Tile> tile = weakTile.lock()) {
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
        // A pick the table made itself is left as is, so its Shift+click keeps running from its own anchor.
        if (!std::ranges::equal(m_table->selection(), rows)) {
            m_syncingFolders = true;
            m_table->select(rows);
            m_syncingFolders = false;
        }
    }
}

void EditorAssetTable::BuildTable(const Asset::ProjectFileSnapshot& snapshot, const bool recent) {
    m_tileWidgets.clear();
    // Details: a table to sort by name, type, size, date or folder. Recent: what changed lately
    // across everything below, newest first, bucketed by the engine's age labels.
    auto table = prism::make<prism::TableView>();
    const std::int64_t today = StartOfToday();
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
        for (std::size_t c = 0; c < kDetailsColumns.size(); ++c) {
            prism::TableColumn column;
            column.id = kDetailsColumns[c].first;
            column.title = kDetailsColumns[c].first;
            column.width = c == 0 ? 240.0F : c == 4 ? 200.0F : 110.0F;
            table->addColumn(column);
        }
    }
    table->cellText = [this, recent, today](const int row, const int col) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= m_facts.size()) {
            return {};
        }
        const ContentItemFacts& f = m_facts[static_cast<std::size_t>(row)];
        if (recent) {
            switch (col) {
                case 0: return f.name;
                case 1: return GetContentAgeLabel(ClassifyContentAge(f.modified, today));
                case 2: return FormatTime(f.modified);
                case 3: return f.type;
                default: return f.folder.empty() ? std::string("Content") : f.folder;
            }
        }
        switch (col) {
            case 0: return f.name;
            case 1: return f.type;
            case 2: return f.isFolder ? std::string("-") : FormatSize(f.size);
            case 3: return f.isFolder ? std::string("-") : FormatTime(f.modified);
            default: return f.folder.empty() ? std::string("Content") : f.folder;
        }
    };
    table->paintCell = [this](prism::Canvas& c, const int row, const int col, const prism::Rect r, bool) {
        if (row < 0 || static_cast<std::size_t>(row) >= m_facts.size() || row >= static_cast<int>(m_shown.size())) {
            return;
        }
        const prism::Theme& t = prism::theme();
        const ContentItemFacts& f = m_facts[static_cast<std::size_t>(row)];
        const Asset::ProjectFileSnapshot snap = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        std::string text = m_table && m_table->cellText ? m_table->cellText(row, col) : f.name;
        float x = r.x + 6.0F;
        if (col == 0 && static_cast<std::size_t>(m_shown[static_cast<std::size_t>(row)]) < snap.entries.size()) {
            const ItemType type =
                m_editor.GetAssetTableEntryType(snap.entries[m_shown[static_cast<std::size_t>(row)]]);
            if (const EditorIcons::Image icon = TypeIcon(type, 16); icon) {
                c.blitBlend(icon.pixels, icon.size, icon.size, icon.size,
                            prism::Rect{x, std::floor(r.y + (r.h - 16.0F) * 0.5F), 16.0F, 16.0F});
            }
            x += 22.0F;
        }
        c.textIn(*t.ui, prism::Rect{x, r.y, r.right() - x - 4.0F, r.h}, text, col == 0 ? t.text : t.textDim,
                 prism::HAlign::Left, true);
    };
    // The order is the engine's (SortContentFacts); a header click changes the engine's sort key.
    table->lessThan = [](const int a, const int b, int) { return a < b; };
    table->onSort = [this, recent](const int col, const prism::SortState state) {
        if (recent || m_syncingFolders || col < 0 || static_cast<std::size_t>(col) >= kDetailsColumns.size()) {
            return;
        }
        m_editor.m_contentSortKey = kDetailsColumns[static_cast<std::size_t>(col)].second;
        m_editor.m_contentSortAscending = state != prism::SortState::Descending;
        Changed();
    };
    table->onSelect = [this](const std::vector<int>& rows) {
        if (m_syncingFolders) {
            return;
        }
        // The table does its own Ctrl / Shift picking: the content selection takes its rows as they are.
        const std::vector<std::string>& shown = m_editor.m_contentShownOrderPrevious;
        std::vector<std::size_t> picked;
        for (const int row : rows) {
            if (row >= 0 && static_cast<std::size_t>(row) < m_shown.size() && static_cast<std::size_t>(row) < shown.size()) {
                picked.push_back(static_cast<std::size_t>(row));
            }
        }
        if (picked.empty()) {
            m_editor.m_contentSelection.Clear();
            m_editor.m_selectedProjectFile.reset();
            m_editor.m_selectedAsset.reset();
            return;
        }
        // The details follow the row just added to the pick, else the last one.
        const auto added = std::ranges::find_if(
            picked, [this, &shown](const std::size_t n) { return !m_editor.m_contentSelection.Contains(shown[n]); });
        const std::size_t primary = added != picked.end() ? *added : picked.back();
        m_editor.m_contentSelection.Clear();
        for (const std::size_t n : picked) {
            if (n != primary) {
                m_editor.m_contentSelection.Click(shown, shown[n], true, false);
            }
        }
        SelectEntry(m_shown[primary], true, false);
    };
    table->onActivate = [this](const int row) {
        if (row >= 0 && static_cast<std::size_t>(row) < m_shown.size()) {
            OpenEntry(m_shown[static_cast<std::size_t>(row)]);
        }
    };
    table->onContext = [this](const int row, const prism::Vec2 at) {
        if (row >= 0 && static_cast<std::size_t>(row) < m_shown.size()) {
            ShowItemMenu(m_shown[static_cast<std::size_t>(row)], at);
        } else {
            ShowEmptyMenu(at);
        }
    };
    table->setRowCount(static_cast<int>(m_shown.size()));
    m_table = table;
    // Show the engine's sort on the header without sorting again.
    if (!recent) {
        int sortColumn = 0;
        for (std::size_t c = 0; c < kDetailsColumns.size(); ++c) {
            if (kDetailsColumns[c].second == m_editor.m_contentSortKey) {
                sortColumn = static_cast<int>(c);
            }
        }
        m_syncingFolders = true;
        table->sortBy(sortColumn, prism::SortState::Ascending);
        m_syncingFolders = false;
    }
    // Picked as UpdatePick picks; the file the details follow goes last, as the table's Shift+click anchor.
    std::vector<int> picked;
    int primary = -1;
    for (std::size_t n = 0; n < m_shown.size(); ++n) {
        const Asset::ProjectFileEntry& entry = snapshot.entries[m_shown[n]];
        const bool current =
            m_editor.m_selectedProjectFile.has_value() && m_editor.m_selectedProjectFile->relativePath == entry.relativePath;
        if (!m_editor.m_contentSelection.Contains(entry.relativePath.generic_string()) &&
            !(current && m_editor.m_contentSelection.IsEmpty())) {
            continue;
        }
        if (current) {
            primary = static_cast<int>(n);
        } else {
            picked.push_back(static_cast<int>(n));
        }
    }
    if (primary >= 0) {
        picked.push_back(primary);
    }
    m_syncingFolders = true;
    table->select(picked);
    m_syncingFolders = false;
    m_itemsHost->setChild(table);
}

void EditorAssetTable::BuildColumns(const Asset::ProjectFileSnapshot& snapshot) {
    m_tileWidgets.clear();
    // Columns: one list per folder level from Content down to the open folder, then a preview of
    // the file picked. Picking a folder in any column opens it.
    auto row = prism::hbox(1.0F);
    std::vector<std::filesystem::path> levels{std::filesystem::path{}};
    for (const auto& part : m_editor.m_assetTableDirectory) {
        levels.push_back(levels.back() / part);
    }
    for (std::size_t level = 0; level < levels.size(); ++level) {
        const std::filesystem::path& dir = levels[level];
        const std::vector<std::size_t> entries =
            level + 1 == levels.size() ? m_shown : ListContentFolder(snapshot.entries, dir, "");
        auto list = prism::make<prism::TreeView>();
        std::vector<std::size_t> ids;
        int selectedId = 0;
        for (const std::size_t i : entries) {
            const Asset::ProjectFileEntry& entry = snapshot.entries[i];
            const ItemType type = m_editor.GetAssetTableEntryType(entry);
            ids.push_back(i);
            prism::TreeNode* node =
                list->addNode(nullptr, DisplayName(entry), IconFor(type), static_cast<int>(ids.size()));
            node->toggles = false;
            if (const EditorIcons::Image icon = TypeIcon(type, 16); icon) {
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
            if (i < snapshotEntries.size() && snapshotEntries[i].kind == Asset::ProjectFileEntryKind::Directory) {
                GoToFolder(snapshotEntries[i].relativePath);
            } else {
                SelectEntry(i, false, false);
                Changed();
            }
        };
        list->onActivate = [this, ids](prism::TreeNode* node) {
            if (node != nullptr && node->id > 0) {
                OpenEntry(ids[static_cast<std::size_t>(node->id - 1)]);
            }
        };
        list->onContext = [this, ids](prism::TreeNode* node, const prism::Vec2 at) {
            if (node != nullptr && node->id > 0) {
                ShowItemMenu(ids[static_cast<std::size_t>(node->id - 1)], at);
            } else {
                ShowEmptyMenu(at);
            }
        };
        list->onDelete = [this](prism::TreeNode*) { AskDeleteContent(PickedContent()); };
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
        m_editor.m_selectedProjectFile->kind != Asset::ProjectFileEntryKind::Directory) {
        const Asset::ProjectFileEntry entry = *m_editor.m_selectedProjectFile;
        const ItemType type = m_editor.GetAssetTableEntryType(entry);
        const std::uint32_t* pixels = nullptr;
        int pw = 0;
        int ph = 0;
        float scale = 1.0F;
        if (const Preview* p = PreviewFor(entry, type); p != nullptr) {
            pixels = p->pixels.data();
            pw = p->width;
            ph = p->height;
        } else if (const EditorIcons::Image icon = TypeIcon(type, 64); icon) {
            pixels = icon.pixels;
            pw = icon.size;
            ph = icon.size;
            scale = 0.62F;
        }
        const auto found = std::find_if(snapshot.entries.begin(), snapshot.entries.end(),
                                        [&](const Asset::ProjectFileEntry& e) { return e.relativePath == entry.relativePath; });
        const std::size_t index = found == snapshot.entries.end() ? 0U
                                                                  : static_cast<std::size_t>(found - snapshot.entries.begin());
        preview->add(prism::make<Tile>(*this, index, DisplayName(entry), type, true, pixels, pw, ph, scale,
                                          prism::Vec2(160.0F, 198.0F), false),
                     prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
        const ContentFileFacts disk = m_editor.GetContentFileFacts(snapshot.contentRoot, entry, snapshot.refreshGeneration);
        for (const std::string& line : {std::string("Type: ") + Editor::GetAssetTableItemTypeLabel(type),
                                        std::string("Size: ") + FormatSize(disk.size),
                                        std::string("Modified: ") + FormatTime(disk.modified)}) {
            auto label = prism::make<prism::Label>(line, prism::FontRole::Small);
            label->setColor(prism::ThemeColor(&prism::Theme::textDim));
            preview->add(label);
        }
    }
    row->add(preview, prism::Slot{1.0F});
    m_table.reset();
    m_itemsHost->setChild(row);
}

void EditorAssetTable::BuildBoard(const Asset::ProjectFileSnapshot& snapshot) {
    m_tileWidgets.clear();
    // Board: everything below the folder in lanes by kind (the engine's GroupContentByType).
    std::vector<std::size_t> order(m_facts.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    const auto lanes = GroupContentByType(order, m_facts);
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
            const Asset::ProjectFileEntry& entry = snapshot.entries[i];
            const ItemType type = m_editor.GetAssetTableEntryType(entry);
            ids.push_back(i);
            prism::TreeNode* node = list->addNode(nullptr, DisplayName(entry), IconFor(type), static_cast<int>(ids.size()));
            node->toggles = false;
            if (const EditorIcons::Image icon = TypeIcon(type, 16); icon) {
                node->image = icon.pixels;
                node->imageSize = icon.size;
            }
        }
        list->onSelect = [this, ids](const std::vector<prism::TreeNode*>& nodes) {
            if (!m_syncingFolders && !nodes.empty() && nodes.front()->id > 0) {
                SelectEntry(ids[static_cast<std::size_t>(nodes.front()->id - 1)], false, false);
            }
        };
        list->onActivate = [this, ids](prism::TreeNode* node) {
            if (node != nullptr && node->id > 0) {
                OpenEntry(ids[static_cast<std::size_t>(node->id - 1)]);
            }
        };
        list->onContext = [this, ids](prism::TreeNode* node, const prism::Vec2 at) {
            if (node != nullptr && node->id > 0) {
                ShowItemMenu(ids[static_cast<std::size_t>(node->id - 1)], at);
            }
        };
        list->onDelete = [this](prism::TreeNode*) { AskDeleteContent(PickedContent()); };
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

void EditorAssetTable::ShowViewMenu() {
    // The view mode: the engine's five Asset Table modes, the tile size and the sort.
    using ViewMode = Editor::AssetTableViewMode;
    std::vector<prism::MenuItem> items;
    static constexpr std::array<std::pair<AssetTableMode, const char*>, 5> kModes{{
        {AssetTableMode::Tiles, "Tiles"},
        {AssetTableMode::Columns, "Columns"},
        {AssetTableMode::Details, "Details"},
        {AssetTableMode::Recent, "Recent"},
        {AssetTableMode::Board, "Board"},
    }};
    for (const auto& [mode, label] : kModes) {
        items.push_back(prism::MenuItem::Check(label, m_editor.m_assetTableMode == mode, [this, m = mode] {
            m_editor.m_assetTableMode = m;
            Changed();
        }));
    }
    items.push_back(prism::MenuItem::Sep());
    std::vector<prism::MenuItem> sizes;
    for (const auto& [view, label] : std::array<std::pair<ViewMode, const char*>, 3>{
             {{ViewMode::LargeTiles, "Large Tiles"}, {ViewMode::SmallTiles, "Small Tiles"}, {ViewMode::List, "List"}}}) {
        sizes.push_back(prism::MenuItem::Check(label, m_editor.m_assetTableViewMode == view, [this, v = view] {
            m_editor.m_assetTableViewMode = v;
            m_editor.m_assetTableMode = AssetTableMode::Tiles;
            Changed();
        }));
    }
    items.push_back(prism::MenuItem::Sub("Tile Size", std::move(sizes), prism::Icon::Grid));
    std::vector<prism::MenuItem> sort;
    for (std::size_t c = 0; c < kDetailsColumns.size(); ++c) {
        const ContentSortKey key = kDetailsColumns[c].second;
        sort.push_back(prism::MenuItem::Check(kDetailsColumns[c].first, m_editor.m_contentSortKey == key, [this, key] {
            m_editor.m_contentSortKey = key;
            Changed();
        }));
    }
    sort.push_back(prism::MenuItem::Sep());
    sort.push_back(prism::MenuItem::Check("Ascending", m_editor.m_contentSortAscending, [this] {
        m_editor.m_contentSortAscending = !m_editor.m_contentSortAscending;
        Changed();
    }));
    items.push_back(prism::MenuItem::Sub("Sort By", std::move(sort), prism::Icon::Filter));
    prism::showMenu(std::move(items), m_viewButton ? m_viewButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

void EditorAssetTable::ShowSplitMenu() {
    // The split mode: how the Asset Table's own sidebar and items share its space.
    std::vector<prism::MenuItem> items;
    static constexpr std::array<std::pair<SplitMode, const char*>, 4> kSplits{{
        {SplitMode::SideBySide, "Side by Side"},
        {SplitMode::Stacked, "Stacked (sidebar above)"},
        {SplitMode::ItemsOnly, "Items Only"},
        {SplitMode::SidebarOnly, "Sidebar Only"},
    }};
    for (const auto& [mode, label] : kSplits) {
        items.push_back(prism::MenuItem::Check(label, m_splitMode == mode, [this, m = mode] {
            m_splitMode = m;
            ApplySplitMode();
        }));
    }
    prism::showMenu(std::move(items), m_splitButton ? m_splitButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

std::vector<prism::MenuItem> EditorAssetTable::CatalogueMenu() {
    // Laid out as the standard Asset Table Add: GET (import), CREATE (a folder, the assets made most,
    // then every kind by category, each opening beside the menu). Only what the engine makes from
    // nothing is here: objects (lights, meshes ...) are placed with the Outliner's '+', and textures,
    // meshes and sounds come in through Import.
    std::vector<prism::MenuItem> items;
    const auto item = [this](const char* label, const std::string& id, const prism::Icon icon, const char* typeLabel) {
        prism::MenuItem entry = prism::MenuItem::Item(label, [this, id] { CreateCatalogueItem(id); }, "", icon);
        if (typeLabel != nullptr) {
            if (const EditorIcons::Image image = m_icons.ContentType(typeLabel, 16, icon); image) {
                entry.withImage(image.pixels, image.size);
            }
        }
        return entry;
    };
    const auto entityClass = [this] {
        prism::MenuItem entry = prism::MenuItem::Item("Entity Class", [this] {
            const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
            m_editor.OpenEntityClassPicker(snapshot.contentRoot / m_editor.m_assetTableDirectory);
        }, "", prism::Icon::EntityAsset);
        if (const EditorIcons::Image image = m_icons.ContentType("Entity", 16, prism::Icon::EntityAsset); image) {
            entry.withImage(image.pixels, image.size);
        }
        return entry;
    };
    // A material of a given kind, made in the current folder.
    const auto material = [this](const char* label, const char* stem, const Asset::MaterialKind kind) {
        prism::MenuItem entry = prism::MenuItem::Item(label, [this, stem, kind] { CreateMaterial(stem, kind); }, "", prism::Icon::Material);
        if (const EditorIcons::Image image = m_icons.ContentType("Material", 16, prism::Icon::Material); image) {
            entry.withImage(image.pixels, image.size);
        }
        return entry;
    };

    items.push_back(prism::MenuItem::Header("GET"));
    items.push_back(prism::MenuItem::Item("Import Files to Current Folder...", [this] { CreateCatalogueItem(""); }, "Ctrl+I", prism::Icon::Open));
    items.push_back(prism::MenuItem::Item("Import Folder to Current Folder...", [this] {
        const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        static_cast<void>(m_editor.PromptImportFolder(snapshot.contentRoot / m_editor.m_assetTableDirectory));
    }, "", prism::Icon::FolderOpen));
    items.push_back(prism::MenuItem::Header("CREATE"));
    items.push_back(item("New Folder", "folder", prism::Icon::Folder, "Folder"));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(entityClass());
    items.push_back(item("Level", "viewport_3d", prism::Icon::SceneAsset, "Viewport3D"));
    items.push_back(item("Material", "material", prism::Icon::Material, "Material"));
    items.push_back(item("Motion Montage", "animation-montage", prism::Icon::AnimMontage, "Motion Montage"));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Sub("Animation", {
        item("Animation Library", "animation-library", prism::Icon::AnimLibrary, "Animation Library"),
        item("Motion Montage", "animation-montage", prism::Icon::AnimMontage, "Motion Montage"),
        item("Motion Montage Library", "montage-library", prism::Icon::MontageLibrary, "Motion Montage Library"),
    }));
    // A Blueprint Class: gameplay as nodes (what an object's Script slot runs).
    prism::MenuItem blueprintClass =
        prism::MenuItem::Item("Blueprint Class", [this] { CreateBlueprint(); }, "", prism::Icon::BlueprintClass);
    items.push_back(prism::MenuItem::Sub("Blueprint", {blueprintClass, entityClass()}));
    items.push_back(prism::MenuItem::Sub("Gameplay", {blueprintClass}));
    // A Widget Blueprint: a screen of UI (text, buttons, bars ...) with its own graph.
    prism::MenuItem widgetBlueprint =
        prism::MenuItem::Item("Widget Blueprint", [this] { CreateWidgetBlueprint(); }, "", prism::Icon::BlueprintClass);
    items.push_back(prism::MenuItem::Sub("User Interface", {widgetBlueprint}));
    items.push_back(prism::MenuItem::Sub("Visual Effects", {
        prism::MenuItem::Item("Particle System", [this] { CreateParticleSystem(); }, "", prism::Icon::Light),
    }));
    items.push_back(prism::MenuItem::Sub("Material", {
        material("Material", "M_Standard", Asset::MaterialKind::Standard),
        material("ORM Material", "M_ORM", Asset::MaterialKind::Orm),
        material("Shader Material", "M_Shader", Asset::MaterialKind::Shader),
        item("Shader", "shader_script", prism::Icon::Shader, "Shader"),
    }));
    // Textures made here rather than imported: seamless noise (clouds, dirt, stone, a bumpy
    // normal map) and gradients, baked to an ordinary 2D texture any material slot can take.
    const auto generated = [this](const char* label, const char* stem, std::function<bool(Asset::TextureAsset&)> make) {
        return prism::MenuItem::Item(label, [this, stem, make = std::move(make)] { CreateGeneratedTexture(stem, make); }, "",
                                     prism::Icon::NodeTexture);
    };
    items.push_back(prism::MenuItem::Sub("Texture", {
        generated("Noise Texture", "T_Noise", [](Asset::TextureAsset& out) {
            return Asset::GenerateNoiseTexture(Asset::NoiseTextureSettings{}, out);
        }),
        generated("Cellular Noise Texture", "T_Cells", [](Asset::TextureAsset& out) {
            Asset::NoiseTextureSettings settings;
            settings.type = Asset::NoiseTextureType::Cellular;
            settings.frequency = 8U;
            settings.octaves = 2U;
            return Asset::GenerateNoiseTexture(settings, out);
        }),
        generated("Value Noise Texture", "T_ValueNoise", [](Asset::TextureAsset& out) {
            Asset::NoiseTextureSettings settings;
            settings.type = Asset::NoiseTextureType::Value;
            return Asset::GenerateNoiseTexture(settings, out);
        }),
        generated("Noise Normal Map", "T_NoiseNormal", [](Asset::TextureAsset& out) {
            Asset::NoiseTextureSettings settings;
            settings.asNormalMap = true;
            settings.frequency = 8U;
            return Asset::GenerateNoiseTexture(settings, out);
        }),
        generated("Gradient Texture", "T_Gradient", [](Asset::TextureAsset& out) {
            return Asset::GenerateGradientTexture(Asset::GradientTextureSettings{}, out);
        }),
        generated("Radial Gradient Texture", "T_RadialGradient", [](Asset::TextureAsset& out) {
            Asset::GradientTextureSettings settings;
            settings.radial = true;
            settings.colorA = Math::Vector3{1.0F, 1.0F, 1.0F};
            settings.colorB = Math::Vector3{0.0F, 0.0F, 0.0F};
            return Asset::GenerateGradientTexture(settings, out);
        }),
    }));
    items.push_back(prism::MenuItem::Sub("World", {
        item("Level", "viewport_3d", prism::Icon::SceneAsset, "Viewport3D"),
    }));
    return items;
}

void EditorAssetTable::CreateGeneratedTexture(const std::string& stem, const std::function<bool(Asset::TextureAsset&)>& make) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path folder = snapshot.contentRoot / m_editor.m_assetTableDirectory;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path path = Editor::MakeUniqueContentPath(folder, stem, ".uvtex");
    Asset::TextureAsset texture;
    if (!make(texture) || !Asset::SaveTextureAsset(texture, path)) {
        m_editor.m_contentStatusMessage = "Could not make the texture here.";
        return;
    }
    static_cast<void>(m_editor.m_services->GetAssetDatabase().Register(path));
    m_editor.m_contentStatusMessage.clear();
    m_editor.RefreshProjectFileIndex();
    m_folderSignature = 0;
    Changed();
}

void EditorAssetTable::CreateMaterial(const std::string& stem, const Asset::MaterialKind kind) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path folder = snapshot.contentRoot / m_editor.m_assetTableDirectory;
    std::error_code error;
    std::filesystem::create_directories(folder, error);
    const std::filesystem::path path = Editor::MakeUniqueContentPath(folder, stem, ".uvmaterial");
    Asset::MaterialAsset material{};
    material.kind = kind;
    if (kind == Asset::MaterialKind::Orm) {
        material.metallic = 1.0F;
        material.occlusionStrength = 1.0F;
    } else if (kind == Asset::MaterialKind::Shader) {
        material.useCustomShaderCode = true;
    }
    if (!Asset::SaveMaterialAsset(material, path)) {
        m_editor.m_contentStatusMessage = "Could not create a material here.";
        return;
    }
    m_editor.m_contentStatusMessage.clear();
    m_editor.RefreshProjectFileIndex();
    m_folderSignature = 0;
    Changed();
}

namespace {

/// One object of an entity file, as the Blueprint Class dialog lists it.
struct EntityFileObject {
    std::string name;
    std::string graph;                 // the Blueprint its Script slot names (and that exists), or ""
    std::vector<std::string> path;     // names from the root down to it
    int depth = 0;
};

/// The objects of an `.uventity`, root first, each followed by the objects under it.
[[nodiscard]] std::vector<EntityFileObject> ReadEntityFileObjects(const std::filesystem::path& file,
                                                                       const std::filesystem::path& projectRoot) {
    std::ifstream in(file, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::size_t start = bytes.find('{');
    if (start == std::string::npos) {
        return {};
    }
    const nlohmann::json document = nlohmann::json::parse(bytes.begin() + static_cast<std::ptrdiff_t>(start), bytes.end(), nullptr, false);
    if (!document.is_object() || !document.contains("entities") || !document["entities"].is_array()) {
        return {};
    }
    struct Node {
        std::int64_t id = -1;
        std::int64_t parent = -1;
        std::string name;
        std::string graph;
    };
    std::vector<Node> nodes;
    for (const nlohmann::json& entity : document["entities"]) {
        if (!entity.is_object() || !entity.contains("components") || !entity["components"].is_object()) {
            continue;
        }
        const nlohmann::json& components = entity["components"];
        Node node;
        node.id = entity.value("localId", std::int64_t{-1});
        const auto findComp = [&components](const char* key) -> const nlohmann::json* {
            if (const auto it = components.find(key); it != components.end() && it->is_object()) return &(*it);
            if (const auto it = components.find(std::string(key) + "UVE"); it != components.end() && it->is_object()) return &(*it);
            return nullptr;
        };
        if (const nlohmann::json* const h = findComp("HierarchyComponent")) {
            node.parent = h->value("parentLocalId", std::int64_t{-1});
        }
        if (const nlohmann::json* const n = findComp("NameComponent")) {
            node.name = n->value("name", std::string{});
        }
        if (const nlohmann::json* const sc = findComp("ScriptComponent")) {
            const std::string path = sc->value("scriptAssetPath", std::string{});
            std::error_code error;
            if (path.ends_with(".uvbp") && std::filesystem::is_regular_file(projectRoot / path, error)) {
                node.graph = path;
            }
        }
        nodes.push_back(std::move(node));
    }
    std::vector<EntityFileObject> out;
    const std::function<void(std::int64_t, std::vector<std::string>, int)> walk = [&](const std::int64_t parent,
                                                                                       std::vector<std::string> path, const int depth) {
        for (const Node& node : nodes) {
            if (node.parent != parent || out.size() > 4096U) {
                continue;
            }
            std::vector<std::string> here = path;
            here.push_back(node.name);
            out.push_back(EntityFileObject{node.name, node.graph, here, depth});
            if (node.id >= 0) {
                walk(node.id, here, depth + 1);
            }
        }
    };
    walk(-1, {}, 0);
    return out;
}

[[nodiscard]] std::string GraphNameFor(const std::string& object) {
    std::string name = "BP_";
    for (const char c : object) {
        name.push_back(std::isalnum(static_cast<unsigned char>(c)) != 0 ? c : '_');
    }
    return name.size() > 3U ? name : std::string("BP_NewBlueprint");
}

} // namespace

void EditorAssetTable::CreateBlueprint() {
    // A Blueprint runs on an object of an entity class: the class, then the object (None: the
    // entity's root itself, else one of the objects under it). Create makes the graph in this
    // folder and opens the entity with the graph on that object.
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path folder = snapshot.contentRoot / m_editor.m_assetTableDirectory;
    const std::filesystem::path projectRoot = snapshot.contentRoot.parent_path();
    std::vector<std::filesystem::path> entities;
    std::vector<std::string> labels;
    for (const Asset::ProjectFileEntry& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKind::File && LowerExtension(entry.relativePath) == ".uventity") {
            entities.push_back(snapshot.contentRoot / entry.relativePath);
            const std::string where = entry.relativePath.parent_path().generic_string();
            labels.push_back(entry.relativePath.stem().string() + (where.empty() ? std::string{} : "   (" + where + ")"));
        }
    }
    if (entities.empty()) {
        m_editor.m_contentStatusMessage = "Make an Entity Class first: a Blueprint runs on an object of an entity.";
        return;
    }
    struct State {
        int entity = 0;
        int object = 0;
        std::vector<EntityFileObject> objects;
    };
    auto state = std::make_shared<State>();
    auto name = prism::make<prism::TextInput>("BP_NewBlueprint", "Name");
    auto entityCombo = prism::make<prism::ComboBox>(labels, 0);
    // One picker: the entity itself (None), then every object under it, indented by depth. An
    // object that already runs a Blueprint is listed but cannot be picked.
    auto objectPicker = prism::make<prism::ComboBox>(std::vector<std::string>{}, 0);
    auto note = prism::make<prism::Label>("", prism::FontRole::Small);
    note->setColor(prism::ThemeColor(&prism::Theme::textDim));

    const auto makeFieldBlock = [](const char* heading, prism::Ref<prism::Widget> control) {
        auto block = prism::vbox(3.0F);
        auto lbl = prism::make<prism::Label>(heading, prism::FontRole::Small);
        lbl->setColor(prism::ThemeColor(&prism::Theme::textDim));
        block->add(lbl);
        block->add(std::move(control));
        return block;
    };

    auto noteCard = prism::make<prism::Border>();
    noteCard->setBackground(prism::ThemeColor(&prism::Theme::field));
    noteCard->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    noteCard->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    noteCard->setRadius(3.0F);
    noteCard->setChild(note);

    auto form = prism::vbox(8.0F);
    auto intro = prism::make<prism::Label>("Create a Blueprint graph attached to an object of an Entity Class.", prism::FontRole::Small);
    intro->setColor(prism::ThemeColor(&prism::Theme::textDim));
    form->add(intro);
    form->add(makeFieldBlock("Blueprint Name", name));
    form->add(makeFieldBlock("Entity Class", entityCombo));
    form->add(makeFieldBlock("Target Object", objectPicker));
    form->add(noteCard);

    auto rebuild = std::make_shared<std::function<void()>>();
    std::weak_ptr<prism::ComboBox> weakPicker = objectPicker;
    std::weak_ptr<prism::TextInput> weakName = name;
    std::weak_ptr<prism::Label> weakNote = note;
    const auto showPick = [state, weakNote] {
        if (const auto n = weakNote.lock()) {
            const bool noneAvailable = state->object < 0;
            n->setColor(noneAvailable ? prism::ThemeColor(&prism::Theme::warning)
                                      : prism::ThemeColor(&prism::Theme::text));
            n->setText(noneAvailable ? "Every object of this entity already has a Blueprint."
                                     : "Runs on: " + state->objects[static_cast<std::size_t>(state->object)].name);
        }
    };
    *rebuild = [state, weakPicker, showPick] {
        const auto picker = weakPicker.lock();
        if (!picker) {
            return;
        }
        const auto& objects = state->objects;
        // One graph per object: the first free one is picked.
        if (state->object < 0 || state->object >= static_cast<int>(objects.size()) ||
            !objects[static_cast<std::size_t>(state->object)].graph.empty()) {
            state->object = -1;
            for (std::size_t i = 0U; i < objects.size(); ++i) {
                if (objects[i].graph.empty()) {
                    state->object = static_cast<int>(i);
                    break;
                }
            }
        }
        std::vector<std::string> items;
        std::vector<bool> taken;
        for (std::size_t i = 0U; i < objects.size(); ++i) {
            const EntityFileObject& object = objects[i];
            std::string label = i == 0U ? "None (" + object.name + ")" : std::string(static_cast<std::size_t>(object.depth) * 2U, ' ') + object.name;
            if (!object.graph.empty()) {
                label += "  (has " + std::filesystem::path(object.graph).stem().string() + ")";
            }
            items.push_back(std::move(label));
            taken.push_back(!object.graph.empty());
        }
        picker->setItems(std::move(items));
        picker->setDisabledItems(std::move(taken));
        if (state->object >= 0) {
            picker->setSelected(state->object);
        }
        showPick();
    };
    objectPicker->onChange = [state, weakName, showPick](const int index) {
        if (index < 0 || index >= static_cast<int>(state->objects.size())) {
            return;
        }
        state->object = index;
        if (const auto n = weakName.lock()) {
            n->setText(GraphNameFor(state->objects[static_cast<std::size_t>(index)].name));
        }
        showPick();
    };
    const auto load = [state, entities, projectRoot, weakName, rebuild](const int index) {
        state->entity = std::clamp(index, 0, static_cast<int>(entities.size()) - 1);
        state->objects = ReadEntityFileObjects(entities[static_cast<std::size_t>(state->entity)], projectRoot);
        state->object = 0;
        if (const auto n = weakName.lock(); n && !state->objects.empty()) {
            n->setText(GraphNameFor(state->objects.front().name));
        }
        (*rebuild)();
    };
    load(0);
    entityCombo->onChange = [load](const int index) { load(index); };

    auto made = std::make_shared<bool>(false);
    const auto create = [this, state, entities, folder, weakName, made, rebuild] {
        const auto n = weakName.lock();
        if (*made || !n || n->text().empty() || state->object < 0 ||
            state->object >= static_cast<int>(state->objects.size())) {
            return;
        }
        *made = true;
        const std::filesystem::path file = Editor::MakeUniqueContentPath(folder, n->text(), ".uvbp");
        m_editor.m_pendingBlueprintCreate =
            Editor::PendingBlueprintCreate{file, state->objects[static_cast<std::size_t>(state->object)].path};
        if (!m_editor.OpenEntityEditor(entities[static_cast<std::size_t>(state->entity)])) {
            m_editor.m_pendingBlueprintCreate.reset();
            m_editor.m_contentStatusMessage = "Could not open " + entities[static_cast<std::size_t>(state->entity)].filename().string() + ".";
        }
        static_cast<void>(rebuild);
    };
    name->onCommit = [create](const std::string&) {
        create();
        if (prism::Application::exists()) {
            prism::Application::get().closeAllPopups();
        }
    };
    const bool compact = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    prism::showDialog("New Blueprint Class", form,
                      {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"Create", true, create}},
                      compact ? 400.0F : 480.0F);
}

void EditorAssetTable::CreateParticleSystem() {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path folder = snapshot.contentRoot / m_editor.m_assetTableDirectory;
    const std::vector<VFX::ParticleTemplateInfo>& templates = VFX::GetParticleTemplates();
    std::vector<std::string> labels;
    labels.reserve(templates.size());
    for (const VFX::ParticleTemplateInfo& info : templates) {
        labels.emplace_back(info.name);
    }
    auto name = prism::make<prism::TextInput>("VFX_NewEffect", "Name");
    auto picker = prism::make<prism::ComboBox>(labels, 0);

    auto tplTitle = prism::make<prism::Label>(
        templates.empty() ? std::string("Empty") : std::string(templates.front().name),
        prism::FontRole::Semibold);
    tplTitle->setColor(prism::ThemeColor(&prism::Theme::textBright));
    auto tplDesc = prism::make<prism::Label>(
        templates.empty() ? std::string("Blank particle system.") : std::string(templates.front().description),
        prism::FontRole::Small);
    tplDesc->setColor(prism::ThemeColor(&prism::Theme::textDim));

    auto tplCardBody = prism::vbox(3.0F);
    tplCardBody->add(tplTitle);
    tplCardBody->add(tplDesc);
    auto tplCard = prism::make<prism::Border>();
    tplCard->setBackground(prism::ThemeColor(&prism::Theme::field));
    tplCard->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    tplCard->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    tplCard->setRadius(3.0F);
    tplCard->setChild(tplCardBody);

    std::weak_ptr<prism::Label> weakTitle = tplTitle;
    std::weak_ptr<prism::Label> weakDesc = tplDesc;
    picker->onChange = [weakTitle, weakDesc](const int idx) {
        const std::vector<VFX::ParticleTemplateInfo>& infos = VFX::GetParticleTemplates();
        if (infos.empty()) {
            return;
        }
        const auto clamped = static_cast<std::size_t>(std::clamp(idx, 0, static_cast<int>(infos.size()) - 1));
        if (const auto t = weakTitle.lock()) {
            t->setText(std::string(infos[clamped].name));
        }
        if (const auto d = weakDesc.lock()) {
            d->setText(std::string(infos[clamped].description));
        }
    };

    const auto makeFieldBlock = [](const char* heading, prism::Ref<prism::Widget> control) {
        auto block = prism::vbox(3.0F);
        auto lbl = prism::make<prism::Label>(heading, prism::FontRole::Small);
        lbl->setColor(prism::ThemeColor(&prism::Theme::textDim));
        block->add(lbl);
        block->add(std::move(control));
        return block;
    };

    auto form = prism::vbox(8.0F);
    auto intro = prism::make<prism::Label>("Choose a name and starting template for the new VFX asset (.uvfx).", prism::FontRole::Small);
    intro->setColor(prism::ThemeColor(&prism::Theme::textDim));
    form->add(intro);
    form->add(makeFieldBlock("Asset Name", name));
    form->add(makeFieldBlock("Starting Template", picker));
    form->add(tplCard);

    std::weak_ptr<prism::TextInput> weakName = name;
    std::weak_ptr<prism::ComboBox> weakPicker = picker;
    auto made = std::make_shared<bool>(false);
    const auto create = [this, folder, weakName, weakPicker, made] {
        const auto n = weakName.lock();
        const auto p = weakPicker.lock();
        if (*made || !n || !p || n->text().empty()) {
            return;
        }
        const std::vector<VFX::ParticleTemplateInfo>& infos = VFX::GetParticleTemplates();
        const int index = std::clamp(p->selected(), 0, static_cast<int>(infos.size()) - 1);
        *made = true;
        const std::filesystem::path path = m_editor.CreateParticleSystemAsset(
            folder, n->text(), infos[static_cast<std::size_t>(index)].kind);
        if (path.empty()) {
            m_editor.m_contentStatusMessage = "Could not create a Particle System here.";
            return;
        }
        m_editor.m_contentStatusMessage.clear();
        m_editor.RefreshProjectFileIndex();
        m_folderSignature = 0;
        Changed();
        static_cast<void>(m_editor.OpenVfxEditor(path));
    };
    name->onCommit = [create](const std::string&) {
        create();
        if (prism::Application::exists()) {
            prism::Application::get().closeAllPopups();
        }
    };
    const bool compact = prism::Application::exists() && prism::Application::get().platform().singleSurface();
    prism::showDialog("New Particle System", form,
                      {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"Create", true, create}},
                      compact ? 380.0F : 460.0F);
}

void EditorAssetTable::CreateWidgetBlueprint() {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::optional<std::filesystem::path> made =
        m_editor.CreateWidgetBlueprintAsset(snapshot.contentRoot / m_editor.m_assetTableDirectory);
    if (!made.has_value()) {
        m_editor.m_contentStatusMessage = "Could not create a Widget Blueprint here.";
        return;
    }
    m_editor.m_contentStatusMessage.clear();
    m_editor.RefreshProjectFileIndex();
    m_folderSignature = 0;
    Changed();
}

void EditorAssetTable::OpenBlueprint(const std::filesystem::path& full) {
    // A Blueprint is edited on its object: open the entity whose object runs it, then the graph.
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path relative = full.lexically_relative(snapshot.contentRoot);
    const std::string name = full.filename().string();
    for (const Asset::ProjectFileEntry& other : snapshot.entries) {
        if (other.kind != Asset::ProjectFileEntryKind::File || LowerExtension(other.relativePath) != ".uventity") {
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
        if (!m_editor.OpenEntityEditor(entityPath)) {
            break;
        }
        m_editor.m_pendingBlueprintOpen = stored;
        return;
    }
    m_editor.m_contentStatusMessage = name + " is not on any object yet: pick it in an object's Script slot, or use the Entity Editor's Blueprint button.";
}

void EditorAssetTable::CreateCatalogueItem(const std::string& id) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    const std::filesystem::path here = snapshot.contentRoot / m_editor.m_assetTableDirectory;
    if (id.empty()) {
        static_cast<void>(m_editor.PromptImportFiles(here));
    } else if (!m_editor.CreateContentCatalogueItem(id, here).has_value()) {
        m_editor.m_contentStatusMessage = "Could not create that here.";
    } else {
        m_editor.m_contentStatusMessage.clear();
    }
    m_editor.RefreshProjectFileIndex();
    m_folderSignature = 0;
    Changed();
}

void EditorAssetTable::ShowEmptyMenu(const prism::Vec2 at) {
    // Right-click on empty space: the same Add menu, and Refresh.
    std::vector<prism::MenuItem> items = CatalogueMenu();
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("Refresh", [this] {
        m_editor.RefreshProjectFileIndex();
        m_folderSignature = 0;
        Changed();
    }, "", prism::Icon::Refresh));
    prism::showSearchMenu(std::move(items), prism::Rect{at.x, at.y, 1.0F, 1.0F}, prism::PopupPlacement::AtPoint,
                          "Start typing to search", true);
}

void EditorAssetTable::ShowItemMenu(const std::size_t entryIndex, const prism::Vec2 at) {
    // A file's or folder's menu: the same actions the previous Asset Table offered.
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntry entry = snapshot.entries[entryIndex];
    const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
    const std::string name = entry.relativePath.filename().string();
    const bool directory = entry.kind == Asset::ProjectFileEntryKind::Directory;
    const bool allowed = m_editor.IsAuthoringCommandAllowed();
    const ItemType type = m_editor.GetAssetTableEntryType(entry);
    const std::string extension = LowerExtension(entry.relativePath);
    std::vector<prism::MenuItem> items;
    const auto add = [&items](prism::MenuItem item, const bool enabled = true) {
        if (!enabled) {
            item.disabled();
        }
        items.push_back(std::move(item));
    };

    if (directory) {
        add(prism::MenuItem::Item("Open", [this, entryIndex] { OpenEntry(entryIndex); }, "", prism::Icon::FolderOpen));
    }
    if (!directory && IsEntityAsset(entry.relativePath)) {
        // Opens the entity in the Entity Editor's own window; the level stays open beside it.
        add(prism::MenuItem::Item("Open Entity", [this, absolute, name] {
            if (!m_editor.OpenEntityEditor(absolute)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Entity Editor.";
            }
        }, "", prism::Icon::Open), allowed);
        add(prism::MenuItem::Item("Place in Scene", [this, absolute, name] {
            if (m_editor.PlaceEntityAsset(absolute) == Scene::kInvalidEntity) {
                m_editor.m_contentStatusMessage = "Could not place " + name + ".";
            }
        }, "", prism::Icon::Plus), allowed);
        if (extension == ".uventity") {
            const bool isDefault = m_editor.GetDefaultPlayerEntity() == entry.relativePath.generic_string();
            add(prism::MenuItem::Check("Default Player", isDefault, [this, isDefault, rel = entry.relativePath] {
                if (!m_editor.SetDefaultPlayerEntity(isDefault ? std::filesystem::path{} : rel)) {
                    m_editor.m_contentStatusMessage = "Could not save the project settings.";
                }
            }));
        }
    }
    if (type == ItemType::Scene) {
        add(prism::MenuItem::Item("Open Scene", [this, entryIndex] { OpenEntry(entryIndex); }, "", prism::Icon::Globe));
    }
    if (extension == ".uvshader") {
        add(prism::MenuItem::Item("Edit Shader", [this, entry] {
            m_editor.m_selectedProjectFile = entry;
            m_editor.m_selectedAsset.reset();
        }, "", prism::Icon::Script));
    }
    if (type == ItemType::AnimationLibrary) {
        add(prism::MenuItem::Item("Open Library", [this, entryIndex] { OpenEntry(entryIndex); }, "", prism::Icon::AnimLibrary));
    }
    if (type == ItemType::ParticleSystem) {
        add(prism::MenuItem::Item("Open in Particle System Editor", [this, entryIndex] { OpenEntry(entryIndex); }, "",
                                  prism::Icon::Light));
    }
    // A clip becomes a montage: a .uvmontage beside it with the clip on its slot track, opened.
    if (extension == ".uvanim") {
        add(prism::MenuItem::Item("Create Motion Montage", [this, absolute] { CreateMontageFromClip(absolute); }, "", prism::Icon::AnimMontage),
            allowed);
    }
    // A montage joins a motion montage library, the way a clip joins an animation library.
    if (type == ItemType::MotionMontage) {
        std::vector<prism::MenuItem> libraries;
        for (const Asset::ProjectFileEntry& other : snapshot.entries) {
            if (other.kind != Asset::ProjectFileEntryKind::File || LowerExtension(other.relativePath) != ".uvmontagelib") {
                continue;
            }
            const std::filesystem::path library = snapshot.contentRoot / other.relativePath;
            libraries.push_back(prism::MenuItem::Item(other.relativePath.stem().string(),
                                                      [this, absolute, library] { AddMontageToLibrary(absolute, library); }));
        }
        if (!libraries.empty()) {
            libraries.push_back(prism::MenuItem::Sep());
        }
        libraries.push_back(prism::MenuItem::Item("New Motion Montage Library", [this, absolute] {
            const std::filesystem::path library = absolute.parent_path() / "Motion Montage Library.uvmontagelib";
            AddMontageToLibrary(absolute, library);
        }, "", prism::Icon::MontageLibrary));
        add(prism::MenuItem::Sub("Add to Motion Montage Library", std::move(libraries), prism::Icon::MontageLibrary), allowed);
    }
    if (type == ItemType::MontageLibrary) {
        add(prism::MenuItem::Item("Open Library", [this, entryIndex] { OpenEntry(entryIndex); }, "", prism::Icon::MontageLibrary));
    }
    // Model sources and imported models: place, use as the selected object's mesh, retarget.
    const bool modelSource = Editor::IsModelSourcePath(entry.relativePath);
    const std::filesystem::path importedModel =
        modelSource ? m_editor.GetImportedModelPath(entry.relativePath) : std::filesystem::path{};
    std::error_code ec;
    const bool modelReady = !importedModel.empty() && std::filesystem::is_regular_file(importedModel, ec);
    const bool uvmodel = entry.registeredAssetGuid.has_value() && extension == ".uvmodel";
    const EditorModelSourceInfo* const info = modelSource ? m_editor.FindModelSourceInfo(entry.relativePath) : nullptr;
    // A clip of an animation-only file places that file, playing this clip.
    const std::filesystem::path clipSource =
        extension == ".uvanim" ? m_editor.FindAnimationClipSource(entry.relativePath) : std::filesystem::path{};
    if (modelReady || uvmodel || (info != nullptr && info->hasSkeleton) || !clipSource.empty()) {
        add(prism::MenuItem::Item("Place in Scene", [this, rel = entry.relativePath, clipSource, name] {
            const Scene::Entity placed = clipSource.empty()
                                                ? m_editor.PlaceModelSource(rel)
                                                : m_editor.PlaceModelSource(clipSource, Scene::kInvalidEntity, rel);
            if (placed == Scene::kInvalidEntity && m_editor.m_contentStatusMessage.empty()) {
                m_editor.m_contentStatusMessage = "Could not place " + name + ".";
            }
        }, "", prism::Icon::Plus), allowed);
    }
    if (modelReady || uvmodel) {
        Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
        const bool canAssign = m_editor.IsDocumentEntity(m_editor.m_selectedEntity) && allowed &&
                               em.HasComponent<Scene::MeshComponent>(m_editor.m_selectedEntity);
        add(prism::MenuItem::Item("Use as Mesh on Selected Object", [this, modelReady, importedModel, entry] {
            Scene::IEntityManager& entities = m_editor.m_services->GetEntityManager();
            if (!entities.HasComponent<Scene::MeshComponent>(m_editor.m_selectedEntity)) {
                return;
            }
            const Asset::AssetGuid meshGuid =
                modelReady ? m_editor.RegisterImportedModelArtifacts(importedModel) : *entry.registeredAssetGuid;
            static_cast<void>(m_editor.SetSelectedMeshSource(meshGuid, std::nullopt));
        }), canAssign);
    }
    const auto isRetargetableFile = [this](const Asset::ProjectFileEntry& item) -> bool {
        if (item.kind == Asset::ProjectFileEntryKind::Directory) {
            return false;
        }
        const std::string ext = LowerExtension(item.relativePath);
        if (ext == ".uvanim" || ext == ".uvmodel") {
            return true;
        }
        if (Editor::IsModelSourcePath(item.relativePath)) {
            const EditorModelSourceInfo* const src = m_editor.FindModelSourceInfo(item.relativePath);
            return src == nullptr || src->rigged || src->hasSkeleton || src->animationCount > 0U || src->animationOnly;
        }
        return false;
    };
    const auto countInFolder = [&snapshot, &isRetargetableFile](const std::filesystem::path& relFolder) -> std::size_t {
        const std::string prefix = relFolder.generic_string();
        std::size_t count = 0U;
        for (const Asset::ProjectFileEntry& item : snapshot.entries) {
            if (!isRetargetableFile(item)) {
                continue;
            }
            const std::string p = item.relativePath.generic_string();
            if (prefix.empty() || p == prefix ||
                (p.size() > prefix.size() && p.compare(0U, prefix.size(), prefix) == 0 && p[prefix.size()] == '/')) {
                ++count;
            }
        }
        return count;
    };
    const bool inMultiSelect = m_editor.m_contentSelection.Items().size() > 1U &&
                               m_editor.m_contentSelection.Contains(entry.relativePath.generic_string());
    if (inMultiSelect) {
        // Strictly retarget ONLY the multi-selected items (expanding any selected folders recursively).
        const std::vector<std::filesystem::path> picked = TopLevelContentPaths(PickedContent());
        std::unordered_map<std::string, const Asset::ProjectFileEntry*> entryIndex;
        entryIndex.reserve(snapshot.entries.size());
        for (const Asset::ProjectFileEntry& item : snapshot.entries) {
            entryIndex.emplace(item.relativePath.generic_string(), &item);
        }
        std::vector<std::filesystem::path> retargetPicked;
        retargetPicked.reserve(picked.size());
        std::size_t retargetableCount = 0U;
        for (const std::filesystem::path& rel : picked) {
            const auto found = entryIndex.find(rel.generic_string());
            if (found == entryIndex.end()) {
                continue;
            }
            if (found->second->kind == Asset::ProjectFileEntryKind::Directory) {
                const std::size_t c = countInFolder(rel);
                if (c > 0U) {
                    retargetPicked.push_back(rel);
                    retargetableCount += c;
                }
            } else if (isRetargetableFile(*found->second)) {
                retargetPicked.push_back(rel);
                ++retargetableCount;
            }
        }
        if (!retargetPicked.empty()) {
            add(prism::MenuItem::Item(
                    "Retarget Selected (" + std::to_string(retargetableCount) + ")...",
                    [this, retargetPicked = std::move(retargetPicked)] {
                        m_editor.OpenRetargetWindowForPaths(retargetPicked);
                    }),
                allowed);
        }
    } else if (directory) {
        const std::size_t directAnimCount = m_editor.CollectAnimationClipsInFolder(entry.relativePath).size();
        if (directAnimCount > 0U) {
            add(prism::MenuItem::Item(
                    "Create Animation Library (" + std::to_string(directAnimCount) + ")",
                    [this, rel = entry.relativePath] {
                        if (m_editor.CreateAnimationLibraryFromFolder(rel).has_value()) {
                            Changed();
                        }
                    },
                    "", prism::Icon::AnimLibrary),
                allowed);
        }
        const std::size_t folderCount = countInFolder(entry.relativePath);
        if (folderCount > 0U) {
            add(prism::MenuItem::Item(
                    "Retarget Folder (" + std::to_string(folderCount) + ")...",
                    [this, rel = entry.relativePath] { m_editor.OpenRetargetWindowForFolder(rel); }),
                allowed);
        }
    } else {
        const bool canRetarget = extension == ".uvanim" || uvmodel || (info != nullptr && (info->rigged || info->hasSkeleton));
        if (canRetarget) {
            add(prism::MenuItem::Item("Retarget...", [this, extension, absolute, rel = entry.relativePath] {
                if (extension == ".uvanim") {
                    m_editor.OpenRetargetWindow({absolute}, {});
                } else {
                    m_editor.OpenRetargetWindow({}, rel);
                }
            }), allowed);
        }
    }
    if (!items.empty()) {
        items.push_back(prism::MenuItem::Sep());
    }
    // Favorites and shelves.
    const bool pinned = m_editor.IsProjectPathFavorited(entry.relativePath);
    add(prism::MenuItem::Item(pinned ? "Unpin from Favorites" : "Pin to Favorites", [this, rel = entry.relativePath] {
        m_editor.ToggleProjectPathFavorite(rel);
        m_favoriteSignature = 0;
    }, "", prism::Icon::NodeFlag));
    std::vector<prism::MenuItem> shelves;
    for (const ContentShelf& shelf : m_editor.m_contentShelves.GetAll()) {
        const bool onShelf = m_editor.m_contentShelves.Contains(shelf.name, entry.relativePath);
        shelves.push_back(prism::MenuItem::Check(shelf.name, onShelf, [this, shelfName = shelf.name, onShelf, rel = entry.relativePath] {
            if (onShelf) {
                static_cast<void>(m_editor.m_contentShelves.RemoveItem(shelfName, rel));
            } else {
                static_cast<void>(m_editor.m_contentShelves.AddItem(shelfName, rel));
            }
            static_cast<void>(m_editor.SaveSharedShelves());
            m_folderSignature = 0;
            Changed();
        }));
    }
    if (!shelves.empty()) {
        shelves.push_back(prism::MenuItem::Sep());
    }
    shelves.push_back(prism::MenuItem::Item("New Shelf with This", [this, rel = entry.relativePath] {
        const std::string shelfName = m_editor.m_contentShelves.Create("Shelf");
        if (!shelfName.empty()) {
            static_cast<void>(m_editor.m_contentShelves.AddItem(shelfName, rel));
            static_cast<void>(m_editor.SaveSharedShelves());
        }
        m_folderSignature = 0;
    }));
    items.push_back(prism::MenuItem::Sub("Shelves", std::move(shelves), prism::Icon::Layers));
    if (!m_editor.m_assetTableShelf.empty() &&
        m_editor.m_contentShelves.Contains(m_editor.m_assetTableShelf, entry.relativePath)) {
        add(prism::MenuItem::Item("Remove from " + m_editor.m_assetTableShelf, [this, rel = entry.relativePath] {
            static_cast<void>(m_editor.m_contentShelves.RemoveItem(m_editor.m_assetTableShelf, rel));
            static_cast<void>(m_editor.SaveSharedShelves());
            Changed();
        }));
    }
    add(prism::MenuItem::Item("Show in Folder", [this, rel = entry.relativePath] { Reveal(rel); }, "", prism::Icon::FolderOpen));
    add(prism::MenuItem::Item("Copy Path", [rel = entry.relativePath] {
        if (prism::Application::exists()) {
            prism::Application::get().platform().setClipboard(rel.generic_string());
        }
    }, "", prism::Icon::Copy));
    items.push_back(prism::MenuItem::Sep());
    add(prism::MenuItem::Item("Rename", [this, entryIndex] { ShowRename(entryIndex); }, "F2"), allowed);
    add(prism::MenuItem::Item("Duplicate", [this, absolute, name] {
        if (!Editor::DuplicateContentFile(absolute).has_value()) {
            m_editor.m_contentStatusMessage = "Could not duplicate " + name + ".";
        }
        m_editor.RefreshProjectFileIndex();
        m_folderSignature = 0;
        Changed();
    }, "", prism::Icon::Duplicate), allowed);
    // Part of a multi-pick: deletes every picked item, after one question.
    if (m_editor.m_contentSelection.Items().size() > 1U &&
        m_editor.m_contentSelection.Contains(entry.relativePath.generic_string())) {
        const std::vector<std::filesystem::path> picked = TopLevelContentPaths(PickedContent());
        add(prism::MenuItem::Item("Delete " + CountItems(picked.size()), [this, picked] { AskDeleteContent(picked); },
                                  "Delete", prism::Icon::Trash),
            allowed);
    } else {
        // Deleting cannot be undone, so it asks once more in a submenu instead of acting on one click.
        std::vector<prism::MenuItem> confirm;
        confirm.push_back(prism::MenuItem::Item(directory ? "Delete folder and everything in it" : "Delete permanently",
                                                [this, rel = entry.relativePath] { DeleteContent({rel}); }, "",
                                                prism::Icon::Trash));
        prism::MenuItem del = prism::MenuItem::Sub("Delete", std::move(confirm), prism::Icon::Trash);
        if (!allowed) {
            del.disabled();
        }
        items.push_back(std::move(del));
    }
    prism::showMenu(std::move(items), prism::Rect{at.x, at.y, 1.0F, 1.0F}, prism::PopupPlacement::Below);
}

void EditorAssetTable::ShowRename(const std::size_t entryIndex) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntry entry = snapshot.entries[entryIndex];
    const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
    auto field = prism::make<prism::TextInput>(entry.relativePath.stem().string(), "New name");
    field->selectAll();
    std::weak_ptr<prism::TextInput> weakField = field;
    const auto apply = [this, weakField, absolute] {
        auto f = weakField.lock();
        if (!f || f->text().empty()) {
            return;
        }
        if (!m_editor.RenameContentAsset(absolute, f->text()).has_value()) {
            m_editor.m_contentStatusMessage = "Could not rename to \"" + f->text() + "\" (empty, taken or not allowed).";
        }
        m_editor.RefreshProjectFileIndex();
        m_folderSignature = 0;
        Changed();
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

void EditorAssetTable::BeginDrag(const std::size_t entryIndex) {
    // A content drag: the payload is the item's content-relative path (the Outliner places entity
    // assets and models dropped on it).
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    if (entryIndex >= snapshot.entries.size() || !prism::Application::exists()) {
        return;
    }
    const Asset::ProjectFileEntry& entry = snapshot.entries[entryIndex];
    auto op = std::make_shared<prism::DragDropOperation>(kDragTag);
    op->setPayload(entry.relativePath.generic_string());
    const std::string name = entry.relativePath.filename().string();
    const EditorIcons::Image icon = TypeIcon(m_editor.GetAssetTableEntryType(entry), 16);
    op->setDecorator([name, icon](prism::Canvas& c, const prism::Vec2 mouse) {
        const prism::Theme& t = prism::theme();
        const prism::Rect card{mouse.x + 12.0F, mouse.y + 12.0F, std::min(260.0F, t.ui->width(name) + 40.0F), 26.0F};
        c.shadow(card, 3, 6, prism::Color(0, 0, 0, 140));
        c.fillRoundRect(card, 4.0F, prism::Color(35, 45, 60, 240));
        c.strokeRoundRect(card, 4.0F, t.highlight, 1);
        if (icon) {
            c.blitBlend(icon.pixels, icon.size, icon.size, icon.size, prism::Rect{card.x + 6.0F, card.y + 5.0F, 16.0F, 16.0F});
        }
        c.textIn(*t.ui, prism::Rect{card.x + 28.0F, card.y, card.w - 32.0F, card.h}, name, t.textBright, prism::HAlign::Left);
    });
    prism::Application::get().startDragDrop(op);
}

const EditorAssetTable::Preview* EditorAssetTable::PreviewFor(
    const Asset::ProjectFileEntry& entry, const ItemType type) {
    std::uintptr_t texture = 0U;
    bool renderTarget = false;
    switch (type) {
        case ItemType::Texture: texture = m_editor.GetTextureThumbnail(entry.relativePath); break;
        case ItemType::Material:
            texture = m_editor.GetMaterialThumbnail(entry.relativePath);
            renderTarget = true;
            break;
        case ItemType::ParticleSystem:
            texture = m_editor.GetVfxThumbnail(entry.relativePath);
            renderTarget = false;
            break;
        case ItemType::Animation: {
            // A clip's icon is drawn on the CPU already: copied as it is (again when it is redrawn).
            // The content root kept from the last listing: a snapshot copy per tile would cost a
            // whole project listing for each of thousands of clips.
            const std::filesystem::path absolute = m_contentRoot / entry.relativePath;
            const Editor::AnimationIcon* const icon = m_editor.GetAnimationIcon(absolute);
            if (icon == nullptr) {
                return nullptr;
            }
            Preview& preview = m_previews[entry.relativePath.generic_string()];
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
        case ItemType::Entity:
        case ItemType::Prefab:
            // An entity's own look: its models and primitives through the thumbnail's camera.
        case ItemType::Mesh:
        case ItemType::Model:
            // The mesh preview is drawn upright already (its projection is flipped for top-down
            // images), so it is read as it is; reading it bottom-up stood the model on its head.
            texture = m_editor.GetMeshThumbnail(entry.relativePath);
            renderTarget = false;
            break;
        default: return nullptr;
    }
    if (texture == 0U) {
        return nullptr;
    }
    Preview& preview = m_previews[entry.relativePath.generic_string()];
    const std::uint64_t generation =
        type == ItemType::Material ? m_editor.m_materialThumbnailGeneration : m_editor.m_meshThumbnailGeneration;
    if (preview.texture == texture && preview.generation == generation && !preview.pixels.empty()) {
        return &preview;
    }
    // Read the engine's GL texture back once; PRISM draws CPU-side pixels.
    if (!ReadTexturePixels(texture, renderTarget, preview.width, preview.height, preview.pixels)) {
        return nullptr;
    }
    preview.texture = texture;
    preview.generation = generation;
    return &preview;
}

void EditorAssetTable::SelectEntry(const std::size_t entryIndex, const bool additive, const bool range) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntry& entry = snapshot.entries[entryIndex];
    m_editor.m_contentSelection.Click(m_editor.m_contentShownOrderPrevious, entry.relativePath.generic_string(),
                                         additive, range);
    m_editor.m_selectedProjectFile = entry;
    if (entry.registeredAssetGuid.has_value()) {
        m_editor.m_selectedAsset = Asset::AssetRecord{*entry.registeredAssetGuid, snapshot.contentRoot / entry.relativePath};
    } else {
        m_editor.m_selectedAsset.reset();
    }
}

std::vector<std::filesystem::path> EditorAssetTable::PickedContent() const {
    std::vector<std::filesystem::path> picked;
    for (const std::string& item : m_editor.m_contentSelection.Items()) {
        picked.emplace_back(item);
    }
    if (picked.empty() && m_editor.m_selectedProjectFile.has_value()) {
        picked.push_back(m_editor.m_selectedProjectFile->relativePath);
    }
    return picked;
}

void EditorAssetTable::AskDeleteContent(std::vector<std::filesystem::path> relativePaths) {
    relativePaths = TopLevelContentPaths(relativePaths);
    if (relativePaths.empty() || !m_editor.IsAuthoringCommandAllowed() || !prism::Application::exists()) {
        return;
    }
    const std::string what =
        relativePaths.size() == 1U ? relativePaths.front().filename().string() : CountItems(relativePaths.size());
    auto text = prism::make<prism::Label>("Delete " + what + "? This cannot be undone.");
    prism::showDialog("Delete", text,
                      {prism::DialogButton{"Cancel", false, {}},
                       prism::DialogButton{"Delete", true, [this, relativePaths] { DeleteContent(relativePaths); }}},
                      380.0F);
}

void EditorAssetTable::DeleteContent(const std::vector<std::filesystem::path>& relativePaths) {
    const std::filesystem::path contentRoot = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot;
    const std::vector<std::filesystem::path> paths = TopLevelContentPaths(relativePaths);
    if (contentRoot.empty() || paths.empty()) {
        return;
    }
    std::vector<std::string> deleted;
    std::vector<std::string> failed;
    for (const std::filesystem::path& rel : paths) {
        const std::filesystem::path absolute = contentRoot / rel;
        std::error_code error;
        std::filesystem::remove_all(absolute, error);
        if (error) {
            failed.push_back(rel.filename().string());
            continue;
        }
        const std::string key = rel.generic_string();
        deleted.push_back(key);
        if (m_editor.m_onContentDeleted) {
            m_editor.m_onContentDeleted(absolute);
        }
        if (IsSameOrInside(m_editor.GetDefaultPlayerEntity(), key)) {
            static_cast<void>(m_editor.SetDefaultPlayerEntity({}));
        }
        if (m_editor.m_selectedProjectFile.has_value() &&
            IsSameOrInside(m_editor.m_selectedProjectFile->relativePath.generic_string(), key)) {
            m_editor.m_selectedProjectFile.reset();
            m_editor.m_selectedAsset.reset();
        }
    }
    std::vector<std::string> kept;
    for (const std::string& item : m_editor.m_contentSelection.Items()) {
        if (std::ranges::none_of(deleted, [&item](const std::string& gone) { return IsSameOrInside(item, gone); })) {
            kept.push_back(item);
        }
    }
    m_editor.m_contentSelection.KeepOnly(kept);
    std::string message = deleted.empty() ? std::string() : "Deleted " + CountItems(deleted.size()) + ".";
    if (!failed.empty()) {
        std::string names;
        for (std::size_t i = 0; i < failed.size() && i < 3U; ++i) {
            names += (i == 0U ? "" : ", ") + failed[i];
        }
        if (failed.size() > 3U) {
            names += " and " + std::to_string(failed.size() - 3U) + " more";
        }
        message += (message.empty() ? "Could not delete " : " Could not delete ") + names + ".";
    }
    m_editor.m_contentStatusMessage = message;
    m_editor.RefreshProjectFileIndex();
    m_folderSignature = 0;
    Changed();
}

void EditorAssetTable::OpenEntry(const std::size_t entryIndex) {
    const Asset::ProjectFileSnapshot snapshot = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    if (entryIndex >= snapshot.entries.size()) {
        return;
    }
    const Asset::ProjectFileEntry entry = snapshot.entries[entryIndex];
    const std::string name = entry.relativePath.filename().string();
    const std::filesystem::path full = snapshot.contentRoot / entry.relativePath;
    // The same actions the previous Asset Table ran on a double click.
    if (entry.kind == Asset::ProjectFileEntryKind::Directory) {
        GoToFolder(entry.relativePath);
        return;
    }
    switch (m_editor.GetAssetTableEntryType(entry)) {
        case ItemType::Entity:
            if (!m_editor.OpenEntityEditor(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Entity Editor.";
            }
            break;
        case ItemType::Scene:
            if (!m_editor.OpenSceneAsset(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " as a scene.";
            }
            break;
        case ItemType::MotionMontage:
            if (!m_editor.OpenMontageEditor(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Motion Montage Editor.";
            }
            break;
        case ItemType::MontageLibrary:
            ShowMontageLibrary(full);
            break;
        case ItemType::ParticleSystem:
            if (!m_editor.OpenVfxEditor(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Particle System Editor.";
            }
            break;
        case ItemType::AnimationLibrary:
            if (!m_editor.OpenAnimationLibrary(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " as an animation library.";
            }
            break;
        case ItemType::Blueprint:
            OpenBlueprint(full);
            break;
        case ItemType::WidgetBlueprint:
            if (!m_editor.OpenEntityEditor(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Widget Editor.";
            }
            break;
        case ItemType::Script:
            if (!m_editor.OpenUVScriptPath(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " in the Script Editor.";
            }
            break;
        default:
            break;
    }
}

void EditorAssetTable::ShowFilterMenu() {
    static constexpr std::array<std::pair<Focus, const char*>, 12> kFocuses{{
        {Focus::All, "All"},
        {Focus::Folders, "Folders"},
        {Focus::Scene, "Scenes"},
        {Focus::Prefab, "Prefabs"},
        {Focus::Bundle, "Bundles"},
        {Focus::Mesh, "Meshes"},
        {Focus::Texture, "Textures"},
        {Focus::Shader, "Shaders"},
        {Focus::Material, "Materials"},
        {Focus::Save, "Saves"},
        {Focus::Registered, "Registered"},
        {Focus::OtherFiles, "Other files"},
    }};
    std::vector<prism::MenuItem> items;
    for (const auto& [focus, label] : kFocuses) {
        items.push_back(prism::MenuItem::Check(label, m_editor.m_assetTableTypeFocus == focus, [this, f = focus] {
            m_editor.m_assetTableTypeFocus = f;
            m_tileSignature = 0;
        }));
    }
    prism::showMenu(std::move(items), m_filterButton ? m_filterButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

// ---------------------------------------------------------------------------------- montages

void EditorAssetTable::CreateMontageFromClip(const std::filesystem::path& clipPath) {
    Asset::AnimationClipAsset clip;
    if (!Asset::LoadAnimationClipAsset(clipPath, clip)) {
        m_editor.m_contentStatusMessage = "Could not read " + clipPath.filename().string();
        return;
    }
    Asset::IAssetDatabase& database = m_editor.m_services->GetAssetDatabase();
    const Asset::AssetGuid guid = database.Register(clipPath);
    Asset::MotionMontageAsset montage;
    static_cast<void>(Asset::AddMontageSegment(montage, guid, clip.durationSeconds));
    // AM_<clip>, or AM_<clip>_2 ... beside the clip.
    std::filesystem::path path = clipPath.parent_path() / ("MM_" + clipPath.stem().string() + ".uvmontage");
    std::error_code error;
    for (int n = 2; std::filesystem::exists(path, error) && n < 1000; ++n) {
        path = clipPath.parent_path() / ("MM_" + clipPath.stem().string() + "_" + std::to_string(n) + ".uvmontage");
    }
    if (!Asset::SaveMotionMontageAsset(montage, path)) {
        m_editor.m_contentStatusMessage = "Could not write " + path.filename().string();
        return;
    }
    static_cast<void>(database.Register(path));
    static_cast<void>(m_editor.m_services->GetProjectFileIndex().Refresh(m_editor.m_services->GetAssetDatabase()));
    m_editor.m_contentStatusMessage = "Created " + path.filename().string();
    static_cast<void>(m_editor.OpenMontageEditor(path));
}

void EditorAssetTable::AddMontageToLibrary(const std::filesystem::path& montagePath, const std::filesystem::path& libraryPath) {
    Asset::MontageLibraryAsset library;
    std::error_code error;
    if (std::filesystem::exists(libraryPath, error)) {
        if (!Asset::LoadMontageLibraryAsset(libraryPath, library)) {
            m_editor.m_contentStatusMessage = "Not an motion montage library: " + libraryPath.filename().string();
            return;
        }
    } else {
        library.libraryId = libraryPath.stem().string();
    }
    Asset::IAssetDatabase& database = m_editor.m_services->GetAssetDatabase();
    const Asset::AssetGuid guid = database.Register(montagePath);
    if (std::ranges::any_of(library.entries, [guid](const Asset::MontageLibraryEntry& e) { return e.montage == guid; })) {
        m_editor.m_contentStatusMessage = montagePath.stem().string() + " is already in " + libraryPath.stem().string();
        return;
    }
    Asset::MontageLibraryEntry entry;
    entry.montage = guid;
    entry.name = montagePath.stem().string();
    library.entries.push_back(std::move(entry));
    if (!Asset::SaveMontageLibraryAsset(library, libraryPath)) {
        m_editor.m_contentStatusMessage = "Could not write " + libraryPath.filename().string();
        return;
    }
    static_cast<void>(database.Register(libraryPath));
    static_cast<void>(m_editor.m_services->GetProjectFileIndex().Refresh(m_editor.m_services->GetAssetDatabase()));
    m_editor.m_contentStatusMessage = "Added " + montagePath.stem().string() + " to " + libraryPath.stem().string();
}

void EditorAssetTable::ShowMontageLibrary(const std::filesystem::path& libraryPath) {
    Asset::MontageLibraryAsset library;
    if (!Asset::LoadMontageLibraryAsset(libraryPath, library)) {
        m_editor.m_contentStatusMessage = "Could not read " + libraryPath.filename().string();
        return;
    }
    // Its montages, each with the name a script plays it by, opened or taken off the list.
    Asset::IAssetDatabase& database = m_editor.m_services->GetAssetDatabase();
    auto list = prism::vbox(4.0F);
    list->setPadding(prism::Margin(4.0F));
    if (library.entries.empty()) {
        auto hint = prism::make<prism::Label>("Empty. Right-click a motion montage > Add to Motion Montage Library.", prism::FontRole::Small);
        hint->setColor(prism::ThemeColor(&prism::Theme::textDim));
        list->add(hint);
    }
    for (const Asset::MontageLibraryEntry& entry : library.entries) {
        const std::filesystem::path montage = database.Resolve(entry.montage);
        auto row = prism::hbox(6.0F);
        auto label = prism::make<prism::Label>(entry.name + (montage.empty() ? "   (missing)" : "   " + montage.filename().string()));
        row->add(label, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        auto open = prism::make<prism::Button>("Open", prism::Icon::Open, prism::ButtonStyle::Flat);
        open->setEnabled(!montage.empty());
        open->onClick = [this, montage] {
            if (prism::Application::exists()) {
                prism::Application::get().closeAllPopups();
            }
            static_cast<void>(m_editor.OpenMontageEditor(montage));
        };
        auto remove = prism::make<prism::Button>("Remove", prism::Icon::Trash, prism::ButtonStyle::Flat);
        remove->onClick = [this, libraryPath, guid = entry.montage] {
            Asset::MontageLibraryAsset current;
            if (Asset::LoadMontageLibraryAsset(libraryPath, current)) {
                std::erase_if(current.entries, [guid](const Asset::MontageLibraryEntry& e) { return e.montage == guid; });
                static_cast<void>(Asset::SaveMontageLibraryAsset(current, libraryPath));
            }
            if (prism::Application::exists()) {
                prism::Application::get().closeAllPopups();
            }
            ShowMontageLibrary(libraryPath);
        };
        row->add(open);
        row->add(remove);
        list->add(row);
    }
    prism::showDialog("Motion Montage Library - " + libraryPath.stem().string(), list, {prism::DialogButton{"Close", true, {}}}, 480.0F);
}

} // namespace UVE::Editor
