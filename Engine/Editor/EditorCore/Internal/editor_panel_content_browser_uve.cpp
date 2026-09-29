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
#include <optional>
#include <ctime>
#include <cstdio>
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
    DrawContentBrowserBodyUVE();
    ImGui::End();
}

void EditorUVE::DrawContentBrowserBodyUVE() {

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

    m_contentShownOrderPrevious.swap(m_contentShownOrder);
    m_contentShownOrder.clear();
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
        m_contentSelection.ClearUVE();
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
    // Inside the Entity Editor, Content is where the entity's parts come from: it browses and
    // drags, and does not make new assets (no Add, no right-click create).
    const bool canCreate = !IsEntityEditorOpenUVE();
    if (canCreate) {
        if (GlyphTextButtonUVE("##content-add", GlyphUVE::Plus, "Add", true,
                               "Create an entity, light, shape, folder... here (or right-click empty space)")) {
            m_contentCreateMenuRequested = true;
        }
        ImGui::SameLine(0.0F, 4.0F);
    }
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
        // The mode sets how the whole panel works and where pinned, folders and shelves sit.
        ImGui::TextDisabled("Mode");
        struct ModeChoiceUVE final {
            const char* label;
            ContentBrowserModeUVE mode;
            const char* hint;
        };
        constexpr std::array<ModeChoiceUVE, 5> kModeChoices{{
            {"Tiles", ContentBrowserModeUVE::Tiles, "One folder by picture; sidebar on the left"},
            {"Columns", ContentBrowserModeUVE::Columns, "Walk down folders side by side; pinned and shelves are the first column"},
            {"Details", ContentBrowserModeUVE::Details, "A table to sort by size, date, kind or folder; sidebar on the right"},
            {"Recent", ContentBrowserModeUVE::Recent, "What changed lately below this folder; pinned and shelves along the top"},
            {"Board", ContentBrowserModeUVE::Board, "Everything below this folder by kind; pinned and shelves along the bottom"},
        }};
        for (const ModeChoiceUVE& choice : kModeChoices) {
            if (ImGui::MenuItem(choice.label, nullptr, m_contentBrowserMode == choice.mode)) {
                m_contentBrowserMode = choice.mode;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", choice.hint);
            }
        }
        ImGui::Separator();
        ImGui::TextDisabled("Tile size (Tiles)");
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
        if (ImGui::MenuItem("Show Sidebar (Tiles, Details)", nullptr, m_contentBrowserSplitModeUVE)) {
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

    // The sidebar's three parts, drawn at the cursor. Each mode places them its own way.
    const auto drawPinned = [&]() {
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

    };
    const auto drawTree = [&]() {
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
    };
    const auto drawShelves = [&]() {
        const std::span<const ContentShelfUVE> shelves = m_contentShelves.GetAllUVE();
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
    };
    // The classic sidebar: pinned and the folder tree scrolling, shelves kept at the bottom.
    const auto drawSidebar = [&]() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.105F, 0.115F, 0.135F, 1.0F});
        ImGui::BeginChild("##content-sidebar", ImVec2{listWidth, bodyHeight}, false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleColor();
        const float rowHeight = ImGui::GetFrameHeight();
        const std::size_t shelfCount = m_contentShelves.GetAllUVE().size();
        const bool shelvesOpen = ImGui::GetStateStorage()->GetBool(ImGui::GetID("##section-shelves"), true);
        const float shelvesHeight =
            rowHeight + (shelvesOpen ? static_cast<float>(std::clamp<std::size_t>(shelfCount, 1U, 5U)) *
                                               (line + ImGui::GetStyle().ItemSpacing.y) +
                                           ImGui::GetStyle().ItemSpacing.y * 2.0F
                                     : 0.0F);
        ImGui::BeginChild("##content-sidebar-scroll",
                          ImVec2{0.0F, std::max(rowHeight * 2.0F, ImGui::GetContentRegionAvail().y - shelvesHeight)}, false);
        drawPinned();
        drawTree();
        if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
            m_contentCreateMenuRequested = true;
        }
        ImGui::EndChild();
        drawShelves();
        ImGui::EndChild();
    };
    // Divider: drag to resize the sidebar, which sits left (Tiles) or right (Details).
    const auto drawSplitter = [&](const bool sidebarOnRight) {
        constexpr float kSplitterHitWidthUVE = 8.0F;
        ImGui::InvisibleButton("##content-browser-splitter", ImVec2{kSplitterHitWidthUVE, bodyHeight});
        if (ImGui::IsItemActive() && std::abs(ImGui::GetIO().MouseDelta.x) > 0.0F) {
            const float delta = sidebarOnRight ? -ImGui::GetIO().MouseDelta.x : ImGui::GetIO().MouseDelta.x;
            m_contentBrowserSplitRatio = std::clamp((listWidth + delta) / bodyWidth, 0.12F, 0.6F);
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
    };
    // Recent and Board have no folder tree (they already look through every folder below):
    // pinned paths and shelves ride along as one strip of chips; a chip takes drops too.
    const auto drawPlacesStrip = [&]() {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", kIconStarUVE);
        ImGui::SameLine(0.0F, 4.0F);
        bool any = false;
        for (const std::filesystem::path& path : m_favoriteProjectPaths) {
            const auto it = std::ranges::find(snapshot.entries, path, &Asset::ProjectFileEntryUVE::relativePath);
            if (it == snapshot.entries.end()) {
                continue;
            }
            any = true;
            if (ImGui::SmallButton((path.filename().generic_string() + "##pin-chip-" + path.generic_string()).c_str())) {
                if (it->kind == Asset::ProjectFileEntryKindUVE::Directory) {
                    goToFolder(it->relativePath);
                } else {
                    goToFolder(it->relativePath.parent_path());
                    selectEntry(*it);
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                ImGui::SetTooltip("%s", path.generic_string().c_str());
            }
            ImGui::SameLine(0.0F, 4.0F);
        }
        if (!any) {
            ImGui::TextDisabled("nothing pinned");
            ImGui::SameLine(0.0F, 4.0F);
        }
        ImGui::TextDisabled("|  Shelves");
        ImGui::SameLine(0.0F, 4.0F);
        for (const ContentShelfUVE& shelf : m_contentShelves.GetAllUVE()) {
            const std::string name = shelf.name;
            const bool on = m_contentBrowserShelf == name;
            if (on) {
                ImGui::PushStyleColor(ImGuiCol_Button, kAccentUVE);
            }
            if (ImGui::SmallButton((name + (shelf.shared ? " (team)" : "") + "##shelf-chip").c_str())) {
                goToShelf(name);
            }
            if (on) {
                ImGui::PopStyleColor();
            }
            acceptShelfDrop(name);
            ImGui::SameLine(0.0F, 4.0F);
        }
        if (m_contentShelves.GetAllUVE().empty()) {
            ImGui::TextDisabled("none yet");
            ImGui::SameLine(0.0F, 4.0F);
        }
        ImGui::NewLine();
    };

    const bool sidebarLeft = m_contentBrowserSplitModeUVE && m_contentBrowserMode == ContentBrowserModeUVE::Tiles;
    const bool sidebarRight = m_contentBrowserSplitModeUVE && m_contentBrowserMode == ContentBrowserModeUVE::Details;
    if (sidebarLeft) {
        drawSidebar();
        ImGui::SameLine(0.0F, 0.0F);
        drawSplitter(false);
        ImGui::SameLine(0.0F, 0.0F);
    }

    // ---- items: filter and search, the grid or list, and a status line ----
    // The sidebar may have added, renamed or removed shelves this frame, which moves them in
    // memory; look the shown one up again.
    shownShelf = m_contentBrowserShelf.empty() ? nullptr : m_contentShelves.FindUVE(m_contentBrowserShelf);
    const std::filesystem::path itemsDirectory = shownShelf != nullptr ? std::filesystem::path{} : m_contentBrowserDirectory;
    const float itemsWidth = sidebarRight ? std::max(kMinimumGridWidthUVE, bodyWidth - listWidth - 8.0F) : 0.0F;
    ImGui::BeginChild("##content-items", ImVec2{itemsWidth, bodyHeight}, false);
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

        const std::filesystem::path treeRoot = itemsDirectory;
        const bool searching = m_assetFilter.find_first_not_of(' ') != std::string::npos;
        // Columns walks folders, which a shelf does not have; a shelf shows as Tiles there.
        const ContentBrowserModeUVE mode = shownShelf != nullptr && m_contentBrowserMode == ContentBrowserModeUVE::Columns
                                               ? ContentBrowserModeUVE::Tiles
                                               : m_contentBrowserMode;
        const bool wholeTree = mode == ContentBrowserModeUVE::Recent || mode == ContentBrowserModeUVE::Board;
        std::vector<std::size_t> items = shownShelf != nullptr ? ListContentShelfUVE(snapshot.entries, *shownShelf, m_assetFilter)
                                         : wholeTree ? ListContentTreeUVE(snapshot.entries, treeRoot, m_assetFilter)
                                                     : ListContentFolderUVE(snapshot.entries, itemsDirectory, m_assetFilter);
        std::erase_if(items, [&](const std::size_t index) { return !DoesContentBrowserEntryMatchFocusUVE(snapshot.entries[index]); });
        if (mode == ContentBrowserModeUVE::Recent) {
            std::erase_if(items, [&](const std::size_t index) {
                return snapshot.entries[index].kind == Asset::ProjectFileEntryKindUVE::Directory;
            });
        }

        // What each item looks like: its kind (a model source is relabelled by what its file turned
        // out to hold) and its picture (a preview of the file itself when there is one).
        struct ItemLookUVE final {
            ContentBrowserItemTypeUVE type = ContentBrowserItemTypeUVE::File;
            const char* typeLabel = "";
            const EditorModelSourceInfoUVE* modelSource = nullptr;
            std::uintptr_t icon = 0U;
        };
        const auto lookOf = [this](const Asset::ProjectFileEntryUVE& entry) {
            ItemLookUVE look;
            look.type = ClassifyContentBrowserEntryUVE(entry);
            look.modelSource = look.type == ContentBrowserItemTypeUVE::Mesh ? FindModelSourceInfoUVE(entry.relativePath) : nullptr;
            if (look.modelSource != nullptr && look.modelSource->animationOnly) {
                look.type = ContentBrowserItemTypeUVE::Animation;
            } else if (look.modelSource != nullptr && look.modelSource->rigged) {
                look.type = ContentBrowserItemTypeUVE::Model;
            }
            look.typeLabel = GetContentBrowserItemTypeLabelUVE(look.type);
            const std::uintptr_t preview =
                look.type == ContentBrowserItemTypeUVE::Texture ? GetTextureThumbnailUVE(entry.relativePath)
                : look.type == ContentBrowserItemTypeUVE::Mesh || look.type == ContentBrowserItemTypeUVE::Model
                    ? GetMeshThumbnailUVE(entry.relativePath)
                    : 0U;
            look.icon = preview != 0U ? preview : m_uiAssets.GetContentTypeIconTextureIdUVE(look.typeLabel);
            return look;
        };
        const auto factsOf = [&](const Asset::ProjectFileEntryUVE& entry, const ItemLookUVE& look) {
            const ContentFileFactsUVE disk = GetContentFileFactsUVE(snapshot.contentRoot, entry, snapshot.refreshGeneration);
            const std::string folder = entry.relativePath.parent_path().generic_string();
            return ContentItemFactsUVE{entry.relativePath.filename().generic_string(), look.typeLabel,
                                       folder.empty() ? std::string{"Content"} : folder, disk.size, disk.modified,
                                       entry.kind == Asset::ProjectFileEntryKindUVE::Directory};
        };
        std::size_t selectedShown = 0U;
        // Everything an item does besides being drawn: tooltip, drag, select, open, menus. Called
        // right after the item's own Selectable.
        const auto interact = [&](const Asset::ProjectFileEntryUVE& entry, const ItemLookUVE& look, const bool clicked) {
            const bool hovered = ImGui::IsItemHovered();
            const std::string itemKey = entry.relativePath.generic_string();
            m_contentShownOrder.push_back(itemKey);
            dragContentItem(entry, look.type == ContentBrowserItemTypeUVE::Entity || look.type == ContentBrowserItemTypeUVE::Prefab);
            const std::string displayLabel = entry.relativePath.filename().generic_string();
            if (hovered && !ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                // Anything but a plain folder listing mixes folders, so say where the file lives.
                const bool mixed = searching || shownShelf != nullptr || wholeTree;
                const std::string where = entry.relativePath.parent_path().generic_string();
                const std::string whereLine = mixed ? "\nIn: " + (where.empty() ? std::string{"Content"} : where) : std::string{};
                if (look.modelSource != nullptr && !look.modelSource->summary.empty()) {
                    ImGui::SetTooltip("%s\nType: %s%s\n%s", displayLabel.c_str(), look.typeLabel, whereLine.c_str(),
                                      look.modelSource->summary.c_str());
                } else {
                    ImGui::SetTooltip("%s\nType: %s%s", displayLabel.c_str(), look.typeLabel, whereLine.c_str());
                }
            }
            const bool contextClicked =
                hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseReleased(ImGuiMouseButton_Right));
            if (clicked) {
                const ImGuiIO& io = ImGui::GetIO();
                m_contentSelection.ClickUVE(m_contentShownOrderPrevious, itemKey, io.KeyCtrl || io.KeySuper, io.KeyShift);
                selectEntry(entry);
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory) {
                        goToFolder(entry.relativePath);
                    } else if (look.type == ContentBrowserItemTypeUVE::Entity &&
                               !OpenEntityEditorUVE(snapshot.contentRoot / entry.relativePath)) {
                        m_contentStatusMessage = "Could not open " + entry.relativePath.filename().string() + " in the Entity Editor.";
                    }
                }
            }
            if (contextClicked) {
                // A right-click inside what is picked keeps it all; outside, it picks just that item.
                if (!m_contentSelection.ContainsUVE(itemKey)) {
                    m_contentSelection.ClickUVE(m_contentShownOrderPrevious, itemKey, false, false);
                }
                selectEntry(entry);
                openContext(entry);
            } else {
                trackLongPress(entry, hovered);
            }
        };
        const auto isSelected = [this](const Asset::ProjectFileEntryUVE& entry) {
            if (m_contentSelection.ContainsUVE(entry.relativePath.generic_string())) {
                return true;
            }
            // Picked by something other than a click (a new file, a rename): the one item.
            return m_contentSelection.IsEmptyUVE() && m_selectedProjectFile.has_value() &&
                   m_selectedProjectFile->relativePath == entry.relativePath;
        };
        // A row: icon, name, and a muted note right-aligned (the type, a time...).
        const auto drawRow = [&](const Asset::ProjectFileEntryUVE& entry, const ItemLookUVE& look, const std::string& note,
                                 const float rowHeight) {
            const bool selected = isSelected(entry);
            selectedShown += selected ? 1U : 0U;
            const ImVec2 rowMin = ImGui::GetCursorScreenPos();
            const float width = std::max(40.0F, ImGui::GetContentRegionAvail().x);
            const bool clicked = ImGui::Selectable("##row", selected, ImGuiSelectableFlags_AllowDoubleClick, ImVec2{width, rowHeight});
            interact(entry, look, clicked);
            ImDrawList& drawList = *ImGui::GetWindowDrawList();
            const float iconSize = std::min(18.0F, rowHeight - 2.0F);
            const float iconY = std::floor(rowMin.y + (rowHeight - iconSize) * 0.5F);
            if (look.icon != 0U) {
                drawList.AddImage(static_cast<ImTextureID>(look.icon), ImVec2{rowMin.x + 4.0F, iconY},
                                  ImVec2{rowMin.x + 4.0F + iconSize, iconY + iconSize});
            }
            const float textY = rowMin.y + (rowHeight - line) * 0.5F;
            const float noteWidth = ImGui::CalcTextSize(note.c_str()).x;
            const float nameMax = std::max(20.0F, width - iconSize - noteWidth - 32.0F);
            if (!DrawContentRenameFieldUVE(snapshot.contentRoot, entry, rowMin.x + iconSize + 10.0F, rowMin.y, nameMax)) {
                drawList.AddText(ImVec2{rowMin.x + iconSize + 12.0F, textY}, ImGui::GetColorU32(ImGuiCol_Text),
                                 FitLabelUVE(entry.relativePath.filename().generic_string(), nameMax).c_str());
            }
            drawList.AddText(ImVec2{rowMin.x + width - noteWidth - 8.0F, textY}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                             note.c_str());
        };
        // A tile at the cursor: picture, then the name, then the kind in the muted colour.
        const auto drawTile = [&](const Asset::ProjectFileEntryUVE& entry, const ItemLookUVE& look, const float tileWidth,
                                  const float tileHeight, const float iconSize) {
            constexpr float kPadding = 4.0F;
            const bool selected = isSelected(entry);
            selectedShown += selected ? 1U : 0U;
            const ImVec2 cardMin = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::Selectable("##card", selected, ImGuiSelectableFlags_AllowDoubleClick,
                                                   ImVec2{tileWidth - kPadding, tileHeight - kPadding});
            interact(entry, look, clicked);
            ImDrawList& drawList = *ImGui::GetWindowDrawList();
            if (look.icon != 0U) {
                const float iconX = std::floor(cardMin.x + (tileWidth - iconSize) * 0.5F);
                const float iconY = std::floor(cardMin.y + 6.0F);
                drawList.AddImage(static_cast<ImTextureID>(look.icon), ImVec2{iconX, iconY}, ImVec2{iconX + iconSize, iconY + iconSize});
            }
            const float textMax = tileWidth - kPadding - 6.0F;
            const float nameY = cardMin.y + iconSize + 10.0F;
            if (!DrawContentRenameFieldUVE(snapshot.contentRoot, entry, cardMin.x + 2.0F, nameY - 2.0F, textMax)) {
                drawList.AddText(ImVec2{cardMin.x + 5.0F, nameY}, ImGui::GetColorU32(ImGuiCol_Text),
                                 FitLabelUVE(entry.relativePath.filename().generic_string(), textMax).c_str());
            }
            drawList.AddText(ImVec2{cardMin.x + 5.0F, nameY + line}, ImGui::GetColorU32(ImGuiCol_TextDisabled),
                             FitLabelUVE(look.typeLabel, textMax).c_str());
        };
        const auto formatSize = [](const std::uintmax_t bytes) {
            char text[32];
            if (bytes < 1024U) {
                std::snprintf(text, sizeof(text), "%ju B", bytes);
            } else if (bytes < 1024U * 1024U) {
                std::snprintf(text, sizeof(text), "%.1f KB", static_cast<double>(bytes) / 1024.0);
            } else {
                std::snprintf(text, sizeof(text), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
            }
            return std::string{text};
        };
        const auto formatTime = [](const std::int64_t seconds, const char* format) {
            if (seconds == 0) {
                return std::string{"-"};
            }
            const std::time_t time = static_cast<std::time_t>(seconds);
            std::tm local{};
            localtime_r(&time, &local);
            char text[32];
            std::strftime(text, sizeof(text), format, &local);
            return std::string{text};
        };
        const float tileIcon = m_contentBrowserViewMode == ContentBrowserViewModeUVE::LargeTiles ? 72.0F : 44.0F;
        const float tileWidth = tileIcon + 40.0F;
        const float tileHeight = tileIcon + 14.0F + line * 2.0F;
        if (mode == ContentBrowserModeUVE::Recent) {
            drawPlacesStrip();
        }
        const float stripHeight = mode == ContentBrowserModeUVE::Board ? ImGui::GetFrameHeightWithSpacing() : 0.0F;
        const float statusHeight = line + ImGui::GetStyle().ItemSpacing.y * 2.0F + stripHeight;
        const ImVec2 areaSize{0.0F, std::max(line * 2.0F, ImGui::GetContentRegionAvail().y - statusHeight)};
        std::string statusNote;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4{0.0F, 0.0F, 0.0F, 0.0F});
        if (mode == ContentBrowserModeUVE::Details) {
            // A table to sort: by name, kind, size, last change or folder.
            constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Sortable | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg |
                                                   ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInnerV |
                                                   ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##content-details", 5, tableFlags, areaSize)) {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort, 3.0F, static_cast<ImGuiID>(ContentSortKeyUVE::Name));
                ImGui::TableSetupColumn("Type", 0, 1.2F, static_cast<ImGuiID>(ContentSortKeyUVE::Type));
                ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_PreferSortDescending, 1.0F,
                                        static_cast<ImGuiID>(ContentSortKeyUVE::Size));
                ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_PreferSortDescending, 1.6F,
                                        static_cast<ImGuiID>(ContentSortKeyUVE::Modified));
                ImGui::TableSetupColumn("Folder", 0, 1.6F, static_cast<ImGuiID>(ContentSortKeyUVE::Folder));
                ImGui::TableHeadersRow();
                if (ImGuiTableSortSpecs* const specs = ImGui::TableGetSortSpecs(); specs != nullptr && specs->SpecsCount > 0) {
                    m_contentSortKey = static_cast<ContentSortKeyUVE>(specs->Specs[0].ColumnUserID);
                    m_contentSortAscending = specs->Specs[0].SortDirection != ImGuiSortDirection_Descending;
                    specs->SpecsDirty = false;
                }
                std::vector<ItemLookUVE> looks;
                std::vector<ContentItemFactsUVE> facts;
                looks.reserve(items.size());
                facts.reserve(items.size());
                for (const std::size_t index : items) {
                    looks.push_back(lookOf(snapshot.entries[index]));
                    facts.push_back(factsOf(snapshot.entries[index], looks.back()));
                }
                std::vector<std::size_t> order(items.size());
                for (std::size_t i = 0U; i < order.size(); ++i) {
                    order[i] = i;
                }
                SortContentFactsUVE(order, facts, m_contentSortKey, m_contentSortAscending);
                for (const std::size_t i : order) {
                    const Asset::ProjectFileEntryUVE& entry = snapshot.entries[items[i]];
                    ImGui::PushID(("details-" + entry.relativePath.generic_string()).c_str());
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    const bool selected = isSelected(entry);
                    selectedShown += selected ? 1U : 0U;
                    const float rowX = ImGui::GetCursorScreenPos().x;
                    const bool clicked = ImGui::Selectable((iconGap + facts[i].name).c_str(), selected,
                                                           ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick);
                    interact(entry, looks[i], clicked);
                    drawRowIcon(rowX, looks[i].icon);
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", facts[i].type.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(facts[i].isFolder ? "-" : formatSize(facts[i].size).c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(formatTime(facts[i].modified, "%Y-%m-%d %H:%M").c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextDisabled("%s", facts[i].folder.c_str());
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        } else if (ImGui::BeginChild("##content-browser-grid", areaSize, false,
                                     mode == ContentBrowserModeUVE::Columns ? ImGuiWindowFlags_HorizontalScrollbar
                                                                            : ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
            if (mode == ContentBrowserModeUVE::Tiles) {
                // One folder by picture (or as a list, from Settings).
                const bool listRows = m_contentBrowserViewMode == ContentBrowserViewModeUVE::List;
                const float availableWidth = std::max(tileWidth, ImGui::GetContentRegionAvail().x);
                const int columns = listRows ? 1 : std::max(1, static_cast<int>(availableWidth / tileWidth));
                const ImVec2 origin = ImGui::GetCursorPos();
                const float rowStep = listRows ? 24.0F : tileHeight;
                for (std::size_t position = 0U; position < items.size(); ++position) {
                    const Asset::ProjectFileEntryUVE& entry = snapshot.entries[items[position]];
                    const ItemLookUVE look = lookOf(entry);
                    ImGui::PushID(("content-item-" + entry.relativePath.generic_string()).c_str());
                    ImGui::SetCursorPos(ImVec2{origin.x + static_cast<float>(static_cast<int>(position) % columns) * tileWidth,
                                               origin.y + static_cast<float>(static_cast<int>(position) / columns) * rowStep});
                    if (listRows) {
                        drawRow(entry, look, look.typeLabel, 22.0F);
                    } else {
                        drawTile(entry, look, tileWidth, tileHeight, tileIcon);
                    }
                    ImGui::PopID();
                }
                if (!items.empty()) {
                    const int totalRows = (static_cast<int>(items.size()) + columns - 1) / columns;
                    ImGui::SetCursorPos(ImVec2{origin.x, origin.y + static_cast<float>(totalRows) * rowStep});
                    ImGui::Dummy(ImVec2{0.0F, 0.0F});
                }
            } else if (mode == ContentBrowserModeUVE::Recent) {
                // What changed lately anywhere below, newest first, under Today / Yesterday / ...
                std::vector<ItemLookUVE> looks;
                std::vector<ContentItemFactsUVE> facts;
                for (const std::size_t index : items) {
                    looks.push_back(lookOf(snapshot.entries[index]));
                    facts.push_back(factsOf(snapshot.entries[index], looks.back()));
                }
                std::vector<std::size_t> order(items.size());
                for (std::size_t i = 0U; i < order.size(); ++i) {
                    order[i] = i;
                }
                SortContentFactsUVE(order, facts, ContentSortKeyUVE::Modified, false);
                const std::time_t now = std::time(nullptr);
                std::tm midnight{};
                localtime_r(&now, &midnight);
                midnight.tm_hour = 0;
                midnight.tm_min = 0;
                midnight.tm_sec = 0;
                const std::int64_t startOfToday = static_cast<std::int64_t>(std::mktime(&midnight));
                std::optional<ContentAgeUVE> currentAge;
                for (const std::size_t i : order) {
                    const ContentAgeUVE age = ClassifyContentAgeUVE(facts[i].modified, startOfToday);
                    if (age != currentAge) {
                        currentAge = age;
                        ImGui::Spacing();
                        ImGui::TextDisabled("%s", GetContentAgeLabelUVE(age));
                        ImGui::Separator();
                    }
                    const Asset::ProjectFileEntryUVE& entry = snapshot.entries[items[i]];
                    ImGui::PushID(("recent-" + entry.relativePath.generic_string()).c_str());
                    const char* const format = age == ContentAgeUVE::Today || age == ContentAgeUVE::Yesterday ? "%H:%M"
                                                                                                              : "%b %d";
                    drawRow(entry, looks[i], facts[i].folder + "   " + formatTime(facts[i].modified, format), 22.0F);
                    ImGui::PopID();
                }
                statusNote = "changed files in " + placeName + " and everything in it";
            } else if (mode == ContentBrowserModeUVE::Board) {
                // Everything below by kind: one lane per kind, a strip of tiles in each.
                std::vector<ItemLookUVE> looks;
                std::vector<ContentItemFactsUVE> facts;
                for (const std::size_t index : items) {
                    looks.push_back(lookOf(snapshot.entries[index]));
                    facts.push_back(factsOf(snapshot.entries[index], looks.back()));
                }
                std::vector<std::size_t> order(items.size());
                for (std::size_t i = 0U; i < order.size(); ++i) {
                    order[i] = i;
                }
                std::size_t fileCount = 0U;
                for (const auto& [kind, lane] : GroupContentByTypeUVE(order, facts)) {
                    fileCount += lane.size();
                    ImGui::PushID(kind.c_str());
                    if (SidebarSectionUVE("##lane", nullptr, kind, lane.size(), 0.0F)) {
                        ImGui::BeginChild("##strip", ImVec2{0.0F, tileHeight + ImGui::GetStyle().ScrollbarSize + 4.0F}, false,
                                          ImGuiWindowFlags_HorizontalScrollbar);
                        const ImVec2 origin = ImGui::GetCursorPos();
                        for (std::size_t n = 0U; n < lane.size(); ++n) {
                            const Asset::ProjectFileEntryUVE& entry = snapshot.entries[items[lane[n]]];
                            ImGui::PushID(entry.relativePath.generic_string().c_str());
                            ImGui::SetCursorPos(ImVec2{origin.x + static_cast<float>(n) * tileWidth, origin.y});
                            drawTile(entry, looks[lane[n]], tileWidth, tileHeight, tileIcon);
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                    }
                    ImGui::PopID();
                }
                items.resize(fileCount); // the count below is of files; folders have no lane
                statusNote = "files in " + placeName + " and everything in it, by kind";
            } else {
                // Columns: one per folder level from Content down to the folder on screen, and a
                // preview of the file picked in the last one.
                constexpr float kColumnWidthUVE = 220.0F;
                std::vector<std::filesystem::path> chain{std::filesystem::path{}};
                for (const std::filesystem::path& segment : m_contentBrowserDirectory) {
                    chain.push_back(chain.back() / segment);
                }
                const float columnHeight = std::max(line * 2.0F, ImGui::GetContentRegionAvail().y - ImGui::GetStyle().ScrollbarSize);
                // Places: pinned and shelves are the first column; the columns after it are the tree.
                ImGui::BeginChild("##column-places", ImVec2{kColumnWidthUVE, columnHeight}, true);
                drawPinned();
                drawShelves();
                ImGui::EndChild();
                ImGui::SameLine(0.0F, 2.0F);
                for (std::size_t level = 0U; level < chain.size(); ++level) {
                    const bool last = level + 1U == chain.size();
                    std::vector<std::size_t> listing =
                        last ? items : ListContentFolderUVE(snapshot.entries, chain[level], "");
                    ImGui::PushID(static_cast<int>(level));
                    ImGui::BeginChild("##column", ImVec2{kColumnWidthUVE, columnHeight}, true);
                    for (const std::size_t index : listing) {
                        const Asset::ProjectFileEntryUVE& entry = snapshot.entries[index];
                        const ItemLookUVE look = lookOf(entry);
                        const bool folder = entry.kind == Asset::ProjectFileEntryKindUVE::Directory;
                        // The folder opened from this column stays lit, as the way back.
                        const bool onPath = folder && !last && chain[level + 1U] == entry.relativePath;
                        ImGui::PushID(entry.relativePath.generic_string().c_str());
                        if (onPath) {
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4{kAccentUVE.x, kAccentUVE.y, kAccentUVE.z, 0.45F});
                        }
                        const float rowX = ImGui::GetCursorScreenPos().x;
                        const bool selected = onPath || isSelected(entry);
                        selectedShown += (!onPath && isSelected(entry)) ? 1U : 0U;
                        const std::string label = iconGap + entry.relativePath.filename().generic_string() + (folder ? "  >" : "");
                        const bool clicked = ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick);
                        if (onPath) {
                            ImGui::PopStyleColor();
                        }
                        drawRowIcon(rowX, look.icon);
                        // One click opens a folder here: that is what walking down columns is.
                        interact(entry, look, clicked && !folder);
                        if (clicked && folder) {
                            goToFolder(entry.relativePath);
                            selectEntry(entry);
                        }
                        ImGui::PopID();
                    }
                    if (listing.empty()) {
                        ImGui::TextDisabled(last && (searching || m_contentBrowserTypeFocus != ContentBrowserTypeFocusUVE::All)
                                                ? "Nothing matches."
                                                : "Empty.");
                    }
                    ImGui::EndChild();
                    ImGui::PopID();
                    ImGui::SameLine(0.0F, 2.0F);
                }
                // Preview of the file picked in the folder on screen.
                if (m_selectedProjectFile.has_value() && m_selectedProjectFile->kind == Asset::ProjectFileEntryKindUVE::File &&
                    m_selectedProjectFile->relativePath.parent_path() == m_contentBrowserDirectory) {
                    const Asset::ProjectFileEntryUVE picked = *m_selectedProjectFile;
                    const ItemLookUVE look = lookOf(picked);
                    const ContentItemFactsUVE facts = factsOf(picked, look);
                    ImGui::BeginChild("##column-preview", ImVec2{std::max(kColumnWidthUVE, 240.0F), columnHeight}, true);
                    constexpr float kPreviewSize = 128.0F;
                    const float x = std::max(0.0F, (ImGui::GetContentRegionAvail().x - kPreviewSize) * 0.5F);
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
                    if (look.icon != 0U) {
                        ImGui::Image(static_cast<ImTextureID>(look.icon), ImVec2{kPreviewSize, kPreviewSize});
                    }
                    ImGui::TextWrapped("%s", facts.name.c_str());
                    ImGui::TextDisabled("%s", facts.type.c_str());
                    ImGui::Separator();
                    ImGui::TextDisabled("Size");
                    ImGui::SameLine(80.0F);
                    ImGui::TextUnformatted(formatSize(facts.size).c_str());
                    ImGui::TextDisabled("Changed");
                    ImGui::SameLine(80.0F);
                    ImGui::TextUnformatted(formatTime(facts.modified, "%Y-%m-%d %H:%M").c_str());
                    ImGui::TextDisabled("Folder");
                    ImGui::SameLine(80.0F);
                    ImGui::TextWrapped("%s", facts.folder.c_str());
                    if (look.modelSource != nullptr && !look.modelSource->summary.empty()) {
                        ImGui::Separator();
                        ImGui::TextWrapped("%s", look.modelSource->summary.c_str());
                    }
                    ImGui::Separator();
                    const bool pinned = IsProjectPathFavoritedUVE(picked.relativePath);
                    if (ImGui::Button(pinned ? "Unpin" : "Pin")) {
                        ToggleProjectPathFavoriteUVE(picked.relativePath);
                    }
                    if (look.type == ContentBrowserItemTypeUVE::Entity || look.type == ContentBrowserItemTypeUVE::Prefab) {
                        ImGui::SameLine();
                        ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
                        if (ImGui::Button("Place in Scene") &&
                            PlaceEntityAssetUVE(snapshot.contentRoot / picked.relativePath) == Scene::kInvalidEntityUVE) {
                            m_contentStatusMessage = "Could not place " + facts.name + ".";
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::EndChild();
                }
            }
            // Right-click on empty space: the same menu as "Add".
            if (canCreate && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !ImGui::IsAnyItemHovered() &&
                ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
                m_contentCreateMenuRequested = true;
            }
            if (items.empty() && mode != ContentBrowserModeUVE::Columns) {
                if (!snapshot.contentRootExists) {
                    ImGui::TextDisabled("The content folder does not exist yet.");
                } else if (searching || m_contentBrowserTypeFocus != ContentBrowserTypeFocusUVE::All) {
                    ImGui::TextDisabled("Nothing here matches.");
                } else if (shownShelf != nullptr) {
                    ImGui::TextDisabled("This shelf is empty. Drag files onto it, or right-click a file > Shelves.");
                } else if (wholeTree) {
                    ImGui::TextDisabled("There are no files in this folder or below it.");
                } else {
                    ImGui::TextDisabled("This folder is empty. Add or drop files here.");
                }
            }
        }
        if (mode != ContentBrowserModeUVE::Details) {
            ImGui::EndChild();
        }
        ImGui::PopStyleColor();
        // With a shelf open, dropping anywhere on its items puts the file on it.
        if (shownShelf != nullptr) {
            acceptShelfDrop(shownShelf->name);
        }

        if (mode == ContentBrowserModeUVE::Board) {
            drawPlacesStrip();
        }
        // The floating Retarget button: there while animations are picked. Picking a character too
        // (a model source or a .uvmodel) chooses it for the window.
        {
            std::vector<std::filesystem::path> pickedAnimations;
            std::filesystem::path pickedCharacter;
            if (!m_contentSelection.IsEmptyUVE() && !m_retargetWindow.has_value()) {
                for (const std::string& item : m_contentSelection.ItemsUVE()) {
                    const std::filesystem::path relative{item};
                    const auto found = std::ranges::find(snapshot.entries, relative, &Asset::ProjectFileEntryUVE::relativePath);
                    if (found == snapshot.entries.end() || found->kind == Asset::ProjectFileEntryKindUVE::Directory) {
                        continue;
                    }
                    std::string extension = relative.extension().string();
                    std::ranges::transform(extension, extension.begin(),
                                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    if (extension == ".uvanim") {
                        pickedAnimations.push_back(snapshot.contentRoot / relative);
                    } else if (pickedCharacter.empty() && (extension == ".uvmodel" || IsRiggedModelSourceUVE(relative))) {
                        pickedCharacter = relative;
                    }
                }
            }
            const ImVec2 below = ImGui::GetCursorScreenPos();
            const std::string label = "Retarget " + std::to_string(pickedAnimations.size()) +
                                      (pickedAnimations.size() == 1U ? " animation" : " animations");
            constexpr float kHeight = 28.0F;
            const float width = ImGui::CalcTextSize(label.c_str()).x + 40.0F;
            const ImVec2 windowMin = ImGui::GetWindowPos();
            const float windowWidth = ImGui::GetWindowSize().x;
            if (!pickedAnimations.empty() && below.y - windowMin.y > kHeight + 90.0F) {
                ImGui::SetCursorScreenPos(ImVec2{std::floor(windowMin.x + (windowWidth - width) * 0.5F),
                                                 std::floor(below.y - kHeight - 6.0F)});
                ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0F);
                if (ImGui::BeginChild("##retarget-float", ImVec2{width, kHeight}, ImGuiChildFlags_None,
                                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground)) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{kAccentUVE.x, kAccentUVE.y, kAccentUVE.z, 0.95F});
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccentHoveredUVE);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0F);
                    if (ImGui::Button(label.c_str(), ImVec2{width, kHeight})) {
                        OpenRetargetWindowUVE(pickedAnimations, pickedCharacter);
                    }
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(2);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip("Make these animations and a character the UniVex humanoid's.\nFiles change in place; a backup is kept.");
                    }
                }
                ImGui::EndChild();
                ImGui::PopStyleVar();
                ImGui::SetCursorScreenPos(below);
            }
        }
        // Status line: how many items, how many selected, and what the mode is looking at.
        std::string status = std::to_string(items.size()) + (items.size() == 1U ? " item" : " items");
        if (selectedShown > 0U) {
            status += "  |  " + std::to_string(selectedShown) + " selected";
        }
        if (!statusNote.empty() && shownShelf == nullptr) {
            status += "  |  " + statusNote;
        } else if (searching && shownShelf == nullptr) {
            status += "  |  searching " + placeName + " and everything in it";
        }
        ImGui::Separator();
        ImGui::TextDisabled("%s", status.c_str());
    }
    ImGui::EndChild();
    if (sidebarRight) {
        ImGui::SameLine(0.0F, 0.0F);
        drawSplitter(true);
        ImGui::SameLine(0.0F, 0.0F);
        drawSidebar();
    }

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
}

} // namespace UVE::Editor
