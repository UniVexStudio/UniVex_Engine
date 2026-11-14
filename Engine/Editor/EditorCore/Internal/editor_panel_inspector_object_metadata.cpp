// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The object's Metadata in the Inspector: one row per typed entry, and "Add Metadata", which opens a
// small floating form - name, type, starting value - rather than adding an unnamed placeholder the
// author then has to fix up.
//
// Every change, whatever it is, rewrites the entry list once through the metadata component's
// declared property. That single path is what makes each edit one undo step, and what keeps the
// rules (identifier keys, bounded values) in one place.

#include "editor.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <typeindex>
#include <utility>
#include <vector>

#include "object_metadata_component.h"
#include "i_entity_manager.h"

namespace UVE::Editor {
namespace {

using Core::VariantTypeUVE;
using Core::VariantUVE;

struct MetadataPropertyUVE final {
    const Core::TypeMetadataEntryUVE* entry = nullptr;
    const Core::TypeMetadataPropertyUVE* property = nullptr;
};

[[nodiscard]] MetadataPropertyUVE FindMetadataPropertyUVE() {
    const Core::TypeMetadataEntryUVE* const entry =
        Scene::FindSceneComponentMetadataUVE(std::type_index(typeid(Scene::ObjectMetadataComponentUVE)));
    if (entry == nullptr || entry->properties.empty()) {
        return {};
    }
    return {entry, &entry->properties.front()};
}

}

bool EditorUVE::CommitSelectedObjectMetadataUVE(std::vector<Scene::ObjectMetadataEntryUVE> entries, const bool preview) {
    Scene::ObjectMetadataComponentUVE candidate{std::move(entries)};
    if (!Scene::IsObjectMetadataComponentValidUVE(candidate)) {
        return false;
    }
    const MetadataPropertyUVE target = FindMetadataPropertyUVE();
    if (target.entry == nullptr) {
        return false;
    }
    return preview ? PreviewSelectedComponentPropertyUVE(*target.entry, *target.property, &candidate.entries)
                   : SetSelectedComponentPropertyUVE(*target.entry, *target.property, &candidate.entries);
}

namespace {

[[nodiscard]] const Scene::ObjectMetadataComponentUVE* SelectedMetadataUVE(Scene::IEntityManagerUVE& entityManager,
                                                                         const Scene::EntityUVE entity) {
    return entityManager.HasComponentUVE<Scene::ObjectMetadataComponentUVE>(entity)
               ? &entityManager.GetComponentUVE<Scene::ObjectMetadataComponentUVE>(entity)
               : nullptr;
}

} // namespace

bool EditorUVE::AddSelectedObjectMetadataUVE(const std::string& key, const VariantUVE& value) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::ObjectMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr || Scene::ValidateObjectMetadataKeyUVE(key, *metadata) != Scene::ObjectMetadataKeyIssueUVE::None) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntryUVE> entries = metadata->entries;
    entries.push_back({key, value});
    return CommitSelectedObjectMetadataUVE(std::move(entries));
}

bool EditorUVE::SetSelectedObjectMetadataValueUVE(const std::string& key, const VariantUVE& value) {
    return WriteSelectedObjectMetadataValueUVE(key, value, false);
}

bool EditorUVE::PreviewSelectedObjectMetadataValueUVE(const std::string& key, const VariantUVE& value) {
    return WriteSelectedObjectMetadataValueUVE(key, value, true);
}

bool EditorUVE::WriteSelectedObjectMetadataValueUVE(const std::string& key, const VariantUVE& value,
                                                  const bool preview) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::ObjectMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntryUVE> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    found->value = value;
    return CommitSelectedObjectMetadataUVE(std::move(entries), preview);
}

bool EditorUVE::RenameSelectedObjectMetadataUVE(const std::string& key, const std::string& newKey) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::ObjectMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr ||
        Scene::ValidateObjectMetadataKeyUVE(newKey, *metadata, key) != Scene::ObjectMetadataKeyIssueUVE::None) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntryUVE> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    found->key = newKey;
    return CommitSelectedObjectMetadataUVE(std::move(entries));
}

bool EditorUVE::ChangeSelectedObjectMetadataTypeUVE(const std::string& key, const VariantTypeUVE type,
                                                  const bool allowLoss) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::ObjectMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntryUVE> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    const std::optional<Core::VariantConversionUVE> converted = Core::TryConvertVariantUVE(found->value, type);
    if (!converted.has_value() || (!converted->lossless && !allowLoss)) {
        return false;
    }
    found->value = converted->value;
    return CommitSelectedObjectMetadataUVE(std::move(entries));
}

bool EditorUVE::RemoveSelectedObjectMetadataUVE(const std::string& key) {
    if (!HasSingleDocumentSelectionUVE()) {
        return false;
    }
    const Scene::ObjectMetadataComponentUVE* const metadata =
        SelectedMetadataUVE(m_services->GetEntityManagerUVE(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntryUVE> entries = metadata->entries;
    const auto removed = std::remove_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (removed == entries.end()) {
        return false;
    }
    entries.erase(removed, entries.end());
    return CommitSelectedObjectMetadataUVE(std::move(entries));
}

} // namespace UVE::Editor
