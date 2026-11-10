// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_content_browser.h"

#include "editor.h"
#include "editor_content_browser_model.h"
#include "editor_content_catalogue.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Icons.h"
#include "prism/AssetBrowser.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

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

} // namespace

prism::Icon EditorPrismContentBrowserUVE::IconForUVE(const ItemTypeUVE type) {
    switch (type) {
        case ItemTypeUVE::Folder: return prism::Icon::Folder;
        case ItemTypeUVE::Scene: return prism::Icon::Globe;
        case ItemTypeUVE::Prefab: return prism::Icon::Layers;
        case ItemTypeUVE::Entity: return prism::Icon::Cube;
        case ItemTypeUVE::Bundle: return prism::Icon::Layers;
        case ItemTypeUVE::Mesh: return prism::Icon::Cube;
        case ItemTypeUVE::Model: return prism::Icon::NodeBone;
        case ItemTypeUVE::Texture: return prism::Icon::NodeTexture;
        case ItemTypeUVE::Shader: return prism::Icon::Script;
        case ItemTypeUVE::Material: return prism::Icon::Material;
        case ItemTypeUVE::Save: return prism::Icon::Save;
        case ItemTypeUVE::Animation: return prism::Icon::AnimGraph;
        case ItemTypeUVE::AnimationLibrary: return prism::Icon::NodeClip;
        case ItemTypeUVE::Script: return prism::Icon::Script;
        case ItemTypeUVE::Audio: return prism::Icon::NodeSound;
        case ItemTypeUVE::Font: return prism::Icon::NodeText;
        case ItemTypeUVE::File: return prism::Icon::File;
    }
    return prism::Icon::File;
}

std::pair<std::string, std::string> EditorPrismContentBrowserUVE::DescribeUVE(const ItemTypeUVE type) {
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
        case ItemTypeUVE::Script: return {"Script", "UVS"};
        case ItemTypeUVE::Audio: return {"Audio", "AUD"};
        case ItemTypeUVE::Font: return {"Font", "FNT"};
        case ItemTypeUVE::File: return {"File", "FILE"};
    }
    return {"File", "FILE"};
}

prism::Color EditorPrismContentBrowserUVE::TintForUVE(const ItemTypeUVE type) {
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
        case ItemTypeUVE::AnimationLibrary: return prism::Color(90, 200, 200);
        case ItemTypeUVE::Audio: return prism::Color(230, 190, 90);
        default: return prism::Color(170, 176, 186);
    }
}


/// One item in the tile area: type glyph over the name, accent frame when picked.
class EditorPrismContentBrowserUVE::TileUVE final : public prism::Widget {
public:
    TileUVE(EditorPrismContentBrowserUVE& owner, const std::size_t entryIndex, std::string name, const ItemTypeUVE type,
            const bool selected)
        : m_owner(owner), m_entryIndex(entryIndex), m_name(std::move(name)), m_type(type), m_selected(selected) {}

    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain(prism::Vec2(108.0F, 136.0F)); }

    void paint(prism::Canvas& c) override {
        // PRISM's content tile and thumbnail (PBR previews for meshes / materials, texture, shader,
        // font and level cards), fed with the engine's own classification of the file.
        prism::AssetRecord record{};
        record.name = m_name;
        record.path = m_name;
        record.icon = IconForUVE(m_type);
        record.badgeColor = TintForUVE(m_type);
        const auto [typeName, badge] = DescribeUVE(m_type);
        record.type = typeName;
        record.badgeText = badge;
        prism::paintAssetTile(c, rect(), record, m_selected, m_hot);
    }

    bool onMouseDown(const prism::MouseEvent& e) override {
        if (e.button == prism::MouseButton::Left) {
            m_owner.SelectEntryUVE(m_entryIndex, (e.mods & prism::ModCtrl) != 0, (e.mods & prism::ModShift) != 0);
            if (e.clicks >= 2) {
                m_owner.OpenEntryUVE(m_entryIndex);
            }
            return true;
        }
        return e.button == prism::MouseButton::Right;
    }
    bool onMouseMove(const prism::MouseEvent&) override {
        if (!m_hot) {
            m_hot = true;
            invalidate();
        }
        return false;
    }
    void onMouseLeave() override {
        m_hot = false;
        invalidate();
    }

private:
    EditorPrismContentBrowserUVE& m_owner;
    std::size_t m_entryIndex = 0;
    std::string m_name;
    ItemTypeUVE m_type = ItemTypeUVE::File;
    bool m_selected = false;
    bool m_hot = false;
};

EditorPrismContentBrowserUVE::EditorPrismContentBrowserUVE(EditorUVE& editor) : m_editor(editor) {}

EditorPrismContentBrowserUVE::~EditorPrismContentBrowserUVE() = default;

prism::Ref<prism::Widget> EditorPrismContentBrowserUVE::SectionHeaderUVE(const std::string& title, const bool open,
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

prism::Ref<prism::Widget> EditorPrismContentBrowserUVE::BuildUVE() {
    // Left, as drawn: a collapsed "Shelves" section, the open "Content" section (its search behind
    // the magnifier, then the folder tree), and the create row with '+' at the bottom.
    auto left = prism::vbox(0.0F);

    m_shelves = prism::make<prism::TreeView>();
    m_shelves->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (m_syncingFolders || nodes.empty()) {
            return;
        }
        const int id = nodes.front()->id;
        if (id > 0 && static_cast<std::size_t>(id) <= m_shelfNames.size()) {
            m_editor.m_contentBrowserShelf = m_shelfNames[static_cast<std::size_t>(id - 1)];
            m_editor.m_contentSelection.ClearUVE();
            m_tileSignature = 0;
        }
    };
    m_shelves->setVisible(false);
    left->add(SectionHeaderUVE("Shelves", false, m_shelves, nullptr));
    left->add(m_shelves, prism::Slot{0.0F});

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
    m_folders->onSelect = [this](const std::vector<prism::TreeNode*>& nodes) {
        if (m_syncingFolders || nodes.empty()) {
            return;
        }
        const int id = nodes.front()->id;
        if (id > 0 && static_cast<std::size_t>(id) <= m_folderIds.size()) {
            GoToFolderUVE(std::filesystem::path(m_folderIds[static_cast<std::size_t>(id - 1)]));
        }
    };
    left->add(SectionHeaderUVE("Content", true, m_folders, searchToggle));
    left->add(m_folderSearch, prism::Slot{0.0F, prism::Margin(4.0F, 2.0F, 4.0F, 2.0F)});
    left->add(m_folders, prism::Slot{1.0F});

    m_addButton = prism::make<prism::Button>("", prism::Icon::Plus, prism::ButtonStyle::Flat);
    m_addButton->setTooltip("Create or import in this folder");
    m_addButton->onClick = [this] { ShowAddMenuUVE(); };
    auto createRow = prism::hbox(2.0F);
    createRow->setPadding(prism::Margin(4.0F, 1.0F, 4.0F, 1.0F));
    auto createChevron = prism::make<prism::Button>("Create / Import", prism::Icon::TriRight, prism::ButtonStyle::Flat);
    createChevron->onClick = [this] { ShowAddMenuUVE(); };
    createRow->add(createChevron, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    createRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    createRow->add(m_addButton, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    auto createBar = prism::make<prism::Border>();
    createBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    createBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    createBar->setChild(createRow);
    left->add(createBar);

    // Right: Filter | item search, path, tiles, status.
    auto right = prism::vbox(4.0F);
    right->setPadding(prism::Margin(4.0F));
    auto bar = prism::hbox(4.0F);
    m_filterButton = prism::make<prism::Button>("Filter", prism::Icon::Filter, prism::ButtonStyle::Toolbar);
    m_filterButton->onClick = [this] { ShowFilterMenuUVE(); };
    bar->add(m_filterButton, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    m_itemSearch = prism::make<prism::TextInput>("", "Search items");
    m_itemSearch->onChanged = [this](const std::string& text) {
        m_editor.m_assetFilter = text;
        m_tileSignature = 0;
    };
    bar->add(m_itemSearch, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    right->add(bar);
    m_path = prism::make<prism::Label>("Content", prism::FontRole::Small);
    m_path->setColor(prism::ThemeColor(&prism::Theme::textDim));
    right->add(m_path);
    m_tiles = prism::make<prism::WrapBox>(prism::Orientation::Horizontal, 6.0F, 6.0F);
    m_tilesScroll = prism::make<prism::ScrollView>();
    m_tilesScroll->setContent(m_tiles);
    right->add(m_tilesScroll, prism::Slot{1.0F});
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    right->add(m_status);

    auto split = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    split->add(left, prism::Splitter::Pane{220.0F, 140.0F, 600.0F, false});
    split->add(right, prism::Splitter::Pane{600.0F, 240.0F, 1e9F, true});
    return split;
}

void EditorPrismContentBrowserUVE::RefreshUVE() {
    RebuildFoldersUVE();
    RebuildTilesUVE();
    if (m_status && m_status->text() != m_editor.m_contentStatusMessage) {
        m_status->setText(m_editor.m_contentStatusMessage);
    }
}

void EditorPrismContentBrowserUVE::GoToFolderUVE(const std::filesystem::path& directory) {
    m_editor.m_contentBrowserDirectory = directory;
    m_editor.m_contentBrowserShelf.clear();
    m_editor.m_selectedProjectFile.reset();
    m_editor.m_selectedAsset.reset();
    m_editor.m_contentSelection.ClearUVE();
    m_tileSignature = 0;
}

void EditorPrismContentBrowserUVE::RebuildFoldersUVE() {
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

void EditorPrismContentBrowserUVE::RebuildTilesUVE() {
    if (!m_tiles) {
        return;
    }
    Asset::IProjectFileIndexUVE& index = m_editor.m_services->GetProjectFileIndexUVE();
    const Asset::ProjectFileSnapshotUVE snapshot = index.GetSnapshotUVE();
    m_editor.ReconcileContentBrowserDirectoryUVE(snapshot);
    std::uint64_t h = snapshot.refreshGeneration;
    HashUVE(h, m_editor.m_contentBrowserDirectory.generic_string());
    HashUVE(h, m_editor.m_assetFilter);
    HashUVE(h, m_editor.m_contentBrowserShelf);
    HashUVE(h, static_cast<std::uint64_t>(m_editor.m_contentBrowserTypeFocus));
    for (const std::string& item : m_editor.m_contentSelection.ItemsUVE()) {
        HashUVE(h, item);
    }
    if (m_editor.m_selectedProjectFile.has_value()) {
        HashUVE(h, m_editor.m_selectedProjectFile->relativePath.generic_string());
    }
    if (h == m_tileSignature) {
        return;
    }
    m_tileSignature = h;

    const std::filesystem::path directory = m_editor.m_contentBrowserDirectory;
    const ContentShelfUVE* const shelf =
        m_editor.m_contentBrowserShelf.empty() ? nullptr : m_editor.m_contentShelves.FindUVE(m_editor.m_contentBrowserShelf);
    std::vector<std::size_t> listed = shelf != nullptr
                                          ? ListContentShelfUVE(snapshot.entries, *shelf, m_editor.m_assetFilter)
                                          : ListContentFolderUVE(snapshot.entries, directory, m_editor.m_assetFilter);
    m_shown.clear();
    std::vector<std::string> order;
    for (const std::size_t i : listed) {
        const Asset::ProjectFileEntryUVE& entry = snapshot.entries[i];
        if (!m_editor.DoesContentBrowserEntryMatchFocusUVE(entry)) {
            continue;
        }
        m_shown.push_back(i);
        order.push_back(entry.relativePath.generic_string());
    }
    m_editor.m_contentShownOrderPrevious = order;

    m_tiles->clearChildren();
    for (const std::size_t i : m_shown) {
        const Asset::ProjectFileEntryUVE& entry = snapshot.entries[i];
        const std::string key = entry.relativePath.generic_string();
        const bool selected = m_editor.m_contentSelection.ContainsUVE(key) ||
                              (m_editor.m_contentSelection.IsEmptyUVE() && m_editor.m_selectedProjectFile.has_value() &&
                               m_editor.m_selectedProjectFile->relativePath == entry.relativePath);
        const ItemTypeUVE type = EditorUVE::ClassifyContentBrowserEntryUVE(entry);
        auto tile = prism::make<TileUVE>(*this, i, entry.relativePath.filename().string(), type, selected);
        tile->setTooltip(entry.relativePath.filename().string() + "\nType: " +
                         EditorUVE::GetContentBrowserItemTypeLabelUVE(type));
        m_tiles->add(tile);
    }
    if (m_path) {
        m_path->setText(shelf != nullptr ? "Shelf / " + shelf->name
                         : directory.empty() ? std::string("Content")
                                             : "Content / " + directory.generic_string());
    }
    if (m_shown.empty() && m_status && m_editor.m_contentStatusMessage.empty()) {
        m_status->setText(snapshot.contentRootExists ? "This folder is empty." : "No Content folder in this project.");
    }
}

void EditorPrismContentBrowserUVE::SelectEntryUVE(const std::size_t entryIndex, const bool additive, const bool range) {
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

void EditorPrismContentBrowserUVE::OpenEntryUVE(const std::size_t entryIndex) {
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
        case ItemTypeUVE::AnimationLibrary:
            if (!m_editor.OpenAnimationLibraryUVE(full)) {
                m_editor.m_contentStatusMessage = "Could not open " + name + " as an animation library.";
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

void EditorPrismContentBrowserUVE::ShowAddMenuUVE() {
    // The engine's content catalogue ("+ Add"), grouped as the previous Content Browser grouped it.
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Item("Import Files...", [this] {
        const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        m_editor.PromptImportFilesUVE(snapshot.contentRoot / m_editor.m_contentBrowserDirectory);
    }, "Ctrl+I", prism::Icon::Open));
    items.push_back(prism::MenuItem::Sep());
    for (const std::string_view group : GetContentCatalogueGroupsUVE()) {
        std::vector<prism::MenuItem> sub;
        for (const ContentCatalogueItemUVE& item : GetContentCatalogueItemsUVE()) {
            if (item.group != group) {
                continue;
            }
            sub.push_back(prism::MenuItem::Item(std::string(item.label), [this, id = std::string(item.id)] {
                const Asset::ProjectFileSnapshotUVE snapshot =
                    m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                const std::optional<std::filesystem::path> created = m_editor.CreateContentCatalogueItemUVE(
                    id, snapshot.contentRoot / m_editor.m_contentBrowserDirectory);
                if (!created.has_value()) {
                    m_editor.m_contentStatusMessage = "Could not create that here.";
                }
                m_tileSignature = 0;
                m_folderSignature = 0;
            }));
        }
        if (!sub.empty()) {
            items.push_back(prism::MenuItem::Sub(std::string(group), std::move(sub)));
        }
    }
    prism::showMenu(std::move(items), m_addButton ? m_addButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

void EditorPrismContentBrowserUVE::ShowFilterMenuUVE() {
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

} // namespace UVE::Editor
