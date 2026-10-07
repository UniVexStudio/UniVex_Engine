// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_document_uve.h"

#include <exception>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Config {
namespace {

[[nodiscard]] SettingValueUVE NormalizeForDocumentUVE(const SettingDescriptorUVE& descriptor,
                                                       SettingValueUVE value) {
    // Alpha is not part of an opaque colour's value. Canonicalize it before comparing with stored
    // or default values, otherwise an ignored alpha channel can make a no-op look like a change.
    if (descriptor.type == SettingTypeUVE::Color && !descriptor.colorHasAlpha) {
        if (auto* color = std::get_if<SettingColorUVE>(&value)) {
            color->a = 1.0F;
        }
    }
    return value;
}

} // namespace

bool SettingsDocumentUVE::SetCurrentVersionUVE(const std::uint32_t version) noexcept {
    if (m_versioningFrozen || version == 0U || version < m_currentVersion) {
        return false;
    }
    m_currentVersion = version;
    return true;
}

bool SettingsDocumentUVE::RegisterMigrationUVE(const std::uint32_t fromVersion,
                                               SettingsMigrationFunctionUVE migration) {
    if (m_versioningFrozen || !migration || fromVersion >= m_currentVersion || m_migrations.contains(fromVersion)) {
        return false;
    }
    m_migrations.emplace(fromVersion, std::move(migration));
    return true;
}

bool SettingsDocumentUVE::LoadUVE(const std::filesystem::path& path) {
    const std::vector<const SettingDescriptorUVE*> descriptors = m_registry.GetAllUVE();
    std::vector<std::pair<std::string, SettingValueUVE>> previousValues;
    previousValues.reserve(descriptors.size());
    for (const SettingDescriptorUVE* descriptor : descriptors) {
        if (const std::optional<SettingValueUVE> value = m_registry.GetValueUVE(m_store, descriptor->id)) {
            previousValues.emplace_back(descriptor->id, *value);
        }
    }

    std::error_code error;
    bool exists = std::filesystem::exists(path, error);
    if (error) {
        return false;
    }

    // Load and migrate a separate candidate so a failed parser, future schema or migration never
    // leaves this document half-replaced. ConfigManagerUVE keeps JSON private behind its PIMPL.
    ConfigManagerUVE candidate;
    if (exists && !candidate.LoadUVE(path)) {
        std::error_code retryError;
        if (std::filesystem::exists(path, retryError) || retryError) {
            return false;
        }
        exists = false; // The file vanished between the existence check and open.
    }

    std::uint32_t version = 0U; // Unversioned settings files are the version-zero format.
    if (candidate.HasNodeUVE(kSettingsDocumentVersionKeyUVE)) {
        constexpr std::int64_t kInvalidVersion = -1;
        const std::int64_t storedVersion = candidate.GetIntUVE(kSettingsDocumentVersionKeyUVE, kInvalidVersion);
        if (storedVersion < 0) {
            UVE_ERROR("SettingsDocumentUVE: \"{}\" has invalid version metadata", path.string());
            return false;
        }
        if (static_cast<std::uint64_t>(storedVersion) > static_cast<std::uint64_t>(m_currentVersion)) {
            UVE_WARNING("SettingsDocumentUVE: \"{}\" uses newer version {} (this engine supports {})",
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
                UVE_ERROR("SettingsDocumentUVE: migration from version {} for \"{}\" threw: {}", version,
                          path.string(), exception.what());
                return false;
            } catch (...) {
                UVE_ERROR("SettingsDocumentUVE: migration from version {} for \"{}\" threw an unknown exception",
                          version, path.string());
                return false;
            }
            if (!migrationSucceeded) {
                UVE_ERROR("SettingsDocumentUVE: migration from version {} failed for \"{}\"", version,
                          path.string());
                return false;
            }
        }
        ++version;
        candidate.SetIntUVE(kSettingsDocumentVersionKeyUVE, static_cast<std::int64_t>(version));
    }

    migrated = m_registry.MigrateDeprecatedValuesUVE(candidate) || migrated;
    candidate.SetIntUVE(kSettingsDocumentVersionKeyUVE, static_cast<std::int64_t>(m_currentVersion));
    m_store.ReplaceDocumentUVE(candidate);
    m_path = path;
    m_dirty = exists && migrated;
    m_versioningFrozen = true;

    for (const auto& [id, previousValue] : previousValues) {
        if (const std::optional<SettingValueUVE> newValue = GetValueUVE(id);
            newValue && previousValue != *newValue) {
            m_observers.NotifyChangedUVE(id, previousValue, *newValue);
        }
    }
    return true;
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
    if (descriptor == nullptr || descriptor->HasFlagUVE(kSettingFlagDeprecatedUVE) ||
        !IsSettingValueValidUVE(*descriptor, value)) {
        return false;
    }

    const SettingValueUVE normalizedValue = NormalizeForDocumentUVE(*descriptor, value);
    const std::optional<SettingValueUVE> previousValue = GetValueUVE(id);
    if (!previousValue) {
        return false;
    }

    if (normalizedValue != descriptor->defaultValue &&
        m_registry.GetStoredValueUVE(m_store, id) == normalizedValue) {
        return true;
    }

    if (normalizedValue == descriptor->defaultValue) {
        // Also clears an illegal leftover or a hand-written redundant default. Neither causes a
        // notification unless the effective value actually changes.
        const bool removed = m_registry.ClearValueUVE(m_store, id);
        m_dirty = removed || m_dirty;
        if (const std::optional<SettingValueUVE> newValue = GetValueUVE(id);
            newValue && *previousValue != *newValue) {
            m_observers.NotifyChangedUVE(id, *previousValue, *newValue);
        }
        return true;
    }

    if (!m_registry.SetValueUVE(m_store, id, normalizedValue)) {
        return false;
    }
    m_dirty = true;
    if (const std::optional<SettingValueUVE> newValue = GetValueUVE(id);
        newValue && *previousValue != *newValue) {
        m_observers.NotifyChangedUVE(id, *previousValue, *newValue);
    }
    return true;
}

bool SettingsDocumentUVE::ResetUVE(const std::string_view id) {
    const std::optional<SettingValueUVE> previousValue = GetValueUVE(id);
    if (!previousValue) {
        return false;
    }

    const bool removed = m_registry.ClearValueUVE(m_store, id);
    m_dirty = removed || m_dirty;
    if (removed) {
        if (const std::optional<SettingValueUVE> newValue = GetValueUVE(id);
            newValue && *previousValue != *newValue) {
            m_observers.NotifyChangedUVE(id, *previousValue, *newValue);
        }
    }
    return removed;
}

bool SettingsDocumentUVE::IsModifiedUVE(const std::string_view id) const {
    return m_registry.IsModifiedUVE(m_store, id);
}

SettingsObserverSubscriptionUVE SettingsDocumentUVE::SubscribeToSettingUVE(
    const std::string_view id, SettingsObserverCallbackUVE callback) {
    return m_observers.SubscribeToSettingUVE(id, std::move(callback));
}

SettingsObserverSubscriptionUVE SettingsDocumentUVE::SubscribeToCategoryUVE(
    const std::string_view categoryPrefix, SettingsObserverCallbackUVE callback) {
    return m_observers.SubscribeToCategoryUVE(categoryPrefix, std::move(callback));
}

bool SettingsDocumentUVE::UnsubscribeUVE(const SettingsObserverSubscriptionUVE subscription) {
    return m_observers.UnsubscribeUVE(subscription);
}

} // namespace UVE::Config
