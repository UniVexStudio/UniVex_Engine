// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "uve/config/settings_document_uve.h"
#include "uve/core/engine_config_uve.h"

namespace UVE::Core {

/// Ids of the engine's own project settings, each overriding one field of EngineConfigUVE.
namespace EngineProjectSettingIdUVE {
inline constexpr std::string_view kPhysicsTicksPerSecondUVE = "physics.common.ticksPerSecond";
inline constexpr std::string_view kPhysicsMaxFrameTimeUVE = "physics.common.maxFrameTime";
inline constexpr std::string_view kPhysicsMaxStepsPerFrameUVE = "physics.common.maxStepsPerFrame";
inline constexpr std::string_view kPhysicsGravityUVE = "physics.3d.gravity";
inline constexpr std::string_view kShadowMapResolutionUVE = "rendering.shadows.mapResolution";
inline constexpr std::string_view kShadowFilterUVE = "rendering.shadows.filter";
inline constexpr std::string_view kAutoSaveIntervalUVE = "application.save.autoSaveInterval";
} // namespace EngineProjectSettingIdUVE

/// The two sets of 32 layers a project names: physics layers (what a collider is on and looks
/// for) and render layers (what a camera, light or decal sees).
enum class LayerSetUVE {
    Physics,
    Render,
};
inline constexpr std::size_t kLayerCountUVE = 32U;

/// The project setting holding the name of layer `index` (0-based, bit `index`) of `set`, e.g.
/// "layers.physics.1" for the first physics layer.
[[nodiscard]] std::string GetLayerNameSettingIdUVE(LayerSetUVE set, std::size_t index);
/// The name `document` gives layer `index` of `set`; empty when the project has not named it or
/// `index` is out of range.
[[nodiscard]] std::string GetLayerNameUVE(const Config::SettingsDocumentUVE& document, LayerSetUVE set,
                                          std::size_t index);

/// Declares the engine's project settings in `registry`. Their defaults are EngineConfigUVE's own,
/// so an empty project file changes nothing. Those that override EngineConfigUVE are read once,
/// at startup, and so are flagged RestartRequired; the layer names are read wherever they are
/// shown. False if any declaration is refused - a programming error a test catches.
[[nodiscard]] bool RegisterEngineProjectSettingsUVE(Config::SettingsRegistryUVE& registry);

/// Copies every setting `document` sets into the matching field of `config`, leaving the fields it
/// does not set as they are - whatever the application chose stays unless the project says
/// otherwise.
void ApplyEngineProjectSettingsUVE(const Config::SettingsDocumentUVE& document, EngineConfigUVE& config);

} // namespace UVE::Core
