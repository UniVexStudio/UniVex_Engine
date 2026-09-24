// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Content panel's making and managing of things: the "+ Add" catalogue (also opened by a
// right-click on empty space), the right-click menu of a file, inline rename, and dragging an
// entity asset into the scene.

#include "uve/editor/editor_uve.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <imgui.h>

#include "uve/editor/editor_content_catalogue_uve.h"
#include "editor_chrome_layout_uve.h"
#include "editor_node_icons_uve.h"

namespace UVE::Editor {
namespace {

constexpr std::size_t kMaximumContentRecentUVE = 5U;
constexpr std::size_t kMaximumContentNameBytesUVE = 96U;

[[nodiscard]] std::string LowerExtensionUVE(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return extension;
}

[[nodiscard]] bool IsEntityAssetPathUVE(const std::filesystem::path& path) {
    const std::string extension = LowerExtensionUVE(path);
    return extension == ".uveentity" || extension == ".uveprefab";
}

/// A file name a person typed: not empty, no separators, no "." or "..", nothing a filesystem
/// rejects on any platform we ship to.
[[nodiscard]] bool IsContentNameValidUVE(const std::string_view name) {
    if (name.empty() || name.size() > kMaximumContentNameBytesUVE || name == "." || name == ".." ||
        name.front() == ' ' || name.back() == ' ' || name.back() == '.') {
        return false;
    }
    return std::none_of(name.begin(), name.end(), [](const char character) {
        return std::iscntrl(static_cast<unsigned char>(character)) != 0 ||
               std::string_view{"/\\:*?\"<>|"}.find(character) != std::string_view::npos;
    });
}

} // namespace

std::optional<std::filesystem::path> EditorUVE::RenameContentFileUVE(const std::filesystem::path& file,
                                                                     const std::string_view newStem) {
    std::error_code error;
    if (!IsContentNameValidUVE(newStem) || !std::filesystem::exists(file, error)) {
        return std::nullopt;
    }
    const bool directory = std::filesystem::is_directory(file, error);
    const std::filesystem::path target =
        file.parent_path() / (std::string{newStem} + (directory ? std::string{} : file.extension().string()));
    if (target == file) {
        return target;
    }
    if (std::filesystem::exists(target, error)) {
        return std::nullopt;
    }
    std::filesystem::rename(file, target, error);
    if (error) {
        return std::nullopt;
    }
    return target;
}

std::optional<std::filesystem::path> EditorUVE::DuplicateContentFileUVE(const std::filesystem::path& file) {
    std::error_code error;
    if (!std::filesystem::exists(file, error)) {
        return std::nullopt;
    }
    const bool directory = std::filesystem::is_directory(file, error);
    const std::filesystem::path target =
        MakeUniqueContentPathUVE(file.parent_path(), (directory ? file.filename() : file.stem()).string(),
                                 directory ? std::string_view{} : std::string_view{file.extension().string()});
    std::filesystem::copy(file, target, std::filesystem::copy_options::recursive, error);
    if (error) {
        return std::nullopt;
    }
    return target;
}

void EditorUVE::PushContentCreateRecentUVE(std::vector<std::string>& recent, const std::string_view id) {
    std::erase(recent, id);
    recent.insert(recent.begin(), std::string{id});
    if (recent.size() > kMaximumContentRecentUVE) {
        recent.resize(kMaximumContentRecentUVE);
    }
}

void EditorUVE::BeginContentRenameUVE(const std::filesystem::path& relativePath) {
    m_contentRenamePath = relativePath;
    std::error_code error;
    const std::filesystem::path absolute = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot / relativePath;
    m_contentRenameText = (std::filesystem::is_directory(absolute, error) ? relativePath.filename() : relativePath.stem())
                              .string();
    m_contentRenameFocus = true;
}

bool EditorUVE::DrawContentRenameFieldUVE(const std::filesystem::path& contentRoot,
                                          const Asset::ProjectFileEntryUVE& entry, const float x, const float y,
                                          const float width) {
    if (m_contentRenamePath.empty() || entry.relativePath != m_contentRenamePath) {
        return false;
    }
    ImGui::SetCursorScreenPos(ImVec2{x, y});
    ImGui::SetNextItemWidth(width);
    if (m_contentRenameFocus) {
        ImGui::SetKeyboardFocusHere();
        m_contentRenameFocus = false;
    }
    std::array<char, kMaximumContentNameBytesUVE + 1U> buffer{};
    std::strncpy(buffer.data(), m_contentRenameText.c_str(), buffer.size() - 1U);
    const bool entered = ImGui::InputText("##content-rename", buffer.data(), buffer.size(),
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    m_contentRenameText = buffer.data();
    const bool valid = IsContentNameValidUVE(m_contentRenameText);
    if (!valid && ImGui::IsItemActive()) {
        ImGui::SetTooltip("A name cannot be empty or contain / \\ : * ? \" < > |");
    }
    if (!entered && !ImGui::IsItemDeactivated()) {
        return true;
    }
    // Enter or clicking away keeps the name; Escape keeps the old one.
    if (!ImGui::IsKeyPressed(ImGuiKey_Escape) && valid) {
        const std::optional<std::filesystem::path> renamed =
            RenameContentFileUVE(contentRoot / entry.relativePath, m_contentRenameText);
        if (!renamed.has_value()) {
            m_contentStatusMessage = "Could not rename to \"" + m_contentRenameText + "\" - is the name taken?";
        } else if (*renamed != contentRoot / entry.relativePath) {
            const std::filesystem::path relative = renamed->lexically_relative(contentRoot);
            if (m_selectedProjectFile.has_value() && m_selectedProjectFile->relativePath == entry.relativePath) {
                m_selectedProjectFile = Asset::ProjectFileEntryUVE{relative, entry.kind, std::nullopt};
                m_selectedAsset.reset();
            }
            if (GetDefaultPlayerEntityUVE() == entry.relativePath.generic_string()) {
                static_cast<void>(SetDefaultPlayerEntityUVE(relative));
            }
            m_projectFileSnapshotInitialized = false;
            RefreshProjectFileIndexUVE();
        }
    }
    m_contentRenamePath.clear();
    return true;
}

void EditorUVE::DrawContentCreateMenuUVE(const std::filesystem::path& contentRoot,
                                         const std::filesystem::path& directory) {
    if (ImGui::IsWindowAppearing()) {
        m_contentCreateFilter.clear();
        ImGui::SetKeyboardFocusHere();
    }
    std::array<char, 128> filter{};
    std::strncpy(filter.data(), m_contentCreateFilter.c_str(), filter.size() - 1U);
    ImGui::SetNextItemWidth(240.0F);
    if (ImGui::InputTextWithHint("##content-create-search", "Search: character, light, box...", filter.data(),
                                 filter.size())) {
        m_contentCreateFilter = filter.data();
    }
    ImGui::TextDisabled("Create in %s", directory.empty() ? "main" : directory.generic_string().c_str());
    ImGui::Separator();

    const bool allowed = IsAuthoringCommandAllowedUVE();
    const auto create = [&](const ContentCatalogueItemUVE& item) {
        std::error_code error;
        std::filesystem::create_directories(contentRoot / directory, error);
        const std::optional<std::filesystem::path> created =
            CreateContentCatalogueItemUVE(item.id, contentRoot / directory);
        if (!created.has_value()) {
            m_contentStatusMessage = "Could not create " + std::string{item.label} + ".";
            return;
        }
        PushContentCreateRecentUVE(m_contentCreateRecent, item.id);
        m_projectFileSnapshotInitialized = false;
        RefreshProjectFileIndexUVE();
        const std::filesystem::path relative = created->lexically_relative(contentRoot);
        if (item.action != ContentCatalogueActionUVE::Folder) {
            m_selectedProjectFile = Asset::ProjectFileEntryUVE{relative, Asset::ProjectFileEntryKindUVE::File, std::nullopt};
            m_selectedAsset.reset();
        }
        BeginContentRenameUVE(relative);
        m_contentStatusMessage.clear();
        ImGui::CloseCurrentPopup();
    };
    const auto drawItem = [&](const ContentCatalogueItemUVE& item, const bool showGroup) {
        ImGui::PushID(item.id.data(), item.id.data() + item.id.size());
        DrawNodePickerIconUVE(m_uiAssets.GetNodeIconTextureIdUVE(GetContentCatalogueIconKindUVE(item)));
        const std::string label{item.label};
        const std::string group = showGroup ? std::string{item.group} : std::string{};
        ImGui::BeginDisabled(!allowed);
        const bool picked = ImGui::MenuItem(label.c_str(), showGroup ? group.c_str() : nullptr);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("%.*s", static_cast<int>(item.tooltip.size()), item.tooltip.data());
        }
        ImGui::PopID();
        if (picked) {
            create(item);
        }
    };

    if (!m_contentCreateFilter.empty()) {
        // Searching flattens the groups: best matches first (the name starts with what was typed,
        // then contains it, then only the group or description does), each with its group beside it.
        std::vector<const ContentCatalogueItemUVE*> matches;
        for (const ContentCatalogueItemUVE& item : GetContentCatalogueItemsUVE()) {
            if (RankContentCatalogueItemUVE(item, m_contentCreateFilter) > 0) {
                matches.push_back(&item);
            }
        }
        std::stable_sort(matches.begin(), matches.end(), [this](const auto* left, const auto* right) {
            return RankContentCatalogueItemUVE(*left, m_contentCreateFilter) >
                   RankContentCatalogueItemUVE(*right, m_contentCreateFilter);
        });
        for (const ContentCatalogueItemUVE* const item : matches) {
            drawItem(*item, true);
        }
        if (matches.empty()) {
            ImGui::TextDisabled("Nothing matches \"%s\".", m_contentCreateFilter.c_str());
        } else if (allowed && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            create(*matches.front()); // Enter takes the top match: "char" + Enter makes a Character.
        }
    } else {
        if (!m_contentCreateRecent.empty()) {
            ImGui::TextDisabled("Recent");
            for (const std::string& id : m_contentCreateRecent) {
                if (const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(id)) {
                    drawItem(*item, false);
                }
            }
            ImGui::Separator();
        }
        // "Basic" stays open at the top; every other group is a submenu with its first item's icon.
        for (const std::string_view group : GetContentCatalogueGroupsUVE()) {
            const std::span<const ContentCatalogueItemUVE> items = GetContentCatalogueItemsUVE();
            const auto firstInGroup = std::find_if(items.begin(), items.end(),
                                                   [group](const ContentCatalogueItemUVE& item) { return item.group == group; });
            if (firstInGroup == items.end()) {
                continue;
            }
            if (group == "Basic") {
                for (const ContentCatalogueItemUVE& item : items) {
                    if (item.group == group) {
                        drawItem(item, false);
                    }
                }
                ImGui::Separator();
                continue;
            }
            DrawNodePickerIconUVE(m_uiAssets.GetNodeIconTextureIdUVE(GetContentCatalogueIconKindUVE(*firstInGroup)));
            const std::string groupLabel{group};
            if (ImGui::BeginMenu(groupLabel.c_str())) {
                for (const ContentCatalogueItemUVE& item : items) {
                    if (item.group == group) {
                        drawItem(item, false);
                    }
                }
                ImGui::EndMenu();
            }
        }
    }
    if (!allowed) {
        ImGui::Separator();
        ImGui::TextDisabled("Stop Play to create assets.");
    }
}

void EditorUVE::AcceptContentEntityDropUVE(Scene::EntityUVE parent) {
    if (!IsAuthoringCommandAllowedUVE() || !ImGui::BeginDragDropTarget()) {
        return;
    }
    if (const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(kContentEntityPayloadUVE);
        payload != nullptr && payload->DataSize > 1) {
        const std::string path(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize - 1));
        if (parent == Scene::kInvalidEntityUVE) {
            parent = EnsureDocumentSceneRootUVE();
        }
        if (PlaceEntityAssetUVE(path, parent) == Scene::kInvalidEntityUVE) {
            m_contentStatusMessage = "Could not place " + std::filesystem::path{path}.filename().string() + ".";
        }
    }
    ImGui::EndDragDropTarget();
}

void EditorUVE::DrawFilesystemContextPopupUVE() {
    constexpr const char* kPopupId = "##content-item-menu";
    if (m_filesystemContextVisible) {
        ImGui::OpenPopup(kPopupId);
        m_filesystemContextVisible = false;
    }
    if (!ImGui::BeginPopup(kPopupId)) {
        return;
    }
    if (!m_filesystemContextEntry.has_value()) {
        ImGui::EndPopup();
        return;
    }

    const Asset::ProjectFileEntryUVE contextEntry = *m_filesystemContextEntry;
    const std::filesystem::path contentRoot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    const std::filesystem::path absolute = contentRoot / contextEntry.relativePath;
    const bool directory = contextEntry.kind == Asset::ProjectFileEntryKindUVE::Directory;
    const bool entityAsset = !directory && IsEntityAssetPathUVE(contextEntry.relativePath);
    const bool isEntity = entityAsset && LowerExtensionUVE(contextEntry.relativePath) == ".uveentity";
    const bool allowed = IsAuthoringCommandAllowedUVE();
    const auto refresh = [this]() {
        m_projectFileSnapshotInitialized = false;
        RefreshProjectFileIndexUVE();
    };

    ImGui::TextDisabled("%s", contextEntry.relativePath.filename().generic_string().c_str());
    ImGui::Separator();

    if (entityAsset) {
        ImGui::BeginDisabled(!allowed);
        if (ImGui::MenuItem("Place in Scene")) {
            if (PlaceEntityAssetUVE(absolute) == Scene::kInvalidEntityUVE) {
                m_contentStatusMessage = "Could not place " + contextEntry.relativePath.filename().string() + ".";
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Adds it under the selected node, or to the scene. You can also drag it into the "
                              "Scene panel or the viewport.");
        }
    }
    if (isEntity) {
        if (ImGui::MenuItem("Open Tree")) {
            m_contentStatusMessage = "Opening an entity's tree needs the Entity Editor, which is not in this build yet.";
        }
        const bool isDefault = GetDefaultPlayerEntityUVE() == contextEntry.relativePath.generic_string();
        if (ImGui::MenuItem("Default Player", nullptr, isDefault)) {
            if (!SetDefaultPlayerEntityUVE(isDefault ? std::filesystem::path{} : contextEntry.relativePath)) {
                m_contentStatusMessage = "Could not save the project settings.";
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("The entity the player is spawned from when the game starts (Project Settings > "
                              "Game > Player).");
        }
    }
    if (directory && ImGui::MenuItem("Open")) {
        m_contentBrowserDirectory = contextEntry.relativePath;
        m_contentBrowserShowingFavorites = false;
        m_selectedProjectFile = contextEntry;
        m_selectedAsset.reset();
    }

    // A model source is imported automatically; this puts it on the selected node's mesh. The
    // converted mesh must exist first - naming one that is still importing would leave the node
    // pointing at nothing.
    const std::filesystem::path importedModel =
        IsModelSourcePathUVE(contextEntry.relativePath) ? GetImportedModelPathUVE(contextEntry.relativePath)
                                                        : std::filesystem::path{};
    std::error_code importedError;
    const bool modelReady = !importedModel.empty() && std::filesystem::is_regular_file(importedModel, importedError);
    const bool uvemodel = contextEntry.registeredAssetGuid.has_value() &&
                          contextEntry.relativePath.extension().string() == ".uvemodel";
    if (modelReady || uvemodel) {
        const bool canAssign = IsDocumentEntityUVE(m_selectedEntity) && IsAuthoringCommandAllowedUVE() &&
                               m_services->GetEntityManagerUVE().HasComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity);
        ImGui::BeginDisabled(!canAssign);
        if (ImGui::MenuItem("Use as Mesh on selected node")) {
            Scene::MeshComponentUVE mesh =
                m_services->GetEntityManagerUVE().GetComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity);
            mesh.meshGuid = modelReady ? m_services->GetAssetDatabaseUVE().RegisterUVE(importedModel)
                                       : *contextEntry.registeredAssetGuid;
            static_cast<void>(SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Mesh, mesh));
        }
        ImGui::EndDisabled();
    }
    if (entityAsset || directory || modelReady || uvemodel) {
        ImGui::Separator();
    }

    const bool favorited = IsProjectPathFavoritedUVE(contextEntry.relativePath);
    if (ImGui::MenuItem(favorited ? "Remove from Favorites" : "Add to Favorites")) {
        ToggleProjectPathFavoriteUVE(contextEntry.relativePath);
    }
    if (ImGui::MenuItem("Show in Folder")) {
        // Useful from Favorites or a search: go to where it lives and select it there.
        m_contentBrowserDirectory = contextEntry.relativePath.parent_path();
        m_contentBrowserShowingFavorites = false;
        m_assetFilter.clear();
        m_selectedProjectFile = directory ? std::optional<Asset::ProjectFileEntryUVE>{} : contextEntry;
        m_selectedAsset.reset();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Rename")) {
        BeginContentRenameUVE(contextEntry.relativePath);
    }
    if (ImGui::MenuItem("Duplicate")) {
        if (const std::optional<std::filesystem::path> copy = DuplicateContentFileUVE(absolute)) {
            refresh();
            BeginContentRenameUVE(copy->lexically_relative(contentRoot));
        } else {
            m_contentStatusMessage = "Could not duplicate " + contextEntry.relativePath.filename().string() + ".";
        }
    }
    // Deleting cannot be undone, so it asks once more in a submenu instead of acting on one click.
    if (ImGui::BeginMenu("Delete")) {
        if (ImGui::MenuItem(directory ? "Delete folder and everything in it" : "Delete permanently")) {
            std::error_code error;
            std::filesystem::remove_all(absolute, error);
            if (error) {
                m_contentStatusMessage = "Could not delete " + contextEntry.relativePath.filename().string() + ".";
            } else {
                if (GetDefaultPlayerEntityUVE() == contextEntry.relativePath.generic_string()) {
                    static_cast<void>(SetDefaultPlayerEntityUVE({}));
                }
                if (m_selectedProjectFile.has_value() && m_selectedProjectFile->relativePath == contextEntry.relativePath) {
                    m_selectedProjectFile.reset();
                    m_selectedAsset.reset();
                }
                m_filesystemContextEntry.reset();
                refresh();
            }
        }
        ImGui::EndMenu();
    }
    ImGui::EndPopup();
}

} // namespace UVE::Editor
