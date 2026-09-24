// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/config/settings_document_uve.h"
#include "uve/core/engine_config_uve.h"

namespace UVE::Core {

/// Ids of the engine's own project settings, each overriding one field of EngineConfigUVE.
namespace EngineProjectSettingIdUVE {
inline constexpr std::string_view kPhysicsTicksPerSecondUVE = "physics.common.ticksPerSecond";
inline constexpr std::string_view kPhysicsMaxFrameTimeUVE = "physics.common.maxFrameTime";
inline constexpr std::string_view kShadowMapResolutionUVE = "rendering.shadows.mapResolution";
inline constexpr std::string_view kShadowFilterUVE = "rendering.shadows.filter";
inline constexpr std::string_view kAutoSaveIntervalUVE = "application.save.autoSaveInterval";
} // namespace EngineProjectSettingIdUVE

/// Declares the engine's project settings in `registry`. Their defaults are EngineConfigUVE's own,
/// so an empty project file changes nothing. Each is read once, at startup, and so is flagged
/// RestartRequired. False if any declaration is refused - a programming error a test catches.
[[nodiscard]] bool RegisterEngineProjectSettingsUVE(Config::SettingsRegistryUVE& registry);

/// Copies every setting `document` sets into the matching field of `config`, leaving the fields it
/// does not set as they are - whatever the application chose stays unless the project says
/// otherwise.
void ApplyEngineProjectSettingsUVE(const Config::SettingsDocumentUVE& document, EngineConfigUVE& config);

} // namespace UVE::Core
