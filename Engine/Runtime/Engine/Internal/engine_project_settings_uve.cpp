// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/engine_project_settings_uve.h"

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

#include "uve/commandline/i_command_line_uve.h"
#include "uve/core/shader_cache_path_policy_uve.h"
#include "uve/logging/logging_macros_uve.h"

namespace UVE::Core {
namespace {

using Config::SettingDescriptorUVE;
using Config::SettingValueUVE;

[[nodiscard]] std::string_view GetPlatformSettingsTargetUVE() noexcept {
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

/// One engine project setting and the EngineConfigUVE field it overrides.
struct EngineProjectSettingUVE final {
    SettingDescriptorUVE descriptor;
    void (*apply)(EngineConfigUVE& config, const SettingValueUVE& value) = nullptr;
    bool (*acceptsOverride)(const SettingValueUVE& value) = nullptr;
};

[[nodiscard]] bool IsValidShaderCacheSettingValueUVE(const SettingValueUVE& value) {
    const std::string* const pathValue = std::get_if<std::string>(&value);
    return pathValue != nullptr && IsSafeShaderCachePathUVE(std::filesystem::path(*pathValue));
}

[[nodiscard]] SettingDescriptorUVE RestartRequiredUVE(SettingDescriptorUVE descriptor) {
    descriptor.flags |= Config::kSettingFlagRestartRequiredUVE;
    return descriptor;
}

[[nodiscard]] SettingDescriptorUVE PlatformOverrideUVE(SettingDescriptorUVE descriptor) {
    descriptor.flags |= Config::kSettingFlagPerPlatformUVE;
    return descriptor;
}

[[nodiscard]] SettingDescriptorUVE CommandLineOnlyUVE(SettingDescriptorUVE descriptor) {
    descriptor.flags |= Config::kSettingFlagHiddenUVE | Config::kSettingFlagNotPersistedUVE;
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
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kRenderBackendPreferenceUVE),
             static_cast<std::int64_t>(defaults.renderBackendPreferenceUVE),
             {{0, "Auto"}, {1, "OpenGL"}, {2, "Vulkan"}, {3, "Null"}}, "Renderer Backend",
             "Rendering/General",
             "Preferred backend at startup. Auto uses OpenGL; unavailable backends fall back to the next supported backend."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.renderBackendPreferenceUVE =
                 static_cast<RenderBackendPreferenceUVE>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kRenderResolutionScaleUVE), defaults.renderResolutionScaleUVE,
             kMinimumRenderResolutionScaleUVE, kMaximumRenderResolutionScaleUVE,
             "Resolution Scale", "Rendering/General",
             "Fixed 3D render-target scale, or the starting/upper scale when dynamic resolution is enabled. "
             "The presentation size is unchanged."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.renderResolutionScaleUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kDynamicRenderResolutionEnabledUVE), defaults.dynamicRenderResolutionEnabledUVE,
             "Dynamic Resolution", "Rendering/General",
             "Adjust scene render scale to approach the target frame time; the controller uses wall-clock frame "
             "duration including presentation, not a GPU-only timer."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.dynamicRenderResolutionEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kMinimumRenderResolutionScaleUVE), defaults.minimumRenderResolutionScaleUVE,
             kMinimumRenderResolutionScaleUVE, kMaximumRenderResolutionScaleUVE,
             "Minimum Resolution Scale", "Rendering/General",
             "Lowest 3D render-target scale the dynamic resolution controller may use. It is clamped to the "
             "configured maximum scale if necessary."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.minimumRenderResolutionScaleUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kRenderResolutionTargetFrameTimeMillisecondsUVE),
             defaults.renderResolutionTargetFrameTimeMillisecondsUVE,
             kMinimumRenderResolutionTargetFrameTimeMillisecondsUVE,
             kMaximumRenderResolutionTargetFrameTimeMillisecondsUVE, "Target Frame Time (ms)",
             "Rendering/General",
             "Whole-frame wall-clock budget, measured from frame start through presentation. Account for "
             "V-sync/display refresh; this is not a GPU-only timing budget."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.renderResolutionTargetFrameTimeMillisecondsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFilePathSettingUVE(
             std::string(Id::kShaderProgramCacheDirectoryUVE), defaults.shaderCachePath.generic_string(), 512U,
             "Program Binary Cache Directory", "Rendering/Shaders",
             "Directory relative to the process working directory for the existing OpenGL shader-program "
             "binary cache. Absolute paths and parent-directory segments are rejected; a platform "
             "subdirectory is appended automatically. "
             "This does not enable Vulkan pipeline-cache persistence or startup precompilation."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shaderCachePath = std::filesystem::path(std::get<std::string>(value));
         },
         IsValidShaderCacheSettingValueUVE},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kShaderCompilationModeUVE),
             static_cast<std::int64_t>(defaults.shaderCompilationModeUVE),
             {{static_cast<std::int64_t>(ShaderCompilationModeUVE::SynchronousOnDemandUVE), "Synchronous on-demand"},
              {static_cast<std::int64_t>(ShaderCompilationModeUVE::AsynchronousOnDemandUVE), "Asynchronous on-demand"}},
             "Shader Compilation Mode", "Rendering/Shaders",
             "Choose when on-demand shader work runs. Synchronous completes source preparation and RHI "
             "compile/link in each Create* call and can stall the caller. Asynchronous prepares source on "
             "workers and compiles/links on the render thread as updates drain jobs. Ahead-of-time compilation "
             "and placeholder programs are not implemented."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shaderCompilationModeUVE = static_cast<ShaderCompilationModeUVE>(std::get<std::int64_t>(value));
         }},
        {[&] {
             SettingDescriptorUVE descriptor = Config::MakeBoolSettingUVE(
                 std::string(Id::kVulkanValidationLayersEnabledUVE), defaults.vulkanValidationLayersEnabledUVE,
                 "Vulkan Validation Layers", "Rendering/Debug",
                 "Request VK_LAYER_KHRONOS_validation for Vulkan. Debug builds default to enabled; if the "
                 "validation layer is not installed the engine warns and continues without it. When "
                 "VK_EXT_debug_utils is available, validation warnings and errors are sent to the engine log. "
                 "Has no effect on other render backends.");
             descriptor.flags |= Config::kSettingFlagAdvancedUVE;
             return RestartRequiredUVE(PlatformOverrideUVE(std::move(descriptor)));
         }(),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.vulkanValidationLayersEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kSpatialUpscalingMethodUVE),
             static_cast<std::int64_t>(defaults.spatialUpscalingMethodUVE),
             {{static_cast<std::int64_t>(SpatialUpscalingMethodUVE::BilinearUVE), "Bilinear"},
              {static_cast<std::int64_t>(SpatialUpscalingMethodUVE::NearestUVE), "Nearest neighbour"}},
             "Spatial Upscaling Method", "Rendering/General",
             "Resample the scene render target for presentation with bilinear filtering (default) or nearest "
             "neighbour. Temporal reconstruction is not offered because the renderer has no frame history "
             "or motion vectors."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.spatialUpscalingMethodUVE =
                 static_cast<SpatialUpscalingMethodUVE>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kToneMappingMethodUVE), static_cast<std::int64_t>(defaults.toneMappingMethodUVE),
             {{static_cast<std::int64_t>(ToneMappingMethodUVE::LinearClampUVE), "Linear clamp"},
              {static_cast<std::int64_t>(ToneMappingMethodUVE::AcesUVE), "ACES"}},
             "Tone Mapper", "Rendering/PostProcessing",
             "Map HDR scene values into display range with direct linear clipping or the existing ACES-style "
             "filmic curve. Exposure remains controlled by the active WorldEnvironment."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.toneMappingMethodUVE = static_cast<ToneMappingMethodUVE>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kSharpeningAmountUVE), defaults.sharpeningAmountUVE, 0.0, 1.0,
             "Sharpening Amount", "Rendering/PostProcessing",
             "Apply an edge-clamped unsharp mask in the final scene reconstruction. 0 disables sharpening; "
             "larger values strengthen fine detail."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.sharpeningAmountUVE = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kDitheringEnabledUVE), defaults.ditheringEnabledUVE,
             "Dithering", "Rendering/PostProcessing",
             "Add one-half display-step of deterministic screen-space noise before output quantization to reduce "
             "banding in smooth gradients."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ditheringEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kSsaoEnabledUVE), defaults.ssaoEnabledUVE, "Screen-Space Ambient Occlusion",
             "Rendering/SSAO", "Enable the depth-based screen-space ambient-occlusion pass."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.ssaoEnabledUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kSsaoRadiusUVE), defaults.ssaoRadiusUVE, 0.01, 10.0, "Radius", "Rendering/SSAO",
             "World-space radius sampled by the SSAO hemisphere kernel. An active WorldEnvironment keeps its "
             "existing radius override."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssaoRadiusUVE = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kSsaoIntensityUVE), defaults.ssaoIntensityUVE, 0.0, 4.0, "Intensity",
             "Rendering/SSAO", "Strength of the sampled occlusion. An active WorldEnvironment keeps its existing "
             "intensity override."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssaoIntensityUVE = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kSsaoPowerUVE), defaults.ssaoPowerUVE, 0.1, 4.0, "Power", "Rendering/SSAO",
             "Shape the final unoccluded fraction with a power curve; 1 preserves the linear response."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssaoPowerUVE = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kSsaoQualityUVE), static_cast<std::int64_t>(defaults.ssaoQualityUVE),
             {{0, "Low (4 samples)"}, {1, "Medium (8 samples)"}, {2, "High (12 samples)"}},
             "Sample Quality", "Rendering/SSAO",
             "Trade kernel cost for detail. High uses all 12 samples and preserves the previous SSAO quality."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssaoQualityUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kSsaoBlurEnabledUVE), defaults.ssaoBlurEnabledUVE, "Blur", "Rendering/SSAO",
             "Apply a 3x3 Gaussian blur to the AO term while compositing, reducing half-resolution kernel noise."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssaoBlurEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kPostProcessAAMethodUVE), static_cast<std::int64_t>(defaults.postProcessAAMethodUVE),
             {{static_cast<std::int64_t>(PostProcessAAMethodUVE::NoneUVE), "None"},
              {static_cast<std::int64_t>(PostProcessAAMethodUVE::FastApproximateUVE), "Fast approximate"}},
             "Post-Process AA Method", "Rendering/AntiAliasing",
             "Select none or the implemented fast approximate edge filter. Temporal AA is not offered because "
             "frame history, motion vectors, and camera jitter are not implemented."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.postProcessAAMethodUVE = static_cast<PostProcessAAMethodUVE>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kScreenSpaceAAQualityUVE), static_cast<std::int64_t>(defaults.screenSpaceAAQualityUVE),
             {{0, "Low (3 edge taps)"}, {1, "Medium (5 edge taps)"}, {2, "High (7 edge taps)"}},
             "Screen-Space AA Quality", "Rendering/AntiAliasing",
             "Choose how many edge-direction taps the fast approximate filter uses, in addition to its fixed "
             "four-neighbour contrast test. This has no effect when the post-process AA method is None."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.screenSpaceAAQualityUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kShadowMapResolutionUVE), static_cast<std::int64_t>(defaults.shadowMapResolution),
             {{512, "512"}, {1024, "1024"}, {2048, "2048"}, {4096, "4096"}}, "Map Resolution", "Rendering/Shadows",
             "Width and height, in texels, of the directional light's shadow map."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowMapResolution = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kShadowBiasDefaultUVE), defaults.shadowBiasDefaultUVE, 0.0, 10.0,
             "Default Shadow Bias", "Rendering/Shadows",
             "Depth-bias default for LightEmitter3D shadow components with a negative/inherit Bias. The renderer "
             "scales this authored value by 0.025 for the angle-aware shadow shader; explicit per-light values "
             "take precedence."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowBiasDefaultUVE = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kShadowNormalBiasDefaultUVE), defaults.shadowNormalBiasDefaultUVE, 0.0, 10.0,
             "Default Shadow Normal Bias", "Rendering/Shadows",
             "Angle-aware normal-bias multiplier for LightEmitter3D shadow components with a negative/inherit "
             "Normal Bias. Explicit per-light values take precedence."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowNormalBiasDefaultUVE = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kShadowCascadeSplitBlendUVE), defaults.shadowCascadeSplitLambda, 0.0, 1.0,
             "Default Cascade Split Blend", "Rendering/Shadows",
             "How directional shadow cascades are distributed when a light leaves Split Blend at Inherit: 0 is "
             "uniform, 1 is logarithmic. Explicit per-light values take precedence."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowCascadeSplitLambda = static_cast<float>(std::get<double>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kShadowFilterUVE), static_cast<std::int64_t>(defaults.shadowPcfKernelRadius),
             {{0, "Hard"}, {1, "Soft (3x3)"}, {2, "Softer (5x5)"}}, "Filter", "Rendering/Shadows",
             "How shadow edges are softened. Softer costs more samples per pixel."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowPcfKernelRadius = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kFrustumCullingModeUVE), static_cast<std::int64_t>(defaults.frustumCullingModeUVE),
             {{static_cast<std::int64_t>(FrustumCullingModeUVE::EnabledUVE), "Enabled"},
              {static_cast<std::int64_t>(FrustumCullingModeUVE::DebugFreezeUVE), "Debug freeze (draw everything)"}},
             "Frustum Culling", "Rendering/Culling",
             "Enabled rejects meshes, primitives, surface instances and decals outside the camera frustum. "
             "Debug freeze submits all of them regardless of where the camera looks, which answers \"is it "
             "off-screen or is it missing?\" at the cost of drawing the whole scene. Hidden flags, world "
             "partitioning, visibility regions, LOD and SurfaceInstance draw distances, render layers, "
             "Occluder3D occlusion and the shadow cascades keep culling in both modes."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.frustumCullingModeUVE = static_cast<FrustumCullingModeUVE>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kAutoSaveIntervalUVE), defaults.autoSaveIntervalSecondsUVE, 10.0, 3600.0,
             "Auto-Save Interval", "Application/Save", "Seconds between writes to the game's auto-save slot.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.autoSaveIntervalSecondsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(std::string(Id::kQuitOnLastWindowClosedUVE),
                                    defaults.quitOnLastWindowClosedUVE, "Quit on Last Window Closed",
                                    "Application/Runtime",
                                    "Request application shutdown when the last native window is closed.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.quitOnLastWindowClosedUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeColorSettingUVE(std::string(Id::kBootBackgroundColorUVE),
                                     Config::SettingColorUVE{defaults.backgroundColorUVE[0],
                                                             defaults.backgroundColorUVE[1],
                                                             defaults.backgroundColorUVE[2],
                                                             defaults.backgroundColorUVE[3]},
                                     true, "Background Color", "Application/Boot",
                                     "RGBA clear color shown behind an empty scene and during the boot screen.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             const Config::SettingColorUVE color = std::get<Config::SettingColorUVE>(value);
             config.backgroundColorUVE = {color.r, color.g, color.b, color.a};
         }},
        {RestartRequiredUVE(Config::MakeFilePathSettingUVE(std::string(Id::kBootSplashImageUVE), "", 256U,
                                        "Boot Screen Image", "Application/Boot",
                                        "Optional Content-relative image rendered over the background during standalone startup.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.bootSplashImagePathUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE(std::string(Id::kBootSplashFadeSecondsUVE),
                                     defaults.splashFadeSecondsUVE, 0.0, 60.0, "Fade Time", "Application/Boot",
                                     "Boot-screen fade duration in seconds.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.splashFadeSecondsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE(std::string(Id::kBootSplashMinimumDisplaySecondsUVE),
                                     defaults.splashMinimumDisplaySecondsUVE, 0.0, 600.0, "Minimum Display Time",
                                     "Application/Boot", "Minimum boot-screen display time in seconds.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.splashMinimumDisplaySecondsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(std::string(Id::kBootSplashSkippableUVE), defaults.splashSkippableUVE,
                                    "Skippable", "Application/Boot",
                                    "Allow users to skip the boot screen after its minimum display time.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.splashSkippableUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(std::string(Id::kSkipBootSplashInEditorPlayModeUVE),
                                    defaults.skipSplashInEditorPlayModeUVE, "Skip in Editor Play Mode",
                                    "Application/Boot",
                                    "Do not show the boot screen when starting a game from the Editor.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.skipSplashInEditorPlayModeUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(std::string(Id::kCrashHandlerEnabledUVE), defaults.crashHandlerEnabledUVE,
                                    "Crash Handler", "Application/Crash Reporting",
                                    "Install native unhandled-exception and fatal-signal reporting for standalone runs.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.crashHandlerEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeFilePathSettingUVE(std::string(Id::kCrashDumpDirectoryUVE), "crash-dumps", 256U,
                                        "Crash Dump Directory", "Application/Crash Reporting",
                                        "Directory under user data for crash reports and platform-native minidumps.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.crashDumpDirectoryUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(Config::MakeStringSettingUVE(std::string(Id::kSymbolUploadEndpointUVE), "", 2048U,
                                      "Symbol Upload Endpoint", "Application/Crash Reporting",
                                      "Optional HTTPS endpoint reserved for host integrations; the runtime never uploads automatically.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.symbolUploadEndpointUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowWidthUVE), defaults.windowWidth, 1, 16384, "Width", "Window/Display",
             "Initial window width in framebuffer-independent screen units."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.windowWidth = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowHeightUVE), defaults.windowHeight, 1, 16384, "Height", "Window/Display",
             "Initial window height in framebuffer-independent screen units."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.windowHeight = static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kWindowModeUVE), static_cast<std::int64_t>(defaults.windowModeUVE),
             {{0, "Windowed"}, {1, "Maximized"}, {2, "Fullscreen"},
              {3, "Exclusive Fullscreen"}, {4, "Borderless"}}, "Mode", "Window/Display",
             "Windowed and maximized are decorated unless Borderless is enabled. Fullscreen uses a "
             "borderless desktop-sized window; Exclusive Fullscreen attaches to the monitor mode."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.windowModeUVE = static_cast<Platform::WindowModeUVE>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowResizableUVE), defaults.windowResizableUVE, "Resizable", "Window/Display")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.windowResizableUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowBorderlessUVE), defaults.windowBorderlessUVE, "Borderless", "Window/Display")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowBorderlessUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowAlwaysOnTopUVE), defaults.windowAlwaysOnTopUVE, "Always on Top", "Window/Display")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowAlwaysOnTopUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowTransparentUVE), defaults.windowTransparentUVE, "Transparent Background",
             "Window/Display", "Requests a transparent native framebuffer; compositor/backend support is required.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowTransparentUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowMinimumWidthUVE), 0, 0, 16384, "Minimum Width", "Window/Size Limits",
             "Zero leaves the width unconstrained.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowMinimumWidthUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowMinimumHeightUVE), 0, 0, 16384, "Minimum Height", "Window/Size Limits",
             "Zero leaves the height unconstrained.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowMinimumHeightUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowMaximumWidthUVE), 0, 0, 16384, "Maximum Width", "Window/Size Limits",
             "Zero leaves the width unconstrained.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowMaximumWidthUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowMaximumHeightUVE), 0, 0, 16384, "Maximum Height", "Window/Size Limits",
             "Zero leaves the height unconstrained.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowMaximumHeightUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowPositionSpecifiedUVE), defaults.windowPositionSpecifiedUVE, "Set Initial Position",
             "Window/Placement")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowPositionSpecifiedUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowPositionXUVE), 0, -131072, 131072, "X", "Window/Placement")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowPositionXUVE = static_cast<std::int32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowPositionYUVE), 0, -131072, 131072, "Y", "Window/Placement")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowPositionYUVE = static_cast<std::int32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeStringSettingUVE(
             std::string(Id::kWindowMonitorUVE), "", 256U, "Monitor", "Window/Placement",
             "Exact monitor name; empty selects the primary monitor.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowMonitorNameUVE = std::get<std::string>(value); }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowHighDpiAwareUVE), defaults.highDpiAwareUVE, "High-DPI Aware", "Window/Scaling"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.highDpiAwareUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowPerMonitorScalingUVE), defaults.perMonitorScalingUVE, "Per-Monitor Scaling",
             "Window/Scaling"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.perMonitorScalingUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE(
             std::string(Id::kWindowContentScaleOverrideUVE), defaults.contentScaleOverrideUVE, 0.0, Platform::kMaximumContentScaleOverrideUVE,
             "Content Scale Override", "Window/Scaling", "Zero follows the monitor's reported content scale.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.contentScaleOverrideUVE = std::get<double>(value); }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kWindowStretchModeUVE), static_cast<std::int64_t>(defaults.stretchModeUVE),
             {{0, "Disabled"}, {1, "Canvas Items"}, {2, "Viewport"}}, "Stretch Mode", "Window/Scaling")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.stretchModeUVE = static_cast<Platform::StretchModeUVE>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kWindowAspectPolicyUVE), static_cast<std::int64_t>(defaults.aspectPolicyUVE),
             {{0, "Ignore"}, {1, "Keep"}, {2, "Keep Width"}, {3, "Keep Height"}, {4, "Expand"}},
             "Aspect Policy", "Window/Scaling")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.aspectPolicyUVE = static_cast<Platform::AspectPolicyUVE>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowIntegerOnlyScalingUVE), defaults.integerOnlyScalingUVE, "Integer-Only Scaling",
             "Window/Scaling", "Avoid fractional scale factors for pixel-art viewports.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.integerOnlyScalingUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kWindowOrientationUVE), static_cast<std::int64_t>(defaults.orientationUVE),
             {{0, "Auto"}, {1, "Landscape"}, {2, "Portrait"}, {3, "Landscape Left"},
              {4, "Landscape Right"}, {5, "Portrait Upside Down"}}, "Orientation", "Window/Handheld"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.orientationUVE = static_cast<Platform::DisplayOrientationUVE>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeStringListSettingUVE(
             std::string(Id::kWindowAllowedOrientationsUVE), {"landscape", "portrait"}, 6U, 24U,
             "Allowed Orientations", "Window/Handheld"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             const auto& names = std::get<Config::SettingStringListUVE>(value);
             std::vector<Platform::DisplayOrientationUVE> parsed;
             bool valid = !names.empty();
             for (const std::string& name : names) {
                 const auto orientation = Platform::ParseDisplayOrientationUVE(name);
                 if (!orientation || *orientation == Platform::DisplayOrientationUVE::Auto ||
                     std::find(parsed.begin(), parsed.end(), *orientation) != parsed.end()) {
                     valid = false;
                     break;
                 }
                 parsed.push_back(*orientation);
             }
             if (valid && (config.orientationUVE == Platform::DisplayOrientationUVE::Auto ||
                           std::find(parsed.begin(), parsed.end(), config.orientationUVE) != parsed.end())) {
                 config.allowedOrientationsUVE = std::move(parsed);
             }
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             std::string(Id::kWindowVSyncModeUVE), static_cast<std::int64_t>(defaults.vsyncModeUVE),
             {{0, "Off"}, {1, "On"}, {2, "Adaptive"}, {3, "Mailbox"}}, "V-Sync Mode", "Window/Performance"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.vsyncModeUVE = static_cast<Platform::VSyncModeUVE>(std::get<std::int64_t>(value));
             config.vsyncModeExplicitUVE = true;
             config.vsyncEnabledUVE = config.vsyncModeUVE != Platform::VSyncModeUVE::Off;
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowFocusedFrameRateCapUVE), 0, 0, 1000, "Focused Frame Rate Cap",
             "Window/Performance", "Frames per second; zero is uncapped.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.focusedFrameRateCapUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowUnfocusedFrameRateCapUVE), 0, 0, 1000, "Unfocused Frame Rate Cap",
             "Window/Performance", "Frames per second while unfocused; zero is uncapped.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.unfocusedFrameRateCapUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kWindowAllowDisplaySleepUVE), defaults.allowDisplaySleepUVE, "Allow Display Sleep",
             "Window/Performance", "When disabled, requests that the platform keep the display awake."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.allowDisplaySleepUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeFilePathSettingUVE(
             std::string(Id::kWindowCursorImageUVE), "", 256U, "Cursor Image", "Window/Cursor",
             "Content-relative PNG cursor image; decoded as RGBA8 by the project runtime.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.cursorImagePathUVE = std::get<std::string>(value); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowCursorHotspotXUVE), 0, 0, 8192, "Hotspot X", "Window/Cursor")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.cursorHotspotXUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE(
             std::string(Id::kWindowCursorHotspotYUVE), 0, 0, 8192, "Hotspot Y", "Window/Cursor")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.cursorHotspotYUVE = static_cast<std::uint32_t>(std::get<std::int64_t>(value)); }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(std::string(Id::kWindowCursorVisibleUVE), defaults.cursorVisibleUVE,
                                    "Visible by Default", "Window/Cursor")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.cursorVisibleUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(std::string(Id::kWindowCursorConfinedUVE), defaults.cursorConfinedToWindowUVE,
                                    "Confined to Window", "Window/Cursor",
                                    "Capture the pointer inside the game window; backend behavior varies by host.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.cursorConfinedToWindowUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeStringSettingUVE(
             std::string(Id::kWindowTitleFormatUVE), defaults.windowTitleFormatUVE, 256U, "Title Format",
             "Window/Title", "Supported tokens: {productName}, {projectName}, and {sceneName}.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.windowTitleFormatUVE = std::get<std::string>(value); }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE(std::string(Id::kWindowAppendSceneInEditorPlayUVE),
                                    defaults.appendSceneNameInEditorPlayModeUVE, "Append Scene in Editor Play",
                                    "Window/Title", "Append the active scene name while the editor is playing.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.appendSceneNameInEditorPlayModeUVE = std::get<bool>(value); }},
        {RestartRequiredUVE(Config::MakeVector2SettingUVE(
             "physics.2d.gravity", Config::SettingVector2UVE{0.0, -980.0}, -100000.0, 100000.0,
             "2D Gravity", "Physics/2D", "Default 2D gravity acceleration in pixels/s^2.")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             const auto& g = std::get<Config::SettingVector2UVE>(value);
             config.gravity2DUVE = Math::Vector2UVE{static_cast<float>(g.x), static_cast<float>(g.y)};
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("physics.common.defaultLinearDamping",
                                     defaults.defaultLinearDampingUVE, 0.0, 100.0,
                                     "Default Linear Damping", "Physics/Common")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultLinearDampingUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("physics.common.defaultAngularDamping",
                                     defaults.defaultAngularDampingUVE, 0.0, 100.0,
                                     "Default Angular Damping", "Physics/Common")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultAngularDampingUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("physics.common.sleepThreshold", defaults.sleepThresholdUVE,
                                     0.0, 10.0, "Sleep Threshold", "Physics/Common")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.sleepThresholdUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("physics.common.sleepTimeSeconds", defaults.sleepTimeSecondsUVE,
                                     0.0, 60.0, "Sleep Time", "Physics/Common")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.sleepTimeSecondsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("physics.common.allowSleepingDefault",
                                    defaults.allowSleepingDefaultUVE, "Allow Sleeping by Default",
                                    "Physics/Common")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.allowSleepingDefaultUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("physics.solver.positionIterations",
                                   defaults.physicsPositionIterationsUVE, 1, 64,
                                   "Position Iterations", "Physics/Solver")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.physicsPositionIterationsUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("physics.solver.velocityIterations",
                                   defaults.physicsVelocityIterationsUVE, 1, 64,
                                   "Velocity Iterations", "Physics/Solver")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.physicsVelocityIterationsUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("physics.contacts.contactOffset",
                                     defaults.physicsContactOffsetUVE, 0.0001, 1.0,
                                     "Contact Offset", "Physics/Contacts")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.physicsContactOffsetUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("physics.contacts.penetrationSlop",
                                     defaults.physicsPenetrationSlopUVE, 0.0, 1.0,
                                     "Penetration Slop", "Physics/Contacts")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.physicsPenetrationSlopUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("physics.contacts.maxDepenetrationVelocity",
                                     defaults.physicsMaxDepenetrationVelocityUVE, 0.0, 1000.0,
                                     "Max Depenetration Velocity", "Physics/Contacts")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.physicsMaxDepenetrationVelocityUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeLayerMaskSettingUVE("physics.layers.defaultCollisionLayer",
                                         defaults.defaultCollisionLayerUVE,
                                         "Default Collision Layer", "Physics/Layers")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultCollisionLayerUVE =
                 static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(Config::MakeLayerMaskSettingUVE("physics.layers.defaultCollisionMask",
                                         defaults.defaultCollisionMaskUVE,
                                         "Default Collision Mask", "Physics/Layers")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultCollisionMaskUVE =
                 static_cast<std::uint32_t>(std::get<std::int64_t>(value));
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("physics.common.interpolationDefault",
                                    defaults.physicsInterpolationDefaultUVE,
                                    "Physics Interpolation Default", "Physics/Common")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.physicsInterpolationDefaultUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("physics.simulation.deterministicMode",
                                    defaults.physicsDeterministicModeUVE, "Deterministic Mode",
                                    "Physics/Simulation")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.physicsDeterministicModeUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("input.actions.defaultDeadzone",
                                     defaults.inputDefaultDeadzoneUVE, 0.0, 0.95,
                                     "Default Action Deadzone", "Input/Actions")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.inputDefaultDeadzoneUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("input.mouse.sensitivity", defaults.inputMouseSensitivityUVE,
                                     0.01, 20.0, "Mouse Sensitivity", "Input/Mouse")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.inputMouseSensitivityUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("input.haptics.enabled", defaults.inputHapticsEnabledUVE,
                                    "Enable Haptics", "Input/Haptics")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.inputHapticsEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("audio.output.sampleRate", defaults.audioSampleRateUVE, 8000,
                                   192000, "Sample Rate", "Audio/Output")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.audioSampleRateUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("audio.buses.masterVolume", defaults.audioMasterVolumeUVE, 0.0,
                                     2.0, "Master Volume", "Audio/Buses")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.audioMasterVolumeUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("audio.buses.musicVolume", defaults.audioMusicVolumeUVE, 0.0,
                                     2.0, "Music Volume", "Audio/Buses")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.audioMusicVolumeUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("audio.buses.sfxVolume", defaults.audioSfxVolumeUVE, 0.0,
                                     2.0, "SFX Volume", "Audio/Buses")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.audioSfxVolumeUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("audio.buses.voiceVolume", defaults.audioVoiceVolumeUVE, 0.0,
                                     2.0, "Voice Volume", "Audio/Buses")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.audioVoiceVolumeUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("audio.voices.maxVoices", defaults.audioMaxVoicesUVE, 1, 512,
                                   "Max Voices", "Audio/Voices")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.audioMaxVoicesUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("audio.window.muteOnFocusLoss",
                                    defaults.audioMuteOnFocusLossUVE, "Mute on Focus Loss",
                                    "Audio/Window")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.audioMuteOnFocusLossUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("animation.playback.defaultBlendTime",
                                     defaults.animationDefaultBlendTimeUVE, 0.0, 10.0,
                                     "Default Blend Time", "Animation/Playback")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.animationDefaultBlendTimeUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("animation.ik.defaultSolverIterations",
                                   defaults.animationIkDefaultIterationsUVE, 1, 128,
                                   "Default IK Iterations", "Animation/IK")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.animationIkDefaultIterationsUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("navigation.bake.cellSize", defaults.navCellSizeUVE, 0.05,
                                     10.0, "Cell Size", "Navigation/Bake")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.navCellSizeUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("navigation.bake.agentRadius", defaults.navAgentRadiusUVE,
                                     0.05, 10.0, "Agent Radius", "Navigation/Bake")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.navAgentRadiusUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("navigation.avoidance.enabled", defaults.navAvoidanceEnabledUVE,
                                    "Enable Avoidance", "Navigation/Avoidance")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.navAvoidanceEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("network.transport.defaultPort", defaults.networkDefaultPortUVE,
                                   1, 65535, "Default Port", "Networking/Transport")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.networkDefaultPortUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("network.rates.tickRateHz", defaults.networkTickRateHzUVE, 1.0,
                                     240.0, "Network Tick Rate", "Networking/Rates")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.networkTickRateHzUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("network.lagCompensation.windowMilliseconds",
                                     defaults.networkLagCompensationWindowMsUVE, 0.0, 2000.0,
                                     "Lag Compensation Window", "Networking/LagCompensation")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.networkLagCompensationWindowMsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeStringSettingUVE("localisation.defaultLocale",
                                      defaults.localisationDefaultLocaleUVE, 32U, "Default Locale",
                                      "Localisation/General")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.localisationDefaultLocaleUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("localisation.pseudolocalisationEnabled",
                                    defaults.pseudolocalisationEnabledUVE, "Pseudolocalisation",
                                    "Localisation/Debug")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.pseudolocalisationEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeFilePathSettingUVE(
             "paths.saveGameDirectory", defaults.saveDirectoryPath.generic_string(), 256U,
             "Save Game Directory", "Paths/Directories")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.saveDirectoryPath = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(Config::MakeFilePathSettingUVE(
             "paths.logFilePath", defaults.logFilePath.generic_string(), 256U, "Log File Path",
             "Paths/Logging")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.logFilePath = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "quality.activePreset", defaults.activeQualityTierUVE,
             {{0, "Low"}, {1, "Medium"}, {2, "High"}, {3, "Ultra"}}, "Quality Preset",
             "Quality/Presets"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.activeQualityTierUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeStringListSettingUVE(
             "platform.targetFeatureTags", defaults.platformFeatureTagsUVE, 32U, 64U,
             "Target Feature Tags", "Platform/Capabilities"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.platformFeatureTagsUVE = std::get<Config::SettingStringListUVE>(value);
         }},
        {RestartRequiredUVE(Config::MakeStringSettingUVE("packaging.defaultPresetName",
                                      defaults.packagingDefaultPresetUVE, 64U, "Default Preset",
                                      "Packaging/Presets")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.packagingDefaultPresetUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("packaging.encryptAssets", defaults.packagingEncryptAssetsUVE,
                                    "Encrypt Packaged Assets", "Packaging/Security")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.packagingEncryptAssetsUVE = std::get<bool>(value);
         }},
        // Part 1.3 Extended Rendering Settings
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.general.renderingMethod", defaults.renderingMethodUVE,
             {{0, "Forward"}, {1, "Forward+"}, {2, "Deferred"}, {3, "Mobile"}}, "Rendering Method",
             "Rendering/General"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.renderingMethodUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             "rendering.general.colorSpace", defaults.colorSpaceModeUVE,
             {{0, "Linear sRGB"}, {1, "Gamma sRGB"}}, "Working Colour Space", "Rendering/General")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.colorSpaceModeUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.hdr.enabled", defaults.hdrRenderingEnabledUVE, "HDR Output", "Rendering/HDR"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.hdrRenderingEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.hdr.transferFunction", defaults.hdrTransferFunctionUVE,
             {{0, "PQ (ST.2084)"}, {1, "HLG"}, {2, "scRGB Linear"}}, "HDR Transfer Function", "Rendering/HDR"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.hdrTransferFunctionUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("rendering.hdr.peakLuminanceNits", defaults.hdrPeakLuminanceNitsUVE,
                                     100.0, 10000.0, "Peak Luminance (nits)", "Rendering/HDR")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.hdrPeakLuminanceNitsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("rendering.hdr.paperWhiteNits", defaults.hdrPaperWhiteNitsUVE,
                                     80.0, 1000.0, "Paper White (nits)", "Rendering/HDR")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.hdrPaperWhiteNitsUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.general.renderTargetFormat", defaults.renderTargetFormatUVE,
             {{0, "RGBA8 UNorm"}, {1, "RGB10A2 UNorm"}, {2, "RGBA16 Float"}}, "Render Target Precision",
             "Rendering/General"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.renderTargetFormatUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.threading.multiThreadedRendering", defaults.multiThreadedRenderingUVE,
             "Multi-Threaded Rendering", "Rendering/Threading"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.multiThreadedRenderingUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             "rendering.threading.commandBufferThreads", defaults.commandBufferThreadCountUVE,
             1, 64, "Command Buffer Threads", "Rendering/Threading"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.commandBufferThreadCountUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.antialiasing.msaa2dSamples", defaults.msaa2dSamplesUVE,
             {{1, "Disabled (1x)"}, {2, "2x"}, {4, "4x"}, {8, "8x"}}, "2D MSAA Samples",
             "Rendering/AntiAliasing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.msaa2dSamplesUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.antialiasing.msaa3dSamples", defaults.msaa3dSamplesUVE,
             {{1, "Disabled (1x)"}, {2, "2x"}, {4, "4x"}, {8, "8x"}}, "3D MSAA Samples",
             "Rendering/AntiAliasing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.msaa3dSamplesUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             "rendering.antialiasing.taaSamples", defaults.taaSampleCountUVE, 2, 16, "TAA Sample Count",
             "Rendering/AntiAliasing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.taaSampleCountUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.antialiasing.taaJitterPattern", defaults.taaJitterPatternUVE,
             {{0, "Halton (2,3)"}, {1, "Blue Noise"}, {2, "Uniform 2x"}}, "TAA Jitter Pattern",
             "Rendering/AntiAliasing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.taaJitterPatternUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             "rendering.antialiasing.taaSharpening", defaults.taaSharpeningAmountUVE, 0.0, 1.0,
             "TAA Sharpening", "Rendering/AntiAliasing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.taaSharpeningAmountUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.antialiasing.alphaToCoverage", defaults.alphaToCoverageEnabledUVE,
             "Alpha-to-Coverage", "Rendering/AntiAliasing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.alphaToCoverageEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.shadows.atlasSize", defaults.shadowAtlasSizeUVE,
             {{1024, "1024"}, {2048, "2048"}, {4096, "4096"}, {8192, "8192"}}, "Shadow Atlas Size",
             "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowAtlasSizeUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             "rendering.shadows.directionalSplitCount", defaults.directionalShadowSplitCountUVE, 1, 4,
             "Directional Split Count", "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.directionalShadowSplitCountUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             "rendering.shadows.maxDistance", defaults.shadowMaxDistanceUVE, 1.0, 10000.0,
             "Shadow Max Distance", "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowMaxDistanceUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.shadows.depthFormatBits", defaults.shadowDepthFormatBitsUVE,
             {{16, "16-bit"}, {32, "32-bit"}}, "Shadow Depth Format", "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowDepthFormatBitsUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.shadows.pointSpotCubeResolution", defaults.pointSpotShadowResolutionUVE,
             {{256, "256"}, {512, "512"}, {1024, "1024"}, {2048, "2048"}}, "Point/Spot Cube Resolution",
             "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.pointSpotShadowResolutionUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.shadows.contactShadowsEnabled", defaults.contactShadowsEnabledUVE,
             "Contact Shadows", "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.contactShadowsEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             "rendering.shadows.contactShadowsLength", defaults.contactShadowsLengthUVE, 0.01, 2.0,
             "Contact Shadow Length", "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.contactShadowsLengthUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             "rendering.shadows.contactShadowsSteps", defaults.contactShadowsStepCountUVE, 4, 64,
             "Contact Shadow Steps", "Rendering/Shadows"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.contactShadowsStepCountUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.gi.ssilEnabled", defaults.ssilEnabledUVE, "Screen-Space Indirect Lighting",
             "Rendering/GI"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssilEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.gi.ssrEnabled", defaults.ssrEnabledUVE, "Screen-Space Reflections",
             "Rendering/GI"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssrEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             "rendering.gi.ssrMaxSteps", defaults.ssrMaxStepsUVE, 8, 256, "SSR Max Steps",
             "Rendering/GI"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.ssrMaxStepsUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.gi.reflectionProbeAtlasSize", defaults.reflectionProbeAtlasSizeUVE,
             {{512, "512"}, {1024, "1024"}, {2048, "2048"}, {4096, "4096"}}, "Reflection Probe Atlas Size",
             "Rendering/GI"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.reflectionProbeAtlasSizeUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.gi.voxelGiQuality", defaults.voxelGiQualityUVE,
             {{0, "Low"}, {1, "Medium"}, {2, "High"}, {3, "Ultra"}}, "Voxel GI Quality",
             "Rendering/GI"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.voxelGiQualityUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("rendering.gi.lightmapTexelDensity", defaults.lightmapTexelDensityUVE,
                                      1.0, 256.0, "Lightmap Texel Density", "Rendering/GI")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.lightmapTexelDensityUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("rendering.gi.lightProbeDefaultDensity", defaults.lightProbeDefaultDensityUVE,
                                      0.1, 64.0, "Light Probe Default Density", "Rendering/GI")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.lightProbeDefaultDensityUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.postProcessing.autoExposureEnabled", defaults.autoExposureEnabledUVE,
             "Auto-Exposure", "Rendering/PostProcessing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.autoExposureEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeAssetReferenceSettingUVE(
             "rendering.postProcessing.colorGradingLutAsset", defaults.colorGradingLutAssetUVE, 256U,
             "Colour Grading LUT", "Rendering/PostProcessing")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.colorGradingLutAssetUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.postProcessing.volumetricFogEnabled", defaults.volumetricFogEnabledUVE,
             "Volumetric Fog", "Rendering/PostProcessing"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.volumetricFogEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("rendering.camera.defaultNearClip", defaults.defaultNearClipPlaneUVE,
                                      0.001, 10.0, "Default Near Clip Plane", "Rendering/Culling")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultNearClipPlaneUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("rendering.camera.defaultFarClip", defaults.defaultFarClipPlaneUVE,
                                      10.0, 1000000.0, "Default Far Clip Plane", "Rendering/Culling")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultFarClipPlaneUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.culling.occlusionCullingEnabled", defaults.occlusionCullingEnabledUVE,
             "Occlusion Culling", "Rendering/Culling"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.occlusionCullingEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             "rendering.lod.globalBias", defaults.globalLodBiasUVE, -4.0, 4.0, "Global LOD Bias",
             "Rendering/LOD"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.globalLodBiasUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("rendering.lod.autoGenerateDefault", defaults.meshLodAutoGenerateDefaultUVE,
                                     "Auto-Generate Mesh LODs", "Rendering/LOD")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.meshLodAutoGenerateDefaultUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("rendering.batching.instancingMinCount", defaults.instancingMinCountUVE,
                                    2, 1024, "Instancing Threshold", "Rendering/Batching")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.instancingMinCountUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("rendering.batching.staticBatchingEnabled", defaults.staticBatchingEnabledUVE,
                                     "Static Batching", "Rendering/Batching")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.staticBatchingEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.batching.indirectDrawEnabled", defaults.indirectDrawEnabledUVE,
             "GPU Indirect Draw", "Rendering/Batching"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.indirectDrawEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("rendering.budgets.maxDrawCallsWarning", defaults.maxDrawCallsWarningUVE,
                                   100, 100000, "Max Draw Calls Warning", "Rendering/Budgets")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.maxDrawCallsWarningUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.textures.defaultFilter", defaults.defaultTextureFilterUVE,
             {{0, "Nearest"}, {1, "Bilinear"}, {2, "Trilinear"}}, "Default Texture Filter",
             "Rendering/Textures"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultTextureFilterUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeEnumSettingUVE(
             "rendering.textures.defaultAnisotropy", defaults.defaultAnisotropyLevelUVE,
             {{1, "1x"}, {2, "2x"}, {4, "4x"}, {8, "8x"}, {16, "16x"}}, "Default Anisotropy",
             "Rendering/Textures"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultAnisotropyLevelUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeFloatSettingUVE(
             "rendering.textures.globalMipLodBias", defaults.globalMipLodBiasUVE, -4.0, 4.0,
             "Global Mipmap LOD Bias", "Rendering/Textures"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.globalMipLodBiasUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeBoolSettingUVE(
             "rendering.textures.streamingEnabled", defaults.textureStreamingEnabledUVE,
             "Texture Streaming", "Rendering/Textures"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.textureStreamingEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(PlatformOverrideUVE(Config::MakeIntSettingUVE(
             "rendering.textures.maxSizePerTier", defaults.maxTextureSizePerTierUVE,
             256, 16384, "Max Texture Resolution", "Rendering/Textures"))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.maxTextureSizePerTierUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             "rendering.textures.defaultRepeatMode", defaults.defaultTextureRepeatModeUVE,
             {{0, "Clamp"}, {1, "Repeat"}, {2, "Mirror"}}, "Default Texture Repeat", "Rendering/Textures")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultTextureRepeatModeUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             "rendering.2d.defaultCanvasFilter", defaults.defaultCanvasFilterUVE,
             {{0, "Nearest"}, {1, "Linear"}}, "2D Canvas Filter", "Rendering/2D")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultCanvasFilterUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("rendering.2d.pixelSnapTransforms", defaults.pixelSnap2dTransformsUVE,
                                    "2D Transform Pixel Snap", "Rendering/2D")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.pixelSnap2dTransformsUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("rendering.2d.shadowAtlasSize", defaults.shadowAtlas2dSizeUVE,
                                   256, 8192, "2D Shadow Atlas Size", "Rendering/2D")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.shadowAtlas2dSizeUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             "rendering.2d.spriteSortAxis", defaults.spriteSortAxisUVE,
             {{0, "Z-Index Only"}, {1, "Y-Axis"}, {2, "Custom Axis"}}, "Sprite Sort Axis", "Rendering/2D")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.spriteSortAxisUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             "rendering.environment.defaultSkyMode", defaults.defaultSkyModeUVE,
             {{0, "None"}, {1, "Colour"}, {2, "Gradient"}, {3, "Physical"}, {4, "Panorama"}},
             "Default Sky Mode", "Rendering/Environment")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultSkyModeUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeIntSettingUVE("rendering.environment.skyResolution", defaults.defaultSkyResolutionUVE,
                                   64, 4096, "Default Sky Resolution", "Rendering/Environment")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultSkyResolutionUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeAssetReferenceSettingUVE(
             "rendering.environment.defaultWorldEnvironmentAsset", defaults.defaultWorldEnvironmentAssetUVE, 256U,
             "Default WorldEnvironment Asset", "Rendering/Environment")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultWorldEnvironmentAssetUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(Config::MakeAssetReferenceSettingUVE(
             "physics.materials.defaultAsset", defaults.defaultPhysicsMaterialAssetUVE, 256U,
             "Default Physics Material Asset", "Physics/Materials")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.defaultPhysicsMaterialAssetUVE = std::get<std::string>(value);
         }},
        {RestartRequiredUVE(Config::MakeFloatSettingUVE("accessibility.runtime.uiScale", defaults.runtimeUiScaleUVE,
                                     0.5, 3.0, "Runtime UI Scale", "Accessibility/Runtime")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.runtimeUiScaleUVE = std::get<double>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("accessibility.runtime.subtitlesEnabled", defaults.runtimeSubtitlesEnabledUVE,
                                    "Subtitles Enabled by Default", "Accessibility/Runtime")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.runtimeSubtitlesEnabledUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("accessibility.runtime.reduceMotion", defaults.runtimeReduceMotionUVE,
                                    "Reduce Motion", "Accessibility/Runtime")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.runtimeReduceMotionUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("privacy.telemetry.analyticsOptIn", defaults.telemetryAnalyticsOptInUVE,
                                    "Editor & Runtime Analytics Opt-In", "Privacy/Telemetry")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.telemetryAnalyticsOptInUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(Config::MakeEnumSettingUVE(
             "privacy.telemetry.crashReportPolicy", defaults.crashReportSubmissionPolicyUVE,
             {{0, "Ask Each Time"}, {1, "Always"}, {2, "Never"}}, "Crash Report Submission",
             "Privacy/Telemetry")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.crashReportSubmissionPolicyUVE = std::get<std::int64_t>(value);
         }},
        {RestartRequiredUVE(Config::MakeBoolSettingUVE("privacy.telemetry.anonymousUsageStats", defaults.anonymousUsageStatsOptInUVE,
                                    "Anonymous Usage Statistics", "Privacy/Telemetry")),
         [](EngineConfigUVE& config, const SettingValueUVE& value) {
             config.anonymousUsageStatsOptInUVE = std::get<bool>(value);
         }},
        {RestartRequiredUVE(CommandLineOnlyUVE(Config::MakeBoolSettingUVE(
             std::string(Id::kHeadlessUVE), defaults.headlessUVE, "Headless", "Application/Runtime",
             "Run without a visible window. Command-line-only; use --headless."))),
         [](EngineConfigUVE& config, const SettingValueUVE& value) { config.headlessUVE = std::get<bool>(value); }},
    };
    return settings;
}

[[nodiscard]] bool ParseCommandLineNumberUVE(const std::string_view text, std::int64_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool ParseCommandLineNumberUVE(const std::string_view text, double& value) noexcept {
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value,
                                               std::chars_format::general);
    return error == std::errc{} && end == text.data() + text.size();
}

[[nodiscard]] bool EqualsAsciiCaseInsensitiveUVE(const std::string_view left, const std::string_view right) noexcept {
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

[[nodiscard]] std::optional<std::size_t> ParseCommaSeparatedNumbersUVE(const std::string_view text,
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
        if (!ParseCommandLineNumberUVE(component, values[count])) {
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

[[nodiscard]] std::optional<Config::SettingValueUVE> ParseCommandLineSettingValueUVE(
    const Config::SettingDescriptorUVE& descriptor, const std::optional<std::string>& argument) {
    if (!argument.has_value()) {
        return descriptor.type == Config::SettingTypeUVE::Bool
                   ? std::optional<Config::SettingValueUVE>{Config::SettingValueUVE{true}}
                   : std::nullopt;
    }
    const std::string_view text = *argument;
    switch (descriptor.type) {
    case Config::SettingTypeUVE::Bool:
        if (EqualsAsciiCaseInsensitiveUVE(text, "true") || text == "1" ||
            EqualsAsciiCaseInsensitiveUVE(text, "on")) {
            return Config::SettingValueUVE{true};
        }
        if (EqualsAsciiCaseInsensitiveUVE(text, "false") || text == "0" ||
            EqualsAsciiCaseInsensitiveUVE(text, "off")) {
            return Config::SettingValueUVE{false};
        }
        return std::nullopt;
    case Config::SettingTypeUVE::Int:
    case Config::SettingTypeUVE::LayerMask: {
        std::int64_t value = 0;
        return ParseCommandLineNumberUVE(text, value) ? std::optional<Config::SettingValueUVE>{value} : std::nullopt;
    }
    case Config::SettingTypeUVE::Float: {
        double value = 0.0;
        return ParseCommandLineNumberUVE(text, value) ? std::optional<Config::SettingValueUVE>{value} : std::nullopt;
    }
    case Config::SettingTypeUVE::String:
    case Config::SettingTypeUVE::FilePath:
    case Config::SettingTypeUVE::AssetReference:
    case Config::SettingTypeUVE::KeyBinding:
        return Config::SettingValueUVE{std::string{text}};
    case Config::SettingTypeUVE::StringList:
        return std::nullopt; // The command-line layer does not split or escape list values.
    case Config::SettingTypeUVE::Enum: {
        std::int64_t value = 0;
        if (ParseCommandLineNumberUVE(text, value)) {
            return Config::SettingValueUVE{value};
        }
        for (const Config::SettingEnumEntryUVE& entry : descriptor.enumEntries) {
            if (EqualsAsciiCaseInsensitiveUVE(text, entry.label)) {
                return Config::SettingValueUVE{entry.value};
            }
        }
        return std::nullopt;
    }
    case Config::SettingTypeUVE::Color: {
        std::array<double, 4U> channels{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbersUVE(text, channels);
        if (!count.has_value() || *count < 3U) {
            return std::nullopt;
        }
        Config::SettingColorUVE color{static_cast<float>(channels[0U]), static_cast<float>(channels[1U]),
                                      static_cast<float>(channels[2U])};
        if (*count == 4U) {
            color.a = static_cast<float>(channels[3U]);
        }
        return Config::SettingValueUVE{color};
    }
    case Config::SettingTypeUVE::Vector2: {
        std::array<double, 2U> components{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbersUVE(text, components);
        if (!count.has_value() || *count != components.size()) {
            return std::nullopt;
        }
        return Config::SettingValueUVE{Config::SettingVector2UVE{components[0U], components[1U]}};
    }
    case Config::SettingTypeUVE::Vector3: {
        std::array<double, 3U> components{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbersUVE(text, components);
        if (!count.has_value() || *count != components.size()) {
            return std::nullopt;
        }
        return Config::SettingValueUVE{Config::SettingVector3UVE{components[0U], components[1U], components[2U]}};
    }
    case Config::SettingTypeUVE::Vector4: {
        std::array<double, 4U> components{};
        const std::optional<std::size_t> count = ParseCommaSeparatedNumbersUVE(text, components);
        if (!count.has_value() || *count != components.size()) {
            return std::nullopt;
        }
        return Config::SettingValueUVE{
            Config::SettingVector4UVE{components[0U], components[1U], components[2U], components[3U]}};
    }
    }
    return std::nullopt;
}

void PopulateEngineCommandLineSettingsUVEInternal(const Config::SettingsRegistryUVE& registry,
                                    const CommandLine::ICommandLineUVE& commandLine,
                                    Config::IConfigManagerUVE& store) {
    for (const Config::SettingDescriptorUVE* descriptor : registry.GetAllUVE()) {
        if (!IsEngineConfigSettingIdUVE(descriptor->id) || !commandLine.HasFlagUVE(descriptor->id)) {
            continue;
        }
        const std::optional<Config::SettingValueUVE> value =
            ParseCommandLineSettingValueUVE(*descriptor, commandLine.GetOptionalValueUVE(descriptor->id));
        if (!value.has_value() || !registry.SetValueUVE(store, descriptor->id, *value)) {
            UVE_WARNING("EngineSettingsUVE: ignoring invalid command-line setting override '--{}'", descriptor->id);
        }
    }
}

} // namespace

std::string GetLayerNameSettingIdUVE(const LayerSetUVE set, const std::size_t index) {
    std::string_view prefix = "layers.render.";
    switch (set) {
    case LayerSetUVE::Physics:
        prefix = "layers.physics.";
        break;
    case LayerSetUVE::Physics2D:
        prefix = "layers.physics2d.";
        break;
    case LayerSetUVE::Render:
        prefix = "layers.render.";
        break;
    case LayerSetUVE::Navigation:
        prefix = "layers.navigation.";
        break;
    case LayerSetUVE::Avoidance:
        prefix = "layers.avoidance.";
        break;
    }
    return std::string(prefix) + std::to_string(index + 1U);
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
    allRegistered = registry.RegisterUVE(Config::MakeFilePathSettingUVE(
                        std::string(EngineProjectSettingIdUVE::kDefaultPlayerEntityUVE), "",
                        kMaximumContentPathBytesUVE, "Default Player", "Game/Player",
                        "The entity asset (.uventity, relative to Content) the player is spawned from. Set it "
                        "from the Content panel: right-click an entity, Set as Default Player.")) &&
                    allRegistered;
    // Layer names, 1 to 32 as a person counts them; bit 0 is layer 1.
    constexpr std::size_t kMaximumLayerNameBytesUVE = 32U;
    for (const LayerSetUVE set : {LayerSetUVE::Physics, LayerSetUVE::Physics2D, LayerSetUVE::Render,
                                  LayerSetUVE::Navigation, LayerSetUVE::Avoidance}) {
        std::string_view category = "Layers/Render";
        std::string_view tooltip = "Shown wherever a mesh's, light's or decal's render layers are picked.";
        switch (set) {
        case LayerSetUVE::Physics:
            category = "Layers/Physics";
            tooltip = "Shown wherever a 3D collider's layer or mask is picked.";
            break;
        case LayerSetUVE::Physics2D:
            category = "Layers/Physics2D";
            tooltip = "Shown wherever a 2D collider's layer or mask is picked.";
            break;
        case LayerSetUVE::Render:
            category = "Layers/Render";
            tooltip = "Shown wherever a mesh's, light's or decal's render layers are picked.";
            break;
        case LayerSetUVE::Navigation:
            category = "Layers/Navigation";
            tooltip = "Shown wherever a navigation region or seeker layer mask is picked.";
            break;
        case LayerSetUVE::Avoidance:
            category = "Layers/Avoidance";
            tooltip = "Shown wherever an avoidance obstacle or agent mask is picked.";
            break;
        }
        for (std::size_t index = 0U; index < kLayerCountUVE; ++index) {
            allRegistered =
                registry.RegisterUVE(Config::MakeStringSettingUVE(
                    GetLayerNameSettingIdUVE(set, index), "", kMaximumLayerNameBytesUVE,
                    "Layer " + std::to_string(index + 1U), std::string{category},
                    std::string{tooltip})) &&
                allRegistered;
        }
    }
    return allRegistered;
}

bool IsEngineConfigSettingIdUVE(const std::string_view id) {
    const auto& settings = GetEngineProjectSettingsUVE();
    return std::any_of(settings.begin(), settings.end(),
                       [id](const EngineProjectSettingUVE& setting) { return setting.descriptor.id == id; });
}

void PopulateEngineCommandLineSettingsUVE(const Config::SettingsRegistryUVE& registry,
                                          const CommandLine::ICommandLineUVE& commandLine,
                                          Config::IConfigManagerUVE& store) {
    PopulateEngineCommandLineSettingsUVEInternal(registry, commandLine, store);
}

std::filesystem::path GetPlatformSettingsFilePathUVE(const EngineConfigUVE& config) {
    if (!config.platformSettingsFilePath.empty()) {
        return config.platformSettingsFilePath;
    }
    return config.settingsFilePath.parent_path() / "platforms" / std::string{GetPlatformSettingsTargetUVE()} /
           config.settingsFilePath.filename();
}

bool AttachEngineSettingsLayersUVE(Config::SettingsStackUVE& settings, const Config::IConfigManagerUVE& project,
                                   const Config::IConfigManagerUVE& user, const Config::IConfigManagerUVE& platform,
                                   const Config::IConfigManagerUVE& commandLine) noexcept {
    return settings.AttachLayerUVE(Config::SettingValueSourceUVE::Project, project) &&
           settings.AttachLayerUVE(Config::SettingValueSourceUVE::User, user) &&
           settings.AttachLayerUVE(Config::SettingValueSourceUVE::Platform, platform) &&
           settings.AttachLayerUVE(Config::SettingValueSourceUVE::CommandLine, commandLine);
}

void ApplyEngineSettingsUVE(const Config::SettingsStackUVE& settings, EngineConfigUVE& config) {
    constexpr std::array<Config::SettingValueSourceUVE, 4U> kOverridePriority{
        Config::SettingValueSourceUVE::CommandLine,
        Config::SettingValueSourceUVE::Platform,
        Config::SettingValueSourceUVE::User,
        Config::SettingValueSourceUVE::Project,
    };
    for (const EngineProjectSettingUVE& setting : GetEngineProjectSettingsUVE()) {
        if (setting.acceptsOverride != nullptr) {
            bool ignoredUnsafeOverride = false;
            for (const Config::SettingValueSourceUVE source : kOverridePriority) {
                const std::optional<SettingValueUVE> value = settings.GetStoredValueUVE(source, setting.descriptor.id);
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
                UVE_WARNING("EngineSettingsUVE: ignored unsafe override for setting '{}'", setting.descriptor.id);
            }
            continue;
        }
        if (const std::optional<Config::SettingResolutionUVE> resolved =
                settings.ResolveUVE(setting.descriptor.id);
            resolved.has_value() && resolved->source != Config::SettingValueSourceUVE::EngineDefault) {
            setting.apply(config, resolved->value);
        }
    }
}

void ApplyEngineProjectSettingsUVE(const Config::SettingsDocumentUVE& document, EngineConfigUVE& config) {
    Config::SettingsStackUVE settings(document.GetRegistryUVE());
    if (settings.AttachLayerUVE(Config::SettingValueSourceUVE::Project, document.GetStoreUVE())) {
        ApplyEngineSettingsUVE(settings, config);
    }
}

} // namespace UVE::Core
