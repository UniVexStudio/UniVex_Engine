// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <filesystem>
#include <optional>
#include <string_view>

#include "uve/config/config_manager_uve.h"
#include "uve/config/settings_registry_uve.h"

namespace UVE::Config {

/// One settings file and the settings that may appear in it - the project settings, for one.
///
/// The file holds only what differs from the defaults: setting a value equal to its default
/// removes it instead. The file stays short enough to read, diffs only where a person changed
/// something, and a default changed in a later engine version reaches every project that never
/// overrode it. Keys are written in sorted order, so saving without a change reproduces the file.
///
/// Not thread-safe: register at startup; read and write from one thread afterwards.
class SettingsDocumentUVE final {
public:
    [[nodiscard]] SettingsRegistryUVE& GetRegistryUVE() noexcept { return m_registry; }
    [[nodiscard]] const SettingsRegistryUVE& GetRegistryUVE() const noexcept { return m_registry; }
    [[nodiscard]] const IConfigManagerUVE& GetStoreUVE() const noexcept { return m_store; }

    /// Reads `path`, which later saves write back to. A missing file is an empty document - every
    /// setting at its default - and still returns true; a file that does not parse leaves the
    /// document as it was and returns false.
    bool LoadUVE(const std::filesystem::path& path);
    /// Writes the document to the path it was loaded from. Clears IsDirtyUVE on success.
    bool SaveUVE();
    [[nodiscard]] const std::filesystem::path& GetPathUVE() const noexcept { return m_path; }
    /// Whether a change has been made since the last load or save.
    [[nodiscard]] bool IsDirtyUVE() const noexcept { return m_dirty; }

    /// The value of setting `id`: the one in the file, or the default. Nothing for an unknown id.
    [[nodiscard]] std::optional<SettingValueUVE> GetValueUVE(std::string_view id) const;
    /// The value the file sets for `id`, when it sets a legal one.
    [[nodiscard]] std::optional<SettingValueUVE> GetStoredValueUVE(std::string_view id) const;
    /// Sets `id` to `value`; a value equal to the default is removed from the file instead.
    /// Refused, changing nothing, for an unknown id or an illegal value.
    [[nodiscard]] bool SetValueUVE(std::string_view id, const SettingValueUVE& value);
    /// Removes `id` from the file, so it reads as its default.
    bool ResetUVE(std::string_view id);
    [[nodiscard]] bool IsModifiedUVE(std::string_view id) const;

private:
    SettingsRegistryUVE m_registry;
    ConfigManagerUVE m_store;
    std::filesystem::path m_path;
    bool m_dirty = false;
};

} // namespace UVE::Config
