// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_document_uve.h"

#include <system_error>

namespace UVE::Config {

bool SettingsDocumentUVE::LoadUVE(const std::filesystem::path& path) {
    m_path = path;
    m_dirty = false;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        // A project that never changed a setting has no file yet; that is every default, not a
        // failure worth the store's missing-file warning.
        return true;
    }
    return m_store.LoadUVE(path);
}

bool SettingsDocumentUVE::SaveUVE() {
    if (m_path.empty() || !m_store.SaveUVE(m_path)) {
        return false;
    }
    m_dirty = false;
    return true;
}

std::optional<SettingValueUVE> SettingsDocumentUVE::GetValueUVE(const std::string_view id) const {
    return m_registry.GetValueUVE(m_store, id);
}

std::optional<SettingValueUVE> SettingsDocumentUVE::GetStoredValueUVE(const std::string_view id) const {
    return m_registry.GetStoredValueUVE(m_store, id);
}

bool SettingsDocumentUVE::SetValueUVE(const std::string_view id, const SettingValueUVE& value) {
    const SettingDescriptorUVE* descriptor = m_registry.FindUVE(id);
    if (descriptor == nullptr || !IsSettingValueValidUVE(*descriptor, value)) {
        return false;
    }
    if (m_registry.GetStoredValueUVE(m_store, id) == value) {
        return true;
    }
    if (value == descriptor->defaultValue) {
        // Also clears an illegal leftover, which the comparison above could not see as equal.
        m_dirty = m_registry.ClearValueUVE(m_store, id) || m_dirty;
        return true;
    }
    if (!m_registry.SetValueUVE(m_store, id, value)) {
        return false;
    }
    m_dirty = true;
    return true;
}

bool SettingsDocumentUVE::ResetUVE(const std::string_view id) {
    const bool removed = m_registry.ClearValueUVE(m_store, id);
    m_dirty = removed || m_dirty;
    return removed;
}

bool SettingsDocumentUVE::IsModifiedUVE(const std::string_view id) const {
    return m_registry.IsModifiedUVE(m_store, id);
}

} // namespace UVE::Config
