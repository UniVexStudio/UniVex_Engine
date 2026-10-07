// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/engine_project_settings_uve.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/commandline/command_line_uve.h"
#include "uve/config/config_manager_uve.h"

namespace UVE::Core::Tests {
namespace {

using Config::ConfigManagerUVE;
using Config::SettingValueSourceUVE;

TEST(EngineSettingsLayeringUVETest, AttachesLayersAndAppliesResolvedSourcesToEngineConfig) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    ConfigManagerUVE commandLine;
    Config::SettingsStackUVE settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(settings, project, user, platform, commandLine));

    namespace Id = EngineProjectSettingIdUVE;
    project.SetDoubleUVE(Id::kPhysicsTicksPerSecondUVE, 30.0);
    user.SetDoubleUVE(Id::kPhysicsTicksPerSecondUVE, 90.0);
    platform.SetDoubleUVE(Id::kPhysicsTicksPerSecondUVE, 240.0); // Not marked PerPlatform.

    project.SetIntUVE(Id::kShadowMapResolutionUVE, 4096);
    user.SetIntUVE(Id::kShadowMapResolutionUVE, 2048);
    platform.SetIntUVE(Id::kShadowMapResolutionUVE, 1024);
    commandLine.SetIntUVE(Id::kShadowMapResolutionUVE, 512);

    // NotPersisted settings only read from the command-line layer.
    project.SetBoolUVE(Id::kHeadlessUVE, true);
    user.SetBoolUVE(Id::kHeadlessUVE, true);
    platform.SetBoolUVE(Id::kHeadlessUVE, true);
    commandLine.SetBoolUVE(Id::kHeadlessUVE, true);
    project.SetBoolUVE(Id::kQuitOnLastWindowClosedUVE, true);
    user.SetBoolUVE(Id::kQuitOnLastWindowClosedUVE, false);
    commandLine.SetBoolUVE(Id::kQuitOnLastWindowClosedUVE, true);

    const auto ticks = settings.ResolveUVE(Id::kPhysicsTicksPerSecondUVE);
    ASSERT_TRUE(ticks.has_value());
    EXPECT_EQ(ticks->source, SettingValueSourceUVE::User);
    EXPECT_DOUBLE_EQ(std::get<double>(ticks->value), 90.0);
    EXPECT_FALSE(settings.GetStoredValueUVE(SettingValueSourceUVE::Platform, Id::kPhysicsTicksPerSecondUVE)
                     .has_value());

    const auto shadowResolution = settings.ResolveUVE(Id::kShadowMapResolutionUVE);
    ASSERT_TRUE(shadowResolution.has_value());
    EXPECT_EQ(shadowResolution->source, SettingValueSourceUVE::CommandLine);
    EXPECT_EQ(std::get<std::int64_t>(shadowResolution->value), 512);

    const auto headless = settings.ResolveUVE(Id::kHeadlessUVE);
    ASSERT_TRUE(headless.has_value());
    EXPECT_EQ(headless->source, SettingValueSourceUVE::CommandLine);

    EngineConfigUVE config{};
    config.autoSaveIntervalSecondsUVE = 777.0; // No layer supplies this setting; preserve the caller's base.
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_DOUBLE_EQ(config.fixedUpdateFps, 90.0);
    EXPECT_EQ(config.shadowMapResolution, 512U);
    EXPECT_TRUE(config.headlessUVE);
    EXPECT_TRUE(config.quitOnLastWindowClosedUVE);
    EXPECT_DOUBLE_EQ(config.autoSaveIntervalSecondsUVE, 777.0);

    // An invalid command-line enum value must not mask the valid platform value below it.
    commandLine.SetIntUVE(Id::kShadowMapResolutionUVE, 1536);
    const auto fallback = settings.ResolveUVE(Id::kShadowMapResolutionUVE);
    ASSERT_TRUE(fallback.has_value());
    EXPECT_EQ(fallback->source, SettingValueSourceUVE::Platform);
    EXPECT_EQ(std::get<std::int64_t>(fallback->value), 1024);
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.shadowMapResolution, 1024U);
}

TEST(EngineSettingsLayeringUVETest, WindowSettingsExposeExpectedPortableDefaults) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));
    Config::SettingsStackUVE settings(registry);
    namespace Id = EngineProjectSettingIdUVE;

    const auto rendererBackend = settings.GetValueUVE(Id::kRenderBackendPreferenceUVE);
    ASSERT_TRUE(rendererBackend.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*rendererBackend),
              static_cast<std::int64_t>(RenderBackendPreferenceUVE::AutoUVE));
    const auto renderResolutionScale = settings.GetValueUVE(Id::kRenderResolutionScaleUVE);
    ASSERT_TRUE(renderResolutionScale.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*renderResolutionScale), 1.0);
    const Config::SettingDescriptorUVE* const renderScaleDescriptor =
        registry.FindUVE(Id::kRenderResolutionScaleUVE);
    ASSERT_NE(renderScaleDescriptor, nullptr);
    EXPECT_TRUE(renderScaleDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(renderScaleDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    ASSERT_TRUE(renderScaleDescriptor->minimum.has_value());
    ASSERT_TRUE(renderScaleDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*renderScaleDescriptor->minimum, 0.5);
    EXPECT_DOUBLE_EQ(*renderScaleDescriptor->maximum, 1.0);
    const auto dynamicResolution = settings.GetValueUVE(Id::kDynamicRenderResolutionEnabledUVE);
    ASSERT_TRUE(dynamicResolution.has_value());
    EXPECT_FALSE(std::get<bool>(*dynamicResolution));
    const auto minimumRenderScale = settings.GetValueUVE(Id::kMinimumRenderResolutionScaleUVE);
    ASSERT_TRUE(minimumRenderScale.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*minimumRenderScale), 0.5);
    const auto targetRenderFrameTime =
        settings.GetValueUVE(Id::kRenderResolutionTargetFrameTimeMillisecondsUVE);
    ASSERT_TRUE(targetRenderFrameTime.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*targetRenderFrameTime), 16.6667);
    const auto shaderCacheDirectory = settings.GetValueUVE(Id::kShaderProgramCacheDirectoryUVE);
    ASSERT_TRUE(shaderCacheDirectory.has_value());
    EXPECT_EQ(std::get<std::string>(*shaderCacheDirectory), "shader_cache/");
    const auto shaderCompilationMode = settings.GetValueUVE(Id::kShaderCompilationModeUVE);
    ASSERT_TRUE(shaderCompilationMode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*shaderCompilationMode),
              static_cast<std::int64_t>(ShaderCompilationModeUVE::AsynchronousOnDemandUVE));
    const Config::SettingDescriptorUVE* const shaderCompilationDescriptor =
        registry.FindUVE(Id::kShaderCompilationModeUVE);
    ASSERT_NE(shaderCompilationDescriptor, nullptr);
    EXPECT_EQ(shaderCompilationDescriptor->type, Config::SettingTypeUVE::Enum);
    ASSERT_EQ(shaderCompilationDescriptor->enumEntries.size(), 2U);
    EXPECT_EQ(shaderCompilationDescriptor->enumEntries[0U].value,
              static_cast<std::int64_t>(ShaderCompilationModeUVE::SynchronousOnDemandUVE));
    EXPECT_EQ(shaderCompilationDescriptor->enumEntries[1U].value,
              static_cast<std::int64_t>(ShaderCompilationModeUVE::AsynchronousOnDemandUVE));
    EXPECT_TRUE(shaderCompilationDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(shaderCompilationDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    const auto vulkanValidation = settings.GetValueUVE(Id::kVulkanValidationLayersEnabledUVE);
    ASSERT_TRUE(vulkanValidation.has_value());
    EXPECT_EQ(std::get<bool>(*vulkanValidation), EngineConfigUVE{}.vulkanValidationLayersEnabledUVE);
    const Config::SettingDescriptorUVE* const vulkanValidationDescriptor =
        registry.FindUVE(Id::kVulkanValidationLayersEnabledUVE);
    ASSERT_NE(vulkanValidationDescriptor, nullptr);
    EXPECT_EQ(vulkanValidationDescriptor->type, Config::SettingTypeUVE::Bool);
    EXPECT_TRUE(vulkanValidationDescriptor->HasFlagUVE(Config::kSettingFlagAdvancedUVE));
    EXPECT_TRUE(vulkanValidationDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(vulkanValidationDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    const auto upscalingMethod = settings.GetValueUVE(Id::kSpatialUpscalingMethodUVE);
    ASSERT_TRUE(upscalingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*upscalingMethod),
              static_cast<std::int64_t>(SpatialUpscalingMethodUVE::BilinearUVE));
    const auto toneMappingMethod = settings.GetValueUVE(Id::kToneMappingMethodUVE);
    ASSERT_TRUE(toneMappingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*toneMappingMethod), static_cast<std::int64_t>(ToneMappingMethodUVE::AcesUVE));
    const auto sharpeningAmount = settings.GetValueUVE(Id::kSharpeningAmountUVE);
    ASSERT_TRUE(sharpeningAmount.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*sharpeningAmount), 0.0);
    const Config::SettingDescriptorUVE* const sharpeningDescriptor = registry.FindUVE(Id::kSharpeningAmountUVE);
    ASSERT_NE(sharpeningDescriptor, nullptr);
    EXPECT_EQ(sharpeningDescriptor->type, Config::SettingTypeUVE::Float);
    ASSERT_TRUE(sharpeningDescriptor->minimum.has_value());
    ASSERT_TRUE(sharpeningDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*sharpeningDescriptor->minimum, 0.0);
    EXPECT_DOUBLE_EQ(*sharpeningDescriptor->maximum, 1.0);
    EXPECT_TRUE(sharpeningDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(sharpeningDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    const auto ditheringEnabled = settings.GetValueUVE(Id::kDitheringEnabledUVE);
    ASSERT_TRUE(ditheringEnabled.has_value());
    EXPECT_EQ(std::get<bool>(*ditheringEnabled), EngineConfigUVE{}.ditheringEnabledUVE);
    const Config::SettingDescriptorUVE* const ditheringDescriptor = registry.FindUVE(Id::kDitheringEnabledUVE);
    ASSERT_NE(ditheringDescriptor, nullptr);
    EXPECT_EQ(ditheringDescriptor->type, Config::SettingTypeUVE::Bool);
    EXPECT_TRUE(ditheringDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(ditheringDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    const auto aaMethod = settings.GetValueUVE(Id::kPostProcessAAMethodUVE);
    ASSERT_TRUE(aaMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaMethod), static_cast<std::int64_t>(PostProcessAAMethodUVE::NoneUVE));
    const Config::SettingDescriptorUVE* const aaMethodDescriptor = registry.FindUVE(Id::kPostProcessAAMethodUVE);
    ASSERT_NE(aaMethodDescriptor, nullptr);
    EXPECT_EQ(aaMethodDescriptor->type, Config::SettingTypeUVE::Enum);
    ASSERT_EQ(aaMethodDescriptor->enumEntries.size(), 2U);
    EXPECT_TRUE(aaMethodDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(aaMethodDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    const auto aaQuality = settings.GetValueUVE(Id::kScreenSpaceAAQualityUVE);
    ASSERT_TRUE(aaQuality.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaQuality), 1);
    const auto shadowBiasDefault = settings.GetValueUVE(Id::kShadowBiasDefaultUVE);
    ASSERT_TRUE(shadowBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowBiasDefault),
                     static_cast<double>(EngineConfigUVE{}.shadowBiasDefaultUVE));
    const auto shadowNormalBiasDefault = settings.GetValueUVE(Id::kShadowNormalBiasDefaultUVE);
    ASSERT_TRUE(shadowNormalBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowNormalBiasDefault),
                     static_cast<double>(EngineConfigUVE{}.shadowNormalBiasDefaultUVE));
    const auto shadowCascadeSplitBlend = settings.GetValueUVE(Id::kShadowCascadeSplitBlendUVE);
    ASSERT_TRUE(shadowCascadeSplitBlend.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowCascadeSplitBlend),
                     static_cast<double>(EngineConfigUVE{}.shadowCascadeSplitLambda));
    const Config::SettingDescriptorUVE* const shadowCascadeSplitBlendDescriptor =
        registry.FindUVE(Id::kShadowCascadeSplitBlendUVE);
    ASSERT_NE(shadowCascadeSplitBlendDescriptor, nullptr);
    EXPECT_EQ(shadowCascadeSplitBlendDescriptor->type, Config::SettingTypeUVE::Float);
    EXPECT_TRUE(shadowCascadeSplitBlendDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(shadowCascadeSplitBlendDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    for (const std::string_view id : {Id::kShadowBiasDefaultUVE, Id::kShadowNormalBiasDefaultUVE}) {
        const Config::SettingDescriptorUVE* const biasDescriptor = registry.FindUVE(id);
        ASSERT_NE(biasDescriptor, nullptr);
        EXPECT_EQ(biasDescriptor->type, Config::SettingTypeUVE::Float);
        ASSERT_TRUE(biasDescriptor->minimum.has_value());
        ASSERT_TRUE(biasDescriptor->maximum.has_value());
        EXPECT_DOUBLE_EQ(*biasDescriptor->minimum, 0.0);
        EXPECT_DOUBLE_EQ(*biasDescriptor->maximum, 10.0);
        EXPECT_TRUE(biasDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
        EXPECT_TRUE(biasDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    }
    const Config::SettingDescriptorUVE* const aaQualityDescriptor = registry.FindUVE(Id::kScreenSpaceAAQualityUVE);
    ASSERT_NE(aaQualityDescriptor, nullptr);
    ASSERT_EQ(aaQualityDescriptor->enumEntries.size(), 3U);
    const Config::SettingDescriptorUVE* const shaderCacheDescriptor =
        registry.FindUVE(Id::kShaderProgramCacheDirectoryUVE);
    ASSERT_NE(shaderCacheDescriptor, nullptr);
    EXPECT_EQ(shaderCacheDescriptor->type, Config::SettingTypeUVE::FilePath);
    EXPECT_EQ(shaderCacheDescriptor->maxLength, 512U);
    EXPECT_TRUE(shaderCacheDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(shaderCacheDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    const Config::SettingDescriptorUVE* const targetFrameTimeDescriptor =
        registry.FindUVE(Id::kRenderResolutionTargetFrameTimeMillisecondsUVE);
    ASSERT_NE(targetFrameTimeDescriptor, nullptr);
    ASSERT_TRUE(targetFrameTimeDescriptor->minimum.has_value());
    ASSERT_TRUE(targetFrameTimeDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*targetFrameTimeDescriptor->minimum,
                     kMinimumRenderResolutionTargetFrameTimeMillisecondsUVE);
    EXPECT_DOUBLE_EQ(*targetFrameTimeDescriptor->maximum,
                     kMaximumRenderResolutionTargetFrameTimeMillisecondsUVE);
    const Config::SettingDescriptorUVE* const dynamicResolutionDescriptor =
        registry.FindUVE(Id::kDynamicRenderResolutionEnabledUVE);
    ASSERT_NE(dynamicResolutionDescriptor, nullptr);
    EXPECT_TRUE(dynamicResolutionDescriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(dynamicResolutionDescriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    const auto defaultClearColor = settings.GetValueUVE(Id::kBootBackgroundColorUVE);
    ASSERT_TRUE(defaultClearColor.has_value());
    EXPECT_EQ(std::get<Config::SettingColorUVE>(*defaultClearColor),
              (Config::SettingColorUVE{0.05F, 0.05F, 0.05F, 1.0F}));
    const auto width = settings.GetValueUVE(Id::kWindowWidthUVE);
    ASSERT_TRUE(width.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*width), 1280);
    const auto height = settings.GetValueUVE(Id::kWindowHeightUVE);
    ASSERT_TRUE(height.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*height), 720);
    const auto mode = settings.GetValueUVE(Id::kWindowModeUVE);
    ASSERT_TRUE(mode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*mode),
              static_cast<std::int64_t>(Platform::WindowModeUVE::Windowed));
    const auto vsync = settings.GetValueUVE(Id::kWindowVSyncModeUVE);
    ASSERT_TRUE(vsync.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*vsync), static_cast<std::int64_t>(Platform::VSyncModeUVE::On));
    const auto cursorVisible = settings.GetValueUVE(Id::kWindowCursorVisibleUVE);
    ASSERT_TRUE(cursorVisible.has_value());
    EXPECT_TRUE(std::get<bool>(*cursorVisible));
    const auto cursorConfined = settings.GetValueUVE(Id::kWindowCursorConfinedUVE);
    ASSERT_TRUE(cursorConfined.has_value());
    EXPECT_FALSE(std::get<bool>(*cursorConfined));
    const auto allowDisplaySleep = settings.GetValueUVE(Id::kWindowAllowDisplaySleepUVE);
    ASSERT_TRUE(allowDisplaySleep.has_value());
    EXPECT_TRUE(std::get<bool>(*allowDisplaySleep));
    const auto allowedOrientations = settings.GetValueUVE(Id::kWindowAllowedOrientationsUVE);
    ASSERT_TRUE(allowedOrientations.has_value());
    EXPECT_EQ(std::get<Config::SettingStringListUVE>(*allowedOrientations),
              (Config::SettingStringListUVE{"landscape", "portrait"}));
}

TEST(EngineSettingsLayeringUVETest, WindowSettingsApplyTypedPoliciesAndRespectPlatformOverrides) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));
    namespace Id = EngineProjectSettingIdUVE;

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    ConfigManagerUVE commandLine;
    Config::SettingsStackUVE settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(settings, project, user, platform, commandLine));

    project.SetIntUVE(Id::kWindowWidthUVE, 1920);
    project.SetIntUVE(Id::kWindowHeightUVE, 1080);
    project.SetIntUVE(Id::kWindowModeUVE, 3);
    project.SetIntUVE(Id::kWindowVSyncModeUVE, 2);
    project.SetIntUVE(Id::kWindowMaximumWidthUVE, 2560);
    project.SetIntUVE(Id::kRenderBackendPreferenceUVE,
                      static_cast<std::int64_t>(RenderBackendPreferenceUVE::OpenGLUVE));
    platform.SetIntUVE(Id::kRenderBackendPreferenceUVE,
                       static_cast<std::int64_t>(RenderBackendPreferenceUVE::VulkanUVE));
    project.SetDoubleUVE(Id::kRenderResolutionScaleUVE, 0.9);
    platform.SetDoubleUVE(Id::kRenderResolutionScaleUVE, 0.75);
    project.SetBoolUVE(Id::kDynamicRenderResolutionEnabledUVE, true);
    project.SetDoubleUVE(Id::kMinimumRenderResolutionScaleUVE, 0.65);
    platform.SetDoubleUVE(Id::kMinimumRenderResolutionScaleUVE, 0.6);
    project.SetDoubleUVE(Id::kRenderResolutionTargetFrameTimeMillisecondsUVE, 33.3333);
    platform.SetDoubleUVE(Id::kRenderResolutionTargetFrameTimeMillisecondsUVE, 20.0);
    project.SetStringUVE(Id::kShaderProgramCacheDirectoryUVE, "shader_cache/project");
    platform.SetStringUVE(Id::kShaderProgramCacheDirectoryUVE, "shader_cache/platform");
    project.SetIntUVE(Id::kShaderCompilationModeUVE,
                      static_cast<std::int64_t>(ShaderCompilationModeUVE::SynchronousOnDemandUVE));
    platform.SetIntUVE(Id::kShaderCompilationModeUVE,
                       static_cast<std::int64_t>(ShaderCompilationModeUVE::AsynchronousOnDemandUVE));
    project.SetBoolUVE(Id::kVulkanValidationLayersEnabledUVE, true);
    platform.SetBoolUVE(Id::kVulkanValidationLayersEnabledUVE, false);
    project.SetIntUVE(Id::kSpatialUpscalingMethodUVE,
                      static_cast<std::int64_t>(SpatialUpscalingMethodUVE::NearestUVE));
    platform.SetIntUVE(Id::kSpatialUpscalingMethodUVE,
                       static_cast<std::int64_t>(SpatialUpscalingMethodUVE::BilinearUVE));
    project.SetIntUVE(Id::kToneMappingMethodUVE,
                      static_cast<std::int64_t>(ToneMappingMethodUVE::LinearClampUVE));
    platform.SetIntUVE(Id::kToneMappingMethodUVE,
                       static_cast<std::int64_t>(ToneMappingMethodUVE::AcesUVE));
    project.SetDoubleUVE(Id::kSharpeningAmountUVE, 0.25);
    platform.SetDoubleUVE(Id::kSharpeningAmountUVE, 0.5);
    project.SetBoolUVE(Id::kDitheringEnabledUVE, false);
    platform.SetBoolUVE(Id::kDitheringEnabledUVE, true);
    project.SetIntUVE(Id::kPostProcessAAMethodUVE,
                      static_cast<std::int64_t>(PostProcessAAMethodUVE::FastApproximateUVE));
    platform.SetIntUVE(Id::kPostProcessAAMethodUVE,
                       static_cast<std::int64_t>(PostProcessAAMethodUVE::NoneUVE));
    project.SetIntUVE(Id::kScreenSpaceAAQualityUVE, 2);
    platform.SetIntUVE(Id::kScreenSpaceAAQualityUVE, 1);
    project.SetDoubleUVE(Id::kShadowCascadeSplitBlendUVE, 0.25);
    platform.SetDoubleUVE(Id::kShadowCascadeSplitBlendUVE, 0.75);
    project.SetDoubleUVE(Id::kShadowBiasDefaultUVE, 0.2);
    platform.SetDoubleUVE(Id::kShadowBiasDefaultUVE, 0.4);
    project.SetDoubleUVE(Id::kShadowNormalBiasDefaultUVE, 1.5);
    platform.SetDoubleUVE(Id::kShadowNormalBiasDefaultUVE, 0.5);
    project.SetDoubleUVE(Id::kWindowContentScaleOverrideUVE, 1.5);
    project.SetIntUVE(Id::kWindowFocusedFrameRateCapUVE, 144);
    project.SetIntUVE(Id::kWindowUnfocusedFrameRateCapUVE, 30);
    project.SetStringUVE(Id::kWindowTitleFormatUVE, "{projectName} — {sceneName}");
    project.SetBoolUVE(Id::kWindowAppendSceneInEditorPlayUVE, true);
    ASSERT_TRUE(registry.SetValueUVE(
        project, Id::kWindowAllowedOrientationsUVE,
        Config::SettingValueUVE{Config::SettingStringListUVE{"landscapeLeft", "portrait"}}));
    platform.SetBoolUVE(Id::kWindowPerMonitorScalingUVE, false);
    user.SetBoolUVE(Id::kWindowAlwaysOnTopUVE, true);

    EngineConfigUVE config;
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.windowWidth, 1920U);
    EXPECT_EQ(config.windowHeight, 1080U);
    EXPECT_EQ(config.windowModeUVE, Platform::WindowModeUVE::ExclusiveFullscreen);
    EXPECT_EQ(config.vsyncModeUVE, Platform::VSyncModeUVE::Adaptive);
    EXPECT_TRUE(config.vsyncModeExplicitUVE);
    EXPECT_TRUE(config.vsyncEnabledUVE);
    EXPECT_EQ(config.windowMaximumWidthUVE, 2560U);
    EXPECT_EQ(config.renderBackendPreferenceUVE, RenderBackendPreferenceUVE::VulkanUVE);
    EXPECT_DOUBLE_EQ(config.renderResolutionScaleUVE, 0.75);
    EXPECT_TRUE(config.dynamicRenderResolutionEnabledUVE);
    EXPECT_DOUBLE_EQ(config.minimumRenderResolutionScaleUVE, 0.6);
    EXPECT_DOUBLE_EQ(config.renderResolutionTargetFrameTimeMillisecondsUVE, 20.0);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("shader_cache/platform"));
    EXPECT_EQ(config.shaderCompilationModeUVE, ShaderCompilationModeUVE::AsynchronousOnDemandUVE);
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kShaderCompilationModeUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.shaderCompilationModeUVE, ShaderCompilationModeUVE::SynchronousOnDemandUVE);
    EXPECT_FALSE(config.vulkanValidationLayersEnabledUVE);
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kVulkanValidationLayersEnabledUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_TRUE(config.vulkanValidationLayersEnabledUVE);
    EXPECT_EQ(config.spatialUpscalingMethodUVE, SpatialUpscalingMethodUVE::BilinearUVE);
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kSpatialUpscalingMethodUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.spatialUpscalingMethodUVE, SpatialUpscalingMethodUVE::NearestUVE);
    EXPECT_EQ(config.toneMappingMethodUVE, ToneMappingMethodUVE::AcesUVE);
    EXPECT_FLOAT_EQ(config.sharpeningAmountUVE, 0.5F);
    EXPECT_TRUE(config.ditheringEnabledUVE);
    EXPECT_FLOAT_EQ(config.shadowCascadeSplitLambda, 0.75F);
    EXPECT_FLOAT_EQ(config.shadowBiasDefaultUVE, 0.4F);
    EXPECT_FLOAT_EQ(config.shadowNormalBiasDefaultUVE, 0.5F);
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kShadowCascadeSplitBlendUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_FLOAT_EQ(config.shadowCascadeSplitLambda, 0.25F);
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kShadowBiasDefaultUVE));
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kShadowNormalBiasDefaultUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_FLOAT_EQ(config.shadowBiasDefaultUVE, 0.2F);
    EXPECT_FLOAT_EQ(config.shadowNormalBiasDefaultUVE, 1.5F);
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kToneMappingMethodUVE));
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kSharpeningAmountUVE));
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kDitheringEnabledUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.toneMappingMethodUVE, ToneMappingMethodUVE::LinearClampUVE);
    EXPECT_FLOAT_EQ(config.sharpeningAmountUVE, 0.25F);
    EXPECT_FALSE(config.ditheringEnabledUVE);
    EXPECT_EQ(config.postProcessAAMethodUVE, PostProcessAAMethodUVE::NoneUVE);
    EXPECT_EQ(config.screenSpaceAAQualityUVE, 1U);
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kPostProcessAAMethodUVE));
    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kScreenSpaceAAQualityUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.postProcessAAMethodUVE, PostProcessAAMethodUVE::FastApproximateUVE);
    EXPECT_EQ(config.screenSpaceAAQualityUVE, 2U);
    EXPECT_DOUBLE_EQ(config.contentScaleOverrideUVE, 1.5);
    EXPECT_FALSE(config.perMonitorScalingUVE);
    EXPECT_TRUE(config.windowAlwaysOnTopUVE);
    EXPECT_EQ(config.focusedFrameRateCapUVE, 144U);
    EXPECT_EQ(config.unfocusedFrameRateCapUVE, 30U);
    EXPECT_EQ(config.allowedOrientationsUVE,
              (std::vector<Platform::DisplayOrientationUVE>{Platform::DisplayOrientationUVE::LandscapeLeft,
                                                           Platform::DisplayOrientationUVE::Portrait}));
    EXPECT_EQ(config.windowTitleFormatUVE, "{projectName} — {sceneName}");
    EXPECT_TRUE(config.appendSceneNameInEditorPlayModeUVE);
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kRenderBackendPreferenceUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kRenderResolutionScaleUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kDynamicRenderResolutionEnabledUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kMinimumRenderResolutionScaleUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kRenderResolutionTargetFrameTimeMillisecondsUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kShaderProgramCacheDirectoryUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kShaderCompilationModeUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kVulkanValidationLayersEnabledUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kSpatialUpscalingMethodUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kToneMappingMethodUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kSharpeningAmountUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kDitheringEnabledUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kShadowCascadeSplitBlendUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kShadowBiasDefaultUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kShadowNormalBiasDefaultUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kPostProcessAAMethodUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kScreenSpaceAAQualityUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kWindowModeUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kWindowCursorImageUVE));
}

TEST(EngineSettingsLayeringUVETest, UnsafeShaderCacheOverrideFallsThroughToSafeLowerLayer) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));
    namespace Id = EngineProjectSettingIdUVE;

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    ConfigManagerUVE commandLine;
    project.SetStringUVE(Id::kShaderProgramCacheDirectoryUVE, "shader_cache/project");
    platform.SetStringUVE(Id::kShaderProgramCacheDirectoryUVE, "shader_cache/platform");
    commandLine.SetStringUVE(Id::kShaderProgramCacheDirectoryUVE, "../outside_cache");

    Config::SettingsStackUVE settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(settings, project, user, platform, commandLine));
    EngineConfigUVE config{};
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("shader_cache/platform"));

    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kShaderProgramCacheDirectoryUVE));
    ASSERT_TRUE(project.RemoveKeyUVE(Id::kShaderProgramCacheDirectoryUVE));
    config.shaderCachePath = "application_cache";
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("application_cache"));
}

TEST(EngineSettingsLayeringUVETest, SSAOControlsHaveDefaultsBoundsAndPlatformOverrides) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));
    Config::SettingsStackUVE defaults(registry);
    namespace Id = EngineProjectSettingIdUVE;

    const auto enabled = defaults.GetValueUVE(Id::kSsaoEnabledUVE);
    ASSERT_TRUE(enabled.has_value());
    EXPECT_TRUE(std::get<bool>(*enabled));
    const auto radius = defaults.GetValueUVE(Id::kSsaoRadiusUVE);
    ASSERT_TRUE(radius.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*radius), 0.5);
    const auto intensity = defaults.GetValueUVE(Id::kSsaoIntensityUVE);
    ASSERT_TRUE(intensity.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*intensity), 1.0);
    const auto power = defaults.GetValueUVE(Id::kSsaoPowerUVE);
    ASSERT_TRUE(power.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*power), 1.0);
    const auto quality = defaults.GetValueUVE(Id::kSsaoQualityUVE);
    ASSERT_TRUE(quality.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*quality), 2);
    const auto blur = defaults.GetValueUVE(Id::kSsaoBlurEnabledUVE);
    ASSERT_TRUE(blur.has_value());
    EXPECT_FALSE(std::get<bool>(*blur));

    for (const std::string_view id : {Id::kSsaoEnabledUVE, Id::kSsaoRadiusUVE, Id::kSsaoIntensityUVE,
                                      Id::kSsaoPowerUVE, Id::kSsaoQualityUVE, Id::kSsaoBlurEnabledUVE}) {
        const Config::SettingDescriptorUVE* const descriptor = registry.FindUVE(id);
        ASSERT_NE(descriptor, nullptr) << id;
        EXPECT_TRUE(descriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE)) << id;
        EXPECT_TRUE(descriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE)) << id;
    }
    const Config::SettingDescriptorUVE* const radiusDescriptor = registry.FindUVE(Id::kSsaoRadiusUVE);
    ASSERT_NE(radiusDescriptor, nullptr);
    ASSERT_TRUE(radiusDescriptor->minimum.has_value());
    ASSERT_TRUE(radiusDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*radiusDescriptor->minimum, 0.01);
    EXPECT_DOUBLE_EQ(*radiusDescriptor->maximum, 10.0);
    const Config::SettingDescriptorUVE* const intensityDescriptor = registry.FindUVE(Id::kSsaoIntensityUVE);
    ASSERT_NE(intensityDescriptor, nullptr);
    ASSERT_TRUE(intensityDescriptor->minimum.has_value());
    ASSERT_TRUE(intensityDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*intensityDescriptor->minimum, 0.0);
    EXPECT_DOUBLE_EQ(*intensityDescriptor->maximum, 4.0);
    const Config::SettingDescriptorUVE* const powerDescriptor = registry.FindUVE(Id::kSsaoPowerUVE);
    ASSERT_NE(powerDescriptor, nullptr);
    ASSERT_TRUE(powerDescriptor->minimum.has_value());
    ASSERT_TRUE(powerDescriptor->maximum.has_value());
    EXPECT_DOUBLE_EQ(*powerDescriptor->minimum, 0.1);
    EXPECT_DOUBLE_EQ(*powerDescriptor->maximum, 4.0);
    const Config::SettingDescriptorUVE* const qualityDescriptor = registry.FindUVE(Id::kSsaoQualityUVE);
    ASSERT_NE(qualityDescriptor, nullptr);
    ASSERT_EQ(qualityDescriptor->enumEntries.size(), 3U);
    EXPECT_EQ(qualityDescriptor->enumEntries[0].label, "Low (4 samples)");
    EXPECT_EQ(qualityDescriptor->enumEntries[1].label, "Medium (8 samples)");
    EXPECT_EQ(qualityDescriptor->enumEntries[2].label, "High (12 samples)");

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    ConfigManagerUVE commandLineValues;
    project.SetDoubleUVE(Id::kSsaoPowerUVE, 1.5);
    platform.SetDoubleUVE(Id::kSsaoIntensityUVE, 2.25);
    CommandLine::CommandLineUVE commandLine({
        "--rendering.ssao.enabled", "false",
        "--rendering.ssao.radius", "1.1",
        "--rendering.ssao.quality", "Low (4 samples)",
        "--rendering.ssao.blurEnabled", "true",
    });
    PopulateEngineCommandLineSettingsUVE(registry, commandLine, commandLineValues);
    Config::SettingsStackUVE settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(settings, project, user, platform, commandLineValues));

    EngineConfigUVE config{};
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_FALSE(config.ssaoEnabledUVE);
    EXPECT_FLOAT_EQ(config.ssaoRadiusUVE, 1.1F);
    EXPECT_FLOAT_EQ(config.ssaoIntensityUVE, 2.25F);
    EXPECT_FLOAT_EQ(config.ssaoPowerUVE, 1.5F);
    EXPECT_EQ(config.ssaoQualityUVE, 0U);
    EXPECT_TRUE(config.ssaoBlurEnabledUVE);
}

TEST(EngineSettingsLayeringUVETest, FrustumCullingModeSettingDefaultsEnabledAndOverridesPerPlatform) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));
    namespace Id = EngineProjectSettingIdUVE;

    Config::SettingsStackUVE defaults(registry);
    const auto mode = defaults.GetValueUVE(Id::kFrustumCullingModeUVE);
    ASSERT_TRUE(mode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*mode), static_cast<std::int64_t>(FrustumCullingModeUVE::EnabledUVE))
        << "culling must stay on unless someone deliberately turns it off";

    const Config::SettingDescriptorUVE* const descriptor = registry.FindUVE(Id::kFrustumCullingModeUVE);
    ASSERT_NE(descriptor, nullptr);
    EXPECT_EQ(descriptor->type, Config::SettingTypeUVE::Enum);
    EXPECT_EQ(descriptor->category, "Rendering/Culling");
    ASSERT_EQ(descriptor->enumEntries.size(), 2U);
    EXPECT_EQ(descriptor->enumEntries[0].label, "Enabled");
    EXPECT_EQ(descriptor->enumEntries[1].label, "Debug freeze (draw everything)");
    EXPECT_EQ(descriptor->enumEntries[0].value, static_cast<std::int64_t>(FrustumCullingModeUVE::EnabledUVE));
    EXPECT_EQ(descriptor->enumEntries[1].value, static_cast<std::int64_t>(FrustumCullingModeUVE::DebugFreezeUVE));
    EXPECT_TRUE(descriptor->HasFlagUVE(Config::kSettingFlagPerPlatformUVE));
    EXPECT_TRUE(descriptor->HasFlagUVE(Config::kSettingFlagRestartRequiredUVE));
    EXPECT_TRUE(IsEngineConfigSettingIdUVE(Id::kFrustumCullingModeUVE));

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    ConfigManagerUVE commandLineValues;
    project.SetIntUVE(Id::kFrustumCullingModeUVE, static_cast<std::int64_t>(FrustumCullingModeUVE::DebugFreezeUVE));
    platform.SetIntUVE(Id::kFrustumCullingModeUVE, static_cast<std::int64_t>(FrustumCullingModeUVE::EnabledUVE));
    Config::SettingsStackUVE settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(settings, project, user, platform, commandLineValues));

    EngineConfigUVE config{};
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.frustumCullingModeUVE, FrustumCullingModeUVE::EnabledUVE);

    ASSERT_TRUE(platform.RemoveKeyUVE(Id::kFrustumCullingModeUVE));
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.frustumCullingModeUVE, FrustumCullingModeUVE::DebugFreezeUVE);

    // The debug flag is reachable from the command line like every other enum: by label.
    CommandLine::CommandLineUVE commandLine({
        "--rendering.culling.frustumCulling", "Debug freeze (draw everything)",
    });
    PopulateEngineCommandLineSettingsUVE(registry, commandLine, commandLineValues);
    const auto cliMode = registry.GetStoredValueUVE(commandLineValues, Id::kFrustumCullingModeUVE);
    ASSERT_TRUE(cliMode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*cliMode), static_cast<std::int64_t>(FrustumCullingModeUVE::DebugFreezeUVE));
    Config::SettingsStackUVE cliSettings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(cliSettings, project, user, platform, commandLineValues));
    EngineConfigUVE cliConfig{};
    ApplyEngineSettingsUVE(cliSettings, cliConfig);
    EXPECT_EQ(cliConfig.frustumCullingModeUVE, FrustumCullingModeUVE::DebugFreezeUVE);
}

TEST(EngineSettingsLayeringUVETest, ConvertsTypedCommandLineFlagsAndUsesThemAsTopLayer) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));

    CommandLine::CommandLineUVE commandLine({
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
    ConfigManagerUVE commandLineValues;
    PopulateEngineCommandLineSettingsUVE(registry, commandLine, commandLineValues);

    namespace Id = EngineProjectSettingIdUVE;
    const auto ticksValue = registry.GetStoredValueUVE(commandLineValues, Id::kPhysicsTicksPerSecondUVE);
    ASSERT_TRUE(ticksValue.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*ticksValue), 120.0);

    const auto maxFrameTime = registry.GetStoredValueUVE(commandLineValues, Id::kPhysicsMaxFrameTimeUVE);
    ASSERT_TRUE(maxFrameTime.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*maxFrameTime), 0.25);

    const auto maxSteps = registry.GetStoredValueUVE(commandLineValues, Id::kPhysicsMaxStepsPerFrameUVE);
    ASSERT_TRUE(maxSteps.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*maxSteps), 12);

    const auto gravity = registry.GetStoredValueUVE(commandLineValues, Id::kPhysicsGravityUVE);
    ASSERT_TRUE(gravity.has_value());
    EXPECT_EQ(std::get<Config::SettingVector3UVE>(*gravity), (Config::SettingVector3UVE{1.0, -9.81, 2.0}));

    const auto shadowFilter = registry.GetStoredValueUVE(commandLineValues, Id::kShadowFilterUVE);
    ASSERT_TRUE(shadowFilter.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*shadowFilter), 2);
    const auto shadowSplitBlend = registry.GetStoredValueUVE(commandLineValues, Id::kShadowCascadeSplitBlendUVE);
    ASSERT_TRUE(shadowSplitBlend.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowSplitBlend), 0.25);
    const auto shadowBiasDefault = registry.GetStoredValueUVE(commandLineValues, Id::kShadowBiasDefaultUVE);
    ASSERT_TRUE(shadowBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowBiasDefault), 0.35);
    const auto shadowNormalBiasDefault =
        registry.GetStoredValueUVE(commandLineValues, Id::kShadowNormalBiasDefaultUVE);
    ASSERT_TRUE(shadowNormalBiasDefault.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*shadowNormalBiasDefault), 1.75);

    const auto shaderCachePath = registry.GetStoredValueUVE(commandLineValues, Id::kShaderProgramCacheDirectoryUVE);
    ASSERT_TRUE(shaderCachePath.has_value());
    EXPECT_EQ(std::get<std::string>(*shaderCachePath), "shader_cache/cli");
    const auto shaderCompilationMode =
        registry.GetStoredValueUVE(commandLineValues, Id::kShaderCompilationModeUVE);
    ASSERT_TRUE(shaderCompilationMode.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*shaderCompilationMode),
              static_cast<std::int64_t>(ShaderCompilationModeUVE::SynchronousOnDemandUVE));
    const auto upscalingMethod = registry.GetStoredValueUVE(commandLineValues, Id::kSpatialUpscalingMethodUVE);
    ASSERT_TRUE(upscalingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*upscalingMethod),
              static_cast<std::int64_t>(SpatialUpscalingMethodUVE::NearestUVE));
    const auto toneMappingMethod = registry.GetStoredValueUVE(commandLineValues, Id::kToneMappingMethodUVE);
    ASSERT_TRUE(toneMappingMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*toneMappingMethod),
              static_cast<std::int64_t>(ToneMappingMethodUVE::LinearClampUVE));
    const auto sharpeningAmount = registry.GetStoredValueUVE(commandLineValues, Id::kSharpeningAmountUVE);
    ASSERT_TRUE(sharpeningAmount.has_value());
    EXPECT_DOUBLE_EQ(std::get<double>(*sharpeningAmount), 0.75);
    const auto ditheringEnabled = registry.GetStoredValueUVE(commandLineValues, Id::kDitheringEnabledUVE);
    ASSERT_TRUE(ditheringEnabled.has_value());
    EXPECT_FALSE(std::get<bool>(*ditheringEnabled));
    const auto aaMethod = registry.GetStoredValueUVE(commandLineValues, Id::kPostProcessAAMethodUVE);
    ASSERT_TRUE(aaMethod.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaMethod),
              static_cast<std::int64_t>(PostProcessAAMethodUVE::FastApproximateUVE));
    const auto aaQuality = registry.GetStoredValueUVE(commandLineValues, Id::kScreenSpaceAAQualityUVE);
    ASSERT_TRUE(aaQuality.has_value());
    EXPECT_EQ(std::get<std::int64_t>(*aaQuality), 2);

    const auto headless = registry.GetStoredValueUVE(commandLineValues, Id::kHeadlessUVE);
    ASSERT_TRUE(headless.has_value());
    EXPECT_TRUE(std::get<bool>(*headless));
    const auto quitOnLastWindowClosed =
        registry.GetStoredValueUVE(commandLineValues, Id::kQuitOnLastWindowClosedUVE);
    ASSERT_TRUE(quitOnLastWindowClosed.has_value());
    EXPECT_FALSE(std::get<bool>(*quitOnLastWindowClosed));
    const auto bootBackground = registry.GetStoredValueUVE(commandLineValues, Id::kBootBackgroundColorUVE);
    ASSERT_TRUE(bootBackground.has_value());
    EXPECT_EQ(std::get<Config::SettingColorUVE>(*bootBackground),
              (Config::SettingColorUVE{0.1F, 0.2F, 0.3F, 0.4F}));
    EXPECT_FALSE(registry.GetStoredValueUVE(commandLineValues, "layers.physics.1").has_value());

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    project.SetDoubleUVE(Id::kPhysicsTicksPerSecondUVE, 30.0);
    user.SetDoubleUVE(Id::kPhysicsTicksPerSecondUVE, 60.0);
    platform.SetIntUVE(Id::kShadowMapResolutionUVE, 1024);
    Config::SettingsStackUVE settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(settings, project, user, platform, commandLineValues));

    const auto resolution = settings.ResolveUVE(Id::kPhysicsTicksPerSecondUVE);
    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution->source, SettingValueSourceUVE::CommandLine);
    EngineConfigUVE config{};
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_DOUBLE_EQ(config.fixedUpdateFps, 120.0);
    EXPECT_DOUBLE_EQ(config.maxDeltaTimeSeconds, 0.25);
    EXPECT_EQ(config.maxFixedStepsPerFrame, 12);
    EXPECT_FLOAT_EQ(config.gravity.x, 1.0F);
    EXPECT_FLOAT_EQ(config.gravity.y, -9.81F);
    EXPECT_FLOAT_EQ(config.gravity.z, 2.0F);
    EXPECT_EQ(config.shadowPcfKernelRadius, 2U);
    EXPECT_FLOAT_EQ(config.shadowCascadeSplitLambda, 0.25F);
    EXPECT_FLOAT_EQ(config.shadowBiasDefaultUVE, 0.35F);
    EXPECT_FLOAT_EQ(config.shadowNormalBiasDefaultUVE, 1.75F);
    EXPECT_EQ(config.shaderCachePath, std::filesystem::path("shader_cache/cli"));
    EXPECT_EQ(config.shaderCompilationModeUVE, ShaderCompilationModeUVE::SynchronousOnDemandUVE);
    EXPECT_EQ(config.spatialUpscalingMethodUVE, SpatialUpscalingMethodUVE::NearestUVE);
    EXPECT_EQ(config.toneMappingMethodUVE, ToneMappingMethodUVE::LinearClampUVE);
    EXPECT_FLOAT_EQ(config.sharpeningAmountUVE, 0.75F);
    EXPECT_FALSE(config.ditheringEnabledUVE);
    EXPECT_EQ(config.postProcessAAMethodUVE, PostProcessAAMethodUVE::FastApproximateUVE);
    EXPECT_EQ(config.screenSpaceAAQualityUVE, 2U);
    EXPECT_TRUE(config.headlessUVE);
    EXPECT_FALSE(config.quitOnLastWindowClosedUVE);
    EXPECT_FLOAT_EQ(config.backgroundColorUVE[0U], 0.1F);
    EXPECT_FLOAT_EQ(config.backgroundColorUVE[1U], 0.2F);
    EXPECT_FLOAT_EQ(config.backgroundColorUVE[2U], 0.3F);
    EXPECT_FLOAT_EQ(config.backgroundColorUVE[3U], 0.4F);
    EXPECT_DOUBLE_EQ(config.splashMinimumDisplaySecondsUVE, 2.5);
    EXPECT_FALSE(config.splashSkippableUVE);
    EXPECT_FALSE(config.crashHandlerEnabledUVE);
}

TEST(EngineSettingsLayeringUVETest, RejectsInvalidTypedCliValueAndPreservesPlatformFallback) {
    Config::SettingsRegistryUVE registry;
    ASSERT_TRUE(RegisterEngineProjectSettingsUVE(registry));

    CommandLine::CommandLineUVE commandLine({"--rendering.shadows.mapResolution", "1536"});
    ConfigManagerUVE commandLineValues;
    PopulateEngineCommandLineSettingsUVE(registry, commandLine, commandLineValues);
    EXPECT_FALSE(registry.GetStoredValueUVE(commandLineValues, EngineProjectSettingIdUVE::kShadowMapResolutionUVE)
                     .has_value());

    ConfigManagerUVE project;
    ConfigManagerUVE user;
    ConfigManagerUVE platform;
    project.SetIntUVE(EngineProjectSettingIdUVE::kShadowMapResolutionUVE, 4096);
    user.SetIntUVE(EngineProjectSettingIdUVE::kShadowMapResolutionUVE, 2048);
    platform.SetIntUVE(EngineProjectSettingIdUVE::kShadowMapResolutionUVE, 1024);

    Config::SettingsStackUVE settings(registry);
    ASSERT_TRUE(AttachEngineSettingsLayersUVE(settings, project, user, platform, commandLineValues));
    const auto resolution = settings.ResolveUVE(EngineProjectSettingIdUVE::kShadowMapResolutionUVE);
    ASSERT_TRUE(resolution.has_value());
    EXPECT_EQ(resolution->source, SettingValueSourceUVE::Platform);
    EXPECT_EQ(std::get<std::int64_t>(resolution->value), 1024);

    EngineConfigUVE config{};
    ApplyEngineSettingsUVE(settings, config);
    EXPECT_EQ(config.shadowMapResolution, 1024U);
}

TEST(EngineSettingsLayeringUVETest, ResolvesPlatformSettingsPathAndHonorsExplicitOverride) {
    EngineConfigUVE config{};
    config.settingsFilePath = std::filesystem::path{"User"} / "settings.uvsettings";

    const std::filesystem::path generatedPath = GetPlatformSettingsFilePathUVE(config);
    EXPECT_EQ(generatedPath.filename(), config.settingsFilePath.filename());
    EXPECT_EQ(generatedPath.parent_path().parent_path().filename(), "platforms");
    EXPECT_EQ(generatedPath.parent_path().parent_path().parent_path(), config.settingsFilePath.parent_path());

    config.platformSettingsFilePath = std::filesystem::path{"Overrides"} / "desktop.uvsettings";
    EXPECT_EQ(GetPlatformSettingsFilePathUVE(config), config.platformSettingsFilePath);
}

} // namespace
} // namespace UVE::Core::Tests
