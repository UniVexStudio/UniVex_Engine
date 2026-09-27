// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Content Browser. One toolbar across the top (create, import, save, back/forward, the path,
// view settings); under it a sidebar with the pinned paths, the project's folder tree and the
// user's shelves, and beside that the filter and search row, the items, and a status line.
//
// What is shown is decided in editor_content_browser_model_uve (listing, search, history,
// shelves) so the rules are tested apart from the drawing, and the editor bridge lists the same
// items the panel draws.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "uve/asset/asset_import_queue_uve.h"
#include "uve/editor/editor_content_browser_model_uve.h"

#include "editor_chrome_layout_uve.h"
#include "editor_node_icons_uve.h"

namespace UVE::Editor {
namespace {

constexpr const char* kPanelLabelContentBrowserUVE = "\xEE\xAA\xAD Content Browser##content-browser-panel";
constexpr const char* kIconStarUVE = "\xEE\xAC\xAE";
constexpr const char* kIconFolderUVE = "\xEE\xAA\xAD";
constexpr const char* kIconAdjustmentsUVE = "\xEE\xA8\x83";
constexpr float kFilesystemLongPressThresholdSecondsUVE = 0.60F;
constexpr ImVec4 kAccentUVE{0.357F, 0.478F, 0.600F, 1.0F};
constexpr ImVec4 kAccentHoveredUVE{0.443F, 0.573F, 0.706F, 1.0F};

/// Small line-drawn symbols for the toolbar. Drawn rather than taken from the icon font so the
/// font subset does not have to grow for a handful of arrows.
enum class GlyphUVE : std::uint8_t {
    Back,
    Forward,
    Search,
    Filter,
    Plus,
    Import,
    Save,
    ChevronRight,
    ChevronDown,
};

void DrawGlyphUVE(ImDrawList& drawList, const GlyphUVE glyph, const ImVec2 c, const float size, const ImU32 color) {
    const float h = size * 0.5F;
    const float t = std::max(1.2F, size / 9.0F);
    const auto line = [&](const float x0, const float y0, const float x1, const float y1) {
        drawList.AddLine(ImVec2{c.x + x0 * h, c.y + y0 * h}, ImVec2{c.x + x1 * h, c.y + y1 * h}, color, t);
    };
    switch (glyph) {
        case GlyphUVE::Back:
            line(0.35F, -0.7F, -0.35F, 0.0F);
            line(-0.35F, 0.0F, 0.35F, 0.7F);
            break;
        case GlyphUVE::Forward:
        case GlyphUVE::ChevronRight:
            line(-0.35F, -0.7F, 0.35F, 0.0F);
            line(0.35F, 0.0F, -0.35F, 0.7F);
            break;
        case GlyphUVE::ChevronDown:
            line(-0.7F, -0.35F, 0.0F, 0.35F);
            line(0.0F, 0.35F, 0.7F, -0.35F);
            break;
        case GlyphUVE::Search:
            drawList.AddCircle(ImVec2{c.x - 0.15F * h, c.y - 0.15F * h}, 0.6F * h, color, 16, t);
            line(0.3F, 0.3F, 0.85F, 0.85F);
            break;
        case GlyphUVE::Filter:
            line(-0.8F, -0.6F, 0.8F, -0.6F);
            line(-0.5F, 0.0F, 0.5F, 0.0F);
            line(-0.2F, 0.6F, 0.2F, 0.6F);
            break;
        case GlyphUVE::Plus:
            line(-0.7F, 0.0F, 0.7F, 0.0F);
            line(0.0F, -0.7F, 0.0F, 0.7F);
            break;
        case GlyphUVE::Import:
            line(0.0F, -0.8F, 0.0F, 0.25F);
            line(-0.4F, -0.15F, 0.0F, 0.25F);
            line(0.0F, 0.25F, 0.4F, -0.15F);
            line(-0.75F, 0.35F, -0.75F, 0.75F);
            line(-0.75F, 0.75F, 0.75F, 0.75F);
            line(0.75F, 0.75F, 0.75F, 0.35F);
            break;
        case GlyphUVE::Save:
            drawList.AddRect(ImVec2{c.x - 0.72F * h, c.y - 0.72F * h}, ImVec2{c.x + 0.72F * h, c.y + 0.72F * h},
                             color, 1.5F, 0, t);
            drawList.AddRectFilled(ImVec2{c.x - 0.38F * h, c.y - 0.72F * h}, ImVec2{c.x + 0.3F * h, c.y - 0.2F * h},
                                   color);
            line(-0.38F, 0.3F, 0.38F, 0.3F);
            break;
    }
}

/// A square button showing only a glyph. Disabled buttons draw muted and never report a click.
bool GlyphButtonUVE(const char* id, const GlyphUVE glyph, const bool enabled, const char* tooltip) {
    const float side = ImGui::GetFrameHeight();
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2{side, side});
    ImGui::EndDisabled();
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (enabled && (hovered || ImGui::IsItemActive())) {
        drawList.AddRectFilled(min, max, ImGui::GetColorU32(ImGui::IsItemActive() ? ImGuiCol_ButtonActive : ImGuiCol_ButtonHovered),
                               ImGui::GetStyle().FrameRounding);
    }
    DrawGlyphUVE(drawList, glyph, ImVec2{(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F}, side * 0.62F,
                 ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled));
    if (hovered && tooltip != nullptr) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed && enabled;
}

/// A framed button with a glyph before its label; `accent` fills it with the editor's accent.
bool GlyphTextButtonUVE(const char* id, const GlyphUVE glyph, const char* label, const bool accent, const char* tooltip) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const float height = ImGui::GetFrameHeight();
    const float glyphSize = height * 0.45F;
    const float width = style.FramePadding.x * 2.0F + glyphSize + 6.0F + ImGui::CalcTextSize(label).x;
    const bool pressed = ImGui::InvisibleButton(id, ImVec2{width, height});
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    const ImU32 fill = accent ? ImGui::GetColorU32(hovered || active ? kAccentHoveredUVE : kAccentUVE)
                              : ImGui::GetColorU32(active    ? ImGuiCol_ButtonActive
                                                   : hovered ? ImGuiCol_ButtonHovered
                                                             : ImGuiCol_Button);
    drawList.AddRectFilled(min, max, fill, style.FrameRounding);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    DrawGlyphUVE(drawList, glyph, ImVec2{min.x + style.FramePadding.x + glyphSize * 0.5F, (min.y + max.y) * 0.5F},
                 glyphSize, text);
    drawList.AddText(ImVec2{min.x + style.FramePadding.x + glyphSize + 6.0F, min.y + style.FramePadding.y}, text, label);
    if (hovered && tooltip != nullptr) {
        ImGui::SetTooltip("%s", tooltip);
    }
    return pressed;
}

/// A thin vertical rule between toolbar groups.
void ToolbarRuleUVE() {
    ImGui::SameLine(0.0F, 8.0F);
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddLine(ImVec2{at.x, at.y + 4.0F}, ImVec2{at.x, at.y + height - 4.0F},
                                        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::Dummy(ImVec2{1.0F, height});
    ImGui::SameLine(0.0F, 8.0F);
}

/// A sidebar section title: a full-width row that folds the section, with the name in small
/// capitals, a count, and room left at the right for the caller's own buttons. Returns whether
/// the section is open; the state lives in ImGui's storage for the session.
bool SidebarSectionUVE(const char* id, const char* icon, const std::string& title, const std::size_t count,
                       const float trailingWidth) {
    ImGuiStorage& storage = *ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID(id);
    bool open = storage.GetBool(key, true);
    const float height = ImGui::GetFrameHeight();
    const float width = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    const ImVec2 min = ImGui::GetCursorScreenPos();
    ImDrawList& drawList = *ImGui::GetWindowDrawList();
    drawList.AddRectFilled(min, ImVec2{min.x + width, min.y + height}, IM_COL32(40, 44, 52, 255));
    if (ImGui::InvisibleButton(id, ImVec2{std::max(1.0F, width - trailingWidth), height})) {
        open = !open;
        storage.SetBool(key, open);
    }
    if (ImGui::IsItemHovered()) {
        drawList.AddRectFilled(min, ImVec2{min.x + width - trailingWidth, min.y + height},
                               ImGui::GetColorU32(ImGuiCol_HeaderHovered, 0.35F));
    }
    const float midY = min.y + height * 0.5F;
    DrawGlyphUVE(drawList, open ? GlyphUVE::ChevronDown : GlyphUVE::ChevronRight, ImVec2{min.x + 10.0F, midY}, 8.0F,
                 ImGui::GetColorU32(ImGuiCol_TextDisabled));
    float x = min.x + 20.0F;
    const float textY = midY - ImGui::GetTextLineHeight() * 0.5F;
    if (icon != nullptr) {
        drawList.AddText(ImVec2{x, textY}, ImGui::GetColorU32(ImGuiCol_TextDisabled), icon);
        x += ImGui::CalcTextSize(icon).x + 6.0F;
    }
    std::string upper = title;
    std::ranges::transform(upper, upper.begin(), [](const unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    drawList.AddText(ImVec2{x, textY}, ImGui::GetColorU32(ImGuiCol_Text), upper.c_str());
    x += ImGui::CalcTextSize(upper.c_str()).x + 8.0F;
    const std::string countText = std::to_string(count);
    drawList.AddText(ImVec2{x, textY}, ImGui::GetColorU32(ImGuiCol_TextDisabled), countText.c_str());
    return open;
}

/// `label` shortened with "..." to fit `maxWidth`.
[[nodiscard]] std::string FitLabelUVE(const std::string& label, const float maxWidth) {
    if (ImGui::CalcTextSize(label.c_str()).x <= maxWidth) {
        return label;
    }
    std::string fitted = label;
    while (!fitted.empty() && ImGui::CalcTextSize((fitted + "...").c_str()).x > maxWidth) {
        fitted.pop_back();
    }
    return fitted.empty() ? fitted : fitted + "...";
}

} // namespace

void EditorUVE::DrawContentBrowserPanelUVE() {
    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    const EditorChromeLayoutUVE layout = ComputeEditorChromeLayoutUVE(*mainViewport, m_bottomDockVisible, m_bottomDockHeight);
    ImGui::SetNextWindowPos(layout.contentBrowserPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(layout.contentBrowserSize, ImGuiCond_Always);
    // The dock tab strip along the bottom names this panel, so it has no title bar of its own: the
    // toolbar row is its top edge.
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar |
                                       ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    ImGui::Begin(kPanelLabelContentBrowserUVE, nullptr, flags);

    Asset::IProjectFileIndexUVE& projectFileIndex = m_services->GetProjectFileIndexUVE();
    const Asset::ProjectChangeSnapshotUVE changeSnapshot = m_services->GetProjectChangeWatcherUVE().GetSnapshotUVE();
    const Asset::ProjectFileSnapshotUVE snapshot = projectFileIndex.GetSnapshotUVE();
    ReconcileContentBrowserDirectoryUVE(snapshot);
    if (!m_contentBrowserShelf.empty() && m_contentShelves.FindUVE(m_contentBrowserShelf) == nullptr) {
        m_contentBrowserShelf.clear();
    }
    // Wherever the location was changed from (tree, path, a menu, the bridge), it is a step back.
    m_contentHistory.GoUVE(ContentLocationUVE{m_contentBrowserDirectory, m_contentBrowserShelf});
    // The team's shelves may change on disk (a pull, a teammate); look about once a second.
    if (ImGui::GetTime() - m_sharedShelvesCheckedAt >= 1.0) {
        m_sharedShelvesCheckedAt = ImGui::GetTime();
        ReloadSharedShelvesIfChangedUVE();
    }

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

    const auto selectEntry = [this, &snapshot](const Asset::ProjectFileEntryUVE& entry) {
        m_selectedProjectFile = entry;
        if (entry.registeredAssetGuid.has_value()) {
            m_selectedAsset = Asset::AssetRecordUVE{*entry.registeredAssetGuid, snapshot.contentRoot / entry.relativePath};
        } else {
            m_selectedAsset.reset();
        }
    };
    const auto clearSelection = [this] {
        m_selectedProjectFile.reset();
        m_selectedAsset.reset();
    };
    const auto goToFolder = [this, &clearSelection](const std::filesystem::path& directory) {
        m_contentBrowserDirectory = directory;
        m_contentBrowserShelf.clear();
        clearSelection();
    };
    const auto goToShelf = [this, &clearSelection](const std::string& shelf) {
        m_contentBrowserShelf = shelf;
        clearSelection();
    };
    const auto isPlace = [this, &snapshot](const ContentLocationUVE& place) {
        return place.shelf.empty() ? IsContentBrowserDirectoryInSnapshotUVE(snapshot, place.directory)
                                   : m_contentShelves.FindUVE(place.shelf) != nullptr;
    };
    // Back and forward skip places that are gone (a deleted folder, a removed shelf).
    const auto stepHistory = [&](const bool back) {
        while (back ? m_contentHistory.BackUVE() : m_contentHistory.ForwardUVE()) {
            if (isPlace(m_contentHistory.CurrentUVE())) {
                m_contentBrowserDirectory = m_contentHistory.CurrentUVE().directory;
                m_contentBrowserShelf = m_contentHistory.CurrentUVE().shelf;
                clearSelection();
                return;
            }
        }
    };
    const auto openContext = [this](const Asset::ProjectFileEntryUVE& entry) {
        m_filesystemContextEntry = entry;
        m_filesystemContextVisible = true;
    };
    // Anything in Content drags onto a shelf. Entity assets keep the payload the Scene panel and
    // the viewport place from; everything else carries its content-relative path.
    const auto dragContentItem = [&snapshot](const Asset::ProjectFileEntryUVE& entry, const bool entityAsset) {
        if (!ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            return;
        }
        const std::string name = entry.relativePath.filename().generic_string();
        if (entityAsset) {
            const std::string absolutePath = (snapshot.contentRoot / entry.relativePath).string();
            ImGui::SetDragDropPayload(kContentEntityPayloadUVE, absolutePath.c_str(), absolutePath.size() + 1U);
            ImGui::Text("Place %s", name.c_str());
        } else {
            const std::string relativePath = entry.relativePath.generic_string();
            ImGui::SetDragDropPayload(kContentItemPayloadUVE, relativePath.c_str(), relativePath.size() + 1U);
            ImGui::Text("%s", name.c_str());
        }
        ImGui::TextDisabled("Drop on a shelf to keep it there");
        ImGui::EndDragDropSource();
    };
    // A drop target over the last item: puts what is dropped on `shelfName`.
    const auto acceptShelfDrop = [this, &snapshot](const std::string& shelfName) {
        if (!ImGui::BeginDragDropTarget()) {
            return;
        }
        std::filesystem::path dropped;
        if (const ImGuiPayload* const item = ImGui::AcceptDragDropPayload(kContentItemPayloadUVE)) {
            dropped = std::filesystem::path{std::string{static_cast<const char*>(item->Data)}};
        } else if (const ImGuiPayload* const entity = ImGui::AcceptDragDropPayload(kContentEntityPayloadUVE)) {
            const std::filesystem::path absolute{std::string{static_cast<const char*>(entity->Data)}};
            dropped = absolute.lexically_relative(snapshot.contentRoot);
            if (dropped.empty() || *dropped.begin() == "..") {
                dropped.clear(); // not from this project's content
            }
        }
        if (!dropped.empty()) {
            if (m_contentShelves.ContainsUVE(shelfName, dropped)) {
                m_contentStatusMessage = dropped.filename().string() + " is already on " + shelfName + ".";
            } else if (m_contentShelves.AddItemUVE(shelfName, dropped)) {
                static_cast<void>(SaveSharedShelvesUVE());
            } else {
                m_contentStatusMessage = "The shelf \"" + shelfName + "\" is full.";
            }
        }
        ImGui::EndDragDropTarget();
    };
    const auto trackLongPress = [this, &openContext](const Asset::ProjectFileEntryUVE& entry, const bool hovered) {
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
    const auto refreshNow = [this] {
        m_projectFileSnapshotInitialized = false;
        m_projectFileRefreshAttemptedForRescan = false;
        RefreshProjectFileIndexUVE();
    };

    // Folders by parent, for the tree and the path's "what is in here" menus.
    std::map<std::string, std::vector<const Asset::ProjectFileEntryUVE*>> directoryChildren;
    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
            directoryChildren[entry.relativePath.parent_path().generic_string()].push_back(&entry);
        }
    }
    const ContentShelfUVE* shownShelf =
        m_contentBrowserShelf.empty() ? nullptr : m_contentShelves.FindUVE(m_contentBrowserShelf);

    // ---- toolbar: create / import / save | back forward | path ............ settings ----
    if (GlyphTextButtonUVE("##content-add", GlyphUVE::Plus, "Add", true,
                           "Create an entity, light, shape, folder... here (or right-click empty space)")) {
        m_contentCreateMenuRequested = true;
    }
    ImGui::SameLine(0.0F, 4.0F);
    if (GlyphTextButtonUVE("##content-import", GlyphUVE::Import, "Import", false,
                           "Rescan the content folder to pick up newly added files")) {
        refreshNow();
    }
    ImGui::SameLine(0.0F, 4.0F);
    if (GlyphTextButtonUVE("##content-save-all", GlyphUVE::Save, "Save All", false,
                           "Save the scene and the open script (Ctrl+Shift+S)")) {
        static_cast<void>(SaveAllUVE());
    }
    ToolbarRuleUVE();
    if (GlyphButtonUVE("##content-back", GlyphUVE::Back, m_contentHistory.CanGoBackUVE(), "Back (Alt+Left)")) {
        stepHistory(true);
    }
    ImGui::SameLine(0.0F, 2.0F);
    if (GlyphButtonUVE("##content-forward", GlyphUVE::Forward, m_contentHistory.CanGoForwardUVE(), "Forward (Alt+Right)")) {
        stepHistory(false);
    }
    ToolbarRuleUVE();

    // The path: each part goes there; the arrow after a part lists the folders inside it.
    const float settingsWidth = ImGui::CalcTextSize(kIconAdjustmentsUVE).x + ImGui::CalcTextSize(" Settings").x +
                                ImGui::GetStyle().FramePadding.x * 2.0F;
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", kIconFolderUVE);
    const auto crumb = [&](const std::string& label, const std::string& id, const bool current) -> bool {
        ImGui::SameLine(0.0F, 2.0F);
        if (current) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }
        const bool pressed = ImGui::Button((label + "##" + id).c_str());
        ImGui::PopStyleColor();
        return pressed;
    };
    const auto crumbChildren = [&](const std::filesystem::path& directory, const int level) {
        ImGui::SameLine(0.0F, 0.0F);
        ImGui::PushID(level);
        const auto childrenIt = directoryChildren.find(directory.generic_string());
        const bool hasChildren = childrenIt != directoryChildren.end();
        if (GlyphButtonUVE("##crumb-more", GlyphUVE::ChevronRight, hasChildren, hasChildren ? "Folders in here" : nullptr)) {
            ImGui::OpenPopup("##crumb-children");
        }
        if (ImGui::BeginPopup("##crumb-children")) {
            if (hasChildren) {
                for (const Asset::ProjectFileEntryUVE* const child : childrenIt->second) {
                    const std::string name = child->relativePath.filename().generic_string();
                    if (ImGui::MenuItem((std::string{kIconFolderUVE} + " " + name).c_str())) {
                        goToFolder(child->relativePath);
                    }
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    };
    if (shownShelf != nullptr) {
        ImGui::SameLine(0.0F, 2.0F);
        ImGui::TextDisabled("Shelves");
        ImGui::SameLine(0.0F, 6.0F);
        ImGui::TextDisabled(">");
        static_cast<void>(crumb(shownShelf->name, "crumb-shelf", true));
    } else {
        if (crumb("Content", "crumb-root", m_contentBrowserDirectory.empty())) {
            goToFolder({});
        }
        crumbChildren({}, 0);
        std::filesystem::path accumulated;
        int level = 1;
        for (const std::filesystem::path& segment : m_contentBrowserDirectory) {
            accumulated /= segment;
            const bool last = accumulated == m_contentBrowserDirectory;
            if (crumb(segment.generic_string(), "crumb-" + accumulated.generic_string(), last)) {
                goToFolder(accumulated);
            }
            crumbChildren(accumulated, level++);
        }
    }
    ImGui::PopStyleColor();

    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - settingsWidth));
    if (ImGui::Button((std::string{kIconAdjustmentsUVE} + " Settings##content-settings").c_str())) {
        ImGui::OpenPopup("##content-settings-menu");
    }
    if (ImGui::BeginPopup("##content-settings-menu")) {
        ImGui::TextDisabled("Items");
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
        if (ImGui::MenuItem("Show Sidebar", nullptr, m_contentBrowserSplitModeUVE)) {
            m_contentBrowserSplitModeUVE = !m_contentBrowserSplitModeUVE;
        }
        if (ImGui::MenuItem("Hide Dock")) {
            m_bottomDockVisible = false;
        }
        ImGui::EndPopup();
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::GetIO().WantTextInput &&
        ImGui::GetIO().KeyAlt) {
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            stepHistory(true);
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
            stepHistory(false);
        }
    }

    // Rare status lines (a failed scan, a pending rescan, the last action) sit under the toolbar.
    if (!m_projectFileLastRefreshSucceeded) {
        if (ImGui::SmallButton("Retry")) {
            refreshNow();
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

    // ---- body: sidebar | divider | items ----
    const float bodyHeight = std::max(36.0F, ImGui::GetContentRegionAvail().y);
    const float bodyWidth = std::max(1.0F, ImGui::GetContentRegionAvail().x);
    constexpr float kSplitterWidthUVE = 4.0F;
    constexpr float kMinimumListWidthUVE = 180.0F;
    constexpr float kMinimumGridWidthUVE = 280.0F;
    const float listWidth =
        std::clamp(bodyWidth * m_contentBrowserSplitRatio, kMinimumListWidthUVE,
                   std::max(kMinimumListWidthUVE, bodyWidth - kMinimumGridWidthUVE - kSplitterWidthUVE));

    // Rows lead with an icon painted into spaces the label reserves, so imgui keeps the arrows,
    // indent and selection of its own tree and selectable rows.
    const float line = ImGui::GetTextLineHeight();
    const float rowIconSize = std::min(16.0F, std::floor(line));
    const float spaceWidth = std::max(1.0F, ImGui::CalcTextSize(" ").x);
    const std::string iconGap(static_cast<std::size_t>(std::ceil((rowIconSize + 4.0F) / spaceWidth)), ' ');
    const auto drawRowIcon = [&](const float x, const std::uintptr_t icon) {
        if (icon == 0U) {
            return;
        }
        const float rowTop = ImGui::GetItemRectMin().y;
        const float rowHeight = ImGui::GetItemRectSize().y;
        const ImVec2 iconMin{std::floor(x), std::floor(rowTop + ((rowHeight - rowIconSize) * 0.5F))};
        ImGui::GetWindowDrawList()->AddImage(static_cast<ImTextureID>(icon), iconMin,
                                             ImVec2{iconMin.x + rowIconSize, iconMin.y + rowIconSize});
    };
    const auto folderIcon = [this](const bool open) {
        return m_uiAssets.GetContentTypeIconTextureIdUVE(open ? "folder_open" : "Folder");
    };

    if (m_contentBrowserSplitModeUVE) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.105F, 0.115F, 0.135F, 1.0F});
        ImGui::BeginChild("##content-sidebar", ImVec2{listWidth, bodyHeight}, false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleColor();
        const float rowHeight = ImGui::GetFrameHeight();
        // Shelves keep their place at the bottom; the rest scrolls above them.
        const std::span<const ContentShelfUVE> shelves = m_contentShelves.GetAllUVE();
        const bool shelvesOpen = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##section-shelves"), true);
        const float shelvesHeight =
            rowHeight + (shelvesOpen ? static_cast<float>(std::clamp<std::size_t>(shelves.size(), 1U, 5U)) *
                                               (line + ImGui::GetStyle().ItemSpacing.y) +
                                           ImGui::GetStyle().ItemSpacing.y * 2.0F
                                     : 0.0F);
        ImGui::BeginChild("##content-sidebar-scroll",
                          ImVec2{0.0F, std::max(rowHeight * 2.0F, ImGui::GetContentRegionAvail().y - shelvesHeight)}, false);
        {
            // Pinned: files and folders the user keeps at hand.
            std::vector<const Asset::ProjectFileEntryUVE*> pinned;
            for (const std::filesystem::path& path : m_favoriteProjectPaths) {
                const auto it = std::ranges::find(snapshot.entries, path, &Asset::ProjectFileEntryUVE::relativePath);
                if (it != snapshot.entries.end()) {
                    pinned.push_back(&*it);
                }
            }
            if (SidebarSectionUVE("##section-pinned", kIconStarUVE, "Pinned", pinned.size(), 0.0F)) {
                if (pinned.empty()) {
                    ImGui::Indent(8.0F);
                    ImGui::TextDisabled("Right-click a file or folder > Pin");
                    ImGui::Unindent(8.0F);
                }
                for (const Asset::ProjectFileEntryUVE* const entry : pinned) {
                    const bool folder = entry->kind == Asset::ProjectFileEntryKindUVE::Directory;
                    const bool selected = folder ? shownShelf == nullptr && m_contentBrowserDirectory == entry->relativePath
                                                 : m_selectedProjectFile.has_value() &&
                                                       m_selectedProjectFile->relativePath == entry->relativePath;
                    ImGui::PushID(entry->relativePath.generic_string().c_str());
                    const float rowX = ImGui::GetCursorScreenPos().x;
                    if (ImGui::Selectable((iconGap + entry->relativePath.filename().generic_string()).c_str(), selected)) {
                        if (folder) {
                            goToFolder(entry->relativePath);
                        } else {
                            goToFolder(entry->relativePath.parent_path());
                            selectEntry(*entry);
                        }
                    }
                    drawRowIcon(rowX, folder ? folderIcon(false)
                                             : m_uiAssets.GetContentTypeIconTextureIdUVE(GetContentBrowserItemTypeLabelUVE(
                                                   ClassifyContentBrowserEntryUVE(*entry))));
                    dragContentItem(*entry, false);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("%s", entry->relativePath.generic_string().c_str());
                    }
                    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                        selectEntry(*entry);
                        openContext(*entry);
                    }
                    ImGui::PopID();
                }
            }

            // The project's folders, from the content root down. Its magnifier finds a folder.
            // Named after the folder the project lives in; the content root may be given relative.
            std::error_code rootError;
            std::string projectName =
                std::filesystem::absolute(snapshot.contentRoot, rootError).parent_path().filename().generic_string();
            if (projectName.empty()) {
                projectName = "Project";
            }
            std::size_t folderCount = 0U;
            for (const auto& [parent, children] : directoryChildren) {
                folderCount += children.size();
            }
            const float searchButtonWidth = ImGui::GetFrameHeight();
            const bool projectOpen = SidebarSectionUVE("##section-project", nullptr, projectName, folderCount, searchButtonWidth);
            ImGui::SameLine(0.0F, 0.0F);
            if (GlyphButtonUVE("##tree-search", GlyphUVE::Search, true, "Find a folder")) {
                m_contentTreeSearchOpen = !m_contentTreeSearchOpen;
                if (!m_contentTreeSearchOpen) {
                    m_contentTreeFilter.clear();
                } else {
                    ImGui::SetKeyboardFocusHere(1);
                }
            }
            if (projectOpen) {
                if (m_contentTreeSearchOpen) {
                    std::array<char, 128> treeFilter{};
                    m_contentTreeFilter.copy(treeFilter.data(), std::min(m_contentTreeFilter.size(), treeFilter.size() - 1U));
                    ImGui::SetNextItemWidth(-1.0F);
                    if (ImGui::InputTextWithHint("##tree-filter", "Folder name", treeFilter.data(), treeFilter.size())) {
                        m_contentTreeFilter = treeFilter.data();
                    }
                }
                if (!m_projectFileLastRefreshSucceeded && snapshot.refreshGeneration == 0U) {
                    ImGui::TextWrapped("The content folder could not be scanned. The next automatic scan retries.");
                } else if (!snapshot.contentRootExists) {
                    ImGui::TextWrapped("The content folder does not exist yet. Add content and it is picked up.");
                } else {
                    const bool filtering = !m_contentTreeFilter.empty();
                    const std::vector<std::string> visible =
                        filtering ? CollectVisibleContentFoldersUVE(snapshot.entries, m_contentTreeFilter)
                                  : std::vector<std::string>{};
                    const auto isVisible = [&](const std::string& key) {
                        return !filtering || std::ranges::binary_search(visible, key);
                    };
                    ImGuiTreeNodeFlags rootFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                   ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
                    if (shownShelf == nullptr && m_contentBrowserDirectory.empty()) {
                        rootFlags |= ImGuiTreeNodeFlags_Selected;
                    }
                    const float rootX = ImGui::GetCursorScreenPos().x;
                    const bool rootOpen = ImGui::TreeNodeEx((iconGap + "Content##content-tree-root").c_str(), rootFlags);
                    drawRowIcon(rootX + ImGui::GetTreeNodeToLabelSpacing(), folderIcon(rootOpen));
                    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                        goToFolder({});
                    }
                    std::function<void(const std::string&)> renderDirectory = [&](const std::string& parentKey) {
                        const auto childrenIt = directoryChildren.find(parentKey);
                        if (childrenIt == directoryChildren.end()) {
                            return;
                        }
                        for (const Asset::ProjectFileEntryUVE* const dirEntry : childrenIt->second) {
                            const std::string childKey = dirEntry->relativePath.generic_string();
                            if (!isVisible(childKey)) {
                                continue;
                            }
                            const bool hasSubdirectories = directoryChildren.contains(childKey);
                            ImGuiTreeNodeFlags treeFlags = ImGuiTreeNodeFlags_OpenOnArrow |
                                                           ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                                           ImGuiTreeNodeFlags_SpanAvailWidth;
                            if (!hasSubdirectories) {
                                treeFlags |= ImGuiTreeNodeFlags_Leaf;
                            }
                            if (shownShelf == nullptr && m_contentBrowserDirectory == dirEntry->relativePath) {
                                treeFlags |= ImGuiTreeNodeFlags_Selected;
                            }
                            ImGui::PushID(childKey.c_str());
                            if (filtering && hasSubdirectories) {
                                ImGui::SetNextItemOpen(true); // a match deep down stays in sight
                            } else if (IsInsideContentDirectoryUVE(m_contentBrowserDirectory, dirEntry->relativePath)) {
                                ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
                            }
                            const float rowX = ImGui::GetCursorScreenPos().x;
                            const bool open = ImGui::TreeNodeEx(
                                (iconGap + dirEntry->relativePath.filename().generic_string()).c_str(), treeFlags);
                            drawRowIcon(rowX + ImGui::GetTreeNodeToLabelSpacing(), folderIcon(open && hasSubdirectories));
                            dragContentItem(*dirEntry, false);
                            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                                goToFolder(dirEntry->relativePath);
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
                    if (rootOpen) {
                        renderDirectory("");
                        ImGui::TreePop();
                    }
                    if (filtering && visible.empty()) {
                        ImGui::TextDisabled("No folder is named like that.");
                    }
                }
            }
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                m_contentCreateMenuRequested = true;
            }
        }
        ImGui::EndChild();

        // Shelves: hand-picked groups of files from anywhere, shown together in the items area.
        // The team's come first and are saved in the project; the rest are this person's.
        const float addButtonWidth = ImGui::GetFrameHeight();
        const bool shelvesSectionOpen = SidebarSectionUVE("##section-shelves", nullptr, "Shelves", shelves.size(), addButtonWidth);
        ImGui::SameLine(0.0F, 0.0F);
        if (GlyphButtonUVE("##shelf-add", GlyphUVE::Plus, shelves.size() < ContentShelvesUVE::kMaxShelvesUVE,
                           "New shelf")) {
            ImGui::OpenPopup("##shelf-new");
        }
        if (ImGui::BeginPopup("##shelf-new")) {
            const auto create = [this](const bool shared) {
                const std::string name = m_contentShelves.CreateUVE(shared ? "Team Shelf" : "Shelf", shared);
                m_contentShelfRenaming = name;
                m_contentShelfRenameText = name;
                static_cast<void>(SaveSharedShelvesUVE());
            };
            if (ImGui::MenuItem("Personal Shelf")) {
                create(false);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Only you see it; kept with your editor session.");
            }
            if (ImGui::MenuItem("Team Shelf")) {
                create(true);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("Saved in the project (project.uvshelves), so everyone who has the project sees it.");
            }
            ImGui::EndPopup();
        }
        if (shelvesSectionOpen) {
            ImGui::BeginChild("##content-shelves", ImVec2{0.0F, 0.0F}, false);
            if (m_contentShelves.GetAllUVE().empty()) {
                ImGui::Indent(8.0F);
                ImGui::TextDisabled("+ starts one; drag files onto it");
                ImGui::Unindent(8.0F);
            }
            std::string removeShelf;
            std::string toggleShared;
            bool listChanged = false;
            const std::uintptr_t teamIcon = m_uiAssets.GetNodeCategoryIconTextureIdUVE("world");
            const std::uintptr_t personalIcon = m_uiAssets.GetContentTypeIconTextureIdUVE("Bundle");
            for (const bool teamPass : {true, false}) {
                for (const ContentShelfUVE& shelf : m_contentShelves.GetAllUVE()) {
                    if (listChanged) {
                        break;
                    }
                    if (shelf.shared != teamPass) {
                        continue;
                    }
                    ImGui::PushID(shelf.name.c_str());
                    if (m_contentShelfRenaming == shelf.name) {
                        std::array<char, 96> nameBuffer{};
                        m_contentShelfRenameText.copy(nameBuffer.data(),
                                                      std::min(m_contentShelfRenameText.size(), nameBuffer.size() - 1U));
                        ImGui::SetNextItemWidth(-1.0F);
                        if (!ImGui::IsAnyItemActive()) {
                            ImGui::SetKeyboardFocusHere();
                        }
                        const bool entered = ImGui::InputText("##shelf-name", nameBuffer.data(), nameBuffer.size(),
                                                              ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                        m_contentShelfRenameText = nameBuffer.data();
                        if (entered || ImGui::IsItemDeactivated()) {
                            const std::string from = m_contentShelfRenaming;
                            m_contentShelfRenaming.clear();
                            if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && m_contentShelfRenameText != from) {
                                if (m_contentShelves.RenameUVE(from, m_contentShelfRenameText)) {
                                    if (m_contentBrowserShelf == from) {
                                        m_contentBrowserShelf = m_contentShelfRenameText;
                                    }
                                    static_cast<void>(SaveSharedShelvesUVE());
                                } else {
                                    m_contentStatusMessage = "A shelf needs a name no other shelf has.";
                                }
                            }
                            listChanged = true; // the shelf list may have changed under this loop
                        }
                    } else {
                        const float rowX = ImGui::GetCursorScreenPos().x;
                        const bool selected = shownShelf != nullptr && shownShelf->name == shelf.name;
                        if (ImGui::Selectable((iconGap + shelf.name).c_str(), selected)) {
                            goToShelf(shelf.name);
                        }
                        drawRowIcon(rowX, shelf.shared && teamIcon != 0U ? teamIcon : personalIcon);
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort) && ImGui::GetDragDropPayload() == nullptr) {
                            ImGui::SetTooltip(shelf.shared ? "Team shelf, saved in the project (project.uvshelves)"
                                                           : "Your shelf; only you see it");
                        }
                        const std::string shelfName = shelf.name;
                        acceptShelfDrop(shelfName);
                        const std::string count = std::to_string(shelf.items.size());
                        const ImVec2 rowMax = ImGui::GetItemRectMax();
                        ImGui::GetWindowDrawList()->AddText(
                            ImVec2{rowMax.x - ImGui::CalcTextSize(count.c_str()).x - 6.0F, ImGui::GetItemRectMin().y},
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), count.c_str());
                        if (ImGui::BeginPopupContextItem("##shelf-menu")) {
                            if (ImGui::MenuItem("Rename")) {
                                m_contentShelfRenaming = shelfName;
                                m_contentShelfRenameText = shelfName;
                            }
                            if (ImGui::MenuItem(shelf.shared ? "Keep to Myself" : "Share with Team")) {
                                toggleShared = shelfName;
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                                ImGui::SetTooltip(shelf.shared ? "Moves it out of the project into your own session."
                                                               : "Saves it in the project (project.uvshelves) for everyone.");
                            }
                            if (ImGui::MenuItem("Remove Shelf")) {
                                removeShelf = shelfName;
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                                ImGui::SetTooltip("Only the shelf goes; the files on it stay where they are.");
                            }
                            ImGui::EndPopup();
                        }
                    }
                    ImGui::PopID();
                }
            }
            if (!toggleShared.empty()) {
                const ContentShelfUVE* const shelf = m_contentShelves.FindUVE(toggleShared);
                if (shelf != nullptr && m_contentShelves.SetSharedUVE(toggleShared, !shelf->shared)) {
                    static_cast<void>(SaveSharedShelvesUVE());
                }
            }
            if (!removeShelf.empty()) {
                static_cast<void>(m_contentShelves.RemoveUVE(removeShelf));
                static_cast<void>(SaveSharedShelvesUVE());
                if (m_contentBrowserShelf == removeShelf) {
                    m_contentBrowserShelf.clear();
                }
            }
            ImGui::EndChild();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0F, 0.0F);

        // Divider: drag to resize the sidebar. Hiding it is in Settings.
        constexpr float kSplitterHitWidthUVE = 8.0F;
        ImGui::InvisibleButton("##content-browser-splitter", ImVec2{kSplitterHitWidthUVE, bodyHeight});
        if (ImGui::IsItemActive() && std::abs(ImGui::GetIO().MouseDelta.x) > 0.0F) {
            m_contentBrowserSplitRatio = std::clamp((listWidth + ImGui::GetIO().MouseDelta.x) / bodyWidth, 0.12F, 0.6F);
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        const ImVec2 hitMin = ImGui::GetItemRectMin();
        const ImVec2 hitMax = ImGui::GetItemRectMax();
        const float barX = (hitMin.x + hitMax.x) * 0.5F;
        ImGui::GetWindowDrawList()->AddLine(ImVec2{barX, hitMin.y}, ImVec2{barX, hitMax.y},
                                            ImGui::IsItemActive() || ImGui::IsItemHovered()
                                                ? ImGui::GetColorU32(kAccentUVE)
                                                : IM_COL32(28, 32, 39, 255),
                                            ImGui::IsItemActive() ? 2.0F : 1.0F);
        ImGui::SameLine(0.0F, 0.0F);
    }

    // ---- items: filter and search, the grid or list, and a status line ----
    // The sidebar may have added, renamed or removed shelves this frame, which moves them in
    // memory; look the shown one up again.
    shownShelf = m_contentBrowserShelf.empty() ? nullptr : m_contentShelves.FindUVE(m_contentBrowserShelf);
    const std::filesystem::path itemsDirectory = shownShelf != nullptr ? std::filesystem::path{} : m_contentBrowserDirectory;
    ImGui::BeginChild("##content-items", ImVec2{0.0F, bodyHeight}, false);
    {
        const bool focused = m_contentBrowserTypeFocus != ContentBrowserTypeFocusUVE::All;
        const std::string filterLabel = focused ? std::string{"Only "} + GetContentBrowserFocusLabelUVE(m_contentBrowserTypeFocus)
                                                : std::string{"Filter"};
        if (GlyphTextButtonUVE("##content-filter-button", GlyphUVE::Filter, filterLabel.c_str(), focused,
                               "Show only one kind of file")) {
            ImGui::OpenPopup("##content-filter-menu");
        }
        if (ImGui::BeginPopup("##content-filter-menu")) {
            using Focus = ContentBrowserTypeFocusUVE;
            for (const Focus focus : {Focus::All, Focus::Folders, Focus::Scene, Focus::Prefab, Focus::Mesh, Focus::Texture,
                                      Focus::Material, Focus::Shader, Focus::Bundle, Focus::Save, Focus::Registered,
                                      Focus::OtherFiles}) {
                if (ImGui::MenuItem(GetContentBrowserFocusLabelUVE(focus), nullptr, m_contentBrowserTypeFocus == focus)) {
                    m_contentBrowserTypeFocus = focus;
                }
                if (focus == Focus::All) {
                    ImGui::Separator();
                }
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0.0F, 6.0F);
        const std::string placeName = shownShelf != nullptr ? shownShelf->name
                                      : m_contentBrowserDirectory.empty()
                                          ? std::string{"Content"}
                                          : m_contentBrowserDirectory.filename().generic_string();
        std::array<char, 256> filterBuffer{};
        m_assetFilter.copy(filterBuffer.data(), std::min(m_assetFilter.size(), filterBuffer.size() - 1U));
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::GetFrameHeight() * 0.5F);
        if (ImGui::InputTextWithHint("##content-filter", ("Search " + placeName).c_str(), filterBuffer.data(),
                                     filterBuffer.size())) {
            m_assetFilter = filterBuffer.data();
        }
        ImGui::PopStyleVar();

        std::vector<std::size_t> items = shownShelf != nullptr
                                             ? ListContentShelfUVE(snapshot.entries, *shownShelf, m_assetFilter)
                                             : ListContentFolderUVE(snapshot.entries, itemsDirectory, m_assetFilter);
        std::erase_if(items, [&](const std::size_t index) { return !DoesContentBrowserEntryMatchFocusUVE(snapshot.entries[index]); });
        const bool searching = m_assetFilter.find_first_not_of(' ') != std::string::npos;

        const bool listMode = m_contentBrowserViewMode == ContentBrowserViewModeUVE::List;
        const bool largeTiles = m_contentBrowserViewMode == ContentBrowserViewModeUVE::LargeTiles;
        const float iconSize = listMode ? 18.0F : (largeTiles ? 72.0F : 44.0F);
        // A tile: icon, then the name, then the kind of file in the muted colour.
        const float cardHeight = listMode ? 24.0F : iconSize + 14.0F + line * 2.0F;
        constexpr float kCardPaddingUVE = 4.0F;
        const float statusHeight = line + ImGui::GetStyle().ItemSpacing.y * 2.0F;
        std::size_t selectedShown = 0U;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        if (ImGui::BeginChild("##content-browser-grid",
                              ImVec2{0.0F, std::max(line * 2.0F, ImGui::GetContentRegionAvail().y - statusHeight)}, false,
                              ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            const float tileWidth = iconSize + 40.0F;
            const float availableWidth = std::max(tileWidth, ImGui::GetContentRegionAvail().x);
            const float cardWidth = listMode ? availableWidth : tileWidth;
            const int columns = listMode ? 1 : std::max(1, static_cast<int>(availableWidth / cardWidth));
            const ImVec2 gridOrigin = ImGui::GetCursorPos();
            ImDrawList* const gridDrawList = ImGui::GetWindowDrawList();
            for (std::size_t position = 0U; position < items.size(); ++position) {
                const Asset::ProjectFileEntryUVE& entry = snapshot.entries[items[position]];
                const int column = static_cast<int>(position) % columns;
                const int row = static_cast<int>(position) / columns;
                ContentBrowserItemTypeUVE type = ClassifyContentBrowserEntryUVE(entry);
                // A model source is relabelled by what its file turned out to hold.
                const EditorModelSourceInfoUVE* const modelSource =
                    type == ContentBrowserItemTypeUVE::Mesh ? FindModelSourceInfoUVE(entry.relativePath) : nullptr;
                if (modelSource != nullptr && modelSource->animationOnly) {
                    type = ContentBrowserItemTypeUVE::Animation;
                } else if (modelSource != nullptr && modelSource->rigged) {
                    type = ContentBrowserItemTypeUVE::Model;
                }
                const char* const typeLabel = GetContentBrowserItemTypeLabelUVE(type);
                const std::string displayLabel = entry.relativePath.filename().generic_string();
                ImGui::PushID(("content-item-" + entry.relativePath.generic_string()).c_str());
                ImGui::SetCursorPos(ImVec2{gridOrigin.x + static_cast<float>(column) * cardWidth,
                                           gridOrigin.y + static_cast<float>(row) * cardHeight});
                const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                const bool selected = m_selectedProjectFile.has_value() &&
                                      m_selectedProjectFile->relativePath == entry.relativePath;
                selectedShown += selected ? 1U : 0U;
                const bool clicked = ImGui::Selectable("##card", selected, ImGuiSelectableFlags_AllowDoubleClick,
                                                       ImVec2{cardWidth - kCardPaddingUVE, cardHeight - kCardPaddingUVE});
                const bool rowHovered = ImGui::IsItemHovered();
                // An entity asset drags into the Scene panel or the viewport to be placed there; any
                // item drags onto a shelf.
                dragContentItem(entry, type == ContentBrowserItemTypeUVE::Entity || type == ContentBrowserItemTypeUVE::Prefab);
                if (rowHovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    // A search or a shelf mixes folders, so the tooltip says where the file lives.
                    const std::string where = entry.relativePath.parent_path().generic_string();
                    const std::string whereLine = (searching || shownShelf != nullptr)
                                                      ? "\nIn: " + (where.empty() ? std::string{"Content"} : where)
                                                      : std::string{};
                    if (modelSource != nullptr && !modelSource->summary.empty()) {
                        ImGui::SetTooltip("%s\nType: %s%s\n%s", displayLabel.c_str(), typeLabel, whereLine.c_str(),
                                          modelSource->summary.c_str());
                    } else {
                        ImGui::SetTooltip("%s\nType: %s%s", displayLabel.c_str(), typeLabel, whereLine.c_str());
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
                    contentThumbnail != 0U ? contentThumbnail : m_uiAssets.GetContentTypeIconTextureIdUVE(typeLabel);
                if (listMode) {
                    // One row: icon, name, and the type right-aligned in the muted colour.
                    const float iconY = std::floor(cardMin.y + (cardHeight - kCardPaddingUVE - iconSize) * 0.5F);
                    if (iconTexture != 0U) {
                        gridDrawList->AddImage(static_cast<ImTextureID>(iconTexture), ImVec2{cardMin.x + 4.0F, iconY},
                                               ImVec2{cardMin.x + 4.0F + iconSize, iconY + iconSize});
                    }
                    const float textY = cardMin.y + (cardHeight - kCardPaddingUVE - line) * 0.5F;
                    const float typeWidth = ImGui::CalcTextSize(typeLabel).x;
                    const float nameMax = std::max(20.0F, cardWidth - iconSize - typeWidth - 32.0F);
                    if (!DrawContentRenameFieldUVE(snapshot.contentRoot, entry, cardMin.x + iconSize + 10.0F, cardMin.y,
                                                   nameMax)) {
                        gridDrawList->AddText(ImVec2{cardMin.x + iconSize + 12.0F, textY}, ImGui::GetColorU32(ImGuiCol_Text),
                                              FitLabelUVE(displayLabel, nameMax).c_str());
                    }
                    gridDrawList->AddText(ImVec2{cardMin.x + cardWidth - typeWidth - 12.0F, textY},
                                          ImGui::GetColorU32(ImGuiCol_TextDisabled), typeLabel);
                } else {
                    if (iconTexture != 0U) {
                        const float iconX = std::floor(cardMin.x + (cardWidth - iconSize) * 0.5F);
                        const float iconY = std::floor(cardMin.y + 6.0F);
                        gridDrawList->AddImage(static_cast<ImTextureID>(iconTexture), ImVec2{iconX, iconY},
                                               ImVec2{iconX + iconSize, iconY + iconSize});
                    }
                    const float textMax = cardWidth - kCardPaddingUVE - 6.0F;
                    const float nameY = cardMin.y + iconSize + 10.0F;
                    if (!DrawContentRenameFieldUVE(snapshot.contentRoot, entry, cardMin.x + 2.0F, nameY - 2.0F, textMax)) {
                        const std::string name = FitLabelUVE(displayLabel, textMax);
                        gridDrawList->AddText(ImVec2{cardMin.x + 5.0F, nameY}, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
                    }
                    gridDrawList->AddText(ImVec2{cardMin.x + 5.0F, nameY + line}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                          FitLabelUVE(typeLabel, textMax).c_str());
                }
                const bool contextClicked = rowHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                                                           ImGui::IsMouseReleased(ImGuiMouseButton_Right));
                const bool opened = clicked && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                const Asset::ProjectFileEntryUVE clickedEntry = entry;
                ImGui::PopID();
                if (clicked) {
                    selectEntry(clickedEntry);
                    if (opened && clickedEntry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
                        goToFolder(clickedEntry.relativePath);
                    } else if (opened && type == ContentBrowserItemTypeUVE::Entity) {
                        m_contentStatusMessage =
                            "Opening an entity's tree needs the Entity Editor, which is not in this build yet.";
                    }
                }
                if (contextClicked) {
                    selectEntry(clickedEntry);
                    openContext(clickedEntry);
                } else {
                    trackLongPress(clickedEntry, rowHovered);
                }
            }
            // Right-click on empty space: the same menu as "Add".
            if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                m_contentCreateMenuRequested = true;
            }
            if (items.empty()) {
                ImGui::SetCursorPos(gridOrigin);
                if (!snapshot.contentRootExists) {
                    ImGui::TextDisabled("The content folder does not exist yet.");
                } else if (searching || m_contentBrowserTypeFocus != ContentBrowserTypeFocusUVE::All) {
                    ImGui::TextDisabled("Nothing here matches.");
                } else if (shownShelf != nullptr) {
                    ImGui::TextDisabled("This shelf is empty. Right-click a file > Shelves to put it here.");
                } else {
                    ImGui::TextDisabled("This folder is empty. Add or drop files here.");
                }
            } else {
                const int totalRows = (static_cast<int>(items.size()) + columns - 1) / columns;
                ImGui::SetCursorPos(ImVec2{gridOrigin.x, gridOrigin.y + static_cast<float>(totalRows) * cardHeight});
                ImGui::Dummy(ImVec2{0.0F, 0.0F});
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        // With a shelf open, dropping anywhere on its items puts the file on it.
        if (shownShelf != nullptr) {
            acceptShelfDrop(shownShelf->name);
        }

        // Status line: how many items, how many selected, and where a search looks.
        std::string status = std::to_string(items.size()) + (items.size() == 1U ? " item" : " items");
        if (selectedShown > 0U) {
            status += "  |  " + std::to_string(selectedShown) + " selected";
        }
        if (searching && shownShelf == nullptr) {
            status += "  |  searching " + placeName + " and everything in it";
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s", status.c_str());
    }
    ImGui::EndChild();

    constexpr const char* kCreateMenuId = "##content-create-menu";
    const bool createMenuOpened = m_contentCreateMenuRequested;
    if (createMenuOpened) {
        m_contentCreateMenuRequested = false;
        ImGui::OpenPopup(kCreateMenuId);
        m_contentCreateMenuFrames = 0;
    }
    // Only while it is open: a SetNextWindow* call with no popup to take it would land on the
    // next window drawn.
    if (ImGui::IsPopupOpen(kCreateMenuId)) {
        PlaceContentMenuUVE(createMenuOpened, m_contentCreateMenuAnchorX, m_contentCreateMenuAnchorY,
                            m_contentCreateMenuFrames, m_contentCreateMenuX, m_contentCreateMenuY);
    }
    if (ImGui::BeginPopup(kCreateMenuId)) {
        SettleContentMenuUVE(m_contentCreateMenuFrames, m_contentCreateMenuX, m_contentCreateMenuY);
        DrawContentCreateMenuUVE(snapshot.contentRoot, itemsDirectory);
        ImGui::EndPopup();
    }
    ImGui::End();
}

} // namespace UVE::Editor
