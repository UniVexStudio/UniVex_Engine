// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "engine_project_settings.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "command_line.h"
#include "config_manager.h"

namespace UVE::Core::Tests {
namespace {

using Config::ConfigManager;
using Config::SettingValueSource;

TEST(EngineSettingsLayeringTest, AttachesLayersAndAppliesResolvedSourcesToEngineConfig) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLine;
    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLine));

    namespace Id = EngineProjectSettingId;
    project.SetDouble(Id::kPhysicsTicksPerSecond, 30.0);
    user.SetDouble(Id::kPhysicsTicksPerSecond, 90.0);
    platform.SetDouble(Id::kPhysicsTicksPerSecond, 240.0); // Not marked PerPlatform.

    project.SetInt(Id::kShadowMapResolution, 4096);
    user.SetInt(Id::kShadowMapResolution, 2048);
    platform.SetInt(Id::kShadowMapResolution, 1024);
    commandLine.SetInt(Id::kShadowMapResolution, 512);

    // NotPersisted settings only read from the command-line layer.
    project.SetBool(Id::kHeadless, true);
    user.SetBool(Id::kHeadless, true);
    platform.SetBool(Id::kHeadless, true);
    commandLine.SetBool(Id::kHeadless, true);
    project.SetBool(Id::kQuitOnLastWindowClosed, true);
    user.SetBool(Id::kQuitOnLastWindowClosed, false);
    commandLine.SetBool(Id::kQuitOnLastWindowClosed, true);

    const auto ticks = settings.Resolve(Id::kPhysicsTicksPerSecond);
    ASSERT_TRUE(ticks.has_value());
    EXPECT_EQ(ticks->source, SettingValueSource::User);
    EXPECT_DOUBLE_EQ(std::get<double>(ticks->value), 90.0);
    EXPECT_FALSE(settings.GetStoredValue(SettingValueSource::Platform, Id::kPhysicsTicksPerSecond)
                     .has_value());

    const auto shadowResolution = settings.Resolve(Id::kShadowMapResolution);
    ASSERT_TRUE(shadowResolution.has_value());
    EXPECT_EQ(shadowResolution->source, SettingValueSource::CommandLine);
    EXPECT_EQ(std::get<std::int64_t>(shadowResolution->value), 512);

    const auto headless = settings.Resolve(Id::kHeadless);
    ASSERT_TRUE(headless.has_value());
    EXPECT_EQ(headless->source, SettingValueSource::CommandLine);

    EngineConfig config{};
    config.autoSaveIntervalSeconds = 777.0; // No layer supplies this setting; preserve the caller's base.
    ApplyEngineSettings(settings, config);
    EXPECT_DOUBLE_EQ(config.fixedUpdateFps, 90.0);
    EXPECT_EQ(config.shadowMapResolution, 512U);
    EXPECT_TRUE(config.headless);
    EXPECT_TRUE(config.quitOnLastWindowClosed);
    EXPECT_DOUBLE_EQ(config.autoSaveIntervalSeconds, 777.0);

    // An invalid command-line enum value must not mask the valid platform value below it.
    commandLine.SetInt(Id::kShadowMapResolution, 1536);
    const auto fallback = settings.Resolve(Id::kShadowMapResolution);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->source, SettingValueSource::Platform);
    EXPECT_EQ(std::get<std::int64_t>(fallback->value), 1024);
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.shadowMapResolution, 1024U);
}

TEST(EngineSettingsLayeringTest, WindowSettingsExposeExpectedPortableDefaults) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    Config::SettingsStack settings(registry);
    namespace Id = EngineProjectSettingId;

    const auto rendererBackend = settings.GetValue(Id::kRenderBackendPreference);
    ASSERT_TRUE(rendererBackend.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*rendererBackend),
              static_cast<std::int64_t>(RenderBackendPreference::Auto));
    const auto renderResolutionScale = settings.GetValue(Id::kRenderResolutionScale);
    ASSERT_TRUE(renderResolutionScale.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*renderResolutionScale), 1.0);
    const Config::SettingDescriptor* const renderScaleDescriptor =
        registry.Find(Id::kRenderResolutionScale);
    ASSERT_NE(renderScaleDescriptor, nullptr);
    EXPECT_TRUE(renderScaleDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(renderScaleDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    ASSERT_TRUE(renderScaleDescriptor->minimum.has_value());
    ASSERT_TRUE(renderScaleDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*renderScaleDescriptor->minimum, 0.5);
    EXPECT_DOUBLE_EQ(*renderScaleDescriptor->maximum, 1.0);
    const auto dynamicResolution = settings.GetValue(Id::kDynamicRenderResolutionEnabled);
    ASSERT_TRUE(dynamicResolution.has_value());
    EXPECT_FALSE(std::get<bool>(*dynamicResolution));
    const auto minimumRenderScale = settings.GetValue(Id::kMinimumRenderResolutionScale);
    ASSERT_TRUE(minimumRenderScale.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*minimumRenderScale), 0.5);
    const auto targetRenderFrameTime =
        settings.GetValue(Id::kRenderResolutionTargetFrameTimeMilliseconds);
    ASSERT_TRUE(targetRenderFrameTime.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*targetRenderFrameTime), 16.6667);
    const auto shaderCacheDirectory = settings.GetValue(Id::kShaderProgramCacheDirectory);
    ASSERT_TRUE(shaderCacheDirectory.has_value());
    EXPECT_EQ(std::get<std::string>(*shaderCacheDirectory), "shader_cache/");
    const auto shaderCompilationMode = settings.GetValue(Id::kShaderCompilationMode);
    ASSERT_TRUE(shaderCompilationMode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*shaderCompilationMode),
              static_cast<std::int64_t>(ShaderCompilationMode::AsynchronousOnDemand));
    const Config::SettingDescriptor* const shaderCompilationDescriptor =
        registry.Find(Id::kShaderCompilationMode);
    ASSERT_NE(shaderCompilationDescriptor, nullptr);
    EXPECT_EQ(shaderCompilationDescriptor->type, Config::SettingType::Enum);
    ASSERT_EQ(shaderCompilationDescriptor->enumEntries.size(), 2U);
    EXPECT_EQ(shaderCompilationDescriptor->enumEntries[0U].value,
              static_cast<std::int64_t>(ShaderCompilationMode::SynchronousOnDemand));
    EXPECT_EQ(shaderCompilationDescriptor->enumEntries[1U].value,
              static_cast<std::int64_t>(ShaderCompilationMode::AsynchronousOnDemand));
    EXPECT_TRUE(shaderCompilationDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(shaderCompilationDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    const auto vulkanValidation = settings.GetValue(Id::kVulkanValidationLayersEnabled);
    ASSERT_TRUE(vulkanValidation.has_value());
    EXPECT_EQ(std::get<bool>(*vulkanValidation), EngineConfig{}.vulkanValidationLayersEnabled);
    const Config::SettingDescriptor* const vulkanValidationDescriptor =
        registry.Find(Id::kVulkanValidationLayersEnabled);
    ASSERT_NE(vulkanValidationDescriptor, nullptr);
    EXPECT_EQ(vulkanValidationDescriptor->type, Config::SettingType::Bool);
    EXPECT_TRUE(vulkanValidationDescriptor->HasFlag(Config::kSettingFlagAdvanced));
    EXPECT_TRUE(vulkanValidationDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(vulkanValidationDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    const auto upscalingMethod = settings.GetValue(Id::kSpatialUpscalingMethod);
    ASSERT_TRUE(upscalingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*upscalingMethod),
              static_cast<std::int64_t>(SpatialUpscalingMethod::Bilinear));
    const auto toneMappingMethod = settings.GetValue(Id::kToneMappingMethod);
    ASSERT_TRUE(toneMappingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*toneMappingMethod),
              static_cast<std::int64_t>(ToneMappingMethod::SoftFilmic));
    const auto sharpeningAmount = settings.GetValue(Id::kSharpeningAmount);
    ASSERT_TRUE(sharpeningAmount.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*sharpeningAmount), 0.0);
    const Config::SettingDescriptor* const sharpeningDescriptor = registry.Find(Id::kSharpeningAmount);
    ASSERT_NE(sharpeningDescriptor, nullptr);
    EXPECT_EQ(sharpeningDescriptor->type, Config::SettingType::Float);
    ASSERT_TRUE(sharpeningDescriptor->minimum.has_value());
    ASSERT_TRUE(sharpeningDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*sharpeningDescriptor->minimum, 0.0);
    EXPECT_DOUBLE_EQ(*sharpeningDescriptor->maximum, 1.0);
    EXPECT_TRUE(sharpeningDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(sharpeningDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    const auto ditheringEnabled = settings.GetValue(Id::kDitheringEnabled);
    ASSERT_TRUE(ditheringEnabled.has_value());
    EXPECT_EQ(std::get<bool>(*ditheringEnabled), EngineConfig{}.ditheringEnabled);
    const Config::SettingDescriptor* const ditheringDescriptor = registry.Find(Id::kDitheringEnabled);
    ASSERT_NE(ditheringDescriptor, nullptr);
    EXPECT_EQ(ditheringDescriptor->type, Config::SettingType::Bool);
    EXPECT_TRUE(ditheringDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(ditheringDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    const auto aaMethod = settings.GetValue(Id::kPostProcessAAMethod);
    ASSERT_TRUE(aaMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaMethod), static_cast<std::int64_t>(PostProcessAAMethod::None));
    const Config::SettingDescriptor* const aaMethodDescriptor = registry.Find(Id::kPostProcessAAMethod);
    ASSERT_NE(aaMethodDescriptor, nullptr);
    EXPECT_EQ(aaMethodDescriptor->type, Config::SettingType::Enum);
    ASSERT_EQ(aaMethodDescriptor->enumEntries.size(), 2U);
    EXPECT_TRUE(aaMethodDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(aaMethodDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    const auto aaQuality = settings.GetValue(Id::kScreenSpaceAAQuality);
    ASSERT_TRUE(aaQuality.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaQuality), 1);
    const auto shadowBiasDefault = settings.GetValue(Id::kShadowBiasDefault);
    ASSERT_TRUE(shadowBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowBiasDefault),
                     static_cast<double>(EngineConfig{}.shadowBiasDefault));
    const auto shadowNormalBiasDefault = settings.GetValue(Id::kShadowNormalBiasDefault);
    ASSERT_TRUE(shadowNormalBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowNormalBiasDefault),
                     static_cast<double>(EngineConfig{}.shadowNormalBiasDefault));
    const auto shadowCascadeSplitBlend = settings.GetValue(Id::kShadowCascadeSplitBlend);
    ASSERT_TRUE(shadowCascadeSplitBlend.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowCascadeSplitBlend),
                     static_cast<double>(EngineConfig{}.shadowCascadeSplitLambda));
    const Config::SettingDescriptor* const shadowCascadeSplitBlendDescriptor =
        registry.Find(Id::kShadowCascadeSplitBlend);
    ASSERT_NE(shadowCascadeSplitBlendDescriptor, nullptr);
    EXPECT_EQ(shadowCascadeSplitBlendDescriptor->type, Config::SettingType::Float);
    EXPECT_TRUE(shadowCascadeSplitBlendDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(shadowCascadeSplitBlendDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    for (const std::string_view id : {Id::kShadowBiasDefault, Id::kShadowNormalBiasDefault}) {
        const Config::SettingDescriptor* const biasDescriptor = registry.Find(id);
        ASSERT_NE(biasDescriptor, nullptr);
        EXPECT_EQ(biasDescriptor->type, Config::SettingType::Float);
        ASSERT_TRUE(biasDescriptor->minimum.has_value());
        ASSERT_TRUE(biasDescriptor->maximum.has_value());
        EXPECT_DOUBLE_EQ(*biasDescriptor->minimum, 0.0);
        EXPECT_DOUBLE_EQ(*biasDescriptor->maximum, 10.0);
        EXPECT_TRUE(biasDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
        EXPECT_TRUE(biasDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    }
    const Config::SettingDescriptor* const aaQualityDescriptor = registry.Find(Id::kScreenSpaceAAQuality);
    ASSERT_NE(aaQualityDescriptor, nullptr);
    ASSERT_EQ(aaQualityDescriptor->enumEntries.size(), 3U);
    const Config::SettingDescriptor* const shaderCacheDescriptor =
        registry.Find(Id::kShaderProgramCacheDirectory);
    ASSERT_NE(shaderCacheDescriptor, nullptr);
    EXPECT_EQ(shaderCacheDescriptor->type, Config::SettingType::FilePath);
    EXPECT_EQ(shaderCacheDescriptor->maxLength, 512U);
    EXPECT_TRUE(shaderCacheDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(shaderCacheDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    const Config::SettingDescriptor* const targetFrameTimeDescriptor =
        registry.Find(Id::kRenderResolutionTargetFrameTimeMilliseconds);
    ASSERT_NE(targetFrameTimeDescriptor, nullptr);
    ASSERT_TRUE(targetFrameTimeDescriptor->minimum.has_value());
    ASSERT_TRUE(targetFrameTimeDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*targetFrameTimeDescriptor->minimum,
                     kMinimumRenderResolutionTargetFrameTimeMilliseconds);
    EXPECT_DOUBLE_EQ(*targetFrameTimeDescriptor->maximum,
                     kMaximumRenderResolutionTargetFrameTimeMilliseconds);
    const Config::SettingDescriptor* const dynamicResolutionDescriptor =
        registry.Find(Id::kDynamicRenderResolutionEnabled);
    ASSERT_NE(dynamicResolutionDescriptor, nullptr);
    EXPECT_TRUE(dynamicResolutionDescriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(dynamicResolutionDescriptor->HasFlag(Config::kSettingFlagRestartRequired));
    const auto defaultClearColor = settings.GetValue(Id::kBootBackgroundColor);
    ASSERT_TRUE(defaultClearColor.has_value());
    EXPECT_EQ(std::get<Config::SettingColor>(*defaultClearColor),
              (Config::SettingColor{0.05F, 0.05F, 0.05F, 1.0F}));
    const auto width = settings.GetValue(Id::kWindowWidth);
    ASSERT_TRUE(width.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*width), 1280);
    const auto height = settings.GetValue(Id::kWindowHeight);
    ASSERT_TRUE(height.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*height), 720);
    const auto mode = settings.GetValue(Id::kWindowMode);
    ASSERT_TRUE(mode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*mode),
              static_cast<std::int64_t>(Platform::WindowMode::Windowed));
    const auto vsync = settings.GetValue(Id::kWindowVSyncMode);
    ASSERT_TRUE(vsync.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*vsync), static_cast<std::int64_t>(Platform::VSyncMode::On));
    const auto cursorVisible = settings.GetValue(Id::kWindowCursorVisible);
    ASSERT_TRUE(cursorVisible.has_value());
    EXPECT_TRUE(std::get<bool>(*cursorVisible));
    const auto cursorConfined = settings.GetValue(Id::kWindowCursorConfined);
    ASSERT_TRUE(cursorConfined.has_value());
    EXPECT_FALSE(std::get<bool>(*cursorConfined));
    const auto allowDisplaySleep = settings.GetValue(Id::kWindowAllowDisplaySleep);
    ASSERT_TRUE(allowDisplaySleep.has_value());
    EXPECT_TRUE(std::get<bool>(*allowDisplaySleep));
    const auto allowedOrientations = settings.GetValue(Id::kWindowAllowedOrientations);
    ASSERT_TRUE(allowedOrientations.has_value());
    EXPECT_EQ(std::get<Config::SettingStringList>(*allowedOrientations),
              (Config::SettingStringList{"landscape", "portrait"}));
}

TEST(EngineSettingsLayeringTest, WindowSettingsApplyTypedPoliciesAndRespectPlatformOverrides) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    namespace Id = EngineProjectSettingId;

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLine;
    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLine));

    project.SetInt(Id::kWindowWidth, 1920);
    project.SetInt(Id::kWindowHeight, 1080);
    project.SetInt(Id::kWindowMode, 3);
    project.SetInt(Id::kWindowVSyncMode, 2);
    project.SetInt(Id::kWindowMaximumWidth, 2560);
    project.SetInt(Id::kRenderBackendPreference,
                      static_cast<std::int64_t>(RenderBackendPreference::OpenGL));
    platform.SetInt(Id::kRenderBackendPreference,
                       static_cast<std::int64_t>(RenderBackendPreference::Vulkan));
    project.SetDouble(Id::kRenderResolutionScale, 0.9);
    platform.SetDouble(Id::kRenderResolutionScale, 0.75);
    project.SetBool(Id::kDynamicRenderResolutionEnabled, true);
    project.SetDouble(Id::kMinimumRenderResolutionScale, 0.65);
    platform.SetDouble(Id::kMinimumRenderResolutionScale, 0.6);
    project.SetDouble(Id::kRenderResolutionTargetFrameTimeMilliseconds, 33.3333);
    platform.SetDouble(Id::kRenderResolutionTargetFrameTimeMilliseconds, 20.0);
    project.SetString(Id::kShaderProgramCacheDirectory, "shader_cache/project");
    platform.SetString(Id::kShaderProgramCacheDirectory, "shader_cache/platform");
    project.SetInt(Id::kShaderCompilationMode,
                      static_cast<std::int64_t>(ShaderCompilationMode::SynchronousOnDemand));
    platform.SetInt(Id::kShaderCompilationMode,
                       static_cast<std::int64_t>(ShaderCompilationMode::AsynchronousOnDemand));
    project.SetBool(Id::kVulkanValidationLayersEnabled, true);
    platform.SetBool(Id::kVulkanValidationLayersEnabled, false);
    project.SetInt(Id::kSpatialUpscalingMethod,
                      static_cast<std::int64_t>(SpatialUpscalingMethod::Nearest));
    platform.SetInt(Id::kSpatialUpscalingMethod,
                       static_cast<std::int64_t>(SpatialUpscalingMethod::Bilinear));
    project.SetInt(Id::kToneMappingMethod,
                      static_cast<std::int64_t>(ToneMappingMethod::LinearClamp));
    platform.SetInt(Id::kToneMappingMethod,
                       static_cast<std::int64_t>(ToneMappingMethod::Aces));
    project.SetDouble(Id::kSharpeningAmount, 0.25);
    platform.SetDouble(Id::kSharpeningAmount, 0.5);
    project.SetBool(Id::kDitheringEnabled, false);
    platform.SetBool(Id::kDitheringEnabled, true);
    project.SetInt(Id::kPostProcessAAMethod,
                      static_cast<std::int64_t>(PostProcessAAMethod::FastApproximate));
    platform.SetInt(Id::kPostProcessAAMethod,
                       static_cast<std::int64_t>(PostProcessAAMethod::None));
    project.SetInt(Id::kScreenSpaceAAQuality, 2);
    platform.SetInt(Id::kScreenSpaceAAQuality, 1);
    project.SetDouble(Id::kShadowCascadeSplitBlend, 0.25);
    platform.SetDouble(Id::kShadowCascadeSplitBlend, 0.75);
    project.SetDouble(Id::kShadowBiasDefault, 0.2);
    platform.SetDouble(Id::kShadowBiasDefault, 0.4);
    project.SetDouble(Id::kShadowNormalBiasDefault, 1.5);
    platform.SetDouble(Id::kShadowNormalBiasDefault, 0.5);
    project.SetDouble(Id::kWindowContentScaleOverride, 1.5);
    project.SetInt(Id::kWindowFocusedFrameRateCap, 144);
    project.SetInt(Id::kWindowUnfocusedFrameRateCap, 30);
    project.SetString(Id::kWindowTitleFormat, "{projectName} — {sceneName}");
    project.SetBool(Id::kWindowAppendSceneInEditorPlay, true);
    ASSERT_TRUE(registry.SetValue(
        project, Id::kWindowAllowedOrientations,
        Config::SettingValue{Config::SettingStringList{"landscapeLeft", "portrait"}}));
    platform.SetBool(Id::kWindowPerMonitorScaling, false);
    user.SetBool(Id::kWindowAlwaysOnTop, true);

    EngineConfig config;
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.windowWidth, 1920U);
    EXPECT_EQ(config.windowHeight, 1080U);
    EXPECT_EQ(config.windowMode, Platform::WindowMode::ExclusiveFullscreen);
    EXPECT_EQ(config.vsyncMode, Platform::VSyncMode::Adaptive);
    EXPECT_TRUE(config.vsyncModeExplicit);
    EXPECT_TRUE(config.vsyncEnabled);
    EXPECT_EQ(config.windowMaximumWidth, 2560U);
    EXPECT_EQ(config.renderBackendPreference, RenderBackendPreference::Vulkan);
    EXPECT_DOUBLE_EQ(config.renderResolutionScale, 0.75);
    EXPECT_TRUE(config.dynamicRenderResolutionEnabled);
    EXPECT_DOUBLE_EQ(config.minimumRenderResolutionScale, 0.6);
    EXPECT_DOUBLE_EQ(config.renderResolutionTargetFrameTimeMilliseconds, 20.0);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("shader_cache/platform"));
    EXPECT_EQ(config.shaderCompilationMode, ShaderCompilationMode::AsynchronousOnDemand);
    ASSERT_TRUE(platform.RemoveKey(Id::kShaderCompilationMode));
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.shaderCompilationMode, ShaderCompilationMode::SynchronousOnDemand);
    EXPECT_FALSE(config.vulkanValidationLayersEnabled);
    ASSERT_TRUE(platform.RemoveKey(Id::kVulkanValidationLayersEnabled));
    ApplyEngineSettings(settings, config);
    EXPECT_TRUE(config.vulkanValidationLayersEnabled);
    EXPECT_EQ(config.spatialUpscalingMethod, SpatialUpscalingMethod::Bilinear);
    ASSERT_TRUE(platform.RemoveKey(Id::kSpatialUpscalingMethod));
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.spatialUpscalingMethod, SpatialUpscalingMethod::Nearest);
    EXPECT_EQ(config.toneMappingMethod, ToneMappingMethod::Aces);
    EXPECT_FLOAT_EQ(config.sharpeningAmount, 0.5F);
    EXPECT_TRUE(config.ditheringEnabled);
    EXPECT_FLOAT_EQ(config.shadowCascadeSplitLambda, 0.75F);
    EXPECT_FLOAT_EQ(config.shadowBiasDefault, 0.4F);
    EXPECT_FLOAT_EQ(config.shadowNormalBiasDefault, 0.5F);
    ASSERT_TRUE(platform.RemoveKey(Id::kShadowCascadeSplitBlend));
    ApplyEngineSettings(settings, config);
    EXPECT_FLOAT_EQ(config.shadowCascadeSplitLambda, 0.25F);
    ASSERT_TRUE(platform.RemoveKey(Id::kShadowBiasDefault));
    ASSERT_TRUE(platform.RemoveKey(Id::kShadowNormalBiasDefault));
    ApplyEngineSettings(settings, config);
    EXPECT_FLOAT_EQ(config.shadowBiasDefault, 0.2F);
    EXPECT_FLOAT_EQ(config.shadowNormalBiasDefault, 1.5F);
    ASSERT_TRUE(platform.RemoveKey(Id::kToneMappingMethod));
    ASSERT_TRUE(platform.RemoveKey(Id::kSharpeningAmount));
    ASSERT_TRUE(platform.RemoveKey(Id::kDitheringEnabled));
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.toneMappingMethod, ToneMappingMethod::LinearClamp);
    EXPECT_FLOAT_EQ(config.sharpeningAmount, 0.25F);
    EXPECT_FALSE(config.ditheringEnabled);
    EXPECT_EQ(config.postProcessAAMethod, PostProcessAAMethod::None);
    EXPECT_EQ(config.screenSpaceAAQuality, 1U);
    ASSERT_TRUE(platform.RemoveKey(Id::kPostProcessAAMethod));
    ASSERT_TRUE(platform.RemoveKey(Id::kScreenSpaceAAQuality));
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.postProcessAAMethod, PostProcessAAMethod::FastApproximate);
    EXPECT_EQ(config.screenSpaceAAQuality, 2U);
    EXPECT_DOUBLE_EQ(config.contentScaleOverride, 1.5);
    EXPECT_FALSE(config.perMonitorScaling);
    EXPECT_TRUE(config.windowAlwaysOnTop);
    EXPECT_EQ(config.focusedFrameRateCap, 144U);
    EXPECT_EQ(config.unfocusedFrameRateCap, 30U);
    EXPECT_EQ(config.allowedOrientations,
              (std::vector<Platform::DisplayOrientation>{Platform::DisplayOrientation::LandscapeLeft,
                                                           Platform::DisplayOrientation::Portrait}));
    EXPECT_EQ(config.windowTitleFormat, "{projectName} — {sceneName}");
    EXPECT_TRUE(config.appendSceneNameInEditorPlayMode);
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kRenderBackendPreference));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kRenderResolutionScale));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kDynamicRenderResolutionEnabled));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kMinimumRenderResolutionScale));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kRenderResolutionTargetFrameTimeMilliseconds));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kShaderProgramCacheDirectory));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kShaderCompilationMode));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kVulkanValidationLayersEnabled));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kSpatialUpscalingMethod));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kToneMappingMethod));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kSharpeningAmount));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kDitheringEnabled));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kShadowCascadeSplitBlend));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kShadowBiasDefault));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kShadowNormalBiasDefault));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kPostProcessAAMethod));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kScreenSpaceAAQuality));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kWindowMode));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kWindowCursorImage));
}

TEST(EngineSettingsLayeringTest, UnsafeShaderCacheOverrideFallsThroughToSafeLowerLayer) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    namespace Id = EngineProjectSettingId;

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLine;
    project.SetString(Id::kShaderProgramCacheDirectory, "shader_cache/project");
    platform.SetString(Id::kShaderProgramCacheDirectory, "shader_cache/platform");
    commandLine.SetString(Id::kShaderProgramCacheDirectory, "../outside_cache");

    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLine));
    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("shader_cache/platform"));

    ASSERT_TRUE(platform.RemoveKey(Id::kShaderProgramCacheDirectory));
    ASSERT_TRUE(project.RemoveKey(Id::kShaderProgramCacheDirectory));
    config.shaderCachePath = "application_cache";
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("application_cache"));
}

TEST(EngineSettingsLayeringTest, SSAOControlsHaveDefaultsBoundsAndPlatformOverrides) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    Config::SettingsStack defaults(registry);
    namespace Id = EngineProjectSettingId;

    const auto enabled = defaults.GetValue(Id::kSsaoEnabled);
    ASSERT_TRUE(enabled.has_value());
    EXPECT_TRUE(std::get<bool>(*enabled));
    const auto radius = defaults.GetValue(Id::kSsaoRadius);
    ASSERT_TRUE(radius.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*radius), 0.5);
    const auto intensity = defaults.GetValue(Id::kSsaoIntensity);
    ASSERT_TRUE(intensity.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*intensity), 1.0);
    const auto power = defaults.GetValue(Id::kSsaoPower);
    ASSERT_TRUE(power.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*power), 1.0);
    const auto quality = defaults.GetValue(Id::kSsaoQuality);
    ASSERT_TRUE(quality.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*quality), 2);
    const auto blur = defaults.GetValue(Id::kSsaoBlurEnabled);
    ASSERT_TRUE(blur.has_value());
    EXPECT_TRUE(std::get<bool>(*blur));

    for (const std::string_view id : {Id::kSsaoEnabled, Id::kSsaoRadius, Id::kSsaoIntensity,
                                      Id::kSsaoPower, Id::kSsaoQuality, Id::kSsaoBlurEnabled}) {
        const Config::SettingDescriptor* const descriptor = registry.Find(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagPerPlatform)) << id;
        EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagRestartRequired)) << id;
    }
    const Config::SettingDescriptor* const radiusDescriptor = registry.Find(Id::kSsaoRadius);
    ASSERT_NE(radiusDescriptor, nullptr);
    ASSERT_TRUE(radiusDescriptor->minimum.has_value());
    ASSERT_TRUE(radiusDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*radiusDescriptor->minimum, 0.01);
    EXPECT_DOUBLE_EQ(*radiusDescriptor->maximum, 10.0);
    const Config::SettingDescriptor* const intensityDescriptor = registry.Find(Id::kSsaoIntensity);
    ASSERT_NE(intensityDescriptor, nullptr);
    ASSERT_TRUE(intensityDescriptor->minimum.has_value());
    ASSERT_TRUE(intensityDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*intensityDescriptor->minimum, 0.0);
    EXPECT_DOUBLE_EQ(*intensityDescriptor->maximum, 4.0);
    const Config::SettingDescriptor* const powerDescriptor = registry.Find(Id::kSsaoPower);
    ASSERT_NE(powerDescriptor, nullptr);
    ASSERT_TRUE(powerDescriptor->minimum.has_value());
    ASSERT_TRUE(powerDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*powerDescriptor->minimum, 0.1);
    EXPECT_DOUBLE_EQ(*powerDescriptor->maximum, 4.0);
    const Config::SettingDescriptor* const qualityDescriptor = registry.Find(Id::kSsaoQuality);
    ASSERT_NE(qualityDescriptor, nullptr);
    ASSERT_EQ(qualityDescriptor->enumEntries.size(), 3U);
    EXPECT_EQ(qualityDescriptor->enumEntries[0].label, "Low (4 samples)");
    EXPECT_EQ(qualityDescriptor->enumEntries[1].label, "Medium (8 samples)");
    EXPECT_EQ(qualityDescriptor->enumEntries[2].label, "High (12 samples)");

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLineValues;
    project.SetDouble(Id::kSsaoPower, 1.5);
    platform.SetDouble(Id::kSsaoIntensity, 2.25);
    CommandLine::CommandLine commandLine({
        "--rendering.ssao.enabled", "false",
        "--rendering.ssao.radius", "1.1",
        "--rendering.ssao.quality", "Low (4 samples)",
        "--rendering.ssao.blurEnabled", "true",
    });
    PopulateEngineCommandLineSettings(registry, commandLine, commandLineValues);
    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));

    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_FALSE(config.ssaoEnabled);
    EXPECT_FLOAT_EQ(config.ssaoRadius, 1.1F);
    EXPECT_FLOAT_EQ(config.ssaoIntensity, 2.25F);
    EXPECT_FLOAT_EQ(config.ssaoPower, 1.5F);
    EXPECT_EQ(config.ssaoQuality, 0U);
    EXPECT_TRUE(config.ssaoBlurEnabled);
}

TEST(EngineSettingsLayeringTest, ContactShadowControlsHaveDefaultsBoundsAndPlatformOverrides) {
    // Contact shadows landed as a pass in front of three project settings that already existed and
    // had never been read by anything. This pins the whole family the pass now consumes: the two new
    // knobs, the bounds the editor shows, and that a value reaches EngineConfig through the
    // ordinary layer order.
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    namespace Id = EngineProjectSettingId;
    const EngineConfig defaults{};

    Config::SettingsStack defaultsStack(registry);
    const auto enabled = defaultsStack.GetValue(Id::kContactShadowsEnabled);
    ASSERT_TRUE(enabled.has_value());
    EXPECT_FALSE(std::get<bool>(*enabled)) << "contact shadows must stay opt-in";
    const auto length = defaultsStack.GetValue(Id::kContactShadowsLength);
    ASSERT_TRUE(length.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*length), defaults.contactShadowsLength);
    const auto thickness = defaultsStack.GetValue(Id::kContactShadowsThickness);
    ASSERT_TRUE(thickness.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*thickness), defaults.contactShadowsThickness);
    const auto strength = defaultsStack.GetValue(Id::kContactShadowsStrength);
    ASSERT_TRUE(strength.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*strength), defaults.contactShadowsStrength);
    const auto steps = defaultsStack.GetValue(Id::kContactShadowsSteps);
    ASSERT_TRUE(steps.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*steps), defaults.contactShadowsStepCount);

    for (const std::string_view id :
         {Id::kContactShadowsEnabled, Id::kContactShadowsLength, Id::kContactShadowsSteps,
          Id::kContactShadowsThickness, Id::kContactShadowsStrength}) {
        const Config::SettingDescriptor* const descriptor = registry.Find(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagPerPlatform)) << id;
        EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagRestartRequired)) << id;
    }

    const Config::SettingDescriptor* const strengthDescriptor = registry.Find(Id::kContactShadowsStrength);
    ASSERT_NE(strengthDescriptor, nullptr);
    ASSERT_TRUE(strengthDescriptor->minimum.has_value());
    ASSERT_TRUE(strengthDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*strengthDescriptor->minimum, 0.0);
    EXPECT_DOUBLE_EQ(*strengthDescriptor->maximum, 1.0);

    const Config::SettingDescriptor* const thicknessDescriptor = registry.Find(Id::kContactShadowsThickness);
    ASSERT_NE(thicknessDescriptor, nullptr);
    ASSERT_TRUE(thicknessDescriptor->minimum.has_value());
    EXPECT_DOUBLE_EQ(*thicknessDescriptor->minimum, 0.0);

    // The upper bound is not a taste call: kContactShadowMaxStepCount caps the pass's fixed
    // sample loop, and a setting that offered more would be silently clamped at render time with no
    // visible reason. The constant lives in the RHI layer this suite does not link, so the number is
    // spelled here; Test/RHI/RenderSystems/contact_shadows_uve_tests.cpp pins the constant itself.
    const Config::SettingDescriptor* const stepsDescriptor = registry.Find(Id::kContactShadowsSteps);
    ASSERT_NE(stepsDescriptor, nullptr);
    ASSERT_TRUE(stepsDescriptor->minimum.has_value());
    ASSERT_TRUE(stepsDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*stepsDescriptor->minimum, 1.0);
    EXPECT_DOUBLE_EQ(*stepsDescriptor->maximum, 16.0);

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLineValues;
    platform.SetBool(Id::kContactShadowsEnabled, true);
    CommandLine::CommandLine commandLine({
        "--rendering.shadows.contactShadowsEnabled", "true",
        "--rendering.shadows.contactShadowsLength", "0.4",
        "--rendering.shadows.contactShadowsThickness", "0.05",
        "--rendering.shadows.contactShadowsStrength", "0.9",
        "--rendering.shadows.contactShadowsSteps", "12",
    });
    PopulateEngineCommandLineSettings(registry, commandLine, commandLineValues);
    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));

    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_TRUE(config.contactShadowsEnabled);
    EXPECT_DOUBLE_EQ(config.contactShadowsLength, 0.4);
    EXPECT_DOUBLE_EQ(config.contactShadowsThickness, 0.05);
    EXPECT_DOUBLE_EQ(config.contactShadowsStrength, 0.9);
    EXPECT_EQ(config.contactShadowsStepCount, 12);
}

TEST(EngineSettingsLayeringTest, FrustumCullingModeSettingDefaultsEnabledAndOverridesPerPlatform) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    namespace Id = EngineProjectSettingId;

    Config::SettingsStack defaults(registry);
    const auto mode = defaults.GetValue(Id::kFrustumCullingMode);
    ASSERT_TRUE(mode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*mode), static_cast<std::int64_t>(FrustumCullingMode::Enabled))
        << "culling must stay on unless someone deliberately turns it off";

    const Config::SettingDescriptor* const descriptor = registry.Find(Id::kFrustumCullingMode);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->type, Config::SettingType::Enum);
    EXPECT_EQ(descriptor->category, "Rendering/Culling");
    ASSERT_EQ(descriptor->enumEntries.size(), 2U);
    EXPECT_EQ(descriptor->enumEntries[0].label, "Enabled");
    EXPECT_EQ(descriptor->enumEntries[1].label, "Debug freeze (draw everything)");
    EXPECT_EQ(descriptor->enumEntries[0].value, static_cast<std::int64_t>(FrustumCullingMode::Enabled));
    EXPECT_EQ(descriptor->enumEntries[1].value, static_cast<std::int64_t>(FrustumCullingMode::DebugFreeze));
    EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagRestartRequired));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kFrustumCullingMode));

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLineValues;
    project.SetInt(Id::kFrustumCullingMode, static_cast<std::int64_t>(FrustumCullingMode::DebugFreeze));
    platform.SetInt(Id::kFrustumCullingMode, static_cast<std::int64_t>(FrustumCullingMode::Enabled));
    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));

    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.frustumCullingMode, FrustumCullingMode::Enabled);

    ASSERT_TRUE(platform.RemoveKey(Id::kFrustumCullingMode));
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.frustumCullingMode, FrustumCullingMode::DebugFreeze);

    // The debug flag is reachable from the command line like every other enum: by label.
    CommandLine::CommandLine commandLine({
        "--rendering.culling.frustumCulling", "Debug freeze (draw everything)",
    });
    PopulateEngineCommandLineSettings(registry, commandLine, commandLineValues);
    const auto cliMode = registry.GetStoredValue(commandLineValues, Id::kFrustumCullingMode);
    ASSERT_TRUE(cliMode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*cliMode), static_cast<std::int64_t>(FrustumCullingMode::DebugFreeze));
    Config::SettingsStack cliSettings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(cliSettings, project, user, platform, commandLineValues));
    EngineConfig cliConfig{};
    ApplyEngineSettings(cliSettings, cliConfig);
    EXPECT_EQ(cliConfig.frustumCullingMode, FrustumCullingMode::DebugFreeze);
}

TEST(EngineSettingsLayeringTest, GpuOcclusionCullingIsOffByDefaultAndReachesTheConfig) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));
    namespace Id = EngineProjectSettingId;

    Config::SettingsStack defaults(registry);
    const auto defaultValue = defaults.GetValue(Id::kGpuOcclusionCulling);
    ASSERT_TRUE(defaultValue.has_value());
    EXPECT_FALSE(std::get<bool>(*defaultValue))
        << "the query and proxy draw per candidate must be opted into, not paid for by default";

    const Config::SettingDescriptor* const descriptor = registry.Find(Id::kGpuOcclusionCulling);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->type, Config::SettingType::Bool);
    EXPECT_EQ(descriptor->category, "Rendering/Culling");
    EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagPerPlatform));
    EXPECT_TRUE(descriptor->HasFlag(Config::kSettingFlagRestartRequired));
    EXPECT_TRUE(IsEngineConfigSettingId(Id::kGpuOcclusionCulling));

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLineValues;
    project.SetBool(Id::kGpuOcclusionCulling, true);
    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));

    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_TRUE(config.gpuOcclusionCullingEnabled);

    platform.SetBool(Id::kGpuOcclusionCulling, false);
    ApplyEngineSettings(settings, config);
    EXPECT_FALSE(config.gpuOcclusionCullingEnabled) << "the platform layer outranks the project";
}

TEST(EngineSettingsLayeringTest, ConvertsTypedCommandLineFlagsAndUsesThemAsTopLayer) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));

    CommandLine::CommandLine commandLine({
        "--physics.common.ticksPerSecond", "120",
        "--physics.common.maxFrameTime", "0.25",
        "--physics.common.maxStepsPerFrame", "12",
        "--physics.3d.gravity", "1,-9.81,2",
        "--rendering.shadows.filter", "softer (5x5)",
        "--rendering.shadows.defaultCascadeSplitBlend", "0.25",
        "--rendering.shadows.defaultBias", "0.35",
        "--rendering.shadows.defaultNormalBias", "1.75",
        "--rendering.shaders.programCacheDirectory", "shader_cache/cli",
        "--rendering.shaders.compilationMode", "Synchronous on-demand",
        "--rendering.upscaling.method", "Nearest neighbour",
        "--rendering.postProcessing.toneMapper", "Linear clamp",
        "--rendering.postProcessing.sharpeningAmount", "0.75",
        "--rendering.postProcessing.ditheringEnabled", "false",
        "--rendering.antialiasing.postProcessMethod", "Fast approximate",
        "--rendering.antialiasing.screenSpaceQuality", "High (7 edge taps)",
        "--application.quitOnLastWindowClosed", "false",
        "--application.boot.backgroundColor", "0.1,0.2,0.3,0.4",
        "--application.boot.minimumDisplaySeconds", "2.5",
        "--application.boot.skippable", "false",
        "--application.crash.enabled", "false",
        "--headless",
        "--layers.physics.1", "CLI must not register project layer names",
    });
    ConfigManager commandLineValues;
    PopulateEngineCommandLineSettings(registry, commandLine, commandLineValues);

    namespace Id = EngineProjectSettingId;
    const auto ticksValue = registry.GetStoredValue(commandLineValues, Id::kPhysicsTicksPerSecond);
    ASSERT_TRUE(ticksValue.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*ticksValue), 120.0);

    const auto maxFrameTime = registry.GetStoredValue(commandLineValues, Id::kPhysicsMaxFrameTime);
    ASSERT_TRUE(maxFrameTime.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*maxFrameTime), 0.25);

    const auto maxSteps = registry.GetStoredValue(commandLineValues, Id::kPhysicsMaxStepsPerFrame);
    ASSERT_TRUE(maxSteps.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*maxSteps), 12);

    const auto gravity = registry.GetStoredValue(commandLineValues, Id::kPhysicsGravity);
    ASSERT_TRUE(gravity.has_value());
    EXPECT_EQ(std::get<Config::SettingVector3>(*gravity), (Config::SettingVector3{1.0, -9.81, 2.0}));

    const auto shadowFilter = registry.GetStoredValue(commandLineValues, Id::kShadowFilter);
    ASSERT_TRUE(shadowFilter.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*shadowFilter), 2);
    const auto shadowSplitBlend = registry.GetStoredValue(commandLineValues, Id::kShadowCascadeSplitBlend);
    ASSERT_TRUE(shadowSplitBlend.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowSplitBlend), 0.25);
    const auto shadowBiasDefault = registry.GetStoredValue(commandLineValues, Id::kShadowBiasDefault);
    ASSERT_TRUE(shadowBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowBiasDefault), 0.35);
    const auto shadowNormalBiasDefault =
        registry.GetStoredValue(commandLineValues, Id::kShadowNormalBiasDefault);
    ASSERT_TRUE(shadowNormalBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowNormalBiasDefault), 1.75);

    const auto shaderCachePath = registry.GetStoredValue(commandLineValues, Id::kShaderProgramCacheDirectory);
    ASSERT_TRUE(shaderCachePath.has_value());
    EXPECT_EQ(std::get<std::string>(*shaderCachePath), "shader_cache/cli");
    const auto shaderCompilationMode =
        registry.GetStoredValue(commandLineValues, Id::kShaderCompilationMode);
    ASSERT_TRUE(shaderCompilationMode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*shaderCompilationMode),
              static_cast<std::int64_t>(ShaderCompilationMode::SynchronousOnDemand));
    const auto upscalingMethod = registry.GetStoredValue(commandLineValues, Id::kSpatialUpscalingMethod);
    ASSERT_TRUE(upscalingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*upscalingMethod),
              static_cast<std::int64_t>(SpatialUpscalingMethod::Nearest));
    const auto toneMappingMethod = registry.GetStoredValue(commandLineValues, Id::kToneMappingMethod);
    ASSERT_TRUE(toneMappingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*toneMappingMethod),
              static_cast<std::int64_t>(ToneMappingMethod::LinearClamp));
    const auto sharpeningAmount = registry.GetStoredValue(commandLineValues, Id::kSharpeningAmount);
    ASSERT_TRUE(sharpeningAmount.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*sharpeningAmount), 0.75);
    const auto ditheringEnabled = registry.GetStoredValue(commandLineValues, Id::kDitheringEnabled);
    ASSERT_TRUE(ditheringEnabled.has_value());
    EXPECT_FALSE(std::get<bool>(*ditheringEnabled));
    const auto aaMethod = registry.GetStoredValue(commandLineValues, Id::kPostProcessAAMethod);
    ASSERT_TRUE(aaMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaMethod),
              static_cast<std::int64_t>(PostProcessAAMethod::FastApproximate));
    const auto aaQuality = registry.GetStoredValue(commandLineValues, Id::kScreenSpaceAAQuality);
    ASSERT_TRUE(aaQuality.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaQuality), 2);

    const auto headless = registry.GetStoredValue(commandLineValues, Id::kHeadless);
    ASSERT_TRUE(headless.has_value());
    EXPECT_TRUE(std::get<bool>(*headless));
    const auto quitOnLastWindowClosed =
        registry.GetStoredValue(commandLineValues, Id::kQuitOnLastWindowClosed);
    ASSERT_TRUE(quitOnLastWindowClosed.has_value());
    EXPECT_FALSE(std::get<bool>(*quitOnLastWindowClosed));
    const auto bootBackground = registry.GetStoredValue(commandLineValues, Id::kBootBackgroundColor);
    ASSERT_TRUE(bootBackground.has_value());
    EXPECT_EQ(std::get<Config::SettingColor>(*bootBackground),
              (Config::SettingColor{0.1F, 0.2F, 0.3F, 0.4F}));
    EXPECT_FALSE(registry.GetStoredValue(commandLineValues, "layers.physics.1").has_value());

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    project.SetDouble(Id::kPhysicsTicksPerSecond, 30.0);
    user.SetDouble(Id::kPhysicsTicksPerSecond, 60.0);
    platform.SetInt(Id::kShadowMapResolution, 1024);
    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));

    const auto resolution = settings.Resolve(Id::kPhysicsTicksPerSecond);
    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution->source, SettingValueSource::CommandLine);
    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_DOUBLE_EQ(config.fixedUpdateFps, 120.0);
    EXPECT_DOUBLE_EQ(config.maxDeltaTimeSeconds, 0.25);
    EXPECT_EQ(config.maxFixedStepsPerFrame, 12);
    EXPECT_FLOAT_EQ(config.gravity.x, 1.0F);
    EXPECT_FLOAT_EQ(config.gravity.y, -9.81F);
    EXPECT_FLOAT_EQ(config.gravity.z, 2.0F);
    EXPECT_EQ(config.shadowPcfKernelRadius, 2U);
    EXPECT_FLOAT_EQ(config.shadowCascadeSplitLambda, 0.25F);
    EXPECT_FLOAT_EQ(config.shadowBiasDefault, 0.35F);
    EXPECT_FLOAT_EQ(config.shadowNormalBiasDefault, 1.75F);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("shader_cache/cli"));
    EXPECT_EQ(config.shaderCompilationMode, ShaderCompilationMode::SynchronousOnDemand);
    EXPECT_EQ(config.spatialUpscalingMethod, SpatialUpscalingMethod::Nearest);
    EXPECT_EQ(config.toneMappingMethod, ToneMappingMethod::LinearClamp);
    EXPECT_FLOAT_EQ(config.sharpeningAmount, 0.75F);
    EXPECT_FALSE(config.ditheringEnabled);
    EXPECT_EQ(config.postProcessAAMethod, PostProcessAAMethod::FastApproximate);
    EXPECT_EQ(config.screenSpaceAAQuality, 2U);
    EXPECT_TRUE(config.headless);
    EXPECT_FALSE(config.quitOnLastWindowClosed);
    EXPECT_FLOAT_EQ(config.backgroundColor[0U], 0.1F);
    EXPECT_FLOAT_EQ(config.backgroundColor[1U], 0.2F);
    EXPECT_FLOAT_EQ(config.backgroundColor[2U], 0.3F);
    EXPECT_FLOAT_EQ(config.backgroundColor[3U], 0.4F);
    EXPECT_DOUBLE_EQ(config.splashMinimumDisplaySeconds, 2.5);
    EXPECT_FALSE(config.splashSkippable);
    EXPECT_FALSE(config.crashHandlerEnabled);
}

TEST(EngineSettingsLayeringTest, RejectsInvalidTypedCliValueAndPreservesPlatformFallback) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));

    CommandLine::CommandLine commandLine({"--rendering.shadows.mapResolution", "1536"});
    ConfigManager commandLineValues;
    PopulateEngineCommandLineSettings(registry, commandLine, commandLineValues);
    EXPECT_FALSE(registry.GetStoredValue(commandLineValues, EngineProjectSettingId::kShadowMapResolution)
                     .has_value());

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    project.SetInt(EngineProjectSettingId::kShadowMapResolution, 4096);
    user.SetInt(EngineProjectSettingId::kShadowMapResolution, 2048);
    platform.SetInt(EngineProjectSettingId::kShadowMapResolution, 1024);

    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));
    const auto resolution = settings.Resolve(EngineProjectSettingId::kShadowMapResolution);
    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution->source, SettingValueSource::Platform);
    EXPECT_EQ(std::get<std::int64_t>(resolution->value), 1024);

    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.shadowMapResolution, 1024U);
}

TEST(EngineSettingsLayeringTest, ResolvesPlatformSettingsPathAndHonorsExplicitOverride) {
    EngineConfig config{};
    config.settingsFilePath = std::filesystem::path{"User"} / "settings.uvsettings";

    const std::filesystem::path generatedPath = GetPlatformSettingsFilePath(config);
    EXPECT_EQ(generatedPath.filename(), config.settingsFilePath.filename());
    EXPECT_EQ(generatedPath.parent_path().parent_path().filename(), "platforms");
    EXPECT_EQ(generatedPath.parent_path().parent_path().parent_path(), config.settingsFilePath.parent_path());

    config.platformSettingsFilePath = std::filesystem::path{"Overrides"} / "desktop.uvsettings";
    EXPECT_EQ(GetPlatformSettingsFilePath(config), config.platformSettingsFilePath);
}

TEST(EngineSettingsLayeringTest,
     RegistersAndAppliesPart1PhysicsInputAudioAnimNavNetworkLocalisationLayersAndQualitySettings) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));

    EXPECT_EQ(GetLayerNameSettingId(LayerSet::Navigation, 5U), "layers.navigation.6");
    EXPECT_EQ(GetLayerNameSettingId(LayerSet::Avoidance, 31U), "layers.avoidance.32");
    EXPECT_NE(registry.Find("layers.navigation.6"), nullptr);
    EXPECT_NE(registry.Find("layers.avoidance.32"), nullptr);

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLineValues;
    ASSERT_TRUE(
        registry.SetValue(project, "physics.layers.defaultCollisionMask", std::int64_t{0x0FLL}));
    ASSERT_TRUE(registry.SetValue(project, "audio.output.sampleRate", std::int64_t{44100}));
    ASSERT_TRUE(registry.SetValue(project, "network.transport.defaultPort", std::int64_t{9000}));
    ASSERT_TRUE(registry.SetValue(platform, "quality.activePreset", std::int64_t{3}));

    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));

    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_EQ(config.defaultCollisionMask, 0x0FU);
    EXPECT_EQ(config.audioSampleRate, 44100);
    EXPECT_EQ(config.networkDefaultPort, 9000);
    EXPECT_EQ(config.activeQualityTier, 3);
}

TEST(EngineSettingsLayeringTest,
     RegistersAndAppliesExtendedRenderingAccessibilityAndTelemetrySettings) {
    Config::SettingsRegistry registry;
    ASSERT_TRUE(RegisterEngineProjectSettings(registry));

    ConfigManager project;
    ConfigManager user;
    ConfigManager platform;
    ConfigManager commandLineValues;
    ASSERT_TRUE(registry.SetValue(project, "rendering.hdr.enabled", true));
    ASSERT_TRUE(registry.SetValue(project, "rendering.antialiasing.msaa3dSamples", std::int64_t{4}));
    ASSERT_TRUE(registry.SetValue(project, "rendering.shadows.atlasSize", std::int64_t{8192}));
    ASSERT_TRUE(registry.SetValue(project, "rendering.gi.ssrEnabled", true));
    ASSERT_TRUE(registry.SetValue(project, "rendering.postProcessing.colorGradingLutAsset",
                                     std::string{"Content/LUTs/film.uvtex"}));
    ASSERT_TRUE(registry.SetValue(project, "rendering.textures.defaultAnisotropy", std::int64_t{16}));
    ASSERT_TRUE(registry.SetValue(project, "rendering.environment.defaultWorldEnvironmentAsset",
                                     std::string{"Content/Env/default.uvenv"}));
    ASSERT_TRUE(registry.SetValue(project, "physics.materials.defaultAsset",
                                     std::string{"Content/Physics/default.uvphysmat"}));
    ASSERT_TRUE(registry.SetValue(project, "accessibility.runtime.uiScale", 1.5));
    ASSERT_TRUE(registry.SetValue(project, "privacy.telemetry.crashReportPolicy", std::int64_t{2}));

    Config::SettingsStack settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayers(settings, project, user, platform, commandLineValues));

    EngineConfig config{};
    ApplyEngineSettings(settings, config);
    EXPECT_TRUE(config.hdrRenderingEnabled);
    EXPECT_EQ(config.msaa3dSamples, 4);
    EXPECT_EQ(config.shadowAtlasSize, 8192);
    EXPECT_TRUE(config.ssrEnabled);
    EXPECT_EQ(config.colorGradingLutAsset, "Content/LUTs/film.uvtex");
    EXPECT_EQ(config.defaultAnisotropyLevel, 16);
    EXPECT_EQ(config.defaultWorldEnvironmentAsset, "Content/Env/default.uvenv");
    EXPECT_EQ(config.defaultPhysicsMaterialAsset, "Content/Physics/default.uvphysmat");
    EXPECT_DOUBLE_EQ(config.runtimeUiScale, 1.5);
    EXPECT_EQ(config.crashReportSubmissionPolicy, 2);
}

} // namespace
} // namespace UVE::Core::Tests
