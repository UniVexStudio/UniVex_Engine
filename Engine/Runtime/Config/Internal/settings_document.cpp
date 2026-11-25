// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "settings_document.h"

#include <exception>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "logging_macros.h"

namespace UVE::Config {
namespace {

[[nodiscard]] SettingValue NormalizeForDocument(const SettingDescriptor& descriptor,
                                                       SettingValue value) {
    // Alpha is not part of an opaque colour's value. Canonicalize it before comparing with stored
    // or default values, otherwise an ignored alpha channel can make a no-op look like a change.
    if (descriptor.type == SettingType::Color && !descriptor.colorHasAlpha) {
        if (auto* color = std::get_if<SettingColor>(&value)) {
            color->a = 1.0F;
        }
    }
    return value;
}

} // namespace

bool SettingsDocument::SetCurrentVersion(const std::uint32_t version) noexcept {
    if (m_versioningFrozen || version == 0U || version < m_currentVersion) {
        return false;
    }
    m_currentVersion = version;
    return true;
}

bool SettingsDocument::RegisterMigration(const std::uint32_t fromVersion,
                                               SettingsMigrationFunction migration) {
    if (m_versioningFrozen || !migration || fromVersion >= m_currentVersion || m_migrations.contains(fromVersion)) {
        return false;
    }
    m_migrations.emplace(fromVersion, std::move(migration));
    return true;
}

bool SettingsDocument::Load(const std::filesystem::path& path) {
    const std::vector<const SettingDescriptor*> descriptors = m_registry.GetAll();
    std::vector<std::pair<std::string, SettingValue>> previousValues;
    previousValues.reserve(descriptors.size());
    for (const SettingDescriptor* descriptor : descriptors) {
        if (const std::optional<SettingValue> value = m_registry.GetValue(m_store, descriptor->id)) {
            previousValues.emplace_back(descriptor->id, *value);
        }
    }

    std::error_code error;
    bool exists = std::filesystem::exists(path, error);
    if (error) {
        return false;
    }

    // Load and migrate a separate candidate so a failed parser, future schema or migration never
    // leaves this document half-replaced. ConfigManager keeps JSON private behind its PIMPL.
    ConfigManager candidate;
    if (exists && !candidate.Load(path)) {
        std::error_code retryError;
        if (std::filesystem::exists(path, retryError) || retryError) {
            return false;
        }
        exists = false; // The file vanished between the existence check and open.
    }

    std::uint32_t version = 0U; // Unversioned settings files are the version-zero format.
    if (candidate.HasNode(kSettingsDocumentVersionKey)) {
        constexpr std::int64_t kInvalidVersion = -1;
        const std::int64_t storedVersion = candidate.GetInt(kSettingsDocumentVersionKey, kInvalidVersion);
        if (storedVersion < 0) {
            UVE_ERROR("SettingsDocument: \"{}\" has invalid version metadata", path.string());
            return false;
        }
        if (static_cast<std::uint64_t>(storedVersion) > static_cast<std::uint64_t>(m_currentVersion)) {
            UVE_WARNING("SettingsDocument: \"{}\" uses newer version {} (this engine supports {})",
                        path.string(), storedVersion, m_currentVersion);
            return false;
        }
        version = static_cast<std::uint32_t>(storedVersion);
    }

    bool migrated = version != m_currentVersion;
    while (version < m_currentVersion) {
        const auto migration = m_migrations.find(version);
        if (migration != m_migrations.end()) {
            bool migrationSucceeded = false;
            try {
                migrationSucceeded = migration->second(candidate);
            } catch (const std::exception& exception) {
                UVE_ERROR("SettingsDocument: migration from version {} for \"{}\" threw: {}", version,
                          path.string(), exception.what());
                return false;
            } catch (...) {
                UVE_ERROR("SettingsDocument: migration from version {} for \"{}\" threw an unknown exception",
                          version, path.string());
                return false;
            }
            if (!migrationSucceeded) {
                UVE_ERROR("SettingsDocument: migration from version {} failed for \"{}\"", version,
                          path.string());
                return false;
            }
        }
        ++version;
        candidate.SetInt(kSettingsDocumentVersionKey, static_cast<std::int64_t>(version));
    }

    migrated = m_registry.MigrateDeprecatedValues(candidate) || migrated;
    candidate.SetInt(kSettingsDocumentVersionKey, static_cast<std::int64_t>(m_currentVersion));
    m_store.ReplaceDocument(candidate);
    m_path = path;
    m_dirty = exists && migrated;
    m_versioningFrozen = true;

    for (const auto& [id, previousValue] : previousValues) {
        if (const std::optional<SettingValue> newValue = GetValue(id);
            newValue && previousValue != *newValue) {
            m_observers.NotifyChanged(id, previousValue, *newValue);
        }
    }
    return true;
}

bool SettingsDocument::Save() {
    if (m_path.empty() || !m_store.Save(m_path)) {
        return false;
    }
    m_dirty = false;
    return true;
}

std::optional<SettingValue> SettingsDocument::GetValue(const std::string_view id) const {
    return m_registry.GetValue(m_store, id);
}

std::optional<SettingValue> SettingsDocument::GetStoredValue(const std::string_view id) const {
    return m_registry.GetStoredValue(m_store, id);
}

bool SettingsDocument::SetValue(const std::string_view id, const SettingValue& value) {
    const SettingDescriptor* descriptor = m_registry.Find(id);
    if (descriptor == nullptr || descriptor->HasFlag(kSettingFlagDeprecated) ||
        !IsSettingValueValid(*descriptor, value)) {
        return false;
    }

    const SettingValue normalizedValue = NormalizeForDocument(*descriptor, value);
    const std::optional<SettingValue> previousValue = GetValue(id);
    if (!previousValue) {
        return false;
    }

    if (normalizedValue != descriptor->defaultValue &&
        m_registry.GetStoredValue(m_store, id) == normalizedValue) {
        return true;
    }

    if (normalizedValue == descriptor->defaultValue) {
        // Also clears an illegal leftover or a hand-written redundant default. Neither causes a
        // notification unless the effective value actually changes.
        const bool removed = m_registry.ClearValue(m_store, id);
        m_dirty = removed || m_dirty;
        if (const std::optional<SettingValue> newValue = GetValue(id);
            newValue && *previousValue != *newValue) {
            m_observers.NotifyChanged(id, *previousValue, *newValue);
        }
        return true;
    }

    if (!m_registry.SetValue(m_store, id, normalizedValue)) {
        return false;
    }
    m_dirty = true;
    if (const std::optional<SettingValue> newValue = GetValue(id);
        newValue && *previousValue != *newValue) {
        m_observers.NotifyChanged(id, *previousValue, *newValue);
    }
    return true;
}

bool SettingsDocument::Reset(const std::string_view id) {
    const std::optional<SettingValue> previousValue = GetValue(id);
    if (!previousValue) {
        return false;
    }

    const bool removed = m_registry.ClearValue(m_store, id);
    m_dirty = removed || m_dirty;
    if (removed) {
        if (const std::optional<SettingValue> newValue = GetValue(id);
            newValue && *previousValue != *newValue) {
            m_observers.NotifyChanged(id, *previousValue, *newValue);
        }
    }
    return removed;
}

bool SettingsDocument::IsModified(const std::string_view id) const {
    return m_registry.IsModified(m_store, id);
}

SettingsObserverSubscription SettingsDocument::SubscribeToSetting(
    const std::string_view id, SettingsObserverCallback callback) {
    return m_observers.SubscribeToSetting(id, std::move(callback));
}

SettingsObserverSubscription SettingsDocument::SubscribeToCategory(
    const std::string_view categoryPrefix, SettingsObserverCallback callback) {
    return m_observers.SubscribeToCategory(categoryPrefix, std::move(callback));
}

bool SettingsDocument::Unsubscribe(const SettingsObserverSubscription subscription) {
    return m_observers.Unsubscribe(subscription);
}

} // namespace UVE::Config
