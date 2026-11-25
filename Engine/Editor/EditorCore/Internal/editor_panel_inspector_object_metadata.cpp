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

using Core::VariantType;
using Core::Variant;

struct MetadataProperty final {
    const Core::TypeMetadataEntry* entry = nullptr;
    const Core::TypeMetadataProperty* property = nullptr;
};

[[nodiscard]] MetadataProperty FindMetadataProperty() {
    const Core::TypeMetadataEntry* const entry =
        Scene::FindSceneComponentMetadata(std::type_index(typeid(Scene::ObjectMetadataComponent)));
    if (entry == nullptr || entry->properties.empty()) {
        return {};
    }
    return {entry, &entry->properties.front()};
}

}

bool Editor::CommitSelectedObjectMetadata(std::vector<Scene::ObjectMetadataEntry> entries, const bool preview) {
    Scene::ObjectMetadataComponent candidate{std::move(entries)};
    if (!Scene::IsObjectMetadataComponentValid(candidate)) {
        return false;
    }
    const MetadataProperty target = FindMetadataProperty();
    if (target.entry == nullptr) {
        return false;
    }
    return preview ? PreviewSelectedComponentProperty(*target.entry, *target.property, &candidate.entries)
                   : SetSelectedComponentProperty(*target.entry, *target.property, &candidate.entries);
}

namespace {

[[nodiscard]] const Scene::ObjectMetadataComponent* SelectedMetadata(Scene::IEntityManager& entityManager,
                                                                         const Scene::Entity entity) {
    return entityManager.HasComponent<Scene::ObjectMetadataComponent>(entity)
               ? &entityManager.GetComponent<Scene::ObjectMetadataComponent>(entity)
               : nullptr;
}

} // namespace

bool Editor::AddSelectedObjectMetadata(const std::string& key, const Variant& value) {
    if (!HasSingleDocumentSelection()) {
        return false;
    }
    const Scene::ObjectMetadataComponent* const metadata =
        SelectedMetadata(m_services->GetEntityManager(), m_selectedEntity);
    if (metadata == nullptr || Scene::ValidateObjectMetadataKey(key, *metadata) != Scene::ObjectMetadataKeyIssue::None) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntry> entries = metadata->entries;
    entries.push_back({key, value});
    return CommitSelectedObjectMetadata(std::move(entries));
}

bool Editor::SetSelectedObjectMetadataValue(const std::string& key, const Variant& value) {
    return WriteSelectedObjectMetadataValue(key, value, false);
}

bool Editor::PreviewSelectedObjectMetadataValue(const std::string& key, const Variant& value) {
    return WriteSelectedObjectMetadataValue(key, value, true);
}

bool Editor::WriteSelectedObjectMetadataValue(const std::string& key, const Variant& value,
                                                  const bool preview) {
    if (!HasSingleDocumentSelection()) {
        return false;
    }
    const Scene::ObjectMetadataComponent* const metadata =
        SelectedMetadata(m_services->GetEntityManager(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntry> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    found->value = value;
    return CommitSelectedObjectMetadata(std::move(entries), preview);
}

bool Editor::RenameSelectedObjectMetadata(const std::string& key, const std::string& newKey) {
    if (!HasSingleDocumentSelection()) {
        return false;
    }
    const Scene::ObjectMetadataComponent* const metadata =
        SelectedMetadata(m_services->GetEntityManager(), m_selectedEntity);
    if (metadata == nullptr ||
        Scene::ValidateObjectMetadataKey(newKey, *metadata, key) != Scene::ObjectMetadataKeyIssue::None) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntry> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    found->key = newKey;
    return CommitSelectedObjectMetadata(std::move(entries));
}

bool Editor::ChangeSelectedObjectMetadataType(const std::string& key, const VariantType type,
                                                  const bool allowLoss) {
    if (!HasSingleDocumentSelection()) {
        return false;
    }
    const Scene::ObjectMetadataComponent* const metadata =
        SelectedMetadata(m_services->GetEntityManager(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntry> entries = metadata->entries;
    const auto found = std::find_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (found == entries.end()) {
        return false;
    }
    const std::optional<Core::VariantConversion> converted = Core::TryConvertVariant(found->value, type);
    if (!converted.has_value() || (!converted->lossless && !allowLoss)) {
        return false;
    }
    found->value = converted->value;
    return CommitSelectedObjectMetadata(std::move(entries));
}

bool Editor::RemoveSelectedObjectMetadata(const std::string& key) {
    if (!HasSingleDocumentSelection()) {
        return false;
    }
    const Scene::ObjectMetadataComponent* const metadata =
        SelectedMetadata(m_services->GetEntityManager(), m_selectedEntity);
    if (metadata == nullptr) {
        return false;
    }
    std::vector<Scene::ObjectMetadataEntry> entries = metadata->entries;
    const auto removed = std::remove_if(entries.begin(), entries.end(), [&key](const auto& entry) { return entry.key == key; });
    if (removed == entries.end()) {
        return false;
    }
    entries.erase(removed, entries.end());
    return CommitSelectedObjectMetadata(std::move(entries));
}

} // namespace UVE::Editor
