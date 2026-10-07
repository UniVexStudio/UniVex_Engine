// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "uve/config/settings_document_uve.h"
#include "uve/config/settings_stack_uve.h"
#include "uve/core/engine_config_uve.h"

namespace UVE::CommandLine {
class ICommandLineUVE;
}

namespace UVE::Core {

/// Ids of settings registered by the engine. Most are project settings overriding fields of
/// EngineConfigUVE; `headless` is a hidden, command-line-only startup option.
namespace EngineProjectSettingIdUVE {
inline constexpr std::string_view kPhysicsTicksPerSecondUVE = "physics.common.ticksPerSecond";
inline constexpr std::string_view kPhysicsMaxFrameTimeUVE = "physics.common.maxFrameTime";
inline constexpr std::string_view kPhysicsMaxStepsPerFrameUVE = "physics.common.maxStepsPerFrame";
inline constexpr std::string_view kPhysicsGravityUVE = "physics.3d.gravity";
inline constexpr std::string_view kRenderBackendPreferenceUVE = "rendering.backend.preference";
inline constexpr std::string_view kRenderResolutionScaleUVE = "rendering.resolution.scale";
inline constexpr std::string_view kDynamicRenderResolutionEnabledUVE = "rendering.resolution.dynamicEnabled";
inline constexpr std::string_view kMinimumRenderResolutionScaleUVE = "rendering.resolution.minimumScale";
inline constexpr std::string_view kRenderResolutionTargetFrameTimeMillisecondsUVE =
    "rendering.resolution.targetFrameTimeMilliseconds";
inline constexpr std::string_view kShaderProgramCacheDirectoryUVE = "rendering.shaders.programCacheDirectory";
inline constexpr std::string_view kShaderCompilationModeUVE = "rendering.shaders.compilationMode";
inline constexpr std::string_view kVulkanValidationLayersEnabledUVE = "rendering.validation.vulkanLayers";
inline constexpr std::string_view kSpatialUpscalingMethodUVE = "rendering.upscaling.method";
inline constexpr std::string_view kToneMappingMethodUVE = "rendering.postProcessing.toneMapper";
inline constexpr std::string_view kSharpeningAmountUVE = "rendering.postProcessing.sharpeningAmount";
inline constexpr std::string_view kDitheringEnabledUVE = "rendering.postProcessing.ditheringEnabled";
inline constexpr std::string_view kSsaoEnabledUVE = "rendering.ssao.enabled";
inline constexpr std::string_view kSsaoRadiusUVE = "rendering.ssao.radius";
inline constexpr std::string_view kSsaoIntensityUVE = "rendering.ssao.intensity";
inline constexpr std::string_view kSsaoPowerUVE = "rendering.ssao.power";
inline constexpr std::string_view kSsaoQualityUVE = "rendering.ssao.quality";
inline constexpr std::string_view kSsaoBlurEnabledUVE = "rendering.ssao.blurEnabled";
inline constexpr std::string_view kPostProcessAAMethodUVE = "rendering.antialiasing.postProcessMethod";
inline constexpr std::string_view kScreenSpaceAAQualityUVE = "rendering.antialiasing.screenSpaceQuality";
inline constexpr std::string_view kShadowMapResolutionUVE = "rendering.shadows.mapResolution";
inline constexpr std::string_view kShadowBiasDefaultUVE = "rendering.shadows.defaultBias";
inline constexpr std::string_view kShadowNormalBiasDefaultUVE = "rendering.shadows.defaultNormalBias";
inline constexpr std::string_view kShadowCascadeSplitBlendUVE = "rendering.shadows.defaultCascadeSplitBlend";
inline constexpr std::string_view kShadowFilterUVE = "rendering.shadows.filter";
inline constexpr std::string_view kFrustumCullingModeUVE = "rendering.culling.frustumCulling";
inline constexpr std::string_view kAutoSaveIntervalUVE = "application.save.autoSaveInterval";
inline constexpr std::string_view kQuitOnLastWindowClosedUVE = "application.quitOnLastWindowClosed";
inline constexpr std::string_view kBootBackgroundColorUVE = "application.boot.backgroundColor";
inline constexpr std::string_view kBootSplashImageUVE = "application.boot.image";
inline constexpr std::string_view kBootSplashFadeSecondsUVE = "application.boot.fadeSeconds";
inline constexpr std::string_view kBootSplashMinimumDisplaySecondsUVE = "application.boot.minimumDisplaySeconds";
inline constexpr std::string_view kBootSplashSkippableUVE = "application.boot.skippable";
inline constexpr std::string_view kSkipBootSplashInEditorPlayModeUVE = "application.boot.skipInEditorPlayMode";
inline constexpr std::string_view kCrashHandlerEnabledUVE = "application.crash.enabled";
inline constexpr std::string_view kCrashDumpDirectoryUVE = "application.crash.dumpDirectory";
inline constexpr std::string_view kSymbolUploadEndpointUVE = "application.crash.symbolUploadEndpoint";
inline constexpr std::string_view kWindowWidthUVE = "window.width";
inline constexpr std::string_view kWindowHeightUVE = "window.height";
inline constexpr std::string_view kWindowModeUVE = "window.mode";
inline constexpr std::string_view kWindowResizableUVE = "window.resizable";
inline constexpr std::string_view kWindowBorderlessUVE = "window.borderless";
inline constexpr std::string_view kWindowAlwaysOnTopUVE = "window.alwaysOnTop";
inline constexpr std::string_view kWindowTransparentUVE = "window.transparent";
inline constexpr std::string_view kWindowMinimumWidthUVE = "window.minimumWidth";
inline constexpr std::string_view kWindowMinimumHeightUVE = "window.minimumHeight";
inline constexpr std::string_view kWindowMaximumWidthUVE = "window.maximumWidth";
inline constexpr std::string_view kWindowMaximumHeightUVE = "window.maximumHeight";
inline constexpr std::string_view kWindowPositionSpecifiedUVE = "window.position.enabled";
inline constexpr std::string_view kWindowPositionXUVE = "window.position.x";
inline constexpr std::string_view kWindowPositionYUVE = "window.position.y";
inline constexpr std::string_view kWindowMonitorUVE = "window.monitor";
inline constexpr std::string_view kWindowHighDpiAwareUVE = "window.highDpiAware";
inline constexpr std::string_view kWindowPerMonitorScalingUVE = "window.perMonitorScaling";
inline constexpr std::string_view kWindowContentScaleOverrideUVE = "window.contentScaleOverride";
inline constexpr std::string_view kWindowStretchModeUVE = "window.stretchMode";
inline constexpr std::string_view kWindowAspectPolicyUVE = "window.aspectPolicy";
inline constexpr std::string_view kWindowIntegerOnlyScalingUVE = "window.integerOnlyScaling";
inline constexpr std::string_view kWindowOrientationUVE = "window.orientation";
inline constexpr std::string_view kWindowAllowedOrientationsUVE = "window.allowedOrientations";
inline constexpr std::string_view kWindowVSyncModeUVE = "window.vsyncMode";
inline constexpr std::string_view kWindowFocusedFrameRateCapUVE = "window.focusedFrameRateCap";
inline constexpr std::string_view kWindowUnfocusedFrameRateCapUVE = "window.unfocusedFrameRateCap";
inline constexpr std::string_view kWindowAllowDisplaySleepUVE = "window.allowDisplaySleep";
inline constexpr std::string_view kWindowCursorImageUVE = "window.cursor.image";
inline constexpr std::string_view kWindowCursorHotspotXUVE = "window.cursor.hotspotX";
inline constexpr std::string_view kWindowCursorHotspotYUVE = "window.cursor.hotspotY";
inline constexpr std::string_view kWindowCursorVisibleUVE = "window.cursor.visible";
inline constexpr std::string_view kWindowCursorConfinedUVE = "window.cursor.confined";
inline constexpr std::string_view kWindowTitleFormatUVE = "window.titleFormat";
inline constexpr std::string_view kWindowAppendSceneInEditorPlayUVE = "window.appendSceneNameInEditorPlayMode";
/// The `.uventity` (Content-relative path) a player is spawned from when Play starts. Empty: none.
inline constexpr std::string_view kDefaultPlayerEntityUVE = "game.player.defaultEntity";
inline constexpr std::string_view kHeadlessUVE = "headless";
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

/// Declares engine configuration settings and project settings in `registry`. Defaults match
/// EngineConfigUVE's defaults; startup settings are RestartRequired. Renderer backend preference,
/// render-resolution policies, and shadow resolution/filter are PerPlatform; the hidden `headless` option
/// is NotPersisted and command-line-only. Layer names are read wherever they are shown. False if any
/// declaration is refused - a programming error a test catches.
[[nodiscard]] bool RegisterEngineProjectSettingsUVE(Config::SettingsRegistryUVE& registry);
/// Whether `id` is one of the registered settings that directly overrides an EngineConfigUVE field.
[[nodiscard]] bool IsEngineConfigSettingIdUVE(std::string_view id);

/// Converts registered EngineConfig setting flags (`--<setting.id> <value>`) into typed values in
/// `store`. A presence-only bool becomes true; unknown engine settings and invalid values are
/// ignored, so invalid command-line overrides cannot hide valid lower layers.
void PopulateEngineCommandLineSettingsUVE(const Config::SettingsRegistryUVE& registry,
                                          const CommandLine::ICommandLineUVE& commandLine,
                                          Config::IConfigManagerUVE& store);

/// Returns the active platform settings path, honoring an explicit override or using
/// `platforms/<target>/<user settings filename>` beside the user settings file.
[[nodiscard]] std::filesystem::path GetPlatformSettingsFilePathUVE(const EngineConfigUVE& config);

/// Attaches all four runtime stores to `settings` in the fixed project/user/platform/command-line
/// slots. Returns false if any attachment fails.
[[nodiscard]] bool AttachEngineSettingsLayersUVE(Config::SettingsStackUVE& settings,
                                                 const Config::IConfigManagerUVE& project,
                                                 const Config::IConfigManagerUVE& user,
                                                 const Config::IConfigManagerUVE& platform,
                                                 const Config::IConfigManagerUVE& commandLine) noexcept;

/// Applies every non-default value resolved by `settings` to the matching field of `config`.
/// EngineConfigUVE remains the caller's base configuration when no project/user/platform/command-line
/// store supplies an effective value.
void ApplyEngineSettingsUVE(const Config::SettingsStackUVE& settings, EngineConfigUVE& config);
/// Compatibility convenience for a project-only stack: copies every setting `document` stores into
/// its matching field, leaving application-selected fields alone when the project does not set them.
void ApplyEngineProjectSettingsUVE(const Config::SettingsDocumentUVE& document, EngineConfigUVE& config);

} // namespace UVE::Core
