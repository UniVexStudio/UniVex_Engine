// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/node_metadata_component_uve.h"

#include <algorithm>

namespace UVE::Scene {

bool IsNodeMetadataComponentValidUVE(const NodeMetadataComponentUVE& component) noexcept {
    if (component.entries.size() > kMaximumNodeMetadataEntriesUVE) {
        return false;
    }
    for (std::size_t index = 0; index < component.entries.size(); ++index) {
        const NodeMetadataEntryUVE& entry = component.entries[index];
        if (entry.key.empty() || entry.key.size() > kMaximumNodeMetadataKeyBytesUVE ||
            entry.value.size() > kMaximumNodeMetadataValueBytesUVE) {
            return false;
        }
        // A duplicate key would make FindNodeMetadataUVE's answer depend on which entry happens to
        // come first, which is a bug that only appears once someone reorders the list.
        const auto duplicate = std::find_if(component.entries.cbegin() + static_cast<std::ptrdiff_t>(index) + 1,
                                            component.entries.cend(),
                                            [&entry](const NodeMetadataEntryUVE& other) {
                                                return other.key == entry.key;
                                            });
        if (duplicate != component.entries.cend()) {
            return false;
        }
    }
    return true;
}

const std::string* FindNodeMetadataUVE(const NodeMetadataComponentUVE& component,
                                       const std::string_view key) noexcept {
    const auto iterator = std::find_if(component.entries.cbegin(), component.entries.cend(),
                                       [key](const NodeMetadataEntryUVE& entry) { return entry.key == key; });
    return iterator == component.entries.cend() ? nullptr : &iterator->value;
}

} // namespace UVE::Scene
