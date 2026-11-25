// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "material_asset.h"
#include "texture_asset.h"

#include "editor.h"
#include "editor_asset_table_model.h"
#include "editor_icons.h"

#include "prism/Icons.h"
#include "prism/Menu.h"
#include "prism/Widget.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace prism {
class Border;
class Button;
class Label;
class ScrollView;
class Splitter;
class TableView;
class TextInput;
class TreeView;
class WrapBox;
} // namespace prism

namespace UVE::Editor {

/// The editor's Asset Table drawn with PRISM widgets. Everything it shows and does is the
/// engine's own: the project file index snapshot, the content model (ListContentFolder,
/// ListContentTree, CollectVisibleContentFolders, ContentSelection, SortContentFacts,
/// GroupContentByType), the type classification and focus filter, the content catalogue, the
/// favorites and shelves, and the same actions the previous Asset Table ran.
///
///   [ + Add  Save All  Import    ] | [ View  Split  Filter  search          ]
///   [ Favorites                  ] | [ path                                 ]
///   [ Content (folder tree)      ] | [ Tiles / Columns / Details / Recent /  ]
///   [ Shelves                    ] | [ Board                                ]
///
/// Two separate settings: the view mode (the engine's five modes, tile size and sort) and the
/// split mode (how the Asset Table's own sidebar and items share its space).
class EditorAssetTable final {
public:
    explicit EditorAssetTable(Editor& editor);
    ~EditorAssetTable();

    [[nodiscard]] prism::Ref<prism::Widget> Build();
    /// Re-reads the project snapshot and editor state; rebuilds only what changed.
    void Refresh();

    /// Goes to the folder of `absolutePath` (a file under the content root) and picks it there.
    void RevealFile(const std::filesystem::path& absolutePath);

    /// Drag payload tag for a content item (the payload is its content-relative path).
    static constexpr const char* kDragTag = "uve.content";

    /// How the sidebar and the items share the Asset Table.
    enum class SplitMode : std::uint8_t { SideBySide = 0, Stacked, ItemsOnly, SidebarOnly };

private:
    class Tile;
    class ItemsArea;
    using ItemType = Editor::AssetTableItemType;
    using Focus = Editor::AssetTableTypeFocus;
    [[nodiscard]] static prism::Icon IconFor(ItemType type);
    [[nodiscard]] static prism::Color TintFor(ItemType type);
    [[nodiscard]] static std::pair<std::string, std::string> Describe(ItemType type);
    [[nodiscard]] static prism::Ref<prism::Widget> SectionHeader(const std::string& title, bool open,
                                                                    std::shared_ptr<prism::Widget> body,
                                                                    std::shared_ptr<prism::Widget> extra);

    std::filesystem::path m_contentRoot;
    std::uint64_t m_iconGenerationShown = 0U;
    std::chrono::steady_clock::time_point m_iconGenerationAt{};
    void RebuildFolders();
    void RebuildFavorites();
    void RebuildItems();
    void BuildTiles(const Asset::ProjectFileSnapshot& snapshot);
    void BuildTable(const Asset::ProjectFileSnapshot& snapshot, bool recent);
    void BuildColumns(const Asset::ProjectFileSnapshot& snapshot);
    void BuildBoard(const Asset::ProjectFileSnapshot& snapshot);
    void UpdatePick(const Asset::ProjectFileSnapshot& snapshot);
    void ApplySplitMode();
    void ShowViewMenu();
    void ShowSplitMenu();
    void ShowFilterMenu();
    void ShowItemMenu(std::size_t entryIndex, prism::Vec2 at);
    void ShowEmptyMenu(prism::Vec2 at);
    void ShowRename(std::size_t entryIndex);
    void BeginDrag(std::size_t entryIndex);
    [[nodiscard]] std::vector<prism::MenuItem> CatalogueMenu();
    void CreateCatalogueItem(const std::string& id);
    /// A new material of `kind` in the current folder, named `stem` (made unique).
    void CreateGeneratedTexture(const std::string& stem, const std::function<bool(Asset::TextureAsset&)>& make);
    void CreateMaterial(const std::string& stem, Asset::MaterialKind kind);
    void CreateBlueprint();
    void CreateParticleSystem();
    void CreateWidgetBlueprint();
    void OpenBlueprint(const std::filesystem::path& full);
    void OpenEntry(std::size_t entryIndex);
    /// Right-click a clip > Create Montage: AM_<clip>.uvmontage beside it, the clip on its slot
    /// track, opened in the Motion Montage Editor.
    void CreateMontageFromClip(const std::filesystem::path& clipPath);
    /// Puts the montage in the library (made when it does not exist yet).
    void AddMontageToLibrary(const std::filesystem::path& montagePath, const std::filesystem::path& libraryPath);
    /// A motion montage library's list: open a montage, or take it off.
    void ShowMontageLibrary(const std::filesystem::path& libraryPath);
    void SelectEntry(std::size_t entryIndex, bool additive, bool range);
    /// The picked items (content-relative): the content selection, else the one picked file.
    [[nodiscard]] std::vector<std::filesystem::path> PickedContent() const;
    /// Asks once ("Delete N items? This cannot be undone."), then deletes `relativePaths`.
    void AskDeleteContent(std::vector<std::filesystem::path> relativePaths);
    /// Deletes content-relative paths for good, refreshes the index once and drops them from the pick.
    void DeleteContent(const std::vector<std::filesystem::path>& relativePaths);
    void GoToFolder(const std::filesystem::path& directory);
    void Reveal(const std::filesystem::path& relativePath);
    void Changed();
    [[nodiscard]] std::string DisplayName(const Asset::ProjectFileEntry& entry) const;
    [[nodiscard]] EditorIcons::Image TypeIcon(ItemType type, int size);

    Editor& m_editor;

    // Sidebar.
    std::shared_ptr<prism::Widget> m_sidebar;
    std::shared_ptr<prism::TreeView> m_favorites;
    std::vector<std::filesystem::path> m_favoriteIds; // tree id - 1 -> favorite
    std::shared_ptr<prism::TextInput> m_folderSearch;
    std::shared_ptr<prism::TreeView> m_folders;
    std::shared_ptr<prism::TreeView> m_shelves;
    std::vector<std::string> m_shelfNames;
    std::vector<std::string> m_folderIds; // tree id - 1 -> folder (generic path)

    // Items.
    std::shared_ptr<prism::Widget> m_items;
    std::shared_ptr<prism::Button> m_viewButton;
    std::shared_ptr<prism::Button> m_splitButton;
    std::shared_ptr<prism::Button> m_filterButton;
    std::shared_ptr<prism::TextInput> m_itemSearch;
    std::shared_ptr<prism::Label> m_path;
    std::shared_ptr<prism::Border> m_itemsHost;     // holds the current mode's view
    std::shared_ptr<prism::TableView> m_table;
    std::shared_ptr<prism::Label> m_status;

    // The Asset Table's own split.
    std::shared_ptr<prism::Border> m_root;
    std::shared_ptr<prism::Splitter> m_split;
    SplitMode m_splitMode = SplitMode::SideBySide;

    /// A preview read back from the engine's thumbnail texture (kept while the texture id is the same).
    struct Preview final {
        std::uintptr_t texture = 0U;
        std::vector<std::uint32_t> pixels;
        int width = 0;
        int height = 0;
        /// The editor's mesh-picture generation it was copied at (a texture id can be reused).
        std::uint64_t generation = 0U;
    };
    [[nodiscard]] const Preview* PreviewFor(const Asset::ProjectFileEntry& entry, ItemType type);

    EditorIcons m_icons;
    std::map<std::string, Preview> m_previews;
    std::uint64_t m_folderSignature = 0;
    std::uint64_t m_tileSignature = 0;
    std::uint64_t m_favoriteSignature = 0;
    std::uint64_t m_pickSignature = 0;
    std::vector<std::pair<std::string, std::weak_ptr<Tile>>> m_tileWidgets; // shown tiles, by path
    std::vector<std::size_t> m_shown;      // snapshot entry indices, in the order shown
    std::vector<ContentItemFacts> m_facts; // per shown item (Details / Recent / Board)
    bool m_syncingFolders = false;
};

} // namespace UVE::Editor
