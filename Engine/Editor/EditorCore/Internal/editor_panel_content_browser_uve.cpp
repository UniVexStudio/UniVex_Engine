// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Content Browser: the project filesystem tree, the asset grid beside it, the draggable
// splitter between them, and the right-click context menu for files and folders.
//
// Split out of editor_uve.cpp as the fourth panel to move. The cleanest cut of the set - by the
// time it came up, every helper it shares with the rest of the editor had already been lifted
// into a shared header by the earlier slices, so this one carried nothing out with it except its
// own constants.
//
// Moved verbatim. Not one line of the two functions differs from what was in editor_uve.cpp.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/asset/asset_import_queue_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/logging/logging_macros_uve.h"

#include "editor_chrome_layout_uve.h"
#include "editor_fonts_uve.h"
#include "editor_node_icons_uve.h"
#include "editor_text_search_uve.h"

namespace UVE::Editor {
namespace {

// File-scope in editor_uve.cpp, and used only by this panel - verified before moving, the same
// check every slice in this sequence runs. A constant with callers on both sides of a split has
// to become shared instead; these had none.
constexpr const char* kPanelLabelContentBrowserUVE = "\xEE\xAA\xAD Content Browser##content-browser-panel";
constexpr const char* kIconStarUVE = "\xEE\xAC\xAE";
constexpr float kFilesystemLongPressThresholdSecondsUVE = 0.60F;

} // namespace

void EditorUVE::DrawContentBrowserPanelUVE() {
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight);
    // Always, not FirstUseEver - see DrawHierarchyPanelUVE()'s comment on the same change.
    ImGui::SetNextWindowPos(layout.contentBrowserPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.contentBrowserSize, ImGuiCond_Always);
    // NoTitleBar dropped (see DrawInspectorPanelUVE()'s comment) and given a real title. The window
    // title bar already names this panel "Content Browser" - like every other panel - so no
    // redundant in-content caps label is drawn; the single toolbar row below (main / Favorites /
    // Search, plus the "..." overflow right-aligned) is the only chrome above the list/grid body,
    // matching the reference's one-header layout.
    // The dock tab strip along the bottom names this panel, so it has no title bar of its own: the
    // toolbar row (Add, Import, path, Favorites, Search, view mode) is its top edge.
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin(kPanelLabelContentBrowserUVE, nullptr, flags);

    Asset::IProjectFileIndexUVE& projectFileIndex = m_services->GetProjectFileIndexUVE();
    const Asset::ProjectChangeSnapshotUVE changeSnapshot = m_services->GetProjectChangeWatcherUVE().GetSnapshotUVE();
    const Asset::ProjectFileSnapshotUVE snapshot = projectFileIndex.GetSnapshotUVE();
    // Rare, conditional status lines (only when a scan failed or a rescan is pending) get their own
    // row above the toolbar so they never collide with it - normally nothing is drawn here.
    if (!m_projectFileLastRefreshSucceeded) {
        if (ImGui::SmallButton("Retry")) {
            m_projectFileSnapshotInitialized = false;
            m_projectFileRefreshAttemptedForRescan = false;
            RefreshProjectFileIndexUVE();
        }
        ImGui::SameLine();
        ImGui::TextColored(ImVec4{0.95F, 0.55F, 0.35F, 1.0F}, "scan failed");
    }
    if (changeSnapshot.rescanRequired) {
        ImGui::TextColored(ImVec4{0.95F, 0.72F, 0.30F, 1.0F}, "rescan required");
    }
    if (!m_contentStatusMessage.empty()) {
        if (ImGui::SmallButton("x##content-status")) {
            m_contentStatusMessage.clear();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_contentStatusMessage.c_str());
    }
    ReconcileContentBrowserDirectoryUVE(snapshot);

    if (m_selectedProjectFile.has_value()) {
        const auto selectedIt = std::find_if(
            snapshot.entries.begin(), snapshot.entries.end(), [this](const Asset::ProjectFileEntryUVE& entry) {
                return entry.relativePath == m_selectedProjectFile->relativePath && entry.kind == m_selectedProjectFile->kind;
            });
        if (selectedIt == snapshot.entries.end()) {
            m_selectedProjectFile.reset();
            m_selectedAsset.reset();
        } else {
            m_selectedProjectFile = *selectedIt;
            if (selectedIt->registeredAssetGuid.has_value()) {
                m_selectedAsset = Asset::AssetRecordUVE{*selectedIt->registeredAssetGuid,
                                                         snapshot.contentRoot / selectedIt->relativePath};
            } else {
                m_selectedAsset.reset();
            }
        }
    }

    // ---- "+ Add" / "Import" toolbar ----
    // "+ Add" opens the Content catalogue: ready-made entities (Character, Prop, Trigger...), lights,
    // shapes and the rest, each written as an asset into the folder on screen. Right-clicking
    // empty Content space opens the same menu.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.357F, 0.478F, 0.600F, 1.0F});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4{0.443F, 0.573F, 0.706F, 1.0F});
    if (ImGui::SmallButton("+ Add")) {
        m_contentCreateMenuRequested = true;
    }
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Create an entity, light, shape, folder... here (or right-click empty space)");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Import")) {
        m_projectFileSnapshotInitialized = false;
        m_projectFileRefreshAttemptedForRescan = false;
        RefreshProjectFileIndexUVE();
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Rescan the content folder to pick up newly added files");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // ---- breadcrumb: main > folder > sub (each segment clickable to navigate up) ----
    const bool showingMainRoot = !m_contentBrowserShowingFavorites && m_contentBrowserDirectory.empty();
    if (showingMainRoot) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.20F, 0.21F, 0.23F, 1.0F});
    }
    if (ImGui::SmallButton("main##content-root")) {
        m_contentBrowserShowingFavorites = false;
        m_contentBrowserDirectory.clear();
        m_selectedProjectFile.reset();
        m_selectedAsset.reset();
    }
    if (showingMainRoot) {
        ImGui::PopStyleColor();
    }
    if (!m_contentBrowserShowingFavorites && !m_contentBrowserDirectory.empty()) {
        std::filesystem::path accumulated;
        for (const std::filesystem::path& segment : m_contentBrowserDirectory) {
            accumulated /= segment;
            ImGui::SameLine(0.0F, 4.0F);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0.0F, 4.0F);
            const std::string crumbLabel = segment.generic_string() + "##crumb-" + accumulated.generic_string();
            if (ImGui::SmallButton(crumbLabel.c_str())) {
                m_contentBrowserDirectory = accumulated;
                m_selectedProjectFile.reset();
                m_selectedAsset.reset();
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    if (m_contentBrowserShowingFavorites) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.20F, 0.21F, 0.23F, 1.0F});
    }
    const std::string favoritesButtonLabel = std::string(kIconStarUVE) + " Favorites##favorites-root";
    if (ImGui::SmallButton(favoritesButtonLabel.c_str())) {
        m_contentBrowserShowingFavorites = true;
        m_selectedProjectFile.reset();
        m_selectedAsset.reset();
    }
    if (m_contentBrowserShowingFavorites) {
        ImGui::PopStyleColor();
    }

    std::array<char, 256> filterBuffer{};
    const std::size_t copiedCharacters = std::min(m_assetFilter.size(), filterBuffer.size() - 1U);
    m_assetFilter.copy(filterBuffer.data(), copiedCharacters);
    // One toolbar row: Search takes the room left after the path and before "...", so the header
    // costs one line of the dock rather than two. On a narrow dock it keeps a usable minimum.
    ImGui::SameLine();
    const float menuButtonWidth = ImGui::CalcTextSize("...").x + (ImGui::GetStyle().FramePadding.x * 2.0F);
    const float searchWidth =
        std::max(90.0F, ImGui::GetContentRegionAvail().x - menuButtonWidth - ImGui::GetStyle().ItemSpacing.x);
    ImGui::SetNextItemWidth(searchWidth);
    if (ImGui::InputTextWithHint("##content-filter", "Search", filterBuffer.data(), filterBuffer.size())) {
        m_assetFilter = filterBuffer.data();
    }

    // "..." view-mode menu, right-aligned at the end of the same row.
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - menuButtonWidth));
    if (ImGui::SmallButton("...##filesystem-menu")) {
        ImGui::OpenPopup("filesystem-overflow-menu");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("View mode");
    }
    if (ImGui::BeginPopup("filesystem-overflow-menu")) {
        ImGui::TextDisabled("View");
        ImGui::Separator();
        struct ModeUVE final {
            const char* label;
            ContentBrowserViewModeUVE mode;
        };
        constexpr std::array<ModeUVE, 3> kModes{{{"Large Tiles", ContentBrowserViewModeUVE::LargeTiles},
                                                 {"Small Tiles", ContentBrowserViewModeUVE::SmallTiles},
                                                 {"List", ContentBrowserViewModeUVE::List}}};
        for (const ModeUVE& mode : kModes) {
            if (ImGui::MenuItem(mode.label, nullptr, m_contentBrowserViewMode == mode.mode)) {
                m_contentBrowserViewMode = mode.mode;
            }
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Show Folders", nullptr, m_contentBrowserSplitModeUVE)) {
            m_contentBrowserSplitModeUVE = !m_contentBrowserSplitModeUVE;
        }
        ImGui::Separator();
        // Named "Close" to match the real menu item Godot's own FileSystem "..." overflow shows
        // (per the user's reference screenshots) - functionally this already was "hide the dock",
        // just under a name that didn't say so. A literal "Make Floating"/Dock-Position-grid pair
        // like Godot's is not added here: this editor's panels are independently-positioned
        // floating ImGui windows arranged to look tiled, not a real DockSpace/DockBuilder tree, so
        // there is no docking-slot concept for "Dock Position" to move a panel between, and every
        // panel is already un-parented (no ImGuiWindowFlags_NoMove) - a "Make Floating" item would
        // be a no-op button. Building real dock-slot infrastructure is a separate, larger effort.
        if (ImGui::MenuItem("Hide Dock")) {
            m_bottomDockVisible = false;
        }
        ImGui::EndPopup();
    }
    // Directory the right-hand grid shows: the selected entry if it's itself a directory,
    // otherwise the current browse directory - same resolution rule the pre-merge Contents panel
    // used, preserved as-is.
    std::filesystem::path gridDirectory = m_contentBrowserDirectory;
    if (m_selectedProjectFile.has_value() &&
        m_selectedProjectFile->kind == Asset::ProjectFileEntryKindUVE::Directory) {
        gridDirectory = m_selectedProjectFile->relativePath;
    }

    const auto selectEntry = [this, &snapshot](const Asset::ProjectFileEntryUVE& entry) {
        m_selectedProjectFile = entry;
        if (entry.registeredAssetGuid.has_value()) {
            m_selectedAsset = Asset::AssetRecordUVE{*entry.registeredAssetGuid, snapshot.contentRoot / entry.relativePath};
        } else {
            m_selectedAsset.reset();
        }
    };
    const auto openContext = [this](const Asset::ProjectFileEntryUVE& entry) {
        m_filesystemContextEntry = entry;
        m_filesystemContextVisible = true;
    };
    const auto trackLongPress = [this, &openContext](const Asset::ProjectFileEntryUVE& entry,
                                                       const bool hovered) {
        if (!hovered || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                m_filesystemLongPressPath.clear();
                m_filesystemLongPressSeconds = 0.0F;
            }
            return;
        }
        if (m_filesystemLongPressPath != entry.relativePath) {
            m_filesystemLongPressPath = entry.relativePath;
            m_filesystemLongPressSeconds = 0.0F;
        }
        m_filesystemLongPressSeconds += std::max(0.0F, ImGui::GetIO().DeltaTime);
        if (m_filesystemLongPressSeconds >= kFilesystemLongPressThresholdSecondsUVE) {
            openContext(entry);
            m_filesystemLongPressSeconds = 0.0F;
            m_filesystemLongPressPath.clear();
        }
    };

    // ---- left folder list | divider (resize + flip toggle) | right thumbnail grid ----
    // The divider doubles as the "filesystem flip mode" control: dragging it resizes the two panes;
    // a plain click (no drag) flips between split mode (list + grid) and single mode (grid only at
    // full width) - Godot's FileSystem dock split toggle.
    const float bodyHeight = std::max(36.0F, ImGui::GetContentRegionAvail().y);
    const float bodyWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    constexpr float kSplitterWidthUVE = 4.0F;
    constexpr float kMinimumListWidthUVE = 180.0F;
    constexpr float kMinimumGridWidthUVE = 280.0F;
    const float listWidth =
        std::clamp(bodyWidth * m_contentBrowserSplitRatio, kMinimumListWidthUVE,
                   std::max(kMinimumListWidthUVE, bodyWidth - kMinimumGridWidthUVE - kSplitterWidthUVE));

    if (m_contentBrowserSplitModeUVE) {
    ImGui::BeginChild("##content-browser-list", ImVec2{listWidth, bodyHeight}, true,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    {
        if (!m_projectFileLastRefreshSucceeded && snapshot.refreshGeneration == 0U) {
            ImGui::TextUnformatted("Project content root could not be scanned. Correct the root; the next automatic scan will retry.");
        } else if (!snapshot.contentRootExists) {
            ImGui::TextUnformatted("Project content root does not exist yet. Add content; the next automatic scan will index it.");
        } else if (snapshot.entries.empty()) {
            ImGui::TextUnformatted("Project content root is empty.");
        } else if (m_contentBrowserShowingFavorites) {
            // Favorites view stays a flat list of the favorited entries.
            std::vector<const Asset::ProjectFileEntryUVE*> favoriteEntries;
            for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
                if (IsProjectPathFavoritedUVE(entry.relativePath) &&
                    ContainsCaseInsensitiveUVE(entry.relativePath.generic_string(), m_assetFilter)) {
                    favoriteEntries.push_back(&entry);
                }
            }
            if (favoriteEntries.empty()) {
                ImGui::TextUnformatted("No favorites yet. Right-click a file or folder and choose \"Add to Favorites\".");
            }
            for (const Asset::ProjectFileEntryUVE* const entry : favoriteEntries) {
                const bool selected = m_selectedProjectFile.has_value() &&
                                      m_selectedProjectFile->relativePath == entry->relativePath &&
                                      m_selectedProjectFile->kind == entry->kind;
                const std::string favLabel =
                    entry->relativePath.generic_string() + "##fav-" + entry->relativePath.generic_string();
                if (ImGui::Selectable(favLabel.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    selectEntry(*entry);
                    if (entry->kind == Asset::ProjectFileEntryKindUVE::Directory &&
                        ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        m_contentBrowserDirectory = entry->relativePath;
                        m_contentBrowserShowingFavorites = false;
                    }
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    selectEntry(*entry);
                    openContext(*entry);
                }
            }
        } else {
            // Nested, indented folder tree (directories only) - Unreal's Sources panel / Godot's
            // FileSystem tree. Selecting a folder drives the right-hand grid; files live in the grid.
            std::map<std::string, std::vector<const Asset::ProjectFileEntryUVE*>> directoryChildren;
            for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
                if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
                    directoryChildren[entry.relativePath.parent_path().generic_string()].push_back(&entry);
                }
            }
            // Each row leads with a folder icon, painted into spaces the label reserves for it, so
            // the tree keeps imgui's own arrows, indent and selection instead of a custom row.
            const float line = ImGui::GetTextLineHeight();
            const float folderIconSize = std::min(16.0F, std::floor(line));
            const float spaceWidth = std::max(1.0F, ImGui::CalcTextSize(" ").x);
            const std::string iconGap(
                static_cast<std::size_t>(std::ceil((folderIconSize + 4.0F) / spaceWidth)), ' ');
            const auto drawFolderIcon = [&](const float x, const bool open) {
                const std::uintptr_t icon = m_uiAssets.GetContentTypeIconTextureIdUVE(open ? "folder_open" : "Folder");
                if (icon == 0U) {
                    return;
                }
                const float rowTop = ImGui::GetItemRectMin().y;
                const float rowHeight = ImGui::GetItemRectSize().y;
                const ImVec2 iconMin{std::floor(x), std::floor(rowTop + ((rowHeight - folderIconSize) * 0.5F))};
                ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(icon), iconMin,
                                                     ImVec2{iconMin.x + folderIconSize, iconMin.y + folderIconSize});
            };
            // "main" root row: always click back to the content root.
            const bool rootSelected = m_contentBrowserDirectory.empty();
            if (ImGui::Selectable((iconGap + "main##content-tree-root").c_str(), rootSelected)) {
                m_contentBrowserDirectory.clear();
                m_selectedProjectFile.reset();
                m_selectedAsset.reset();
            }
            drawFolderIcon(ImGui::GetItemRectMin().x, true);
            std::function<void(const std::string&)> renderDirectory = [&](const std::string& parentKey) {
                const auto childrenIt = directoryChildren.find(parentKey);
                if (childrenIt == directoryChildren.end()) {
                    return;
                }
                for (const Asset::ProjectFileEntryUVE* const dirEntry : childrenIt->second) {
                    const std::string childKey = dirEntry->relativePath.generic_string();
                    const bool hasSubdirectories = directoryChildren.count(childKey) > 0U;
                    ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                   ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth;
                    if (!hasSubdirectories) {
                        treeFlags |= ImGuiTreeNodeFlags_Leaf;
                    }
                    if (m_contentBrowserDirectory == dirEntry->relativePath) {
                        treeFlags |= ImGuiTreeNodeFlags_Selected;
                    }
                    ImGui::PushID(childKey.c_str());
                    const std::string nodeLabel = iconGap + dirEntry->relativePath.filename().generic_string();
                    const float rowX = ImGui::GetCursorScreenPos().x;
                    const bool open = ImGui::TreeNodeEx(nodeLabel.c_str(), treeFlags);
                    drawFolderIcon(rowX + ImGui::GetTreeNodeToLabelSpacing(), open && hasSubdirectories);
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        m_contentBrowserDirectory = dirEntry->relativePath;
                        selectEntry(*dirEntry);
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        selectEntry(*dirEntry);
                        openContext(*dirEntry);
                    }
                    if (open) {
                        if (hasSubdirectories) {
                            renderDirectory(childKey);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            };
            renderDirectory("");
        }
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            m_contentCreateMenuRequested = true;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine(0.0F, 0.0F);
    } // end split-mode left list

    // Divider handle: drag to resize (split mode only), click (no drag) to flip split<->single mode.
    // The hit target is wider than the thin visual bar so the flip-click is easy to land (a 4px
    // strip is too small to reliably click); the grip is drawn centered inside it.
    constexpr float kSplitterHitWidthUVE = 10.0F;
    ImGui::InvisibleButton("##content-browser-splitter", ImVec2{kSplitterHitWidthUVE, bodyHeight});
    // A real drag moves more than a click's sub-pixel jitter; only then treat it as a resize (and
    // suppress the flip-on-release). Anything smaller is a click that flips the split mode.
    if (ImGui::IsItemActive() && m_contentBrowserSplitModeUVE &&
        std::abs(ImGui::GetIO().MouseDelta.x) > 1.0F) {
        m_contentBrowserSplitRatio = std::clamp((listWidth + ImGui::GetIO().MouseDelta.x) / bodyWidth, 0.15F, 0.7F);
        m_contentBrowserSplitterDraggingUVE = true;
    }
    if (ImGui::IsItemDeactivated()) {
        if (!m_contentBrowserSplitterDraggingUVE) {
            m_contentBrowserSplitModeUVE = !m_contentBrowserSplitModeUVE;
        }
        m_contentBrowserSplitterDraggingUVE = false;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(m_contentBrowserSplitModeUVE ? ImGuiMouseCursor_ResizeEW
                                                           : ImGuiMouseCursor_Hand);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip(m_contentBrowserSplitModeUVE
                                  ? "Drag to resize - click to hide the folder list"
                                  : "Click to show the folder list");
        }
    }
    {
        const ImVec2 hitMin = ImGui::GetItemRectMin();
        const ImVec2 hitMax = ImGui::GetItemRectMax();
        const float dotX = (hitMin.x + hitMax.x) * 0.5F;
        const float centerY = (hitMin.y + hitMax.y) * 0.5F;
        // Thin visual bar (kSplitterWidthUVE) centered inside the wider hit target.
        const ImVec2 splitterMin{dotX - kSplitterWidthUVE * 0.5F, hitMin.y};
        const ImVec2 splitterMax{dotX + kSplitterWidthUVE * 0.5F, hitMax.y};
        ImDrawList* const splitterDrawList = ImGui::GetWindowDrawList();
        splitterDrawList->AddRectFilled(splitterMin, splitterMax, IM_COL32(28, 32, 39, 255));
        for (int dotIndex = -1; dotIndex <= 1; ++dotIndex) {
            splitterDrawList->AddCircleFilled(ImVec2{dotX, centerY + static_cast<float>(dotIndex) * 4.0F}, 1.1F,
                                              IM_COL32(107, 113, 131, 255));
        }
    }
    ImGui::SameLine(0.0F, 0.0F);

    // The chosen view mode sets the card shape: big tiles for looking, small tiles for many files,
    // a list for reading names and types.
    const bool listMode = m_contentBrowserViewMode == ContentBrowserViewModeUVE::List;
    const bool largeTiles = m_contentBrowserViewMode == ContentBrowserViewModeUVE::LargeTiles;
    const float kCardIconSizeUVE = listMode ? 18.0F : (largeTiles ? 72.0F : 44.0F);
    const float kCardHeightUVE = listMode ? 24.0F : kCardIconSizeUVE + 38.0F;
    constexpr float kCardPaddingUVE = 4.0F;
    const auto truncateLabelUVE = [](const std::string& label, const float maxWidth) {
        if (ImGui::CalcTextSize(label.c_str()).x <= maxWidth) {
            return label;
        }
        std::string truncated = label;
        while (!truncated.empty() &&
               ImGui::CalcTextSize((truncated + "...").c_str()).x > maxWidth) {
            truncated.pop_back();
        }
        return truncated.empty() ? truncated : truncated + "...";
    };

    if (ImGui::BeginChild("##content-browser-grid", ImVec2{0.0F, bodyHeight}, true,
                           ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        const float tileWidth = kCardIconSizeUVE + 32.0F;
        const float availableWidth = std::max(tileWidth, ImGui::GetContentRegionAvail().x);
        const float kCardWidthUVE = listMode ? availableWidth : tileWidth;
        const int columns = listMode ? 1 : std::max(1, static_cast<int>(availableWidth / kCardWidthUVE));
        const ImVec2 gridOrigin = ImGui::GetCursorPos();
        ImDrawList* const gridDrawList = ImGui::GetWindowDrawList();
        std::size_t visibleCount = 0U;
        for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
            if (entry.relativePath.parent_path() != gridDirectory) {
                continue;
            }
            const std::string entryPath = entry.relativePath.generic_string();
            if (!ContainsCaseInsensitiveUVE(entryPath, m_assetFilter)) {
                continue;
            }
            const int column = static_cast<int>(visibleCount) % columns;
            const int row = static_cast<int>(visibleCount) / columns;
            ++visibleCount;
            ContentBrowserItemTypeUVE type = ClassifyContentBrowserEntryUVE(entry);
            // A model source is relabelled by what its file turned out to hold.
            const EditorModelSourceInfoUVE* const modelSource =
                type == ContentBrowserItemTypeUVE::Mesh ? FindModelSourceInfoUVE(entry.relativePath) : nullptr;
            if (modelSource != nullptr && modelSource->animationOnly) {
                type = ContentBrowserItemTypeUVE::Animation;
            } else if (modelSource != nullptr && modelSource->rigged) {
                type = ContentBrowserItemTypeUVE::Model;
            }
            const std::string displayLabel = entry.relativePath.filename().generic_string();
            const std::string rowId = "folder-content-entry-" + entry.relativePath.generic_string();
            ImGui::PushID(rowId.c_str());
            ImGui::SetCursorPos(ImVec2{gridOrigin.x + static_cast<float>(column) * kCardWidthUVE,
                                       gridOrigin.y + static_cast<float>(row) * kCardHeightUVE});
            const ImVec2 cardMin = ImGui::GetCursorScreenPos();
            const bool selected = m_selectedProjectFile.has_value() &&
                                  m_selectedProjectFile->relativePath == entry.relativePath;
            const bool clicked = ImGui::Selectable("##card", selected, ImGuiSelectableFlags_AllowDoubleClick,
                                                   ImVec2{kCardWidthUVE - kCardPaddingUVE, kCardHeightUVE - kCardPaddingUVE});
            const bool rowHovered = ImGui::IsItemHovered();
            // An entity asset drags into the Scene panel or the viewport to be placed there.
            if ((type == ContentBrowserItemTypeUVE::Entity || type == ContentBrowserItemTypeUVE::Prefab) &&
                ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                const std::string absolutePath = (snapshot.contentRoot / entry.relativePath).string();
                ImGui::SetDragDropPayload(kContentEntityPayloadUVE, absolutePath.c_str(), absolutePath.size() + 1U);
                ImGui::Text("Place %s", displayLabel.c_str());
                ImGui::EndDragDropSource();
            }
            if (rowHovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                if (modelSource != nullptr && !modelSource->summary.empty()) {
                    ImGui::SetTooltip("%s\nType: %s\n%s", displayLabel.c_str(), GetContentBrowserItemTypeLabelUVE(type),
                                      modelSource->summary.c_str());
                } else {
                    ImGui::SetTooltip("%s\nType: %s", displayLabel.c_str(), GetContentBrowserItemTypeLabelUVE(type));
                }
            }
            const std::uintptr_t contentThumbnail =
                type == ContentBrowserItemTypeUVE::Texture ? GetTextureThumbnailUVE(entry.relativePath)
                : type == ContentBrowserItemTypeUVE::Mesh || type == ContentBrowserItemTypeUVE::Model
                    ? GetMeshThumbnailUVE(entry.relativePath)
                                                            : 0U;
            // A preview of the file itself when there is one, its type's icon otherwise - a rig
            // with no geometry (bones only) has no mesh preview, and shows the Model icon.
            const std::uintptr_t iconTexture =
                contentThumbnail != 0U ? contentThumbnail
                                       : m_uiAssets.GetContentTypeIconTextureIdUVE(GetContentBrowserItemTypeLabelUVE(type));
            if (listMode) {
                // One row: icon, name, and the type right-aligned in the muted colour.
                const float iconY = std::floor(cardMin.y + (kCardHeightUVE - kCardPaddingUVE - kCardIconSizeUVE) * 0.5F);
                if (iconTexture != 0U) {
                    gridDrawList->AddImage(static_cast<ImTextureID>(iconTexture), ImVec2{cardMin.x + 4.0F, iconY},
                                           ImVec2{cardMin.x + 4.0F + kCardIconSizeUVE, iconY + kCardIconSizeUVE});
                }
                const float textY = cardMin.y + (kCardHeightUVE - kCardPaddingUVE - ImGui::GetTextLineHeight()) * 0.5F;
                const char* const typeLabel = GetContentBrowserItemTypeLabelUVE(type);
                const float typeWidth = ImGui::CalcTextSize(typeLabel).x;
                const float nameMax = std::max(20.0F, kCardWidthUVE - kCardIconSizeUVE - typeWidth - 32.0F);
                if (!DrawContentRenameFieldUVE(snapshot.contentRoot, entry, cardMin.x + kCardIconSizeUVE + 10.0F,
                                               cardMin.y, nameMax)) {
                    gridDrawList->AddText(ImVec2{cardMin.x + kCardIconSizeUVE + 12.0F, textY},
                                          ImGui::GetColorU32(ImGuiCol_Text),
                                          truncateLabelUVE(displayLabel, nameMax).c_str());
                }
                gridDrawList->AddText(ImVec2{cardMin.x + kCardWidthUVE - typeWidth - 12.0F, textY},
                                      ImGui::GetColorU32(ImGuiCol_TextDisabled), typeLabel);
            } else {
                if (iconTexture != 0U) {
                    const float iconX = std::floor(cardMin.x + (kCardWidthUVE - kCardIconSizeUVE) * 0.5F);
                    const float iconY = std::floor(cardMin.y + 4.0F);
                    gridDrawList->AddImage(static_cast<ImTextureID>(iconTexture), ImVec2{iconX, iconY},
                                           ImVec2{iconX + kCardIconSizeUVE, iconY + kCardIconSizeUVE});
                }
                if (!DrawContentRenameFieldUVE(snapshot.contentRoot, entry, cardMin.x + 2.0F,
                                               cardMin.y + kCardIconSizeUVE + 6.0F, kCardWidthUVE - kCardPaddingUVE - 4.0F)) {
                    const std::string truncatedLabel = truncateLabelUVE(displayLabel, kCardWidthUVE - kCardPaddingUVE);
                    const float labelWidth = ImGui::CalcTextSize(truncatedLabel.c_str()).x;
                    const float labelX = cardMin.x + std::max(0.0F, (kCardWidthUVE - labelWidth) * 0.5F);
                    gridDrawList->AddText(ImVec2{labelX, cardMin.y + kCardIconSizeUVE + 8.0F},
                                          ImGui::GetColorU32(ImGuiCol_Text), truncatedLabel.c_str());
                }
            }
            const bool contextClicked = rowHovered &&
                                         (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                          ImGui::IsMouseReleased(ImGuiMouseButton_Right));
            if (clicked) {
                m_selectedProjectFile = entry;
                if (entry.registeredAssetGuid.has_value()) {
                    m_selectedAsset = Asset::AssetRecordUVE{*entry.registeredAssetGuid, snapshot.contentRoot / entry.relativePath};
                } else {
                    m_selectedAsset.reset();
                }
                if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory &&
                    ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_contentBrowserDirectory = entry.relativePath;
                    m_contentBrowserShowingFavorites = false;
                }
                if (type == ContentBrowserItemTypeUVE::Entity && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    m_contentStatusMessage =
                        "Opening an entity's tree needs the Entity Editor, which is not in this build yet.";
                }
            }
            ImGui::PopID();
            if (contextClicked) {
                m_selectedProjectFile = entry;
                if (entry.registeredAssetGuid.has_value()) {
                    m_selectedAsset = Asset::AssetRecordUVE{*entry.registeredAssetGuid,
                                                             snapshot.contentRoot / entry.relativePath};
                } else {
                    m_selectedAsset.reset();
                }
                openContext(entry);
            } else {
                trackLongPress(entry, rowHovered);
            }
        }
        // Right-click on empty space: the same menu as "+ Add".
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            m_contentCreateMenuRequested = true;
        }
        if (visibleCount == 0U) {
            ImGui::SetCursorPos(gridOrigin);
            ImGui::TextDisabled(gridDirectory.empty() ? "main is empty." : "This folder is empty.");
        } else {
            const int totalRows = (static_cast<int>(visibleCount) + columns - 1) / columns;
            ImGui::SetCursorPos(
                ImVec2{gridOrigin.x, gridOrigin.y + static_cast<float>(totalRows) * kCardHeightUVE});
            ImGui::Dummy(ImVec2{0.0F, 0.0F});
        }
        ImGui::EndChild();
    }

    constexpr const char* kCreateMenuId = "##content-create-menu";
    if (m_contentCreateMenuRequested) {
        m_contentCreateMenuRequested = false;
        ImGui::OpenPopup(kCreateMenuId);
    }
    if (ImGui::BeginPopup(kCreateMenuId)) {
        DrawContentCreateMenuUVE(snapshot.contentRoot, gridDirectory);
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace UVE::Editor
