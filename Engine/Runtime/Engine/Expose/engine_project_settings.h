// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#include "settings_document.h"
#include "settings_stack.h"
#include "engine_config.h"

namespace UVE::CommandLine {
class ICommandLine;
}

namespace UVE::Render {
class IRenderer3D;
}

namespace UVE::Core {

/// Ids of settings registered by the engine. Most are project settings overriding fields of
/// EngineConfig; `headless` is a hidden, command-line-only startup option.
namespace EngineProjectSettingId {
inline constexpr std::string_view kPhysicsTicksPerSecond = "physics.common.ticksPerSecond";
inline constexpr std::string_view kPhysicsMaxFrameTime = "physics.common.maxFrameTime";
inline constexpr std::string_view kPhysicsMaxStepsPerFrame = "physics.common.maxStepsPerFrame";
inline constexpr std::string_view kPhysicsGravity = "physics.3d.gravity";
inline constexpr std::string_view kRenderBackendPreference = "rendering.backend.preference";
inline constexpr std::string_view kRenderResolutionScale = "rendering.resolution.scale";
inline constexpr std::string_view kDynamicRenderResolutionEnabled = "rendering.resolution.dynamicEnabled";
inline constexpr std::string_view kMinimumRenderResolutionScale = "rendering.resolution.minimumScale";
inline constexpr std::string_view kRenderResolutionTargetFrameTimeMilliseconds =
    "rendering.resolution.targetFrameTimeMilliseconds";
inline constexpr std::string_view kShaderProgramCacheDirectory = "rendering.shaders.programCacheDirectory";
inline constexpr std::string_view kShaderCompilationMode = "rendering.shaders.compilationMode";
inline constexpr std::string_view kVulkanValidationLayersEnabled = "rendering.validation.vulkanLayers";
inline constexpr std::string_view kSpatialUpscalingMethod = "rendering.upscaling.method";
inline constexpr std::string_view kToneMappingMethod = "rendering.postProcessing.toneMapper";
inline constexpr std::string_view kSharpeningAmount = "rendering.postProcessing.sharpeningAmount";
inline constexpr std::string_view kDitheringEnabled = "rendering.postProcessing.ditheringEnabled";
inline constexpr std::string_view kSsaoEnabled = "rendering.ssao.enabled";
inline constexpr std::string_view kSsaoRadius = "rendering.ssao.radius";
inline constexpr std::string_view kSsaoIntensity = "rendering.ssao.intensity";
inline constexpr std::string_view kSsaoPower = "rendering.ssao.power";
inline constexpr std::string_view kSsaoQuality = "rendering.ssao.quality";
inline constexpr std::string_view kSsaoBlurEnabled = "rendering.ssao.blurEnabled";
inline constexpr std::string_view kPostProcessAAMethod = "rendering.antialiasing.postProcessMethod";
inline constexpr std::string_view kScreenSpaceAAQuality = "rendering.antialiasing.screenSpaceQuality";
inline constexpr std::string_view kShadowMapResolution = "rendering.shadows.mapResolution";
inline constexpr std::string_view kShadowBiasDefault = "rendering.shadows.defaultBias";
inline constexpr std::string_view kShadowNormalBiasDefault = "rendering.shadows.defaultNormalBias";
inline constexpr std::string_view kShadowCascadeSplitBlend = "rendering.shadows.defaultCascadeSplitBlend";
inline constexpr std::string_view kShadowFilter = "rendering.shadows.filter";
/// Contact shadows: one family under Rendering/Shadows, matching the shadow settings beside it.
/// The renderer normalises what it receives, but these bounds are the contract the editor shows.
inline constexpr std::string_view kContactShadowsEnabled = "rendering.shadows.contactShadowsEnabled";
inline constexpr std::string_view kContactShadowsLength = "rendering.shadows.contactShadowsLength";
inline constexpr std::string_view kContactShadowsSteps = "rendering.shadows.contactShadowsSteps";
inline constexpr std::string_view kContactShadowsThickness = "rendering.shadows.contactShadowsThickness";
inline constexpr std::string_view kContactShadowsStrength = "rendering.shadows.contactShadowsStrength";
inline constexpr std::string_view kFrustumCullingMode = "rendering.culling.frustumCulling";
inline constexpr std::string_view kGpuOcclusionCulling = "rendering.culling.gpuOcclusionCulling";
inline constexpr std::string_view kAutoSaveInterval = "application.save.autoSaveInterval";
inline constexpr std::string_view kQuitOnLastWindowClosed = "application.quitOnLastWindowClosed";
inline constexpr std::string_view kBootBackgroundColor = "application.boot.backgroundColor";
inline constexpr std::string_view kBootSplashImage = "application.boot.image";
inline constexpr std::string_view kBootSplashFadeSeconds = "application.boot.fadeSeconds";
inline constexpr std::string_view kBootSplashMinimumDisplaySeconds = "application.boot.minimumDisplaySeconds";
inline constexpr std::string_view kBootSplashSkippable = "application.boot.skippable";
inline constexpr std::string_view kSkipBootSplashInEditorPlayMode = "application.boot.skipInEditorPlayMode";
inline constexpr std::string_view kCrashHandlerEnabled = "application.crash.enabled";
inline constexpr std::string_view kCrashDumpDirectory = "application.crash.dumpDirectory";
inline constexpr std::string_view kSymbolUploadEndpoint = "application.crash.symbolUploadEndpoint";
inline constexpr std::string_view kWindowWidth = "window.width";
inline constexpr std::string_view kWindowHeight = "window.height";
inline constexpr std::string_view kWindowMode = "window.mode";
inline constexpr std::string_view kWindowResizable = "window.resizable";
inline constexpr std::string_view kWindowBorderless = "window.borderless";
inline constexpr std::string_view kWindowAlwaysOnTop = "window.alwaysOnTop";
inline constexpr std::string_view kWindowTransparent = "window.transparent";
inline constexpr std::string_view kWindowMinimumWidth = "window.minimumWidth";
inline constexpr std::string_view kWindowMinimumHeight = "window.minimumHeight";
inline constexpr std::string_view kWindowMaximumWidth = "window.maximumWidth";
inline constexpr std::string_view kWindowMaximumHeight = "window.maximumHeight";
inline constexpr std::string_view kWindowPositionSpecified = "window.position.enabled";
inline constexpr std::string_view kWindowPositionX = "window.position.x";
inline constexpr std::string_view kWindowPositionY = "window.position.y";
inline constexpr std::string_view kWindowMonitor = "window.monitor";
inline constexpr std::string_view kWindowHighDpiAware = "window.highDpiAware";
inline constexpr std::string_view kWindowPerMonitorScaling = "window.perMonitorScaling";
inline constexpr std::string_view kWindowContentScaleOverride = "window.contentScaleOverride";
inline constexpr std::string_view kWindowStretchMode = "window.stretchMode";
inline constexpr std::string_view kWindowAspectPolicy = "window.aspectPolicy";
inline constexpr std::string_view kWindowIntegerOnlyScaling = "window.integerOnlyScaling";
inline constexpr std::string_view kWindowOrientation = "window.orientation";
inline constexpr std::string_view kWindowAllowedOrientations = "window.allowedOrientations";
inline constexpr std::string_view kWindowVSyncMode = "window.vsyncMode";
inline constexpr std::string_view kWindowFocusedFrameRateCap = "window.focusedFrameRateCap";
inline constexpr std::string_view kWindowUnfocusedFrameRateCap = "window.unfocusedFrameRateCap";
inline constexpr std::string_view kWindowAllowDisplaySleep = "window.allowDisplaySleep";
inline constexpr std::string_view kWindowCursorImage = "window.cursor.image";
inline constexpr std::string_view kWindowCursorHotspotX = "window.cursor.hotspotX";
inline constexpr std::string_view kWindowCursorHotspotY = "window.cursor.hotspotY";
inline constexpr std::string_view kWindowCursorVisible = "window.cursor.visible";
inline constexpr std::string_view kWindowCursorConfined = "window.cursor.confined";
inline constexpr std::string_view kWindowTitleFormat = "window.titleFormat";
inline constexpr std::string_view kWindowAppendSceneInEditorPlay = "window.appendSceneNameInEditorPlayMode";
/// The `.uventity` (Content-relative path) a player is spawned from when Play starts. Empty: none.
inline constexpr std::string_view kDefaultPlayerEntity = "game.player.defaultEntity";
inline constexpr std::string_view kHeadless = "headless";
} // namespace EngineProjectSettingId

/// The sets of 32 layers a project names: 3D physics layers, render layers,
/// navigation layers, and avoidance layers.
enum class LayerSet {
    Physics,
    Render,
    Navigation,
    Avoidance,
};
inline constexpr std::size_t kLayerCount = 32U;

/// The project setting holding the name of layer `index` (0-based, bit `index`) of `set`, e.g.
/// "layers.physics.1" for the first physics layer.
[[nodiscard]] std::string GetLayerNameSettingId(LayerSet set, std::size_t index);
/// The name `document` gives layer `index` of `set`; empty when the project has not named it or
/// `index` is out of range.
[[nodiscard]] std::string GetLayerName(const Config::SettingsDocument& document, LayerSet set,
                                          std::size_t index);

/// Declares engine configuration settings and project settings in `registry`. Defaults match
/// EngineConfig's defaults; startup settings are RestartRequired. Renderer backend preference,
/// render-resolution policies, and shadow resolution/filter are PerPlatform; the hidden `headless` option
/// is NotPersisted and command-line-only. Layer names are read wherever they are shown. False if any
/// declaration is refused - a programming error a test catches.
[[nodiscard]] bool RegisterEngineProjectSettings(Config::SettingsRegistry& registry);
/// Whether `id` is one of the registered settings that directly overrides an EngineConfig field.
[[nodiscard]] bool IsEngineConfigSettingId(std::string_view id);

/// Converts registered EngineConfig setting flags (`--<setting.id> <value>`) into typed values in
/// `store`. A presence-only bool becomes true; unknown engine settings and invalid values are
/// ignored, so invalid command-line overrides cannot hide valid lower layers.
void PopulateEngineCommandLineSettings(const Config::SettingsRegistry& registry,
                                          const CommandLine::ICommandLine& commandLine,
                                          Config::IConfigManager& store);

/// Returns the active platform settings path, honoring an explicit override or using
/// `platforms/<target>/<user settings filename>` beside the user settings file.
[[nodiscard]] std::filesystem::path GetPlatformSettingsFilePath(const EngineConfig& config);

/// Attaches all four runtime stores to `settings` in the fixed project/user/platform/command-line
/// slots. Returns false if any attachment fails.
[[nodiscard]] bool AttachEngineSettingsLayers(Config::SettingsStack& settings,
                                                 const Config::IConfigManager& project,
                                                 const Config::IConfigManager& user,
                                                 const Config::IConfigManager& platform,
                                                 const Config::IConfigManager& commandLine) noexcept;

/// Applies every non-default value resolved by `settings` to the matching field of `config`.
/// EngineConfig remains the caller's base configuration when no project/user/platform/command-line
/// store supplies an effective value.
void ApplyEngineSettings(const Config::SettingsStack& settings, EngineConfig& config);
/// Compatibility convenience for a project-only stack: copies every setting `document` stores into
/// its matching field, leaving application-selected fields alone when the project does not set them.
void ApplyEngineProjectSettings(const Config::SettingsDocument& document, EngineConfig& config);

/// Pushes the runtime-mutable rendering settings from `config` (clear color, post-processing,
/// SSAO, contact shadows, SSR, shadow bias/atlas, culling, and default material samplers) onto
/// `renderer`. Used both at renderer construction and when saving Project Settings live in Editor.
void ApplyEngineConfigToRenderer3D(const EngineConfig& config, Render::IRenderer3D& renderer);

} // namespace UVE::Core
