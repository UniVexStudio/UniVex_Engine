// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "settings_stack.h"

#include <array>
#include <cstddef>

namespace UVE::Config {
namespace {

constexpr std::array<SettingValueSource, 4U> kResolutionOrder{
    SettingValueSource::CommandLine,
    SettingValueSource::Platform,
    SettingValueSource::User,
    SettingValueSource::Project,
};

} // namespace

SettingsStack::SettingsStack(const SettingsRegistry& registry) noexcept : m_registry(&registry) {}

bool SettingsStack::AttachLayer(SettingValueSource source, const IConfigManager& store) noexcept {
    const std::optional<std::size_t> index = LayerIndex(source);
    if (!index.has_value()) {
        return false;
    }
    m_layers[*index] = &store;
    return true;
}

bool SettingsStack::DetachLayer(SettingValueSource source) noexcept {
    const std::optional<std::size_t> index = LayerIndex(source);
    if (!index.has_value() || m_layers[*index] == nullptr) {
        return false;
    }
    m_layers[*index] = nullptr;
    return true;
}

std::optional<SettingResolution> SettingsStack::Resolve(std::string_view id) const {
    const SettingDescriptor* descriptor = m_registry->Find(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }

    for (const SettingValueSource source : kResolutionOrder) {
        if (const std::optional<SettingValue> value = GetStoredValue(source, id); value.has_value()) {
            return SettingResolution{*value, source};
        }
    }

    return SettingResolution{descriptor->defaultValue, SettingValueSource::EngineDefault};
}

std::optional<SettingValue> SettingsStack::GetValue(std::string_view id) const {
    const std::optional<SettingResolution> resolved = Resolve(id);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    return resolved->value;
}

std::optional<SettingValue> SettingsStack::GetStoredValue(SettingValueSource source,
                                                                  std::string_view id) const {
    const IConfigManager* layer = GetLayer(source);
    const SettingDescriptor* descriptor = m_registry != nullptr ? m_registry->Find(id) : nullptr;
    if (layer == nullptr || descriptor == nullptr ||
        (source == SettingValueSource::Platform && !descriptor->HasFlag(kSettingFlagPerPlatform)) ||
        (source != SettingValueSource::CommandLine && descriptor->HasFlag(kSettingFlagNotPersisted))) {
        return std::nullopt;
    }
    return m_registry->GetStoredValue(*layer, id);
}

std::optional<std::size_t> SettingsStack::LayerIndex(SettingValueSource source) noexcept {
    switch (source) {
    case SettingValueSource::Project:
        return 0U;
    case SettingValueSource::User:
        return 1U;
    case SettingValueSource::Platform:
        return 2U;
    case SettingValueSource::CommandLine:
        return 3U;
    case SettingValueSource::EngineDefault:
        return std::nullopt;
    }
    return std::nullopt;
}

const IConfigManager* SettingsStack::GetLayer(SettingValueSource source) const noexcept {
    const std::optional<std::size_t> index = LayerIndex(source);
    return index.has_value() ? m_layers[*index] : nullptr;
}

} // namespace UVE::Config
