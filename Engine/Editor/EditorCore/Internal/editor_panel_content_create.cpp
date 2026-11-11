#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif
// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Content panel's making and managing of things: the "+ Add" catalogue (also opened by a
// right-click on empty space), the right-click menu of a file, inline rename, and dragging an
// entity asset into the scene.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include "uve_file_envelope.h"
#include "shader_asset.h"
#include <imgui.h>

#include "name_component.h"
#include "editor_content_catalogue.h"
#include "editor_chrome_layout.h"
#include "editor_object_icons.h"

namespace UVE::Editor {
namespace {

#if defined(_WIN32)
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"
#endif
using GetOpenFileNameWFn = BOOL (WINAPI *)(LPOPENFILENAMEW);
inline GetOpenFileNameWFn LoadGetOpenFileNameW(HMODULE comdlg) noexcept {
    if (comdlg == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<GetOpenFileNameWFn>(GetProcAddress(comdlg, "GetOpenFileNameW"));
}
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#endif

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
    return extension == ".uventity" || extension == ".uvprefab";
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

/// The top strip of a Content menu: shows `title` and moves the menu while dragged. Popups cannot
/// be moved by ImGui itself, so the caller places the window at (x, y) every frame.
void DrawMenuDragStripUVE(const char* const title, float& x, float& y) {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float width = std::max(ImGui::GetContentRegionAvail().x, 160.0F);
    const float height = ImGui::GetTextLineHeight() + 4.0F;
    ImGui::InvisibleButton("##menu-drag", ImVec2{width, height});
    const bool hovered = ImGui::IsItemHovered();
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0F)) {
        const ImGuiViewport* const viewport = ImGui::GetMainViewport();
        const ImVec2 size = ImGui::GetWindowSize();
        x = std::clamp(x + ImGui::GetIO().MouseDelta.x, viewport->WorkPos.x,
                       std::max(viewport->WorkPos.x, viewport->WorkPos.x + viewport->WorkSize.x - size.x));
        y = std::clamp(y + ImGui::GetIO().MouseDelta.y, viewport->WorkPos.y,
                       std::max(viewport->WorkPos.y, viewport->WorkPos.y + viewport->WorkSize.y - size.y));
    }
    if (hovered || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }
    ImDrawList* const drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(start, ImVec2{start.x + width, start.y + height},
                            ImGui::GetColorU32(hovered || ImGui::IsItemActive() ? ImGuiCol_HeaderHovered : ImGuiCol_Header),
                            3.0F);
    // A grip of dots on the left says "drag me"; the title follows it.
    for (int column = 0; column < 2; ++column) {
        for (int row = 0; row < 3; ++row) {
            drawList->AddCircleFilled(ImVec2{start.x + 7.0F + static_cast<float>(column) * 4.0F,
                                             start.y + height * 0.5F + static_cast<float>(row - 1) * 4.0F},
                                      1.2F, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        }
    }
    drawList->AddText(ImVec2{start.x + 18.0F, start.y + 2.0F}, ImGui::GetColorU32(ImGuiCol_TextDisabled), title);
}

} // namespace

namespace {
// A popup sizes itself on its second frame; before that a bottom anchor would place it wrong.
constexpr int kContentMenuSettleFramesUVE = 3;
} // namespace

void EditorUVE::PlaceContentMenuUVE(const bool justOpened, float& anchorX, float& anchorY, const int frames,
                                    const float x, const float y) {
    if (justOpened) {
        anchorX = ImGui::GetMousePos().x;
        anchorY = ImGui::GetMousePos().y;
    }
    if (justOpened || frames < kContentMenuSettleFramesUVE) {
        // Content is at the bottom of the window: grow upward from the pointer, over the viewport.
        ImGui::SetNextWindowPos(ImVec2{anchorX, anchorY}, ImGuiCond_Always, ImVec2{0.0F, 1.0F});
        const ImGuiViewport* const viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowSizeConstraints(ImVec2{0.0F, 0.0F},
                                            ImVec2{FLT_MAX, std::max(120.0F, anchorY - viewport->WorkPos.y - 8.0F)});
    } else {
        ImGui::SetNextWindowPos(ImVec2{x, y}, ImGuiCond_Always);
    }
}

void EditorUVE::SettleContentMenuUVE(int& frames, float& x, float& y) {
    if (frames < kContentMenuSettleFramesUVE) {
        ++frames;
        x = ImGui::GetWindowPos().x;
        y = ImGui::GetWindowPos().y;
    }
}

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

std::optional<std::filesystem::path> EditorUVE::RenameContentAssetUVE(const std::filesystem::path& file,
                                                                      const std::string_view newStem) {
    const std::optional<std::filesystem::path> target = RenameContentFileUVE(file, newStem);
    if (!target.has_value() || *target == file) {
        return target;
    }
    // The file keeps its GUID under the new name, so scenes and players that use it still do.
    Asset::IAssetDatabaseUVE& assetDatabase = m_services->GetAssetDatabaseUVE();
    for (const Asset::AssetRecordUVE& record : assetDatabase.GetRegisteredAssetsUVE()) {
        if (record.path == file.lexically_normal()) {
            static_cast<void>(assetDatabase.RelocateUVE(record.guid, *target));
        }
    }
    const std::string ext = LowerExtensionUVE(*target);
    const bool isSceneOrEntityOrPrefab = (ext == ".uventity" || ext == ".uvprefab" || ext == ".uvscene");

    if (m_entityEditSession.has_value() && m_entityEditSession->assetPath == file) {
        m_entityEditSession->assetPath = *target;
        const Scene::EntityUVE entityRoot = GetEntityEditorRootUVE();
        if (entityRoot != Scene::kInvalidEntityUVE) {
            Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
            const std::string newName = target->stem().string();
            if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(entityRoot)) {
                entityManager.GetComponentUVE<Scene::NameComponentUVE>(entityRoot).name = newName;
            }
            static_cast<void>(SaveEntityEditorUVE());
        }
    } else if (m_activeScenePath == file) {
        m_activeScenePath = *target;
        const Scene::EntityUVE sceneRoot = GetDocumentSceneRootUVE();
        if (sceneRoot != Scene::kInvalidEntityUVE) {
            Scene::IEntityManagerUVE& entityManager = m_services->GetEntityManagerUVE();
            const std::string newName = target->stem().string();
            if (entityManager.HasComponentUVE<Scene::NameComponentUVE>(sceneRoot)) {
                entityManager.GetComponentUVE<Scene::NameComponentUVE>(sceneRoot).name = newName;
            }
        }
    }

    if (isSceneOrEntityOrPrefab) {
        if (const auto fileData = Asset::ReadUveFileUVE(*target); fileData.has_value()) {
            const auto& [header, payload] = *fileData;
            const std::string payloadText(reinterpret_cast<const char*>(payload.data()), payload.size());
            nlohmann::json payloadJson = nlohmann::json::parse(payloadText, nullptr, false);
            if (!payloadJson.is_discarded() && payloadJson.contains("entities") &&
                payloadJson["entities"].is_array() && !payloadJson["entities"].empty()) {
                const std::string newName = target->stem().string();
                for (auto& entityObj : payloadJson["entities"]) {
                    bool isRoot = true;
                    if (entityObj.contains("components") && entityObj["components"].contains("HierarchyComponentUVE")) {
                        const auto& h = entityObj["components"]["HierarchyComponentUVE"];
                        if (h.contains("parentLocalId") && h["parentLocalId"].get<std::int64_t>() >= 0) {
                            isRoot = false;
                        }
                    }
                    if (isRoot) {
                        if (!entityObj.contains("components") || !entityObj["components"].is_object()) {
                            entityObj["components"] = nlohmann::json::object();
                        }
                        if (!entityObj["components"].contains("NameComponentUVE") || !entityObj["components"]["NameComponentUVE"].is_object()) {
                            entityObj["components"]["NameComponentUVE"] = nlohmann::json::object();
                        }
                        entityObj["components"]["NameComponentUVE"]["name"] = newName;
                        break;
                    }
                }
                const std::string dumped = payloadJson.dump(4);
                std::vector<std::byte> payloadBytes(dumped.size());
                std::memcpy(payloadBytes.data(), dumped.data(), dumped.size());
                static_cast<void>(Asset::WriteUveFileUVE(*target, header.assetType, payloadBytes));
            }
        }
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
            RenameContentAssetUVE(contentRoot / entry.relativePath, m_contentRenameText);
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
            // Shelves, pins and the folder on screen follow the file to its new name.
            m_contentShelves.MovePathUVE(entry.relativePath, relative);
            static_cast<void>(SaveSharedShelvesUVE());
            for (std::filesystem::path& pinned : m_favoriteProjectPaths) {
                if (pinned == entry.relativePath) {
                    pinned = relative;
                } else if (IsInsideContentDirectoryUVE(pinned, entry.relativePath)) {
                    pinned = relative / pinned.lexically_relative(entry.relativePath);
                }
            }
            if (m_contentBrowserDirectory == entry.relativePath ||
                IsInsideContentDirectoryUVE(m_contentBrowserDirectory, entry.relativePath)) {
                m_contentBrowserDirectory = relative / m_contentBrowserDirectory.lexically_relative(entry.relativePath);
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
    const std::string title = "Create in " + (directory.empty() ? std::string{"main"} : directory.generic_string());
    DrawMenuDragStripUVE(title.c_str(), m_contentCreateMenuX, m_contentCreateMenuY);
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
    ImGui::Separator();

    if (m_contentCreateFilter.empty()) {
        if (ImGui::MenuItem("Import Assets... (Ctrl+I)")) {
            PromptImportFilesUVE(contentRoot / directory);
            ImGui::CloseCurrentPopup();
            return;
        }
        ImGui::Separator();
    }

    const bool allowed = IsAuthoringCommandAllowedUVE();
    const auto create = [&](const ContentCatalogueItemUVE& item) {
        if (item.action == ContentCatalogueActionUVE::ScriptAsset) {
            OpenCreateScriptModalUVE(contentRoot / directory);
            ImGui::CloseCurrentPopup();
            return;
        }
        if (item.action == ContentCatalogueActionUVE::ShaderAsset) {
            OpenCreateShaderModalUVE(contentRoot / directory);
            ImGui::CloseCurrentPopup();
            return;
        }
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
        std::uintptr_t iconId = 0U;
        ImU32 indicatorColor = 0U;
        if (item.action == ContentCatalogueActionUVE::MaterialAsset) {
            iconId = m_uiAssets.GetContentTypeIconTextureIdUVE("Material");
            indicatorColor = IM_COL32(16, 185, 129, 255);
        } else if (item.action == ContentCatalogueActionUVE::ShaderAsset) {
            iconId = m_uiAssets.GetContentTypeIconTextureIdUVE("Shader");
            indicatorColor = IM_COL32(16, 185, 129, 255);
        } else if (item.action == ContentCatalogueActionUVE::ScriptAsset) {
            iconId = m_uiAssets.GetContentTypeIconTextureIdUVE("Script");
            indicatorColor = IM_COL32(56, 189, 248, 255);
        } else if (item.action == ContentCatalogueActionUVE::Folder) {
            iconId = m_uiAssets.GetContentTypeIconTextureIdUVE("folder");
            indicatorColor = IM_COL32(245, 158, 11, 255);
        } else if (item.action == ContentCatalogueActionUVE::LibraryAsset) {
            iconId = m_uiAssets.GetContentTypeIconTextureIdUVE("AnimationLibrary");
            indicatorColor = IM_COL32(168, 85, 247, 255);
        } else {
            const auto kind = GetContentCatalogueIconKindUVE(item);
            iconId = m_uiAssets.GetObjectIconTextureIdUVE(kind);
            indicatorColor = GetSceneObjectIndicatorColorUVE(kind);
        }
        DrawSlotIndicatorIconUVE(iconId, indicatorColor);
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
        // "CREATE BASIC ASSET" stays open at the top; every other group is a submenu with its first item's icon.
        for (const std::string_view group : GetContentCatalogueGroupsUVE()) {
            const std::span<const ContentCatalogueItemUVE> items = GetContentCatalogueItemsUVE();
            const auto firstInGroup = std::find_if(items.begin(), items.end(),
                                                   [group](const ContentCatalogueItemUVE& item) { return item.group == group; });
            if (firstInGroup == items.end()) {
                continue;
            }
            if (group == "CREATE BASIC ASSET" || group == "Basic") {
                ImGui::TextDisabled("CREATE BASIC ASSET");
                for (const ContentCatalogueItemUVE& item : items) {
                    if (item.group == group) {
                        drawItem(item, false);
                    }
                }
                ImGui::Separator();
                continue;
            }
            const auto firstKind = GetContentCatalogueIconKindUVE(*firstInGroup);
            DrawSlotIndicatorIconUVE(m_uiAssets.GetObjectIconTextureIdUVE(firstKind), GetCategoryIndicatorColorUVE(group));
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
    if (const ImGuiPayload* const payload = ImGui::AcceptDragDropPayload(kContentItemPayloadUVE);
        payload != nullptr && payload->DataSize > 1) {
        const std::string relativePath(static_cast<const char*>(payload->Data), static_cast<std::size_t>(payload->DataSize - 1));
        const std::filesystem::path p{relativePath};
        if (IsModelSourcePathUVE(p) || p.extension() == ".uvmodel") {
            if (parent == Scene::kInvalidEntityUVE) {
                parent = ResolveNewObjectParentUVE();
            }
            if (PlaceModelSourceUVE(p, parent) == Scene::kInvalidEntityUVE && m_contentStatusMessage.empty()) {
                m_contentStatusMessage = "Could not place " + p.filename().string() + ".";
            }
        }
    }
    ImGui::EndDragDropTarget();
}

void EditorUVE::DrawFilesystemContextPopupUVE() {
    constexpr const char* kPopupId = "##content-item-menu";
    const bool justOpened = m_filesystemContextVisible;
    if (justOpened) {
        ImGui::OpenPopup(kPopupId);
        m_filesystemContextVisible = false;
        m_contentItemMenuFrames = 0;
    }
    // Only while it is open: a SetNextWindow* call with no popup to take it would land on the
    // next window drawn.
    if (ImGui::IsPopupOpen(kPopupId)) {
        PlaceContentMenuUVE(justOpened, m_contentItemMenuAnchorX, m_contentItemMenuAnchorY, m_contentItemMenuFrames,
                            m_contentItemMenuX, m_contentItemMenuY);
    }
    if (!ImGui::BeginPopup(kPopupId)) {
        return;
    }
    SettleContentMenuUVE(m_contentItemMenuFrames, m_contentItemMenuX, m_contentItemMenuY);
    if (!m_filesystemContextEntry.has_value()) {
        ImGui::EndPopup();
        return;
    }

    const Asset::ProjectFileEntryUVE contextEntry = *m_filesystemContextEntry;
    const std::filesystem::path contentRoot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    const std::filesystem::path absolute = contentRoot / contextEntry.relativePath;
    const bool directory = contextEntry.kind == Asset::ProjectFileEntryKindUVE::Directory;
    const bool entityAsset = !directory && IsEntityAssetPathUVE(contextEntry.relativePath);
    const bool isEntity = entityAsset && LowerExtensionUVE(contextEntry.relativePath) == ".uventity";
    const bool allowed = IsAuthoringCommandAllowedUVE();
    const auto refresh = [this]() {
        m_projectFileSnapshotInitialized = false;
        RefreshProjectFileIndexUVE();
    };

    DrawMenuDragStripUVE(contextEntry.relativePath.filename().generic_string().c_str(), m_contentItemMenuX,
                         m_contentItemMenuY);
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
            ImGui::SetTooltip("Adds it under the selected object, or to the scene. You can also drag it into the "
                              "Outliner or the viewport.");
        }
    }
    if (isEntity) {
        ImGui::BeginDisabled(!allowed);
        const bool openTree = ImGui::MenuItem("Open Tree");
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Edit it on its own in the Entity Editor window (also: double-click)");
        }
        if (openTree && !OpenEntityEditorUVE(absolute)) {
            m_contentStatusMessage = "Could not open " + contextEntry.relativePath.filename().string() + " in the Entity Editor.";
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
    std::string extLower = contextEntry.relativePath.extension().string();
    std::ranges::transform(extLower, extLower.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const bool isScript = extLower == ".uvs";
    const bool isShader = extLower == ".uvshader";
    if (isScript) {
        ImGui::BeginDisabled(!allowed);
        if (ImGui::MenuItem("Edit Script")) {
            static_cast<void>(OpenUVScriptPathUVE(absolute));
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Open this UVScript in the Script Editor (also: double-click)");
        }
    }
    if (isShader) {
        if (ImGui::MenuItem("Edit Shader")) {
            m_selectedProjectFile = contextEntry;
            m_selectedAsset.reset();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Inspect and edit shader source code in the Inspector (also: double-click)");
        }
    }
    if (directory && ImGui::MenuItem("Open")) {
        m_contentBrowserDirectory = contextEntry.relativePath;
        m_contentBrowserShelf.clear();
        m_selectedProjectFile = contextEntry;
        m_selectedAsset.reset();
    }

    // A model source is imported automatically; this puts it on the selected object's mesh. The
    // converted mesh must exist first - naming one that is still importing would leave the object
    // pointing at nothing.
    const std::filesystem::path importedModel =
        IsModelSourcePathUVE(contextEntry.relativePath) ? GetImportedModelPathUVE(contextEntry.relativePath)
                                                        : std::filesystem::path{};
    std::error_code importedError;
    const bool modelReady = !importedModel.empty() && std::filesystem::is_regular_file(importedModel, importedError);
    const bool uvmodel = contextEntry.registeredAssetGuid.has_value() &&
                          contextEntry.relativePath.extension().string() == ".uvmodel";
    const EditorModelSourceInfoUVE* const sourceInfo =
        IsModelSourcePathUVE(contextEntry.relativePath) ? FindModelSourceInfoUVE(contextEntry.relativePath) : nullptr;
    const bool placeable = modelReady || (sourceInfo != nullptr && sourceInfo->hasSkeleton);
    if (placeable) {
        ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
        if (ImGui::MenuItem("Place in Scene")) {
            if (PlaceModelSourceUVE(contextEntry.relativePath) == Scene::kInvalidEntityUVE &&
                m_contentStatusMessage.empty()) {
                m_contentStatusMessage = "Could not place " + contextEntry.relativePath.filename().string() + ".";
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(sourceInfo != nullptr && sourceInfo->hasSkeleton
                                  ? "Adds it with its skeleton and an AnimationSequencer playing its first take."
                                  : "Adds it as a MeshInstance3D.");
        }
    }
    if (modelReady || uvmodel) {
        const bool canAssign = IsDocumentEntityUVE(m_selectedEntity) && IsAuthoringCommandAllowedUVE() &&
                               m_services->GetEntityManagerUVE().HasComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity);
        ImGui::BeginDisabled(!canAssign);
        if (ImGui::MenuItem("Use as Mesh on selected object")) {
            Scene::MeshComponentUVE mesh =
                m_services->GetEntityManagerUVE().GetComponentUVE<Scene::MeshComponentUVE>(m_selectedEntity);
            mesh.meshGuid = modelReady ? RegisterImportedModelArtifactsUVE(importedModel)
                                       : *contextEntry.registeredAssetGuid;
            static_cast<void>(SetSelectedSceneComponentUVE(EditorSceneComponentKindUVE::Mesh, mesh));
        }
        ImGui::EndDisabled();
    }
    const bool isUvanim = extLower == ".uvanim";
    const bool canRetargetItem = isUvanim || uvmodel || (sourceInfo != nullptr && (sourceInfo->rigged || sourceInfo->hasSkeleton));
    if (canRetargetItem) {
        ImGui::BeginDisabled(!IsAuthoringCommandAllowedUVE());
        if (ImGui::MenuItem("Retarget...")) {
            if (isUvanim) {
                OpenRetargetWindowUVE({absolute}, {});
            } else {
                OpenRetargetWindowUVE({}, contextEntry.relativePath);
            }
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip("Conform skeleton and animations to the UniVex humanoid (auto-generating any missing bones).");
        }
    }
    if (entityAsset || directory || modelReady || uvmodel || placeable || canRetargetItem || isScript || isShader) {
        ImGui::Separator();
    }

    const bool pinned = IsProjectPathFavoritedUVE(contextEntry.relativePath);
    if (ImGui::MenuItem(pinned ? "Unpin" : "Pin")) {
        ToggleProjectPathFavoriteUVE(contextEntry.relativePath);
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::SetTooltip("Pinned files and folders are listed at the top of the Content sidebar.");
    }
    // Shelves: tick to put it on a shelf or take it off; a new shelf starts with it on.
    if (ImGui::BeginMenu("Shelves")) {
        for (const ContentShelfUVE& shelf : m_contentShelves.GetAllUVE()) {
            const bool onShelf = m_contentShelves.ContainsUVE(shelf.name, contextEntry.relativePath);
            if (ImGui::MenuItem(shelf.name.c_str(), nullptr, onShelf)) {
                const std::string name = shelf.name; // the item call below may not outlive the span
                if (onShelf) {
                    static_cast<void>(m_contentShelves.RemoveItemUVE(name, contextEntry.relativePath));
                } else if (!m_contentShelves.AddItemUVE(name, contextEntry.relativePath)) {
                    m_contentStatusMessage = "The shelf \"" + name + "\" is full.";
                }
                static_cast<void>(SaveSharedShelvesUVE());
                break;
            }
        }
        if (!m_contentShelves.GetAllUVE().empty()) {
            ImGui::Separator();
        }
        if (ImGui::MenuItem("New Shelf with This")) {
            const std::string name = m_contentShelves.CreateUVE("Shelf");
            if (name.empty()) {
                m_contentStatusMessage = "There are as many shelves as there can be.";
            } else {
                static_cast<void>(m_contentShelves.AddItemUVE(name, contextEntry.relativePath));
                m_contentShelfRenaming = name;
                m_contentShelfRenameText = name;
            }
        }
        ImGui::EndMenu();
    }
    // On an open shelf, taking the file off it is the likely wish; say so directly.
    if (!m_contentBrowserShelf.empty() && m_contentShelves.ContainsUVE(m_contentBrowserShelf, contextEntry.relativePath) &&
        ImGui::MenuItem(("Remove from " + m_contentBrowserShelf).c_str())) {
        static_cast<void>(m_contentShelves.RemoveItemUVE(m_contentBrowserShelf, contextEntry.relativePath));
        static_cast<void>(SaveSharedShelvesUVE());
    }
    if (ImGui::MenuItem("Show in Folder")) {
        // Useful from a shelf, a pin or a search: go to where it lives and select it there.
        m_contentBrowserDirectory = contextEntry.relativePath.parent_path();
        m_contentBrowserShelf.clear();
        m_assetFilter.clear();
        m_selectedProjectFile = directory ? std::optional<Asset::ProjectFileEntryUVE>{} : contextEntry;
        m_selectedAsset.reset();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Rename", "F2")) {
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

bool EditorUVE::PromptImportFilesUVE(const std::filesystem::path& targetDirectory) {
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path destDir = targetDirectory.empty() ? (snapshot.contentRoot / m_contentBrowserDirectory) : targetDirectory;

#if defined(_WIN32)
    HMODULE comdlg = LoadLibraryA("comdlg32.dll");
    if (comdlg != nullptr) {
        const GetOpenFileNameWFn pGetOpenFileNameW = LoadGetOpenFileNameW(comdlg);
        if (pGetOpenFileNameW != nullptr) {
            std::vector<wchar_t> buffer(65536, 0);
            OPENFILENAMEW ofn;
            std::memset(&ofn, 0, sizeof(ofn));
            ofn.lStructSize = sizeof(ofn);
            ofn.lpstrFile = buffer.data();
            ofn.nMaxFile = static_cast<DWORD>(buffer.size());
            ofn.lpstrTitle = L"Select Assets to Import";
            ofn.lpstrFilter =
                L"All Supported Assets\0*.fbx;*.gltf;*.glb;*.obj;*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr;*.wav;*.ogg;*.mp3;*.uvs;*.uvshader;*.glsl\0"
                L"3D Models (*.fbx;*.gltf;*.glb;*.obj)\0*.fbx;*.gltf;*.glb;*.obj\0"
                L"Images & Textures (*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr)\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.hdr\0"
                L"Audio (*.wav;*.ogg;*.mp3)\0*.wav;*.ogg;*.mp3\0"
                L"Scripts & Shaders (*.uvs;*.uvshader;*.glsl)\0*.uvs;*.uvshader;*.glsl\0"
                L"All Files (*.*)\0*.*\0\0";
            ofn.nFilterIndex = 1;
            ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

            if (pGetOpenFileNameW(&ofn)) {
                FreeLibrary(comdlg);
                std::vector<std::filesystem::path> chosenFiles;
                const wchar_t* ptr = buffer.data();
                std::wstring firstString(ptr);
                ptr += firstString.size() + 1U;
                if (*ptr == L'\0') {
                    // Single file selected
                    chosenFiles.push_back(std::filesystem::path(firstString));
                } else {
                    // Multiple files: first string is directory, followed by null-separated filenames
                    std::filesystem::path baseDir(firstString);
                    while (*ptr != L'\0') {
                        std::wstring filename(ptr);
                        chosenFiles.push_back(baseDir / filename);
                        ptr += filename.size() + 1U;
                    }
                }
                return ImportExternalFilesUVE(chosenFiles, destDir);
            }
        }
        FreeLibrary(comdlg);
    }
#endif

    OpenQuickImportModalUVE(destDir);
    return false;
}

bool EditorUVE::ImportExternalFilesUVE(const std::vector<std::filesystem::path>& sourceFiles,
                                      const std::filesystem::path& targetDirectory) {
    if (m_services == nullptr || sourceFiles.empty()) {
        return false;
    }
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path destDir = targetDirectory.empty() ? (snapshot.contentRoot / m_contentBrowserDirectory) : targetDirectory;
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    std::size_t importedCount = 0;
    for (const std::filesystem::path& src : sourceFiles) {
        if (!std::filesystem::exists(src, ec)) {
            continue;
        }
        const std::filesystem::path destFile = destDir / src.filename();
        std::filesystem::copy_file(src, destFile, std::filesystem::copy_options::overwrite_existing, ec);
        if (!ec) {
            ++importedCount;
            m_services->GetAssetDatabaseUVE().RegisterUVE(destFile);
        }
    }

    if (importedCount > 0U) {
        m_projectFileSnapshotInitialized = false;
        RefreshProjectFileIndexUVE();
        const Asset::ProjectFileSnapshotUVE freshSnapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        QueueModelAutoImportsUVE(freshSnapshot);
        m_contentStatusMessage = "Imported " + std::to_string(importedCount) + " asset(s) into " +
                                 (destDir.filename().empty() ? "Content" : destDir.filename().generic_string()) + ".";
        return true;
    }
    return false;
}

void EditorUVE::OpenQuickImportModalUVE(const std::filesystem::path& targetDirectory) {
    m_quickImportModal = QuickImportModalStateUVE{};
    m_quickImportModal.openRequested = true;
    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    m_quickImportModal.targetDirectory = targetDirectory.empty() ? (snapshot.contentRoot / m_contentBrowserDirectory) : targetDirectory;
}

void EditorUVE::DrawQuickImportModalUVE() {
    constexpr const char* kModalId = "Quick Import Assets##uve_quick_import_modal";
    if (m_quickImportModal.openRequested) {
        ImGui::OpenPopup(kModalId);
        m_quickImportModal.openRequested = false;
    }

    const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    ImGui::SetNextWindowSize(ImVec2{520.0F, 0.0F}, ImGuiCond_Appearing);

    constexpr ImGuiWindowFlags modalFlags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;
    if (!ImGui::BeginPopupModal(kModalId, nullptr, modalFlags)) {
        return;
    }

    QuickImportModalStateUVE& modal = m_quickImportModal;

    ImGui::TextColored(ImVec4{0.95F, 0.95F, 0.98F, 1.0F}, "Import Assets into Project");
    ImGui::TextDisabled("Select or specify external 3D models, textures, audio, shaders or scripts.");
    ImGui::Separator();
    ImGui::Spacing();

    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path relDest = modal.targetDirectory.lexically_relative(snapshot.contentRoot);
    const std::string displayDest = (relDest.empty() || *relDest.begin() == "..") ? "Content" : ("Content / " + relDest.generic_string());

    ImGui::Text("Destination Folder: %s", displayDest.c_str());
    ImGui::Spacing();

    // Source path input
    ImGui::TextUnformatted("Source File Path:");
    std::array<char, 1024> pathBuffer{};
    modal.sourcePath.copy(pathBuffer.data(), std::min(modal.sourcePath.size(), pathBuffer.size() - 1U));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0F);
    if (ImGui::InputText("##source_path_input", pathBuffer.data(), pathBuffer.size())) {
        modal.sourcePath = pathBuffer.data();
        modal.statusMessage.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Browse...")) {
#if defined(_WIN32)
        HMODULE comdlg = LoadLibraryA("comdlg32.dll");
        if (comdlg != nullptr) {
            const GetOpenFileNameWFn pGetOpenFileNameW = LoadGetOpenFileNameW(comdlg);
            if (pGetOpenFileNameW != nullptr) {
                wchar_t buf[2048] = {0};
                OPENFILENAMEW ofn;
                std::memset(&ofn, 0, sizeof(ofn));
                ofn.lStructSize = sizeof(ofn);
                ofn.lpstrFile = buf;
                ofn.nMaxFile = sizeof(buf) / sizeof(wchar_t);
                ofn.lpstrTitle = L"Select File to Import";
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
                if (pGetOpenFileNameW(&ofn)) {
                    modal.sourcePath = std::filesystem::path(buf).string();
                }
            }
            FreeLibrary(comdlg);
        }
#endif
    }

    if (!modal.statusMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4{0.95F, 0.45F, 0.40F, 1.0F}, "%s", modal.statusMessage.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool cancel = ImGui::Button("Cancel", ImVec2{110.0F, 28.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110.0F);

    const bool canImport = !modal.sourcePath.empty();
    ImGui::BeginDisabled(!canImport);
    const bool doImport = ImGui::Button("Import", ImVec2{110.0F, 28.0F}) ||
                          (canImport && ImGui::IsKeyPressed(ImGuiKey_Enter));
    ImGui::EndDisabled();

    if (doImport && canImport) {
        std::error_code ec;
        const std::filesystem::path src(modal.sourcePath);
        if (std::filesystem::exists(src, ec) && std::filesystem::is_regular_file(src, ec)) {
            ImportExternalFilesUVE({src}, modal.targetDirectory);
            ImGui::CloseCurrentPopup();
        } else {
            modal.statusMessage = "File does not exist or is not a regular file.";
        }
    } else if (cancel) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}


void EditorUVE::OpenCreateShaderModalUVE(const std::filesystem::path& targetDirectory,
                                        std::function<void(const std::filesystem::path&)> onCreated) {
    m_createShaderModal = CreateShaderModalStateUVE{};
    m_createShaderModal.isOpen = true;
    m_createShaderModal.openRequested = true;
    m_createShaderModal.targetDirectory = targetDirectory.empty() ? m_contentBrowserDirectory : targetDirectory;
    m_createShaderModal.shaderTypeIndex = 0;
    m_createShaderModal.onShaderCreated = std::move(onCreated);

    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path contentRoot = snapshot.contentRoot;
    std::filesystem::path fullDir = m_createShaderModal.targetDirectory;
    if (fullDir.is_relative()) {
        fullDir = contentRoot / fullDir;
    }
    const std::filesystem::path fullDefault = fullDir / "NewShader.uvshader";
    const std::filesystem::path relDefault = fullDefault.lexically_relative(contentRoot);
    m_createShaderModal.shaderPathText = (relDefault.empty() || *relDefault.begin() == "..") ? fullDefault.generic_string() : relDefault.generic_string();
}

void EditorUVE::DrawCreateShaderModalUVE() {
    if (!m_createShaderModal.isOpen && !m_createShaderModal.openRequested) {
        return;
    }

    if (m_createShaderModal.openRequested) {
        m_createShaderModal.isOpen = true;
        m_createShaderModal.openRequested = false;
        const ImGuiViewport* const mainViewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Appearing, ImVec2{0.5F, 0.5F});
    }

    ImGui::SetNextWindowSize(ImVec2{520.0F, 0.0F}, ImGuiCond_Appearing);

    constexpr ImGuiWindowFlags windowFlags =
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse;
    if (!ImGui::Begin("Create / Assign Shader##uve_shader_window", &m_createShaderModal.isOpen, windowFlags)) {
        ImGui::End();
        return;
    }

    CreateShaderModalStateUVE& modal = m_createShaderModal;

    ImGui::TextColored(ImVec4{0.95F, 0.95F, 0.98F, 1.0F}, "Create / Assign Shader");
    ImGui::TextDisabled("Configure shader type, starting template and shader path.");
    ImGui::Separator();
    ImGui::Spacing();

    const Asset::ProjectFileSnapshotUVE snapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    const std::filesystem::path contentRoot = snapshot.contentRoot;

    if (ImGui::BeginTable("##create_shader_table", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 100.0F);
        ImGui::TableSetupColumn("##field", ImGuiTableColumnFlags_WidthStretch);

        // 1. Language
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Language");
        ImGui::TableSetColumnIndex(1);
        ImGui::BeginDisabled();
        ImGui::Button("UVShader / GLSL (.uvshader)", ImVec2{-FLT_MIN, 0.0F});
        ImGui::EndDisabled();

        // 2. Shader Type / Template
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Shader Type");
        ImGui::TableSetColumnIndex(1);
        constexpr const char* kShaderTypes[] = {
            "Spatial / Standard PBR Surface",
            "Unlit / Canvas / UI",
            "Post-Process / Screen Effect",
            "Custom Vertex / Fragment GLSL"
        };
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::Combo("##shader_type_combo", &modal.shaderTypeIndex, kShaderTypes, 4);

        // 3. Path & Rename (Directly editable path with Picker button)
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Path");
        ImGui::TableSetColumnIndex(1);

        const float pickerBtnWidth = 54.0F;
        const float pathWidth = std::max(120.0F, ImGui::GetContentRegionAvail().x - pickerBtnWidth - 6.0F);

        char pathBuf[512];
        std::snprintf(pathBuf, sizeof(pathBuf), "%s", modal.shaderPathText.c_str());
        ImGui::SetNextItemWidth(pathWidth);
        if (ImGui::InputText("##path_input", pathBuf, sizeof(pathBuf))) {
            modal.shaderPathText = pathBuf;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Edit the shader path and filename directly to rename it");
        }

        ImGui::SameLine(0.0F, 6.0F);
        if (ImGui::Button("Pick##shader_picker_btn", ImVec2{pickerBtnWidth, 0.0F})) {
            ImGui::OpenPopup("##shader_path_picker_popup");
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Choose destination folder or select an existing shader from Content");
        }

        ImGui::SetNextWindowSizeConstraints(ImVec2{360.0F, 220.0F}, ImVec2{540.0F, 420.0F});
        if (ImGui::BeginPopup("##shader_path_picker_popup")) {
            ImGui::TextColored(ImVec4{0.95F, 0.95F, 0.98F, 1.0F}, "Select Shader or Folder in Content");
            ImGui::Separator();

            static char s_shaderPickerFilter[64] = {0};
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputTextWithHint("##filter_shaders", "Filter content...", s_shaderPickerFilter, sizeof(s_shaderPickerFilter));

            if (ImGui::BeginTabBar("##shader_picker_tabs")) {
                if (ImGui::BeginTabItem("Shaders (.uvshader)")) {
                    std::size_t shaderCount = 0U;
                    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
                        const std::string ext = entry.relativePath.extension().string();
                        if (ext != ".uvshader" && ext != ".glsl") {
                            continue;
                        }
                        const std::string relStr = entry.relativePath.generic_string();
                        if (s_shaderPickerFilter[0] != '\0') {
                            std::string lowerFilter = s_shaderPickerFilter;
                            std::string lowerItem = relStr;
                            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                            std::transform(lowerItem.begin(), lowerItem.end(), lowerItem.begin(), ::tolower);
                            if (lowerItem.find(lowerFilter) == std::string::npos) {
                                continue;
                            }
                        }
                        ++shaderCount;
                        const bool selected = (modal.shaderPathText == relStr);
                        if (ImGui::Selectable(relStr.c_str(), selected)) {
                            modal.shaderPathText = relStr;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    if (shaderCount == 0U) {
                        ImGui::TextDisabled("No shaders found in project.");
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Folders")) {
                    std::filesystem::path currentInput(modal.shaderPathText);
                    std::string currentFileName = currentInput.filename().generic_string();
                    if (currentFileName.empty() || currentFileName == ".") {
                        currentFileName = "NewShader.uvshader";
                    } else if (!currentFileName.ends_with(".uvshader") && !currentFileName.ends_with(".glsl")) {
                        currentFileName += ".uvshader";
                    }

                    if (ImGui::Selectable("Content (Root)", false)) {
                        modal.shaderPathText = currentFileName;
                        ImGui::CloseCurrentPopup();
                    }
                    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
                        if (entry.kind != Asset::ProjectFileEntryKindUVE::Directory) {
                            continue;
                        }
                        const std::string relStr = entry.relativePath.generic_string();
                        if (s_shaderPickerFilter[0] != '\0') {
                            std::string lowerFilter = s_shaderPickerFilter;
                            std::string lowerItem = relStr;
                            std::transform(lowerFilter.begin(), lowerFilter.end(), lowerFilter.begin(), ::tolower);
                            std::transform(lowerItem.begin(), lowerItem.end(), lowerItem.begin(), ::tolower);
                            if (lowerItem.find(lowerFilter) == std::string::npos) {
                                continue;
                            }
                        }
                        if (ImGui::Selectable(relStr.c_str(), false)) {
                            modal.shaderPathText = (entry.relativePath / currentFileName).generic_string();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
            ImGui::EndPopup();
        }

        ImGui::EndTable();
    }

    // Resolve path & filename
    std::string text = modal.shaderPathText;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }

    std::filesystem::path inputPath(text.empty() ? "NewShader.uvshader" : text);
    if (!inputPath.filename().generic_string().ends_with(".uvshader") &&
        !inputPath.filename().generic_string().ends_with(".glsl")) {
        inputPath += ".uvshader";
    }
    const std::filesystem::path fullPath = inputPath.is_relative() ? (contentRoot / inputPath) : inputPath;
    const std::string stem = fullPath.stem().string();

    std::string problem;
    bool isExistingFile = false;
    if (stem.empty()) {
        problem = "Shader name cannot be empty.";
    } else {
        std::error_code ec;
        if (std::filesystem::exists(fullPath, ec) && std::filesystem::is_regular_file(fullPath, ec)) {
            isExistingFile = true;
        }
    }

    if (!problem.empty()) {
        ImGui::TextColored(ImVec4{0.95F, 0.45F, 0.40F, 1.0F}, "%s", problem.c_str());
    } else if (isExistingFile) {
        ImGui::TextColored(ImVec4{0.45F, 0.75F, 0.95F, 1.0F}, "Existing shader found in Content. Ready to assign and open.");
    } else {
        ImGui::TextColored(ImVec4{0.45F, 0.80F, 0.55F, 1.0F}, "Ready to create.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Buttons: Cancel & Action
    const bool cancel = ImGui::Button("Cancel", ImVec2{110.0F, 28.0F}) || ImGui::IsKeyPressed(ImGuiKey_Escape);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130.0F);

    const char* actionLabel = isExistingFile ? "Assign / Open" : "Create";
    ImGui::BeginDisabled(!problem.empty());
    const bool doAction = ImGui::Button(actionLabel, ImVec2{130.0F, 28.0F}) ||
                          (problem.empty() && ImGui::IsKeyPressed(ImGuiKey_Enter));
    ImGui::EndDisabled();

    if (doAction && problem.empty()) {
        if (isExistingFile) {
            const Asset::ProjectFileSnapshotUVE projSnapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            const std::filesystem::path rel = fullPath.lexically_relative(projSnapshot.contentRoot);
            m_selectedProjectFile = Asset::ProjectFileEntryUVE{rel, Asset::ProjectFileEntryKindUVE::File, std::nullopt};
            m_selectedAsset.reset();
            m_contentStatusMessage = "Selected shader " + fullPath.filename().string();

            if (modal.onShaderCreated != nullptr) {
                modal.onShaderCreated(fullPath);
            }
            modal.isOpen = false;
        } else {
            std::error_code ec;
            std::filesystem::create_directories(fullPath.parent_path(), ec);

            Asset::ShaderAssetUVE shader{};
            shader.stage = Asset::ShaderStageKindUVE::Fragment;
            shader.entryPointName = "main";

            if (modal.shaderTypeIndex == 0) {
                // Spatial PBR Surface
                shader.sourceCode =
                    "// UniVex Spatial PBR Surface Shader\n"
                    "#version 450\n\n"
                    "layout(location = 0) in vec3 v_Position;\n"
                    "layout(location = 1) in vec3 v_Normal;\n"
                    "layout(location = 2) in vec2 v_TexCoord;\n\n"
                    "layout(location = 0) out vec4 outColor;\n\n"
                    "layout(binding = 0) uniform sampler2D u_AlbedoMap;\n"
                    "layout(binding = 1) uniform sampler2D u_NormalMap;\n\n"
                    "void main() {\n"
                    "    vec4 albedo = texture(u_AlbedoMap, v_TexCoord);\n"
                    "    vec3 N = normalize(v_Normal);\n"
                    "    outColor = vec4(albedo.rgb, 1.0);\n"
                    "}\n";
            } else if (modal.shaderTypeIndex == 1) {
                // Unlit Canvas / UI
                shader.sourceCode =
                    "// UniVex Unlit Canvas/UI Shader\n"
                    "#version 450\n\n"
                    "layout(location = 0) in vec2 v_TexCoord;\n"
                    "layout(location = 1) in vec4 v_Color;\n\n"
                    "layout(location = 0) out vec4 outColor;\n\n"
                    "layout(binding = 0) uniform sampler2D u_Texture;\n\n"
                    "void main() {\n"
                    "    outColor = texture(u_Texture, v_TexCoord) * v_Color;\n"
                    "}\n";
            } else if (modal.shaderTypeIndex == 2) {
                // Post-Process / Screen Effect
                shader.sourceCode =
                    "// UniVex Post-Processing Quad Shader\n"
                    "#version 450\n\n"
                    "layout(location = 0) in vec2 v_TexCoord;\n\n"
                    "layout(location = 0) out vec4 outColor;\n\n"
                    "layout(binding = 0) uniform sampler2D u_SceneColor;\n\n"
                    "void main() {\n"
                    "    vec3 scene = texture(u_SceneColor, v_TexCoord).rgb;\n"
                    "    outColor = vec4(scene, 1.0);\n"
                    "}\n";
            } else {
                // Custom GLSL
                shader.sourceCode =
                    "// UniVex Custom Shader\n"
                    "#version 450\n\n"
                    "layout(location = 0) in vec2 v_TexCoord;\n\n"
                    "layout(location = 0) out vec4 outColor;\n\n"
                    "void main() {\n"
                    "    outColor = vec4(1.0, 1.0, 1.0, 1.0);\n"
                    "}\n";
            }

            if (Asset::SaveShaderAssetUVE(shader, fullPath)) {
                m_services->GetAssetDatabaseUVE().RegisterUVE(fullPath);
                m_projectFileSnapshotInitialized = false;
                RefreshProjectFileIndexUVE();

                const Asset::ProjectFileSnapshotUVE projSnapshot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
                const std::filesystem::path rel = fullPath.lexically_relative(projSnapshot.contentRoot);
                m_selectedProjectFile = Asset::ProjectFileEntryUVE{rel, Asset::ProjectFileEntryKindUVE::File, std::nullopt};
                m_selectedAsset.reset();
                m_contentStatusMessage = "Created shader " + fullPath.filename().string();

                if (modal.onShaderCreated != nullptr) {
                    modal.onShaderCreated(fullPath);
                }
            } else {
                m_contentStatusMessage = "Could not create shader file " + fullPath.filename().string();
            }
            modal.isOpen = false;
        }
    } else if (cancel) {
        modal.isOpen = false;
    }

    ImGui::End();
}

} // namespace UVE::Editor
