// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
// Entity Class: Content "+ Add" -> Entity -> "Entity Class..." asks first and writes second, like
// The standard Pick Parent Class. The PRISM window (shell/blueprint/editor_entity_class_window) lists
// every body an entity can start as; Create makes that body's `.uventity`, selects it in Content and
// opens it in the Entity Editor. Nothing is written until Create.
//
// The list is the content catalogue's (GetContentCatalogueEntityClassesUVE), not a second menu.
#include "editor.h"

#include "editor_content_catalogue.h"
#include "i_project_file_index.h"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace UVE::Editor {

void EditorUVE::OpenEntityClassPickerUVE(std::filesystem::path directory) {
    EntityClassPickerStateUVE montage;
    montage.directory = std::move(directory);
    // The first body is picked for the author rather than opening on an empty pane: the window is a
    // choice, and a choice with nothing chosen reads as broken.
    const std::span<const ContentCatalogueEntityClassUVE> entries = GetContentCatalogueEntityClassesUVE();
    if (!entries.empty()) {
        montage.pickedItemId = std::string{entries.front().itemId};
    }
    m_entityClassPicker = std::move(montage);
}

void EditorUVE::CloseEntityClassPickerUVE() {
    m_entityClassPicker.reset();
}

bool EditorUVE::CreateEntityClassUVE(const std::string_view itemId) {
    if (!m_entityClassPicker.has_value()) {
        return false;
    }
    EntityClassPickerStateUVE& montage = *m_entityClassPicker;
    const ContentCatalogueItemUVE* const item = FindContentCatalogueItemUVE(itemId);
    if (item == nullptr || item->action != ContentCatalogueActionUVE::EntityAsset ||
        !IsContentCatalogueEntityClassUVE(itemId)) {
        montage.status = "That is not an entity class.";
        montage.statusIsError = true;
        return false;
    }
    if (!IsAuthoringCommandAllowedUVE()) {
        montage.status = "Stop Play to make an entity.";
        montage.statusIsError = true;
        return false;
    }
    // An entity is edited alone: making one only to find the editor busy would leave a file behind
    // with nothing open on it, so the refusal comes before the file.
    if (m_entityDocumentActive) {
        montage.status = "Close " + m_entityEditSession->assetPath.filename().string() +
                         " first: an entity is edited on its own.";
        montage.statusIsError = true;
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(montage.directory, error);
    const std::optional<std::filesystem::path> created = CreateContentCatalogueItemUVE(itemId, montage.directory);
    if (!created.has_value()) {
        montage.status = "Could not create " + std::string{item->label} + ".";
        montage.statusIsError = true;
        return false;
    }

    // Made here, so it counts as recently used and Content shows it selected - the same footing as a
    // body made straight from "+ Add".
    PushContentCreateRecentUVE(m_contentCreateRecent, itemId);
    m_projectFileSnapshotInitialized = false;
    RefreshProjectFileIndexUVE();
    const std::filesystem::path contentRoot = m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
    const std::filesystem::path relative = created->lexically_relative(contentRoot);
    if (!relative.empty() && !relative.generic_string().starts_with("..")) {
        m_selectedProjectFile =
            Asset::ProjectFileEntryUVE{relative, Asset::ProjectFileEntryKindUVE::File, std::nullopt};
        m_selectedAsset.reset();
    }

    const std::filesystem::path body = *created;
    // Closed first: the body exists now, and the Entity Editor opens on it.
    m_entityClassPicker.reset();
    if (!OpenEntityEditorUVE(body)) {
        m_contentStatusMessage = "Made " + body.filename().string() + " but could not open it in the Entity Editor.";
        return false;
    }
    m_contentStatusMessage.clear();
    return true;
}

} // namespace UVE::Editor
