// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "material_asset.h"

#include "editor.h"
#include "editor_content_browser_model.h"
#include "editor_icons.h"

#include "prism/Icons.h"
#include "prism/Menu.h"
#include "prism/Widget.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
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

/// The editor's Content Browser drawn with PRISM widgets. Everything it shows and does is the
/// engine's own: the project file index snapshot, the content model (ListContentFolderUVE,
/// ListContentTreeUVE, CollectVisibleContentFoldersUVE, ContentSelectionUVE, SortContentFactsUVE,
/// GroupContentByTypeUVE), the type classification and focus filter, the content catalogue, the
/// favorites and shelves, and the same actions the previous Content Browser ran.
///
///   [ + Add  Save All  Import    ] | [ View  Split  Filter  search          ]
///   [ Favorites                  ] | [ path                                 ]
///   [ Content (folder tree)      ] | [ Tiles / Columns / Details / Recent /  ]
///   [ Shelves                    ] | [ Board                                ]
///
/// Two separate settings: the view mode (the engine's five modes, tile size and sort) and the
/// split mode (how the Content Browser's own sidebar and items share its space).
class EditorContentBrowserUVE final {
public:
    explicit EditorContentBrowserUVE(EditorUVE& editor);
    ~EditorContentBrowserUVE();

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    /// Re-reads the project snapshot and editor state; rebuilds only what changed.
    void RefreshUVE();

    /// Goes to the folder of `absolutePath` (a file under the content root) and picks it there.
    void RevealFileUVE(const std::filesystem::path& absolutePath);

    /// Drag payload tag for a content item (the payload is its content-relative path).
    static constexpr const char* kDragTagUVE = "uve.content";

    /// How the sidebar and the items share the Content Browser.
    enum class SplitModeUVE : std::uint8_t { SideBySide = 0, Stacked, ItemsOnly, SidebarOnly };

private:
    class TileUVE;
    class ItemsAreaUVE;
    using ItemTypeUVE = EditorUVE::ContentBrowserItemTypeUVE;
    using FocusUVE = EditorUVE::ContentBrowserTypeFocusUVE;
    [[nodiscard]] static prism::Icon IconForUVE(ItemTypeUVE type);
    [[nodiscard]] static prism::Color TintForUVE(ItemTypeUVE type);
    [[nodiscard]] static std::pair<std::string, std::string> DescribeUVE(ItemTypeUVE type);
    [[nodiscard]] static prism::Ref<prism::Widget> SectionHeaderUVE(const std::string& title, bool open,
                                                                    std::shared_ptr<prism::Widget> body,
                                                                    std::shared_ptr<prism::Widget> extra);

    std::filesystem::path m_contentRoot;
    std::uint64_t m_iconGenerationShown = 0U;
    std::chrono::steady_clock::time_point m_iconGenerationAt{};
    void RebuildFoldersUVE();
    void RebuildFavoritesUVE();
    void RebuildItemsUVE();
    void BuildTilesUVE(const Asset::ProjectFileSnapshotUVE& snapshot);
    void BuildTableUVE(const Asset::ProjectFileSnapshotUVE& snapshot, bool recent);
    void BuildColumnsUVE(const Asset::ProjectFileSnapshotUVE& snapshot);
    void BuildBoardUVE(const Asset::ProjectFileSnapshotUVE& snapshot);
    void UpdatePickUVE(const Asset::ProjectFileSnapshotUVE& snapshot);
    void ApplySplitModeUVE();
    void ShowViewMenuUVE();
    void ShowSplitMenuUVE();
    void ShowFilterMenuUVE();
    void ShowItemMenuUVE(std::size_t entryIndex, prism::Vec2 at);
    void ShowEmptyMenuUVE(prism::Vec2 at);
    void ShowRenameUVE(std::size_t entryIndex);
    void BeginDragUVE(std::size_t entryIndex);
    [[nodiscard]] std::vector<prism::MenuItem> CatalogueMenuUVE();
    void CreateCatalogueItemUVE(const std::string& id);
    /// A new material of `kind` in the current folder, named `stem` (made unique).
    void CreateMaterialUVE(const std::string& stem, Asset::MaterialKindUVE kind);
    void CreateBlueprintUVE();
    void CreateWidgetBlueprintUVE();
    void OpenBlueprintUVE(const std::filesystem::path& full);
    void OpenEntryUVE(std::size_t entryIndex);
    /// Right-click a clip > Create Montage: AM_<clip>.uvmontage beside it, the clip on its slot
    /// track, opened in the Montage Editor.
    void CreateMontageFromClipUVE(const std::filesystem::path& clipPath);
    /// Puts the montage in the library (made when it does not exist yet).
    void AddMontageToLibraryUVE(const std::filesystem::path& montagePath, const std::filesystem::path& libraryPath);
    /// A montage library's list: open a montage, or take it off.
    void ShowMontageLibraryUVE(const std::filesystem::path& libraryPath);
    void SelectEntryUVE(std::size_t entryIndex, bool additive, bool range);
    void GoToFolderUVE(const std::filesystem::path& directory);
    void RevealUVE(const std::filesystem::path& relativePath);
    void ChangedUVE();
    [[nodiscard]] std::string DisplayNameUVE(const Asset::ProjectFileEntryUVE& entry) const;
    [[nodiscard]] EditorIconsUVE::ImageUVE TypeIconUVE(ItemTypeUVE type, int size);

    EditorUVE& m_editor;

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

    // The Content Browser's own split.
    std::shared_ptr<prism::Border> m_root;
    std::shared_ptr<prism::Splitter> m_split;
    SplitModeUVE m_splitMode = SplitModeUVE::SideBySide;

    /// A preview read back from the engine's thumbnail texture (kept while the texture id is the same).
    struct PreviewUVE final {
        std::uintptr_t texture = 0U;
        std::vector<std::uint32_t> pixels;
        int width = 0;
        int height = 0;
        /// The editor's mesh-picture generation it was copied at (a texture id can be reused).
        std::uint64_t generation = 0U;
    };
    [[nodiscard]] const PreviewUVE* PreviewForUVE(const Asset::ProjectFileEntryUVE& entry, ItemTypeUVE type);

    EditorIconsUVE m_icons;
    std::map<std::string, PreviewUVE> m_previews;
    std::uint64_t m_folderSignature = 0;
    std::uint64_t m_tileSignature = 0;
    std::uint64_t m_favoriteSignature = 0;
    std::uint64_t m_pickSignature = 0;
    std::vector<std::pair<std::string, std::weak_ptr<TileUVE>>> m_tileWidgets; // shown tiles, by path
    std::vector<std::size_t> m_shown;      // snapshot entry indices, in the order shown
    std::vector<ContentItemFactsUVE> m_facts; // per shown item (Details / Recent / Board)
    bool m_syncingFolders = false;
};

} // namespace UVE::Editor
