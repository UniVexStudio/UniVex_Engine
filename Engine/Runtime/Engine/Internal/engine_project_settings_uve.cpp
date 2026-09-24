// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/engine_project_settings_uve.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace UVE::Core {
namespace {

using Config::SettingDescriptorUVE;
using Config::SettingValueUVE;

/// One engine project setting and the EngineConfigUVE field it overrides.
struct EngineProjectSettingUVE final {
    SettingDescriptorUVE descriptor;
    void (*apply)(EngineConfigUVE& config, const SettingValueUVE& value) = nullptr;
};

[[nodiscard]] SettingDescriptorUVE RestartRequiredUVE(SettingDescriptorUVE descriptor) {
    descriptor.flags |= Config::kSettingFlagRestartRequiredUVE;
    return descriptor;
}

[[nodiscard]] const std::vector<EngineProjectSettingUVE>& GetEngineProjectSettingsUVE() {
    namespace Id = EngineProjectSettingIdUVE;
    const EngineConfigUVE defaults{};
    static const std::vector<EngineProjectSettingUVE> settings = {
        {RestartRequiredUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kPhysicsTicksPerSecondUVE), defaults.fixedUpdateFps, 1.0, 1000.0, "Ticks Per Second",
             "Physics/Common", "How many fixed simulation steps run each second.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.fixedUpdateFps = std::get<double>(value); }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kPhysicsMaxFrameTimeUVE), defaults.maxDeltaTimeSeconds, 0.01, 1.0, "Max Frame Time",
             "Physics/Common",
             "The longest frame, in seconds, the simulation catches up on. Anything longer - a stall, a "
             "breakpoint - is cut to this, so the simulation slows down instead of spiralling.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.maxDeltaTimeSeconds = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kShadowMapResolutionUVE), static_cast<std::int64_t>(defaults.shadowMapResolution),
             {{512, "512"}, {1024, "1024"}, {2048, "2048"}, {4096, "4096"}}, "Map Resolution", "Rendering/Shadows",
             "Width and height, in texels, of the directional light's shadow map.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowMapResolution = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kShadowFilterUVE), static_cast<std::int64_t>(defaults.shadowPcfKernelRadius),
             {{0, "Hard"}, {1, "Soft (3x3)"}, {2, "Softer (5x5)"}}, "Filter", "Rendering/Shadows",
             "How shadow edges are softened. Softer costs more samples per pixel.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowPcfKernelRadius = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kAutoSaveIntervalUVE), defaults.autoSaveIntervalSecondsUVE, 10.0, 3600.0,
             "Auto-Save Interval", "Application/Save", "Seconds between writes to the game's auto-save slot.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.autoSaveIntervalSecondsUVE = std::get<double>(value);
         }},
    };
    return settings;
}

} // namespace

bool RegisterEngineProjectSettingsUVE(Config::SettingsRegistryUVE& registry) {
    bool allRegistered = true;
    for (const EngineProjectSettingUVE& setting : GetEngineProjectSettingsUVE()) {
        allRegistered = registry.RegisterUVE(setting.descriptor) && allRegistered;
    }
    return allRegistered;
}

void ApplyEngineProjectSettingsUVE(const Config::SettingsDocumentUVE& document, EngineConfigUVE& config) {
    for (const EngineProjectSettingUVE& setting : GetEngineProjectSettingsUVE()) {
        if (const std::optional<SettingValueUVE> value = document.GetStoredValueUVE(setting.descriptor.id)) {
            setting.apply(config, *value);
        }
    }
}

} // namespace UVE::Core
