// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_prism_icons.h"

#include "prism/Icons.h"
#include "prism/Widget.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace prism {
class Button;
class Label;
class TextInput;
class TreeView;
class WrapBox;
class ScrollView;
} // namespace prism

namespace UVE::Editor {

/// The editor's Content Browser drawn with PRISM widgets. Everything it shows and does is the
/// engine's own: the project file index snapshot, the content model (ListContentFolderUVE,
/// CollectVisibleContentFoldersUVE, ContentSelectionUVE), the type classification and focus filter,
/// the "+ Add" content catalogue, and the same open actions (scene, entity, animation library,
/// script) the previous Content Browser ran on a double click.
///
///   [ > folder search  q ] | [ Filter | item search   q ]
///   [ folder tree        ] | [ tiles                    ]
///   [ +                  ] | [ status                   ]
class EditorPrismContentBrowserUVE final {
public:
    explicit EditorPrismContentBrowserUVE(EditorUVE& editor);
    ~EditorPrismContentBrowserUVE();

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    /// Re-reads the project snapshot and editor state; rebuilds only what changed.
    void RefreshUVE();

private:
    class TileUVE;
    using ItemTypeUVE = EditorUVE::ContentBrowserItemTypeUVE;
    using FocusUVE = EditorUVE::ContentBrowserTypeFocusUVE;
    [[nodiscard]] static prism::Icon IconForUVE(ItemTypeUVE type);
    [[nodiscard]] static prism::Color TintForUVE(ItemTypeUVE type);
    [[nodiscard]] static std::pair<std::string, std::string> DescribeUVE(ItemTypeUVE type);
    [[nodiscard]] static prism::Ref<prism::Widget> SectionHeaderUVE(const std::string& title, bool open,
                                                                    std::shared_ptr<prism::Widget> body,
                                                                    std::shared_ptr<prism::Widget> extra);

    void RebuildFoldersUVE();
    void RebuildTilesUVE();
    void ShowAddMenuUVE(prism::Widget* anchor);
    void ShowFilterMenuUVE();
    void OpenEntryUVE(std::size_t entryIndex);
    void SelectEntryUVE(std::size_t entryIndex, bool additive, bool range);
    void GoToFolderUVE(const std::filesystem::path& directory);

    EditorUVE& m_editor;

    std::shared_ptr<prism::TextInput> m_folderSearch;
    std::shared_ptr<prism::TreeView> m_folders;
    std::shared_ptr<prism::TreeView> m_shelves;
    std::vector<std::string> m_shelfNames;
    std::shared_ptr<prism::Button> m_addButton;
    std::shared_ptr<prism::Button> m_filterButton;
    std::shared_ptr<prism::TextInput> m_itemSearch;
    std::shared_ptr<prism::WrapBox> m_tiles;
    std::shared_ptr<prism::ScrollView> m_tilesScroll;
    std::shared_ptr<prism::Label> m_path;
    std::shared_ptr<prism::Label> m_status;

    /// A preview read back from the engine's thumbnail texture (kept while the texture id is the same).
    struct PreviewUVE final {
        std::uintptr_t texture = 0U;
        std::vector<std::uint32_t> pixels;
        int width = 0;
        int height = 0;
    };
    [[nodiscard]] const PreviewUVE* PreviewForUVE(const Asset::ProjectFileEntryUVE& entry, ItemTypeUVE type);

    EditorPrismIconsUVE m_icons;
    std::map<std::string, PreviewUVE> m_previews;
    std::shared_ptr<prism::Button> m_addAssetButton;
    std::vector<std::string> m_folderIds; // tree id - 1 -> folder (generic path)
    std::uint64_t m_folderSignature = 0;
    std::uint64_t m_tileSignature = 0;
    std::vector<std::size_t> m_shown;      // snapshot entry indices, in tile order
    bool m_syncingFolders = false;
};

} // namespace UVE::Editor
