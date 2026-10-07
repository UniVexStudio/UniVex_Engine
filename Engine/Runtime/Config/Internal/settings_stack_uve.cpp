// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/config/settings_stack_uve.h"

#include <array>
#include <cstddef>

namespace UVE::Config {
namespace {

constexpr std::array<SettingValueSourceUVE, 4U> kResolutionOrderUVE{
    SettingValueSourceUVE::CommandLine,
    SettingValueSourceUVE::Platform,
    SettingValueSourceUVE::User,
    SettingValueSourceUVE::Project,
};

} // namespace

SettingsStackUVE::SettingsStackUVE(const SettingsRegistryUVE& registry) noexcept : m_registry(&registry) {}

bool SettingsStackUVE::AttachLayerUVE(SettingValueSourceUVE source, const IConfigManagerUVE& store) noexcept {
    const std::optional<std::size_t> index = LayerIndexUVE(source);
    if (!index.has_value()) {
        return false;
    }
    m_layers[*index] = &store;
    return true;
}

bool SettingsStackUVE::DetachLayerUVE(SettingValueSourceUVE source) noexcept {
    const std::optional<std::size_t> index = LayerIndexUVE(source);
    if (!index.has_value() || m_layers[*index] == nullptr) {
        return false;
    }
    m_layers[*index] = nullptr;
    return true;
}

std::optional<SettingResolutionUVE> SettingsStackUVE::ResolveUVE(std::string_view id) const {
    const SettingDescriptorUVE* descriptor = m_registry->FindUVE(id);
    if (descriptor == nullptr) {
        return std::nullopt;
    }

    for (const SettingValueSourceUVE source : kResolutionOrderUVE) {
        if (const std::optional<SettingValueUVE> value = GetStoredValueUVE(source, id); value.has_value()) {
            return SettingResolutionUVE{*value, source};
        }
    }

    return SettingResolutionUVE{descriptor->defaultValue, SettingValueSourceUVE::EngineDefault};
}

std::optional<SettingValueUVE> SettingsStackUVE::GetValueUVE(std::string_view id) const {
    const std::optional<SettingResolutionUVE> resolved = ResolveUVE(id);
    if (!resolved.has_value()) {
        return std::nullopt;
    }
    return resolved->value;
}

std::optional<SettingValueUVE> SettingsStackUVE::GetStoredValueUVE(SettingValueSourceUVE source,
                                                                  std::string_view id) const {
    const IConfigManagerUVE* layer = GetLayerUVE(source);
    const SettingDescriptorUVE* descriptor = m_registry != nullptr ? m_registry->FindUVE(id) : nullptr;
    if (layer == nullptr || descriptor == nullptr ||
        (source == SettingValueSourceUVE::Platform && !descriptor->HasFlagUVE(kSettingFlagPerPlatformUVE)) ||
        (source != SettingValueSourceUVE::CommandLine && descriptor->HasFlagUVE(kSettingFlagNotPersistedUVE))) {
        return std::nullopt;
    }
    return m_registry->GetStoredValueUVE(*layer, id);
}

std::optional<std::size_t> SettingsStackUVE::LayerIndexUVE(SettingValueSourceUVE source) noexcept {
    switch (source) {
    case SettingValueSourceUVE::Project:
        return 0U;
    case SettingValueSourceUVE::User:
        return 1U;
    case SettingValueSourceUVE::Platform:
        return 2U;
    case SettingValueSourceUVE::CommandLine:
        return 3U;
    case SettingValueSourceUVE::EngineDefault:
        return std::nullopt;
    }
    return std::nullopt;
}

const IConfigManagerUVE* SettingsStackUVE::GetLayerUVE(SettingValueSourceUVE source) const noexcept {
    const std::optional<std::size_t> index = LayerIndexUVE(source);
    return index.has_value() ? m_layers[*index] : nullptr;
}

} // namespace UVE::Config
