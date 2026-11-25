// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string_view>

#include "config_manager.h"
#include "settings_observer.h"
#include "settings_registry.h"

namespace UVE::Config {

/// A forward migration from document version N to N + 1. It edits the candidate store and returns
/// false to reject the load without replacing the document's current in-memory state.
using SettingsMigrationFunction = std::function<bool(IConfigManager&)>;

/// One settings file and the settings that may appear in it - the project settings, for one.
///
/// The file holds only what differs from the defaults, plus the reserved root-level `version`:
/// setting a value equal to its default removes it instead. The version is advanced through
/// registered forward migrations on load; a default changed in a later engine version reaches every
/// document that never overrode it. Keys are written in sorted order, so saving without a change
/// reproduces the file.
///
/// Not thread-safe: register at startup; read and write from one thread afterwards. Observer
/// callbacks run synchronously on that same calling thread, after a successful load or mutation has
/// committed. Subscribe/unsubscribe and callbacks must therefore also stay on the document's owner
/// thread. A mutation made inside a callback dispatches its own notification immediately (nested),
/// so consumers must avoid feedback loops. Callbacks should not throw; if one does, the setting
/// change is already committed and the exception propagates to the caller.
class SettingsDocument final {
public:
    [[nodiscard]] SettingsRegistry& GetRegistry() noexcept { return m_registry; }
    [[nodiscard]] const SettingsRegistry& GetRegistry() const noexcept { return m_registry; }
    [[nodiscard]] const IConfigManager& GetStore() const noexcept { return m_store; }

    /// The schema version written at the root-level `version` key. Documents start at version 1;
    /// advance this before loading when the schema grows. Versions must move forward and migration
    /// configuration is frozen after the first successful load.
    [[nodiscard]] std::uint32_t GetCurrentVersion() const noexcept { return m_currentVersion; }
    [[nodiscard]] bool SetCurrentVersion(std::uint32_t version) noexcept;
    /// Registers the optional transformation from `fromVersion` to `fromVersion + 1`. Unregistered
    /// steps are identity migrations but are still versioned forward. Register all steps before the
    /// first successful load; duplicate, out-of-range, or empty callbacks are refused.
    [[nodiscard]] bool RegisterMigration(std::uint32_t fromVersion, SettingsMigrationFunction migration);

    /// Reads `path`, which later saves write back to. An absent `version` means version 0; forward
    /// migrations run on a temporary store, then the current version is recorded. A missing file is
    /// an empty document - every setting at its default - and still returns true. A future or
    /// malformed version, failed migration, filesystem error or parse failure leaves the document,
    /// path, and dirty state unchanged and returns false. A successful load notifies observers only
    /// for registered settings whose effective values changed; applying migrations marks an
    /// existing file dirty so Save() can persist the upgraded form.
    bool Load(const std::filesystem::path& path);
    /// Writes the document to the path it was loaded from. Clears IsDirty on success.
    bool Save();
    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_path; }
    /// Whether a change has been made since the last load or save.
    [[nodiscard]] bool IsDirty() const noexcept { return m_dirty; }

    /// The value of setting `id`: the one in the file, or the default. Nothing for an unknown id.
    [[nodiscard]] std::optional<SettingValue> GetValue(std::string_view id) const;
    /// The value the file sets for `id`, when it sets a legal one.
    [[nodiscard]] std::optional<SettingValue> GetStoredValue(std::string_view id) const;
    /// Sets `id` to `value`; a value equal to the default is removed from the file instead.
    /// Refused, changing nothing, for an unknown/deprecated id or an illegal value. Notifies
    /// observers once after the full value is applied, and only if the effective value changed.
    [[nodiscard]] bool SetValue(std::string_view id, const SettingValue& value);
    /// Removes `id` from the file, so it reads as its default. Notifies observers only when the
    /// effective value changes.
    bool Reset(std::string_view id);
    [[nodiscard]] bool IsModified(std::string_view id) const;

    /// Calls `callback` after future effective value changes to exactly `id`. Returns an invalid
    /// handle for an unknown setting or empty callback.
    [[nodiscard]] SettingsObserverSubscription SubscribeToSetting(
        std::string_view id, SettingsObserverCallback callback);
    /// Calls `callback` after changes to settings in `categoryPrefix` or any descendant category.
    /// Category paths are slash-separated and matched on segment boundaries, so "Render/Shadows"
    /// does not match "Render/ShadowsExtra". Returns an invalid handle for an empty callback or a
    /// prefix that currently matches no registered setting.
    [[nodiscard]] SettingsObserverSubscription SubscribeToCategory(
        std::string_view categoryPrefix, SettingsObserverCallback callback);
    /// Removes a subscription from this document. Returns false for an invalid, foreign, or already
    /// removed handle. Changes during a callback affect the current dispatch: an observer removed
    /// before its turn is skipped; a newly added observer starts with the next change.
    [[nodiscard]] bool Unsubscribe(SettingsObserverSubscription subscription);

private:
    SettingsRegistry m_registry;
    SettingsObserverHub m_observers{m_registry};
    ConfigManager m_store;
    std::filesystem::path m_path;
    std::map<std::uint32_t, SettingsMigrationFunction> m_migrations;
    std::uint32_t m_currentVersion = 1U;
    bool m_versioningFrozen = false;
    bool m_dirty = false;
};

} // namespace UVE::Config
