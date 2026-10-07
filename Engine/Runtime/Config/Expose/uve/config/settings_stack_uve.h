// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "uve/config/settings_registry_uve.h"

namespace UVE::Config {

/// The layer that supplied an effective setting value. Layers are resolved from lowest to highest
/// priority in this order: engine default, project, user, platform, command line.
enum class SettingValueSourceUVE : std::uint8_t {
    EngineDefault,
    Project,
    User,
    Platform,
    CommandLine,
};

/// One setting's effective value and the layer that won resolution.
struct SettingResolutionUVE final {
    SettingValueUVE value;
    SettingValueSourceUVE source = SettingValueSourceUVE::EngineDefault;
};

/// Resolves one registered schema against several non-owning configuration stores. A legal value
/// in the highest-priority store wins; a missing or invalid value falls through to the next layer,
/// and the descriptor's default is the final fallback. Platform values are considered only for
/// descriptors marked PerPlatform, and NotPersisted settings are considered only in the command-line
/// layer. This class only reads the stores: writes
/// and saves remain owned by the corresponding ConfigManagerUVE or SettingsDocumentUVE.
///
/// Store and registry lifetimes must exceed this stack's use. Not thread-safe because the registry
/// is not thread-safe; each store retains its own thread-safety contract.
class SettingsStackUVE final {
public:
    explicit SettingsStackUVE(const SettingsRegistryUVE& registry) noexcept;

    /// Attaches or replaces a non-default source. EngineDefault is supplied by the descriptor and
    /// cannot be attached as a store. The stack does not take ownership of `store`.
    [[nodiscard]] bool AttachLayerUVE(SettingValueSourceUVE source,
                                      const IConfigManagerUVE& store) noexcept;
    /// Detaches a previously attached non-default source. False for EngineDefault or an empty slot.
    [[nodiscard]] bool DetachLayerUVE(SettingValueSourceUVE source) noexcept;

    /// Resolves an id from CommandLine down through Platform, User and Project, then the schema's
    /// default. Nothing is returned for an id not in the registry.
    [[nodiscard]] std::optional<SettingResolutionUVE> ResolveUVE(std::string_view id) const;
    /// The effective value alone, when the id is registered.
    [[nodiscard]] std::optional<SettingValueUVE> GetValueUVE(std::string_view id) const;
    /// The legal stored value in one layer, without falling through. EngineDefault and unattached
    /// layers return nothing; invalid or corrupt values are absent. Platform values for settings
    /// without PerPlatform, and non-command-line values for NotPersisted settings, also return none.
    [[nodiscard]] std::optional<SettingValueUVE> GetStoredValueUVE(SettingValueSourceUVE source,
                                                                   std::string_view id) const;

private:
    [[nodiscard]] static std::optional<std::size_t> LayerIndexUVE(SettingValueSourceUVE source) noexcept;
    [[nodiscard]] const IConfigManagerUVE* GetLayerUVE(SettingValueSourceUVE source) const noexcept;

    const SettingsRegistryUVE* m_registry = nullptr;
    std::array<const IConfigManagerUVE*, 4U> m_layers{};
};

} // namespace UVE::Config
