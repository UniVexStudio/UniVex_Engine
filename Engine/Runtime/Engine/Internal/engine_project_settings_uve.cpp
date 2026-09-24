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
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kPhysicsMaxStepsPerFrameUVE), defaults.maxFixedStepsPerFrame, 1, 64,
             "Max Steps Per Frame",
             "Physics/Common",
             "The most fixed steps one frame runs to catch up. Higher keeps the simulation on time "
             "through slow frames; lower keeps a slow frame from getting slower still.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.maxFixedStepsPerFrame = static_cast<int>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(Config::MakeVector3SettingUVE(
             std::string(Id::kPhysicsGravityUVE),
             Config::SettingVector3UVE{defaults.gravity.x, defaults.gravity.y, defaults.gravity.z}, -1000.0, 1000.0,
             "Gravity", "Physics/3D",
             "Acceleration every rigid body, character and particle falls with, in metres per second squared. "
             "Each body scales it by its own gravity scale.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             const auto& gravity = std::get<Config::SettingVector3UVE>(value);
             config.gravity = Math::Vector3UVE{static_cast<float>(gravity.x), static_cast<float>(gravity.y),
                                               static_cast<float>(gravity.z)};
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

std::string GetLayerNameSettingIdUVE(const LayerSetUVE set, const std::size_t index) {
    return std::string(set == LayerSetUVE::Physics ? "layers.physics." : "layers.render.") +
           std::to_string(index + 1U);
}

std::string GetLayerNameUVE(const Config::SettingsDocumentUVE& document, const LayerSetUVE set,
                            const std::size_t index) {
    if (index >= kLayerCountUVE) {
        return {};
    }
    const std::optional<SettingValueUVE> name = document.GetValueUVE(GetLayerNameSettingIdUVE(set, index));
    const auto* text = name ? std::get_if<std::string>(&*name) : nullptr;
    return text != nullptr ? *text : std::string{};
}

bool RegisterEngineProjectSettingsUVE(Config::SettingsRegistryUVE& registry) {
    bool allRegistered = true;
    for (const EngineProjectSettingUVE& setting : GetEngineProjectSettingsUVE()) {
        allRegistered = registry.RegisterUVE(setting.descriptor) && allRegistered;
    }
    constexpr std::size_t kMaximumContentPathBytesUVE = 512U;
    allRegistered = registry.RegisterUVE(Config::MakeStringSettingUVE(
                        std::string(EngineProjectSettingIdUVE::kDefaultPlayerEntityUVE), "",
                        kMaximumContentPathBytesUVE, "Default Player", "Game/Player",
                        "The entity asset (.uveentity, relative to Content) the player is spawned from. Set it "
                        "from the Content panel: right-click an entity, Set as Default Player.")) &&
                    allRegistered;
    // Layer names, 1 to 32 as a person counts them; bit 0 is layer 1.
    constexpr std::size_t kMaximumLayerNameBytesUVE = 32U;
    for (const LayerSetUVE set : {LayerSetUVE::Physics, LayerSetUVE::Render}) {
        const bool physics = set == LayerSetUVE::Physics;
        for (std::size_t index = 0U; index < kLayerCountUVE; ++index) {
            allRegistered =
                registry.RegisterUVE(Config::MakeStringSettingUVE(
                    GetLayerNameSettingIdUVE(set, index), "", kMaximumLayerNameBytesUVE,
                    "Layer " + std::to_string(index + 1U), physics ? "Layers/Physics" : "Layers/Render",
                    physics ? "Shown wherever a collider's layer or mask is picked."
                            : "Shown wherever a mesh's, light's or decal's render layers are picked.")) &&
                allRegistered;
        }
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
