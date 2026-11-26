// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "engine_project_settings.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "i_command_line.h"
#include "shader_cache_path_policy.h"
#include "logging_macros.h"

namespace UVE::Core {
namespace {

using Config::SettingDescriptor;
using Config::SettingValue;

[[nodiscard]] std::string_view GetPlatformSettingsTarget() noexcept {
#if defined(__ANDROID__)
    return "android";
#elif defined(__EMSCRIPTEN__)
    return "web";
#elif defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "apple";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

/// One engine project setting and the EngineConfig field it overrides.
struct EngineProjectSetting final {
    SettingDescriptor descriptor;
    void (*apply)(EngineConfig& config, const SettingValue& value) = nullptr;
    bool (*acceptsOverride)(const SettingValue& value) = nullptr;
};

[[nodiscard]] bool IsValidShaderCacheSettingValue(const SettingValue& value) {
    const std::string* const pathValue = std::get_if<std::string>(&value);
    return pathValue != nullptr && IsSafeShaderCachePath(std::filesystem::path(*pathValue));
}

[[nodiscard]] SettingDescriptor RestartRequired(SettingDescriptor descriptor) {
    descriptor.flags |= Config::kSettingFlagRestartRequired;
    return descriptor;
}

[[nodiscard]] SettingDescriptor PlatformOverride(SettingDescriptor descriptor) {
    descriptor.flags |= Config::kSettingFlagPerPlatform;
    return descriptor;
}

[[nodiscard]] SettingDescriptor CommandLineOnly(SettingDescriptor descriptor) {
    descriptor.flags |= Config::kSettingFlagHidden | Config::kSettingFlagNotPersisted;
    return descriptor;
}

[[nodiscard]] const std::vector<EngineProjectSetting>& GetEngineProjectSettings() {
    namespace Id = EngineProjectSettingId;
    const EngineConfig defaults{};
    static const std::vector<EngineProjectSetting> settings = {
        {RestartRequired(Config::MakeFloatSetting(
             std::string(Id::kPhysicsTicksPerSecond), defaults.fixedUpdateFps, 1.0, 1000.0, "Ticks Per Second",
             "Physics/Common", "How many fixed simulation steps run each second.")),
         [](EngineConfig& config, const SettingValue& value) { config.fixedUpdateFps = std::get<double>(value); }},
        {RestartRequired(Config::MakeFloatSetting(
             std::string(Id::kPhysicsMaxFrameTime), defaults.maxDeltaTimeSeconds, 0.01, 1.0, "Max Frame Time",
             "Physics/Common",
             "The longest frame, in seconds, the simulation catches up on. Anything longer - a stall, a "
             "breakpoint - is cut to this, so the simulation slows down instead of spiralling.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.maxDeltaTimeSeconds = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kPhysicsMaxStepsPerFrame), defaults.maxFixedStepsPerFrame, 1, 64,
             "Max Steps Per Frame",
             "Physics/Common",
             "The most fixed steps one frame runs to catch up. Higher keeps the simulation on time "
             "through slow frames; lower keeps a slow frame from getting slower still.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.maxFixedStepsPerFrame = static_cast<int>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(Config::MakeVector3Setting(
             std::string(Id::kPhysicsGravity),
             Config::SettingVector3{defaults.gravity.x, defaults.gravity.y, defaults.gravity.z}, -1000.0, 1000.0,
             "Gravity", "Physics/3D",
             "Acceleration every rigid body, character and particle falls with, in metres per second squared. "
             "Each body scales it by its own gravity scale.")),
         [](EngineConfig& config, const SettingValue& value) {
             const auto& gravity = std::get<Config::SettingVector3>(value);
             config.gravity = Math::Vector3{static_cast<float>(gravity.x), static_cast<float>(gravity.y),
                                               static_cast<float>(gravity.z)};
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kRenderBackendPreference),
             static_cast<std::int64_t>(defaults.renderBackendPreference),
             {{0, "Auto"}, {1, "OpenGL"}, {2, "Vulkan"}, {3, "Null"}}, "Renderer Backend",
             "Rendering/General",
             "Preferred backend at startup. Auto uses OpenGL; unavailable backends fall back to the next supported backend."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.renderBackendPreference =
                 static_cast<RenderBackendPreference>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kRenderResolutionScale), defaults.renderResolutionScale,
             kMinimumRenderResolutionScale, kMaximumRenderResolutionScale,
             "Resolution Scale", "Rendering/General",
             "Fixed 3D render-target scale, or the starting/upper scale when dynamic resolution is enabled. "
             "The presentation size is unchanged."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.renderResolutionScale = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kDynamicRenderResolutionEnabled), defaults.dynamicRenderResolutionEnabled,
             "Dynamic Resolution", "Rendering/General",
             "Adjust scene render scale to approach the target frame time; the controller uses wall-clock frame "
             "duration including presentation, not a GPU-only timer."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.dynamicRenderResolutionEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kMinimumRenderResolutionScale), defaults.minimumRenderResolutionScale,
             kMinimumRenderResolutionScale, kMaximumRenderResolutionScale,
             "Minimum Resolution Scale", "Rendering/General",
             "Lowest 3D render-target scale the dynamic resolution controller may use. It is clamped to the "
             "configured maximum scale if necessary."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.minimumRenderResolutionScale = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kRenderResolutionTargetFrameTimeMilliseconds),
             defaults.renderResolutionTargetFrameTimeMilliseconds,
             kMinimumRenderResolutionTargetFrameTimeMilliseconds,
             kMaximumRenderResolutionTargetFrameTimeMilliseconds, "Target Frame Time (ms)",
             "Rendering/General",
             "Whole-frame wall-clock budget, measured from frame start through presentation. Account for "
             "V-sync/display refresh; this is not a GPU-only timing budget."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.renderResolutionTargetFrameTimeMilliseconds = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFilePathSetting(
             std::string(Id::kShaderProgramCacheDirectory), defaults.shaderCachePath.generic_string(), 512U,
             "Program Binary Cache Directory", "Rendering/Shaders",
             "Directory relative to the process working directory for the existing OpenGL shader-program "
             "binary cache. Absolute paths and parent-directory segments are rejected; a platform "
             "subdirectory is appended automatically. "
             "This does not enable Vulkan pipeline-cache persistence or startup precompilation."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shaderCachePath = std::filesystem::path(std::get<std::string>(value));
         },
         IsValidShaderCacheSettingValue},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kShaderCompilationMode),
             static_cast<std::int64_t>(defaults.shaderCompilationMode),
             {{static_cast<std::int64_t>(ShaderCompilationMode::SynchronousOnDemand), "Synchronous on-demand"},
              {static_cast<std::int64_t>(ShaderCompilationMode::AsynchronousOnDemand), "Asynchronous on-demand"}},
             "Shader Compilation Mode", "Rendering/Shaders",
             "Choose when on-demand shader work runs. Synchronous completes source preparation and RHI "
             "compile/link in each Create* call and can stall the caller. Asynchronous prepares source on "
             "workers and compiles/links on the render thread as updates drain jobs. Ahead-of-time compilation "
             "and placeholder programs are not implemented."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shaderCompilationMode = static_cast<ShaderCompilationMode>(std::get<std::int64_t>(value));
         }},
        {[&] {
             SettingDescriptor descriptor = Config::MakeBoolSetting(
                 std::string(Id::kVulkanValidationLayersEnabled), defaults.vulkanValidationLayersEnabled,
                 "Vulkan Validation Layers", "Rendering/Debug",
                 "Request VK_LAYER_KHRONOS_validation for Vulkan. Debug builds default to enabled; if the "
                 "validation layer is not installed the engine warns and continues without it. When "
                 "VK_EXT_debug_utils is available, validation warnings and errors are sent to the engine log. "
                 "Has no effect on other render backends.");
             descriptor.flags |= Config::kSettingFlagAdvanced;
             return RestartRequired(PlatformOverride(std::move(descriptor)));
         }(),
         [](EngineConfig& config, const SettingValue& value) {
             config.vulkanValidationLayersEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kSpatialUpscalingMethod),
             static_cast<std::int64_t>(defaults.spatialUpscalingMethod),
             {{static_cast<std::int64_t>(SpatialUpscalingMethod::Bilinear), "Bilinear"},
              {static_cast<std::int64_t>(SpatialUpscalingMethod::Nearest), "Nearest neighbour"}},
             "Spatial Upscaling Method", "Rendering/General",
             "Resample the scene render target for presentation with bilinear filtering (default) or nearest "
             "neighbour. Temporal reconstruction is not offered because the renderer has no frame history "
             "or motion vectors."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.spatialUpscalingMethod =
                 static_cast<SpatialUpscalingMethod>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kToneMappingMethod), static_cast<std::int64_t>(defaults.toneMappingMethod),
             {{static_cast<std::int64_t>(ToneMappingMethod::LinearClamp), "Linear clamp"},
              {static_cast<std::int64_t>(ToneMappingMethod::Aces), "ACES"},
              {static_cast<std::int64_t>(ToneMappingMethod::SoftFilmic), "Soft filmic"}},
             "Tone Mapper", "Rendering/PostProcessing",
             "Map HDR scene values into display range with direct linear clipping, ACES filmic, or soft filmic "
             "highlight roll-off. Exposure remains controlled by the active WorldEnvironment."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.toneMappingMethod = static_cast<ToneMappingMethod>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kSharpeningAmount), defaults.sharpeningAmount, 0.0, 1.0,
             "Sharpening Amount", "Rendering/PostProcessing",
             "Apply an edge-clamped unsharp mask in the final scene reconstruction. 0 disables sharpening; "
             "larger values strengthen fine detail."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.sharpeningAmount = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kDitheringEnabled), defaults.ditheringEnabled,
             "Dithering", "Rendering/PostProcessing",
             "Add one-half display-step of deterministic screen-space noise before output quantization to reduce "
             "banding in smooth gradients."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ditheringEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kSsaoEnabled), defaults.ssaoEnabled, "Screen-Space Ambient Occlusion",
             "Rendering/SSAO", "Enable the depth-based screen-space ambient-occlusion pass."))),
         [](EngineConfig& config, const SettingValue& value) { config.ssaoEnabled = std::get<bool>(value); }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kSsaoRadius), defaults.ssaoRadius, 0.01, 10.0, "Radius", "Rendering/SSAO",
             "World-space radius sampled by the SSAO hemisphere kernel. An active WorldEnvironment keeps its "
             "existing radius override."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssaoRadius = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kSsaoIntensity), defaults.ssaoIntensity, 0.0, 4.0, "Intensity",
             "Rendering/SSAO", "Strength of the sampled occlusion. An active WorldEnvironment keeps its existing "
             "intensity override."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssaoIntensity = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kSsaoPower), defaults.ssaoPower, 0.1, 4.0, "Power", "Rendering/SSAO",
             "Shape the final unoccluded fraction with a power curve; 1 preserves the linear response."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssaoPower = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kSsaoQuality), static_cast<std::int64_t>(defaults.ssaoQuality),
             {{0, "Low (4 samples)"}, {1, "Medium (8 samples)"}, {2, "High (12 samples)"}},
             "Sample Quality", "Rendering/SSAO",
             "Trade kernel cost for detail. High uses all 12 samples and preserves the previous SSAO quality."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssaoQuality = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kSsaoBlurEnabled), defaults.ssaoBlurEnabled, "Blur", "Rendering/SSAO",
             "Apply a depth-aware separable bilateral blur to the AO term, smoothing half-resolution kernel noise while preserving edges."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssaoBlurEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kPostProcessAAMethod), static_cast<std::int64_t>(defaults.postProcessAAMethod),
             {{static_cast<std::int64_t>(PostProcessAAMethod::None), "None"},
              {static_cast<std::int64_t>(PostProcessAAMethod::FastApproximate), "Fast approximate"}},
             "Post-Process AA Method", "Rendering/AntiAliasing",
             "Select none or the implemented fast approximate edge filter. Temporal AA is not offered because "
             "frame history, motion vectors, and camera jitter are not implemented."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.postProcessAAMethod = static_cast<PostProcessAAMethod>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kScreenSpaceAAQuality), static_cast<std::int64_t>(defaults.screenSpaceAAQuality),
             {{0, "Low (3 edge taps)"}, {1, "Medium (5 edge taps)"}, {2, "High (7 edge taps)"}},
             "Screen-Space AA Quality", "Rendering/AntiAliasing",
             "Choose how many edge-direction taps the fast approximate filter uses, in addition to its fixed "
             "four-neighbour contrast test. This has no effect when the post-process AA method is None."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.screenSpaceAAQuality = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kShadowMapResolution), static_cast<std::int64_t>(defaults.shadowMapResolution),
             {{512, "512"}, {1024, "1024"}, {2048, "2048"}, {4096, "4096"}}, "Map Resolution", "Rendering/Shadows",
             "Width and height, in texels, of the directional light's shadow map."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowMapResolution = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kShadowBiasDefault), defaults.shadowBiasDefault, 0.0, 10.0,
             "Default Shadow Bias", "Rendering/Shadows",
             "Depth-bias default for LightEmitter3D shadow components with a negative/inherit Bias. The renderer "
             "scales this authored value by 0.025 for the angle-aware shadow shader; explicit per-light values "
             "take precedence."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowBiasDefault = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kShadowNormalBiasDefault), defaults.shadowNormalBiasDefault, 0.0, 10.0,
             "Default Shadow Normal Bias", "Rendering/Shadows",
             "Angle-aware normal-bias multiplier for LightEmitter3D shadow components with a negative/inherit "
             "Normal Bias. Explicit per-light values take precedence."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowNormalBiasDefault = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kShadowCascadeSplitBlend), defaults.shadowCascadeSplitLambda, 0.0, 1.0,
             "Default Cascade Split Blend", "Rendering/Shadows",
             "How directional shadow cascades are distributed when a light leaves Split Blend at Inherit: 0 is "
             "uniform, 1 is logarithmic. Explicit per-light values take precedence."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowCascadeSplitLambda = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kShadowFilter), static_cast<std::int64_t>(defaults.shadowPcfKernelRadius),
             {{0, "Hard"}, {1, "Soft (3x3)"}, {2, "Softer (5x5)"}}, "Filter", "Rendering/Shadows",
             "How shadow edges are softened. Softer costs more samples per pixel."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowPcfKernelRadius = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kFrustumCullingMode), static_cast<std::int64_t>(defaults.frustumCullingMode),
             {{static_cast<std::int64_t>(FrustumCullingMode::Enabled), "Enabled"},
              {static_cast<std::int64_t>(FrustumCullingMode::DebugFreeze), "Debug freeze (draw everything)"}},
             "Frustum Culling", "Rendering/Culling",
             "Enabled rejects meshes, primitives, surface instances and decals outside the camera frustum. "
             "Debug freeze submits all of them regardless of where the camera looks, which answers \"is it "
             "off-screen or is it missing?\" at the cost of drawing the whole scene. Hidden flags, world "
             "partitioning, visibility regions, LOD and SurfaceInstance draw distances, render layers, "
             "Occluder3D occlusion and the shadow cascades keep culling in both modes."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.frustumCullingMode = static_cast<FrustumCullingMode>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kGpuOcclusionCulling), defaults.gpuOcclusionCullingEnabled,
             "GPU Occlusion Culling", "Rendering/Culling",
             "Skip meshes the previous frame proved hidden behind geometry already drawn. Costs an "
             "occlusion query and a proxy draw per candidate, so it pays off on scenes with many "
             "objects and little benefit on small ones. A skip only happens when the query returned "
             "zero samples and the camera has not moved since it was issued. Built-in primitives are "
             "not covered yet; this applies to meshes and surface instances."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.gpuOcclusionCullingEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting(
             std::string(Id::kAutoSaveInterval), defaults.autoSaveIntervalSeconds, 10.0, 3600.0,
             "Auto-Save Interval", "Application/Save", "Seconds between writes to the game's auto-save slot.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.autoSaveIntervalSeconds = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting(std::string(Id::kQuitOnLastWindowClosed),
                                    defaults.quitOnLastWindowClosed, "Quit on Last Window Closed",
                                    "Application/Runtime",
                                    "Request application shutdown when the last native window is closed.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.quitOnLastWindowClosed = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeColorSetting(std::string(Id::kBootBackgroundColor),
                                     Config::SettingColor{defaults.backgroundColor[0],
                                                             defaults.backgroundColor[1],
                                                             defaults.backgroundColor[2],
                                                             defaults.backgroundColor[3]},
                                     true, "Background Color", "Application/Boot",
                                     "RGBA clear color shown behind an empty scene and during the boot screen.")),
         [](EngineConfig& config, const SettingValue& value) {
             const Config::SettingColor color = std::get<Config::SettingColor>(value);
             config.backgroundColor = {color.r, color.g, color.b, color.a};
         }},
        {RestartRequired(Config::MakeFilePathSetting(std::string(Id::kBootSplashImage), "", 256U,
                                        "Boot Screen Image", "Application/Boot",
                                        "Optional Content-relative image rendered over the background during standalone startup.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.bootSplashImagePath = std::get<std::string>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting(std::string(Id::kBootSplashFadeSeconds),
                                     defaults.splashFadeSeconds, 0.0, 60.0, "Fade Time", "Application/Boot",
                                     "Boot-screen fade duration in seconds.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.splashFadeSeconds = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting(std::string(Id::kBootSplashMinimumDisplaySeconds),
                                     defaults.splashMinimumDisplaySeconds, 0.0, 600.0, "Minimum Display Time",
                                     "Application/Boot", "Minimum boot-screen display time in seconds.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.splashMinimumDisplaySeconds = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting(std::string(Id::kBootSplashSkippable), defaults.splashSkippable,
                                    "Skippable", "Application/Boot",
                                    "Allow users to skip the boot screen after its minimum display time.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.splashSkippable = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting(std::string(Id::kSkipBootSplashInEditorPlayMode),
                                    defaults.skipSplashInEditorPlayMode, "Skip in Editor Play Mode",
                                    "Application/Boot",
                                    "Do not show the boot screen when starting a game from the Editor.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.skipSplashInEditorPlayMode = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting(std::string(Id::kCrashHandlerEnabled), defaults.crashHandlerEnabled,
                                    "Crash Handler", "Application/Crash Reporting",
                                    "Install native unhandled-exception and fatal-signal reporting for standalone runs.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.crashHandlerEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeFilePathSetting(std::string(Id::kCrashDumpDirectory), "crash-dumps", 256U,
                                        "Crash Dump Directory", "Application/Crash Reporting",
                                        "Directory under user data for crash reports and platform-native minidumps.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.crashDumpDirectory = std::get<std::string>(value);
         }},
        {RestartRequired(Config::MakeStringSetting(std::string(Id::kSymbolUploadEndpoint), "", 2048U,
                                      "Symbol Upload Endpoint", "Application/Crash Reporting",
                                      "Optional HTTPS endpoint reserved for host integrations; the runtime never uploads automatically.")),
         [](EngineConfig& config, const SettingValue& value) {
             config.symbolUploadEndpoint = std::get<std::string>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             std::string(Id::kWindowWidth), defaults.windowWidth, 1, 16384, "Width", "Window/Display",
             "Initial window width in framebuffer-independent screen units."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.windowWidth = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             std::string(Id::kWindowHeight), defaults.windowHeight, 1, 16384, "Height", "Window/Display",
             "Initial window height in framebuffer-independent screen units."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.windowHeight = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kWindowMode), static_cast<std::int64_t>(defaults.windowMode),
             {{0, "Windowed"}, {1, "Maximized"}, {2, "Fullscreen"},
              {3, "Exclusive Fullscreen"}, {4, "Borderless"}}, "Mode", "Window/Display",
             "Windowed and maximized are decorated unless Borderless is enabled. Fullscreen uses a "
             "borderless desktop-sized window; Exclusive Fullscreen attaches to the monitor mode."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.windowMode = static_cast<Platform::WindowMode>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(Config::MakeBoolSetting(
             std::string(Id::kWindowResizable), defaults.windowResizable, "Resizable", "Window/Display")),
         [](EngineConfig& config, const SettingValue& value) {
             config.windowResizable = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting(
             std::string(Id::kWindowBorderless), defaults.windowBorderless, "Borderless", "Window/Display")),
         [](EngineConfig& config, const SettingValue& value) { config.windowBorderless = std::get<bool>(value); }},
        {RestartRequired(Config::MakeBoolSetting(
             std::string(Id::kWindowAlwaysOnTop), defaults.windowAlwaysOnTop, "Always on Top", "Window/Display")),
         [](EngineConfig& config, const SettingValue& value) { config.windowAlwaysOnTop = std::get<bool>(value); }},
        {RestartRequired(Config::MakeBoolSetting(
             std::string(Id::kWindowTransparent), defaults.windowTransparent, "Transparent Background",
             "Window/Display", "Requests a transparent native framebuffer; compositor/backend support is required.")),
         [](EngineConfig& config, const SettingValue& value) { config.windowTransparent = std::get<bool>(value); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowMinimumWidth), 0, 0, 16384, "Minimum Width", "Window/Size Limits",
             "Zero leaves the width unconstrained.")),
         [](EngineConfig& config, const SettingValue& value) { config.windowMinimumWidth = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowMinimumHeight), 0, 0, 16384, "Minimum Height", "Window/Size Limits",
             "Zero leaves the height unconstrained.")),
         [](EngineConfig& config, const SettingValue& value) { config.windowMinimumHeight = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowMaximumWidth), 0, 0, 16384, "Maximum Width", "Window/Size Limits",
             "Zero leaves the width unconstrained.")),
         [](EngineConfig& config, const SettingValue& value) { config.windowMaximumWidth = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowMaximumHeight), 0, 0, 16384, "Maximum Height", "Window/Size Limits",
             "Zero leaves the height unconstrained.")),
         [](EngineConfig& config, const SettingValue& value) { config.windowMaximumHeight = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeBoolSetting(
             std::string(Id::kWindowPositionSpecified), defaults.windowPositionSpecified, "Set Initial Position",
             "Window/Placement")),
         [](EngineConfig& config, const SettingValue& value) { config.windowPositionSpecified = std::get<bool>(value); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowPositionX), 0, -131072, 131072, "X", "Window/Placement")),
         [](EngineConfig& config, const SettingValue& value) { config.windowPositionX = static_cast<std::int32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowPositionY), 0, -131072, 131072, "Y", "Window/Placement")),
         [](EngineConfig& config, const SettingValue& value) { config.windowPositionY = static_cast<std::int32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeStringSetting(
             std::string(Id::kWindowMonitor), "", 256U, "Monitor", "Window/Placement",
             "Exact monitor name; empty selects the primary monitor.")),
         [](EngineConfig& config, const SettingValue& value) { config.windowMonitorName = std::get<std::string>(value); }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kWindowHighDpiAware), defaults.highDpiAware, "High-DPI Aware", "Window/Scaling"))),
         [](EngineConfig& config, const SettingValue& value) { config.highDpiAware = std::get<bool>(value); }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kWindowPerMonitorScaling), defaults.perMonitorScaling, "Per-Monitor Scaling",
             "Window/Scaling"))),
         [](EngineConfig& config, const SettingValue& value) { config.perMonitorScaling = std::get<bool>(value); }},
        {RestartRequired(Config::MakeFloatSetting(
             std::string(Id::kWindowContentScaleOverride), defaults.contentScaleOverride, 0.0, Platform::kMaximumContentScaleOverride,
             "Content Scale Override", "Window/Scaling", "Zero follows the monitor's reported content scale.")),
         [](EngineConfig& config, const SettingValue& value) { config.contentScaleOverride = std::get<double>(value); }},
        {RestartRequired(Config::MakeEnumSetting(
             std::string(Id::kWindowStretchMode), static_cast<std::int64_t>(defaults.stretchMode),
             {{0, "Disabled"}, {1, "Canvas Items"}, {2, "Viewport"}}, "Stretch Mode", "Window/Scaling")),
         [](EngineConfig& config, const SettingValue& value) { config.stretchMode = static_cast<Platform::StretchMode>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeEnumSetting(
             std::string(Id::kWindowAspectPolicy), static_cast<std::int64_t>(defaults.aspectPolicy),
             {{0, "Ignore"}, {1, "Keep"}, {2, "Keep Width"}, {3, "Keep Height"}, {4, "Expand"}},
             "Aspect Policy", "Window/Scaling")),
         [](EngineConfig& config, const SettingValue& value) { config.aspectPolicy = static_cast<Platform::AspectPolicy>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeBoolSetting(
             std::string(Id::kWindowIntegerOnlyScaling), defaults.integerOnlyScaling, "Integer-Only Scaling",
             "Window/Scaling", "Avoid fractional scale factors for pixel-art viewports.")),
         [](EngineConfig& config, const SettingValue& value) { config.integerOnlyScaling = std::get<bool>(value); }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kWindowOrientation), static_cast<std::int64_t>(defaults.orientation),
             {{0, "Auto"}, {1, "Landscape"}, {2, "Portrait"}, {3, "Landscape Left"},
              {4, "Landscape Right"}, {5, "Portrait Upside Down"}}, "Orientation", "Window/Handheld"))),
         [](EngineConfig& config, const SettingValue& value) { config.orientation = static_cast<Platform::DisplayOrientation>(std::get<std::int64_t>(value)); }},
        {RestartRequired(PlatformOverride(Config::MakeStringListSetting(
             std::string(Id::kWindowAllowedOrientations), {"landscape", "portrait"}, 6U, 24U,
             "Allowed Orientations", "Window/Handheld"))),
         [](EngineConfig& config, const SettingValue& value) {
             const auto& names = std::get<Config::SettingStringList>(value);
             std::vector<Platform::DisplayOrientation> parsed;
             bool valid = !names.empty();
             for (const std::string& name : names) {
                 const auto orientation = Platform::ParseDisplayOrientation(name);
                 if (!orientation || *orientation == Platform::DisplayOrientation::Auto ||
                     std::find(parsed.begin(), parsed.end(), *orientation) != parsed.end()) {
                     valid = false;
                     break;
                 }
                 parsed.push_back(*orientation);
             }
             if (valid && (config.orientation == Platform::DisplayOrientation::Auto ||
                           std::find(parsed.begin(), parsed.end(), config.orientation) != parsed.end())) {
                 config.allowedOrientations = std::move(parsed);
             }
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             std::string(Id::kWindowVSyncMode), static_cast<std::int64_t>(defaults.vsyncMode),
             {{0, "Off"}, {1, "On"}, {2, "Adaptive"}, {3, "Mailbox"}}, "V-Sync Mode", "Window/Performance"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.vsyncMode = static_cast<Platform::VSyncMode>(std::get<std::int64_t>(value));
             config.vsyncModeExplicit = true;
             config.vsyncEnabled = config.vsyncMode != Platform::VSyncMode::Off;
         }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowFocusedFrameRateCap), 0, 0, 1000, "Focused Frame Rate Cap",
             "Window/Performance", "Frames per second; zero is uncapped.")),
         [](EngineConfig& config, const SettingValue& value) { config.focusedFrameRateCap = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowUnfocusedFrameRateCap), 0, 0, 1000, "Unfocused Frame Rate Cap",
             "Window/Performance", "Frames per second while unfocused; zero is uncapped.")),
         [](EngineConfig& config, const SettingValue& value) { config.unfocusedFrameRateCap = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kWindowAllowDisplaySleep), defaults.allowDisplaySleep, "Allow Display Sleep",
             "Window/Performance", "When disabled, requests that the platform keep the display awake."))),
         [](EngineConfig& config, const SettingValue& value) { config.allowDisplaySleep = std::get<bool>(value); }},
        {RestartRequired(Config::MakeFilePathSetting(
             std::string(Id::kWindowCursorImage), "", 256U, "Cursor Image", "Window/Cursor",
             "Content-relative PNG cursor image; decoded as RGBA8 by the project runtime.")),
         [](EngineConfig& config, const SettingValue& value) { config.cursorImagePath = std::get<std::string>(value); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowCursorHotspotX), 0, 0, 8192, "Hotspot X", "Window/Cursor")),
         [](EngineConfig& config, const SettingValue& value) { config.cursorHotspotX = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeIntSetting(
             std::string(Id::kWindowCursorHotspotY), 0, 0, 8192, "Hotspot Y", "Window/Cursor")),
         [](EngineConfig& config, const SettingValue& value) { config.cursorHotspotY = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequired(Config::MakeBoolSetting(std::string(Id::kWindowCursorVisible), defaults.cursorVisible,
                                    "Visible by Default", "Window/Cursor")),
         [](EngineConfig& config, const SettingValue& value) { config.cursorVisible = std::get<bool>(value); }},
        {RestartRequired(Config::MakeBoolSetting(std::string(Id::kWindowCursorConfined), defaults.cursorConfinedToWindow,
                                    "Confined to Window", "Window/Cursor",
                                    "Capture the pointer inside the game window; backend behavior varies by host.")),
         [](EngineConfig& config, const SettingValue& value) { config.cursorConfinedToWindow = std::get<bool>(value); }},
        {RestartRequired(Config::MakeStringSetting(
             std::string(Id::kWindowTitleFormat), defaults.windowTitleFormat, 256U, "Title Format",
             "Window/Title", "Supported tokens: {productName}, {projectName}, and {sceneName}.")),
         [](EngineConfig& config, const SettingValue& value) { config.windowTitleFormat = std::get<std::string>(value); }},
        {RestartRequired(Config::MakeBoolSetting(std::string(Id::kWindowAppendSceneInEditorPlay),
                                    defaults.appendSceneNameInEditorPlayMode, "Append Scene in Editor Play",
                                    "Window/Title", "Append the active scene name while the editor is playing.")),
         [](EngineConfig& config, const SettingValue& value) { config.appendSceneNameInEditorPlayMode = std::get<bool>(value); }},
        {RestartRequired(Config::MakeFloatSetting("physics.common.defaultLinearDamping",
                                     defaults.defaultLinearDamping, 0.0, 100.0,
                                     "Default Linear Damping", "Physics/Common")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultLinearDamping = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("physics.common.defaultAngularDamping",
                                     defaults.defaultAngularDamping, 0.0, 100.0,
                                     "Default Angular Damping", "Physics/Common")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultAngularDamping = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("physics.common.sleepThreshold", defaults.sleepThreshold,
                                     0.0, 10.0, "Sleep Threshold", "Physics/Common")),
         [](EngineConfig& config, const SettingValue& value) {
             config.sleepThreshold = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("physics.common.sleepTimeSeconds", defaults.sleepTimeSeconds,
                                     0.0, 60.0, "Sleep Time", "Physics/Common")),
         [](EngineConfig& config, const SettingValue& value) {
             config.sleepTimeSeconds = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("physics.common.allowSleepingDefault",
                                    defaults.allowSleepingDefault, "Allow Sleeping by Default",
                                    "Physics/Common")),
         [](EngineConfig& config, const SettingValue& value) {
             config.allowSleepingDefault = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("physics.solver.positionIterations",
                                   defaults.physicsPositionIterations, 1, 64,
                                   "Position Iterations", "Physics/Solver")),
         [](EngineConfig& config, const SettingValue& value) {
             config.physicsPositionIterations = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("physics.solver.velocityIterations",
                                   defaults.physicsVelocityIterations, 1, 64,
                                   "Velocity Iterations", "Physics/Solver")),
         [](EngineConfig& config, const SettingValue& value) {
             config.physicsVelocityIterations = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("physics.contacts.contactOffset",
                                     defaults.physicsContactOffset, 0.0001, 1.0,
                                     "Contact Offset", "Physics/Contacts")),
         [](EngineConfig& config, const SettingValue& value) {
             config.physicsContactOffset = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("physics.contacts.penetrationSlop",
                                     defaults.physicsPenetrationSlop, 0.0, 1.0,
                                     "Penetration Slop", "Physics/Contacts")),
         [](EngineConfig& config, const SettingValue& value) {
             config.physicsPenetrationSlop = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("physics.contacts.maxDepenetrationVelocity",
                                     defaults.physicsMaxDepenetrationVelocity, 0.0, 1000.0,
                                     "Max Depenetration Velocity", "Physics/Contacts")),
         [](EngineConfig& config, const SettingValue& value) {
             config.physicsMaxDepenetrationVelocity = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeLayerMaskSetting("physics.layers.defaultCollisionLayer",
                                         defaults.defaultCollisionLayer,
                                         "Default Collision Layer", "Physics/Layers")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultCollisionLayer =
                 static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(Config::MakeLayerMaskSetting("physics.layers.defaultCollisionMask",
                                         defaults.defaultCollisionMask,
                                         "Default Collision Mask", "Physics/Layers")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultCollisionMask =
                 static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequired(Config::MakeBoolSetting("physics.common.interpolationDefault",
                                    defaults.physicsInterpolationDefault,
                                    "Physics Interpolation Default", "Physics/Common")),
         [](EngineConfig& config, const SettingValue& value) {
             config.physicsInterpolationDefault = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("physics.simulation.deterministicMode",
                                    defaults.physicsDeterministicMode, "Deterministic Mode",
                                    "Physics/Simulation")),
         [](EngineConfig& config, const SettingValue& value) {
             config.physicsDeterministicMode = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("input.actions.defaultDeadzone",
                                     defaults.inputDefaultDeadzone, 0.0, 0.95,
                                     "Default Action Deadzone", "Input/Actions")),
         [](EngineConfig& config, const SettingValue& value) {
             config.inputDefaultDeadzone = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("input.mouse.sensitivity", defaults.inputMouseSensitivity,
                                     0.01, 20.0, "Mouse Sensitivity", "Input/Mouse")),
         [](EngineConfig& config, const SettingValue& value) {
             config.inputMouseSensitivity = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("input.haptics.enabled", defaults.inputHapticsEnabled,
                                    "Enable Haptics", "Input/Haptics")),
         [](EngineConfig& config, const SettingValue& value) {
             config.inputHapticsEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("audio.output.sampleRate", defaults.audioSampleRate, 8000,
                                   192000, "Sample Rate", "Audio/Output")),
         [](EngineConfig& config, const SettingValue& value) {
             config.audioSampleRate = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("audio.buses.masterVolume", defaults.audioMasterVolume, 0.0,
                                     2.0, "Master Volume", "Audio/Buses")),
         [](EngineConfig& config, const SettingValue& value) {
             config.audioMasterVolume = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("audio.buses.musicVolume", defaults.audioMusicVolume, 0.0,
                                     2.0, "Music Volume", "Audio/Buses")),
         [](EngineConfig& config, const SettingValue& value) {
             config.audioMusicVolume = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("audio.buses.sfxVolume", defaults.audioSfxVolume, 0.0,
                                     2.0, "SFX Volume", "Audio/Buses")),
         [](EngineConfig& config, const SettingValue& value) {
             config.audioSfxVolume = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("audio.buses.voiceVolume", defaults.audioVoiceVolume, 0.0,
                                     2.0, "Voice Volume", "Audio/Buses")),
         [](EngineConfig& config, const SettingValue& value) {
             config.audioVoiceVolume = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("audio.voices.maxVoices", defaults.audioMaxVoices, 1, 512,
                                   "Max Voices", "Audio/Voices")),
         [](EngineConfig& config, const SettingValue& value) {
             config.audioMaxVoices = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("audio.window.muteOnFocusLoss",
                                    defaults.audioMuteOnFocusLoss, "Mute on Focus Loss",
                                    "Audio/Window")),
         [](EngineConfig& config, const SettingValue& value) {
             config.audioMuteOnFocusLoss = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("animation.playback.defaultBlendTime",
                                     defaults.animationDefaultBlendTime, 0.0, 10.0,
                                     "Default Blend Time", "Animation/Playback")),
         [](EngineConfig& config, const SettingValue& value) {
             config.animationDefaultBlendTime = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("animation.ik.defaultSolverIterations",
                                   defaults.animationIkDefaultIterations, 1, 128,
                                   "Default IK Iterations", "Animation/IK")),
         [](EngineConfig& config, const SettingValue& value) {
             config.animationIkDefaultIterations = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("navigation.bake.cellSize", defaults.navCellSize, 0.05,
                                     10.0, "Cell Size", "Navigation/Bake")),
         [](EngineConfig& config, const SettingValue& value) {
             config.navCellSize = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("navigation.bake.agentRadius", defaults.navAgentRadius,
                                     0.05, 10.0, "Agent Radius", "Navigation/Bake")),
         [](EngineConfig& config, const SettingValue& value) {
             config.navAgentRadius = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("navigation.avoidance.enabled", defaults.navAvoidanceEnabled,
                                    "Enable Avoidance", "Navigation/Avoidance")),
         [](EngineConfig& config, const SettingValue& value) {
             config.navAvoidanceEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("network.transport.defaultPort", defaults.networkDefaultPort,
                                   1, 65535, "Default Port", "Networking/Transport")),
         [](EngineConfig& config, const SettingValue& value) {
             config.networkDefaultPort = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("network.rates.tickRateHz", defaults.networkTickRateHz, 1.0,
                                     240.0, "Network Tick Rate", "Networking/Rates")),
         [](EngineConfig& config, const SettingValue& value) {
             config.networkTickRateHz = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("network.lagCompensation.windowMilliseconds",
                                     defaults.networkLagCompensationWindowMs, 0.0, 2000.0,
                                     "Lag Compensation Window", "Networking/LagCompensation")),
         [](EngineConfig& config, const SettingValue& value) {
             config.networkLagCompensationWindowMs = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeStringSetting("localisation.defaultLocale",
                                      defaults.localisationDefaultLocale, 32U, "Default Locale",
                                      "Localisation/General")),
         [](EngineConfig& config, const SettingValue& value) {
             config.localisationDefaultLocale = std::get<std::string>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("localisation.pseudolocalisationEnabled",
                                    defaults.pseudolocalisationEnabled, "Pseudolocalisation",
                                    "Localisation/Debug")),
         [](EngineConfig& config, const SettingValue& value) {
             config.pseudolocalisationEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeFilePathSetting(
             "paths.saveGameDirectory", defaults.saveDirectoryPath.generic_string(), 256U,
             "Save Game Directory", "Paths/Directories")),
         [](EngineConfig& config, const SettingValue& value) {
             config.saveDirectoryPath = std::get<std::string>(value);
         }},
        {RestartRequired(Config::MakeFilePathSetting(
             "paths.logFilePath", defaults.logFilePath.generic_string(), 256U, "Log File Path",
             "Paths/Logging")),
         [](EngineConfig& config, const SettingValue& value) {
             config.logFilePath = std::get<std::string>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "quality.activePreset", defaults.activeQualityTier,
             {{0, "Low"}, {1, "Medium"}, {2, "High"}, {3, "Ultra"}}, "Quality Preset",
             "Quality/Presets"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.activeQualityTier = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeStringListSetting(
             "platform.targetFeatureTags", defaults.platformFeatureTags, 32U, 64U,
             "Target Feature Tags", "Platform/Capabilities"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.platformFeatureTags = std::get<Config::SettingStringList>(value);
         }},
        {RestartRequired(Config::MakeStringSetting("packaging.defaultPresetName",
                                      defaults.packagingDefaultPreset, 64U, "Default Preset",
                                      "Packaging/Presets")),
         [](EngineConfig& config, const SettingValue& value) {
             config.packagingDefaultPreset = std::get<std::string>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("packaging.encryptAssets", defaults.packagingEncryptAssets,
                                    "Encrypt Packaged Assets", "Packaging/Security")),
         [](EngineConfig& config, const SettingValue& value) {
             config.packagingEncryptAssets = std::get<bool>(value);
         }},
        // Part 1.3 Extended Rendering Settings
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.general.renderingMethod", defaults.renderingMethod,
             {{0, "Forward"}, {1, "Forward+"}, {2, "Deferred"}, {3, "Mobile"}}, "Rendering Method",
             "Rendering/General"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.renderingMethod = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeEnumSetting(
             "rendering.general.colorSpace", defaults.colorSpaceMode,
             {{0, "Linear sRGB"}, {1, "Gamma sRGB"}}, "Working Colour Space", "Rendering/General")),
         [](EngineConfig& config, const SettingValue& value) {
             config.colorSpaceMode = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.hdr.enabled", defaults.hdrRenderingEnabled, "HDR Output", "Rendering/HDR"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.hdrRenderingEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.hdr.transferFunction", defaults.hdrTransferFunction,
             {{0, "PQ (ST.2084)"}, {1, "HLG"}, {2, "scRGB Linear"}}, "HDR Transfer Function", "Rendering/HDR"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.hdrTransferFunction = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("rendering.hdr.peakLuminanceNits", defaults.hdrPeakLuminanceNits,
                                     100.0, 10000.0, "Peak Luminance (nits)", "Rendering/HDR")),
         [](EngineConfig& config, const SettingValue& value) {
             config.hdrPeakLuminanceNits = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("rendering.hdr.paperWhiteNits", defaults.hdrPaperWhiteNits,
                                     80.0, 1000.0, "Paper White (nits)", "Rendering/HDR")),
         [](EngineConfig& config, const SettingValue& value) {
             config.hdrPaperWhiteNits = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.general.renderTargetFormat", defaults.renderTargetFormat,
             {{0, "RGBA8 UNorm"}, {1, "RGB10A2 UNorm"}, {2, "RGBA16 Float"}}, "Render Target Precision",
             "Rendering/General"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.renderTargetFormat = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.threading.multiThreadedRendering", defaults.multiThreadedRendering,
             "Multi-Threaded Rendering", "Rendering/Threading"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.multiThreadedRendering = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             "rendering.threading.commandBufferThreads", defaults.commandBufferThreadCount,
             1, 64, "Command Buffer Threads", "Rendering/Threading"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.commandBufferThreadCount = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.antialiasing.msaa3dSamples", defaults.msaa3dSamples,
             {{1, "Disabled (1x)"}, {2, "2x"}, {4, "4x"}, {8, "8x"}}, "3D MSAA Samples",
             "Rendering/AntiAliasing"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.msaa3dSamples = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             "rendering.antialiasing.taaSamples", defaults.taaSampleCount, 2, 16, "TAA Sample Count",
             "Rendering/AntiAliasing"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.taaSampleCount = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.antialiasing.taaJitterPattern", defaults.taaJitterPattern,
             {{0, "Halton (2,3)"}, {1, "Blue Noise"}, {2, "Uniform 2x"}}, "TAA Jitter Pattern",
             "Rendering/AntiAliasing"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.taaJitterPattern = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             "rendering.antialiasing.taaSharpening", defaults.taaSharpeningAmount, 0.0, 1.0,
             "TAA Sharpening", "Rendering/AntiAliasing"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.taaSharpeningAmount = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.antialiasing.alphaToCoverage", defaults.alphaToCoverageEnabled,
             "Alpha-to-Coverage", "Rendering/AntiAliasing"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.alphaToCoverageEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.shadows.atlasSize", defaults.shadowAtlasSize,
             {{1024, "1024"}, {2048, "2048"}, {4096, "4096"}, {8192, "8192"}}, "Shadow Atlas Size",
             "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowAtlasSize = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             "rendering.shadows.directionalSplitCount", defaults.directionalShadowSplitCount, 1, 4,
             "Directional Split Count", "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.directionalShadowSplitCount = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             "rendering.shadows.maxDistance", defaults.shadowMaxDistance, 1.0, 10000.0,
             "Shadow Max Distance", "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowMaxDistance = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.shadows.depthFormatBits", defaults.shadowDepthFormatBits,
             {{16, "16-bit"}, {32, "32-bit"}}, "Shadow Depth Format", "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.shadowDepthFormatBits = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.shadows.pointSpotCubeResolution", defaults.pointSpotShadowResolution,
             {{256, "256"}, {512, "512"}, {1024, "1024"}, {2048, "2048"}}, "Point/Spot Cube Resolution",
             "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.pointSpotShadowResolution = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             std::string(Id::kContactShadowsEnabled), defaults.contactShadowsEnabled,
             "Contact Shadows", "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.contactShadowsEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kContactShadowsLength), defaults.contactShadowsLength, 0.01, 2.0,
             "Contact Shadow Length", "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.contactShadowsLength = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             std::string(Id::kContactShadowsSteps), defaults.contactShadowsStepCount, 1, 16,
             "Contact Shadow Steps", "Rendering/Shadows"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.contactShadowsStepCount = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kContactShadowsThickness), defaults.contactShadowsThickness, 0.0, 5.0,
             "Contact Shadow Thickness", "Rendering/Shadows",
             "How far behind the march a depth sample may sit and still count as touching geometry. Larger values "
             "catch thicker contact and start darkening from geometry that is merely nearby."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.contactShadowsThickness = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             std::string(Id::kContactShadowsStrength), defaults.contactShadowsStrength, 0.0, 1.0,
             "Contact Shadow Strength", "Rendering/Shadows",
             "Fraction of the direct light a fully occluded march removes. 1.0 reads as a hole; the default keeps "
             "some lift so contact reads as a seam rather than a cut."))),
         [](EngineConfig& config, const SettingValue& value) {
             config.contactShadowsStrength = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.gi.ssilEnabled", defaults.ssilEnabled, "Screen-Space Indirect Lighting",
             "Rendering/GI"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssilEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.gi.ssrEnabled", defaults.ssrEnabled, "Screen-Space Reflections",
             "Rendering/GI"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssrEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             "rendering.gi.ssrMaxSteps", defaults.ssrMaxSteps, 8, 256, "SSR Max Steps",
             "Rendering/GI"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.ssrMaxSteps = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.gi.reflectionProbeAtlasSize", defaults.reflectionProbeAtlasSize,
             {{512, "512"}, {1024, "1024"}, {2048, "2048"}, {4096, "4096"}}, "Reflection Probe Atlas Size",
             "Rendering/GI"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.reflectionProbeAtlasSize = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.gi.voxelGiQuality", defaults.voxelGiQuality,
             {{0, "Low"}, {1, "Medium"}, {2, "High"}, {3, "Ultra"}}, "Voxel GI Quality",
             "Rendering/GI"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.voxelGiQuality = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("rendering.gi.lightmapTexelDensity", defaults.lightmapTexelDensity,
                                      1.0, 256.0, "Lightmap Texel Density", "Rendering/GI")),
         [](EngineConfig& config, const SettingValue& value) {
             config.lightmapTexelDensity = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("rendering.gi.lightProbeDefaultDensity", defaults.lightProbeDefaultDensity,
                                      0.1, 64.0, "Light Probe Default Density", "Rendering/GI")),
         [](EngineConfig& config, const SettingValue& value) {
             config.lightProbeDefaultDensity = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.postProcessing.autoExposureEnabled", defaults.autoExposureEnabled,
             "Auto-Exposure", "Rendering/PostProcessing"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.autoExposureEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeAssetReferenceSetting(
             "rendering.postProcessing.colorGradingLutAsset", defaults.colorGradingLutAsset, 256U,
             "Colour Grading LUT", "Rendering/PostProcessing")),
         [](EngineConfig& config, const SettingValue& value) {
             config.colorGradingLutAsset = std::get<std::string>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.postProcessing.volumetricFogEnabled", defaults.volumetricFogEnabled,
             "Volumetric Fog", "Rendering/PostProcessing"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.volumetricFogEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("rendering.camera.defaultNearClip", defaults.defaultNearClipPlane,
                                      0.001, 10.0, "Default Near Clip Plane", "Rendering/Culling")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultNearClipPlane = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("rendering.camera.defaultFarClip", defaults.defaultFarClipPlane,
                                      10.0, 1000000.0, "Default Far Clip Plane", "Rendering/Culling")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultFarClipPlane = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.culling.occlusionCullingEnabled", defaults.occlusionCullingEnabled,
             "Occlusion Culling", "Rendering/Culling"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.occlusionCullingEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             "rendering.lod.globalBias", defaults.globalLodBias, -4.0, 4.0, "Global LOD Bias",
             "Rendering/LOD"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.globalLodBias = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("rendering.lod.autoGenerateDefault", defaults.meshLodAutoGenerateDefault,
                                     "Auto-Generate Mesh LODs", "Rendering/LOD")),
         [](EngineConfig& config, const SettingValue& value) {
             config.meshLodAutoGenerateDefault = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("rendering.batching.instancingMinCount", defaults.instancingMinCount,
                                    2, 1024, "Instancing Threshold", "Rendering/Batching")),
         [](EngineConfig& config, const SettingValue& value) {
             config.instancingMinCount = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("rendering.batching.staticBatchingEnabled", defaults.staticBatchingEnabled,
                                     "Static Batching", "Rendering/Batching")),
         [](EngineConfig& config, const SettingValue& value) {
             config.staticBatchingEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.batching.indirectDrawEnabled", defaults.indirectDrawEnabled,
             "GPU Indirect Draw", "Rendering/Batching"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.indirectDrawEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("rendering.budgets.maxDrawCallsWarning", defaults.maxDrawCallsWarning,
                                   100, 100000, "Max Draw Calls Warning", "Rendering/Budgets")),
         [](EngineConfig& config, const SettingValue& value) {
             config.maxDrawCallsWarning = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.textures.defaultFilter", defaults.defaultTextureFilter,
             {{0, "Nearest"}, {1, "Bilinear"}, {2, "Trilinear"}}, "Default Texture Filter",
             "Rendering/Textures"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultTextureFilter = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeEnumSetting(
             "rendering.textures.defaultAnisotropy", defaults.defaultAnisotropyLevel,
             {{1, "1x"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}}, "Default Anisotropy",
             "Rendering/Textures"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultAnisotropyLevel = std::get<std::int64_t>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeFloatSetting(
             "rendering.textures.globalMipLodBias", defaults.globalMipLodBias, -4.0, 4.0,
             "Global Mipmap LOD Bias", "Rendering/Textures"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.globalMipLodBias = std::get<double>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeBoolSetting(
             "rendering.textures.streamingEnabled", defaults.textureStreamingEnabled,
             "Texture Streaming", "Rendering/Textures"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.textureStreamingEnabled = std::get<bool>(value);
         }},
        {RestartRequired(PlatformOverride(Config::MakeIntSetting(
             "rendering.textures.maxSizePerTier", defaults.maxTextureSizePerTier,
             256, 16384, "Max Texture Resolution", "Rendering/Textures"))),
         [](EngineConfig& config, const SettingValue& value) {
             config.maxTextureSizePerTier = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeEnumSetting(
             "rendering.textures.defaultRepeatMode", defaults.defaultTextureRepeatMode,
             {{0, "Clamp"}, {1, "Repeat"}, {2, "Mirror"}}, "Default Texture Repeat", "Rendering/Textures")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultTextureRepeatMode = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeEnumSetting(
             "rendering.environment.defaultSkyMode", defaults.defaultSkyMode,
             {{0, "None"}, {1, "Colour"}, {2, "Gradient"}, {3, "Physical"}, {4, "Panorama"}},
             "Default Sky Mode", "Rendering/Environment")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultSkyMode = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeIntSetting("rendering.environment.skyResolution", defaults.defaultSkyResolution,
                                   64, 4096, "Default Sky Resolution", "Rendering/Environment")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultSkyResolution = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeAssetReferenceSetting(
             "rendering.environment.defaultWorldEnvironmentAsset", defaults.defaultWorldEnvironmentAsset, 256U,
             "Default WorldEnvironment Asset", "Rendering/Environment")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultWorldEnvironmentAsset = std::get<std::string>(value);
         }},
        {RestartRequired(Config::MakeAssetReferenceSetting(
             "physics.materials.defaultAsset", defaults.defaultPhysicsMaterialAsset, 256U,
             "Default Physics Material Asset", "Physics/Materials")),
         [](EngineConfig& config, const SettingValue& value) {
             config.defaultPhysicsMaterialAsset = std::get<std::string>(value);
         }},
        {RestartRequired(Config::MakeFloatSetting("accessibility.runtime.uiScale", defaults.runtimeUiScale,
                                     0.5, 3.0, "Runtime UI Scale", "Accessibility/Runtime")),
         [](EngineConfig& config, const SettingValue& value) {
             config.runtimeUiScale = std::get<double>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("accessibility.runtime.subtitlesEnabled", defaults.runtimeSubtitlesEnabled,
                                    "Subtitles Enabled by Default", "Accessibility/Runtime")),
         [](EngineConfig& config, const SettingValue& value) {
             config.runtimeSubtitlesEnabled = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("accessibility.runtime.reduceMotion", defaults.runtimeReduceMotion,
                                    "Reduce Motion", "Accessibility/Runtime")),
         [](EngineConfig& config, const SettingValue& value) {
             config.runtimeReduceMotion = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("privacy.telemetry.analyticsOptIn", defaults.telemetryAnalyticsOptIn,
                                    "Editor & Runtime Analytics Opt-In", "Privacy/Telemetry")),
         [](EngineConfig& config, const SettingValue& value) {
             config.telemetryAnalyticsOptIn = std::get<bool>(value);
         }},
        {RestartRequired(Config::MakeEnumSetting(
             "privacy.telemetry.crashReportPolicy", defaults.crashReportSubmissionPolicy,
             {{0, "Ask Each Time"}, {1, "Always"}, {2, "Never"}}, "Crash Report Submission",
             "Privacy/Telemetry")),
         [](EngineConfig& config, const SettingValue& value) {
             config.crashReportSubmissionPolicy = std::get<std::int64_t>(value);
         }},
        {RestartRequired(Config::MakeBoolSetting("privacy.telemetry.anonymousUsageStats", defaults.anonymousUsageStatsOptIn,
                                    "Anonymous Usage Statistics", "Privacy/Telemetry")),
         [](EngineConfig& config, const SettingValue& value) {
             config.anonymousUsageStatsOptIn = std::get<bool>(value);
         }},
        {RestartRequired(CommandLineOnly(Config::MakeBoolSetting(
             std::string(Id::kHeadless), defaults.headless, "Headless", "Application/Runtime",
             "Run without a visible window. Command-line-only; use --headless."))),
         [](EngineConfig& config, const SettingValue& value) { config.headless = std::get<bool>(value); }},
    };
    return settings;
}

[[nodiscard]] bool ParseCommandLineNumber(const std::string_view text, std::int64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool ParseCommandLineNumber(const std::string_view text, double& value) noexcept {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value,
                                               std::chars_format::general);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool EqualsAsciiCaseInsensitive(const std::string_view left, const std::string_view right) noexcept {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto leftCharacter = static_cast<unsigned char>(left[index]);
        const auto rightCharacter = static_cast<unsigned char>(right[index]);
        if (std::tolower(leftCharacter) != std::tolower(rightCharacter)) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::optional<std::size_t> ParseCommaSeparatedNumbers(const std::string_view text,
                                                                        const std::span<double> values) noexcept {
    std::size_t count = 0U;
    std::size_t start = 0U;
    while (true) {
        if (count >= values.size()) {
            return std::nullopt;
        }
        const std::size_t comma = text.find(',', start);
        const std::string_view component = comma == std::string_view::npos
                                               ? text.substr(start)
                                               : text.substr(start, comma - start);
        if (!ParseCommandLineNumber(component, values[count])) {
            return std::nullopt;
        }
        ++count;
        if (comma == std::string_view::npos) {
            return count;
        }
        start = comma + 1U;
        if (start == text.size()) {
            return std::nullopt;
        }
    }
}

[[nodiscard]] std::optional<Config::SettingValue> ParseCommandLineSettingValue(
    const Config::SettingDescriptor& descriptor, const std::optional<std::string>& argument) {
    if (!argument.has_value()) {
        return descriptor.type == Config::SettingType::Bool
                   ? std::optional<Config::SettingValue>{Config::SettingValue{true}}
                   : std::nullopt;
    }
    const std::string_view text = *argument;
    switch (descriptor.type) {
    case Config::SettingType::Bool:
        if (EqualsAsciiCaseInsensitive(text, "true") || text == "1" ||
            EqualsAsciiCaseInsensitive(text, "on")) {
            return Config::SettingValue{true};
        }
        if (EqualsAsciiCaseInsensitive(text, "false") || text == "0" ||
            EqualsAsciiCaseInsensitive(text, "off")) {
            return Config::SettingValue{false};
        }
        return std::nullopt;
    case Config::SettingType::Int:
    case Config::SettingType::LayerMask: {
        std::int64_t value = 0;
        return ParseCommandLineNumber(text, value) ? std::optional<Config::SettingValue>{value} : std::nullopt;
    }
    case Config::SettingType::Float: {
        double value = 0.0;
        return ParseCommandLineNumber(text, value) ? std::optional<Config::SettingValue>{value} : std::nullopt;
    }
    case Config::SettingType::String:
    case Config::SettingType::FilePath:
    case Config::SettingType::AssetReference:
    case Config::SettingType::KeyBinding:
        return Config::SettingValue{std::string{text}};
    case Config::SettingType::StringList:
        return std::nullopt; // The command-line layer does not split or escape list values.
    case Config::SettingType::Enum: {
        std::int64_t value = 0;
        if (ParseCommandLineNumber(text, value)) {
            return Config::SettingValue{value};
        }
        for (const Config::SettingEnumEntry& entry : descriptor.enumEntries) {
            if (EqualsAsciiCaseInsensitive(text, entry.label)) {
                return Config::SettingValue{entry.value};
            }
        }
        return std::nullopt;
    }
    case Config::SettingType::Color: {
        std::array<double, 4U> channels{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbers(text, channels);
        if (!count.has_value() || *count < 3U) {
            return std::nullopt;
        }
        Config::SettingColor color{static_cast<float>(channels[0U]), static_cast<float>(channels[1U]),
                                      static_cast<float>(channels[2U])};
        if (*count == 4U) {
            color.a = static_cast<float>(channels[3U]);
        }
        return Config::SettingValue{color};
    }
    case Config::SettingType::Vector2: {
        std::array<double, 2U> components{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbers(text, components);
        if (!count.has_value() || *count != components.size()) {
            return std::nullopt;
        }
        return Config::SettingValue{Config::SettingVector2{components[0U], components[1U]}};
    }
    case Config::SettingType::Vector3: {
        std::array<double, 3U> components{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbers(text, components);
        if (!count.has_value() || *count != components.size()) {
            return std::nullopt;
        }
        return Config::SettingValue{Config::SettingVector3{components[0U], components[1U], components[2U]}};
    }
    case Config::SettingType::Vector4: {
        std::array<double, 4U> components{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbers(text, components);
        if (!count.has_value() || *count != components.size()) {
            return std::nullopt;
        }
        return Config::SettingValue{
            Config::SettingVector4{components[0U], components[1U], components[2U], components[3U]}};
    }
    }
    return std::nullopt;
}

void PopulateEngineCommandLineSettingsInternal(const Config::SettingsRegistry& registry,
                                    const CommandLine::ICommandLine& commandLine,
                                    Config::IConfigManager& store) {
    for (const Config::SettingDescriptor* descriptor : registry.GetAll()) {
        if (!IsEngineConfigSettingId(descriptor->id) || !commandLine.HasFlag(descriptor->id)) {
            continue;
        }
        const std::optional<Config::SettingValue> value =
            ParseCommandLineSettingValue(*descriptor, commandLine.GetOptionalValue(descriptor->id));
        if (!value.has_value() || !registry.SetValue(store, descriptor->id, *value)) {
            UVE_WARNING("EngineSettings: ignoring invalid command-line setting override '--{}'", descriptor->id);
        }
    }
}

} // namespace

std::string GetLayerNameSettingId(const LayerSet set, const std::size_t index) {
    std::string_view prefix = "layers.render.";
    switch (set) {
    case LayerSet::Physics:
        prefix = "layers.physics.";
        break;
    case LayerSet::Render:
        prefix = "layers.render.";
        break;
    case LayerSet::Navigation:
        prefix = "layers.navigation.";
        break;
    case LayerSet::Avoidance:
        prefix = "layers.avoidance.";
        break;
    }
    return std::string(prefix) + std::to_string(index + 1U);
}

std::string GetLayerName(const Config::SettingsDocument& document, const LayerSet set,
                            const std::size_t index) {
    if (index >= kLayerCount) {
        return {};
    }
    const std::optional<SettingValue> name = document.GetValue(GetLayerNameSettingId(set, index));
    const auto* text = name ? std::get_if<std::string>(&*name) : nullptr;
    return text != nullptr ? *text : std::string{};
}

bool RegisterEngineProjectSettings(Config::SettingsRegistry& registry) {
    bool allRegistered = true;
    for (const EngineProjectSetting& setting : GetEngineProjectSettings()) {
        allRegistered = registry.Register(setting.descriptor) && allRegistered;
    }
    constexpr std::size_t kMaximumContentPathBytes = 512U;
    allRegistered = registry.Register(Config::MakeFilePathSetting(
                        std::string(EngineProjectSettingId::kDefaultPlayerEntity), "",
                        kMaximumContentPathBytes, "Default Player", "Game/Player",
                        "The entity asset (.uventity, relative to Content) the player is spawned from. Set it "
                        "from the Content panel: right-click an entity, Set as Default Player.")) &&
                    allRegistered;
    // Layer names, 1 to 32 as a person counts them; bit 0 is layer 1.
    constexpr std::size_t kMaximumLayerNameBytes = 32U;
    for (const LayerSet set : {LayerSet::Physics, LayerSet::Render,
                                  LayerSet::Navigation, LayerSet::Avoidance}) {
        std::string_view category = "Layers/Render";
        std::string_view tooltip = "Shown wherever a mesh's, light's or decal's render layers are picked.";
        switch (set) {
        case LayerSet::Physics:
            category = "Layers/Physics";
            tooltip = "Shown wherever a 3D collider's layer or mask is picked.";
            break;
        case LayerSet::Render:
            category = "Layers/Render";
            tooltip = "Shown wherever a mesh's, light's or decal's render layers are picked.";
            break;
        case LayerSet::Navigation:
            category = "Layers/Navigation";
            tooltip = "Shown wherever a navigation region or seeker layer mask is picked.";
            break;
        case LayerSet::Avoidance:
            category = "Layers/Avoidance";
            tooltip = "Shown wherever an avoidance obstacle or agent mask is picked.";
            break;
        }
        for (std::size_t index = 0U; index < kLayerCount; ++index) {
            allRegistered =
                registry.Register(Config::MakeStringSetting(
                    GetLayerNameSettingId(set, index), "", kMaximumLayerNameBytes,
                    "Layer " + std::to_string(index + 1U), std::string{category},
                    std::string{tooltip})) &&
                allRegistered;
        }
    }
    return allRegistered;
}

bool IsEngineConfigSettingId(const std::string_view id) {
    const auto& settings = GetEngineProjectSettings();
    return std::any_of(settings.begin(), settings.end(),
                       [id](const EngineProjectSetting& setting) { return setting.descriptor.id == id; });
}

void PopulateEngineCommandLineSettings(const Config::SettingsRegistry& registry,
                                          const CommandLine::ICommandLine& commandLine,
                                          Config::IConfigManager& store) {
    PopulateEngineCommandLineSettingsInternal(registry, commandLine, store);
}

std::filesystem::path GetPlatformSettingsFilePath(const EngineConfig& config) {
    if (!config.platformSettingsFilePath.empty()) {
        return config.platformSettingsFilePath;
    }
    return config.settingsFilePath.parent_path() / "platforms" / std::string{GetPlatformSettingsTarget()} /
           config.settingsFilePath.filename();
}

bool AttachEngineSettingsLayers(Config::SettingsStack& settings, const Config::IConfigManager& project,
                                   const Config::IConfigManager& user, const Config::IConfigManager& platform,
                                   const Config::IConfigManager& commandLine) noexcept {
    return settings.AttachLayer(Config::SettingValueSource::Project, project) &&
           settings.AttachLayer(Config::SettingValueSource::User, user) &&
           settings.AttachLayer(Config::SettingValueSource::Platform, platform) &&
           settings.AttachLayer(Config::SettingValueSource::CommandLine, commandLine);
}

void ApplyEngineSettings(const Config::SettingsStack& settings, EngineConfig& config) {
    constexpr std::array<Config::SettingValueSource, 4U> kOverridePriority{
        Config::SettingValueSource::CommandLine,
        Config::SettingValueSource::Platform,
        Config::SettingValueSource::User,
        Config::SettingValueSource::Project,
    };
    for (const EngineProjectSetting& setting : GetEngineProjectSettings()) {
        if (setting.acceptsOverride != nullptr) {
            bool ignoredUnsafeOverride = false;
            for (const Config::SettingValueSource source : kOverridePriority) {
                const std::optional<SettingValue> value = settings.GetStoredValue(source, setting.descriptor.id);
                if (!value.has_value()) {
                    continue;
                }
                if (!setting.acceptsOverride(*value)) {
                    ignoredUnsafeOverride = true;
                    continue;
                }
                setting.apply(config, *value);
                break;
            }
            if (ignoredUnsafeOverride) {
                UVE_WARNING("EngineSettings: ignored unsafe override for setting '{}'", setting.descriptor.id);
            }
            continue;
        }
        if (const std::optional<Config::SettingResolution> resolved =
                settings.Resolve(setting.descriptor.id);
            resolved.has_value() && resolved->source != Config::SettingValueSource::EngineDefault) {
            setting.apply(config, resolved->value);
        }
    }
}

void ApplyEngineProjectSettings(const Config::SettingsDocument& document, EngineConfig& config) {
    Config::SettingsStack settings(document.GetRegistry());
    if (settings.AttachLayer(Config::SettingValueSource::Project, document.GetStore())) {
        ApplyEngineSettings(settings, config);
    }
}

} // namespace UVE::Core
