// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/core/engine_core_uve.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "uve/core/engine_project_settings_uve.h"
#include "uve/window/null_window_manager_uve.h"
#include "uve/window/window_title_format_uve.h"

namespace UVE::Core::Tests {
namespace {

class SettingsTestDirectoryUVE final {
public:
    SettingsTestDirectoryUVE() {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("uve_engine_core_settings_" + std::to_string(timestamp) + "_" +
                  std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed)));
        std::filesystem::create_directories(m_path);
    }

    ~SettingsTestDirectoryUVE() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& GetPathUVE() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

[[nodiscard]] bool WriteTextFileUVE(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << text;
    return output.good();
}

[[nodiscard]] EngineConfigUVE MakeSettingsTestConfigUVE(const std::filesystem::path& directory) {
    EngineConfigUVE config{};
    config.enableConsoleLogging = false;
    config.logFilePath = directory / "engine.log";
    config.threadPoolWorkerCount = 1U;
    config.projectSettingsFilePath = directory / "project.uvsettings";
    config.settingsFilePath = directory / "user.uvsettings";
    config.platformSettingsFilePath = directory / "platform.uvsettings";
    config.inputMapFilePath = directory / "project.uvinput";
    config.assetDatabaseFilePath = directory / "assets.uvassetdb";
    config.headlessUVE = true;
    return config;
}

TEST(EngineCoreSettingsLayeringUVETest, LoadsAllLayersAndAppliesCommandLineBeforeCoreStartupCompletes) {
    SettingsTestDirectoryUVE directory;
    EngineConfigUVE config = MakeSettingsTestConfigUVE(directory.GetPathUVE());
    config.headlessUVE = false; // The presence-only CLI flag must resolve into EngineConfig.
    config.autoSaveIntervalSecondsUVE = 777.0; // Keep the caller's base when no setting layer supplies it.
    config.commandLineArgs = {"--physics.common.ticksPerSecond", "120",
                              "--rendering.shadows.mapResolution", "512", "--headless"};
    ASSERT_TRUE(WriteTextFileUVE(
        config.projectSettingsFilePath,
        R"({"physics":{"common":{"ticksPerSecond":30,"maxFrameTime":0.5,"maxStepsPerFrame":16}},"rendering":{"shadows":{"mapResolution":4096}}})"));
    ASSERT_TRUE(WriteTextFileUVE(
        config.settingsFilePath,
        R"({"physics":{"common":{"ticksPerSecond":90,"maxFrameTime":0.4}},"rendering":{"shadows":{"mapResolution":2048}}})"));
    ASSERT_TRUE(WriteTextFileUVE(config.platformSettingsFilePath,
                                 R"({"rendering":{"shadows":{"mapResolution":1024}}})"));

    EngineCoreUVE engine(config);
    engine.Init();
    EXPECT_DOUBLE_EQ(engine.GetConfigUVE().fixedUpdateFps, 120.0);
    EXPECT_DOUBLE_EQ(engine.GetConfigUVE().maxDeltaTimeSeconds, 0.4);
    EXPECT_EQ(engine.GetConfigUVE().maxFixedStepsPerFrame, 16);
    EXPECT_EQ(engine.GetConfigUVE().shadowMapResolution, 512U);
    EXPECT_TRUE(engine.GetConfigUVE().headlessUVE);
    EXPECT_DOUBLE_EQ(engine.GetConfigUVE().autoSaveIntervalSecondsUVE, 777.0);
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringUVETest, NormalizesDirectEngineConfigBeforeSettingsAreApplied) {
    SettingsTestDirectoryUVE directory;
    EngineConfigUVE config = MakeSettingsTestConfigUVE(directory.GetPathUVE());
    config.shaderCachePath = "../unsafe-cache";
    config.shaderCompilationModeUVE = static_cast<ShaderCompilationModeUVE>(99U);
    config.spatialUpscalingMethodUVE = static_cast<SpatialUpscalingMethodUVE>(99U);
    config.toneMappingMethodUVE = static_cast<ToneMappingMethodUVE>(99U);
    config.postProcessAAMethodUVE = static_cast<PostProcessAAMethodUVE>(99U);
    config.screenSpaceAAQualityUVE = 255U;
    config.ssaoRadiusUVE = std::numeric_limits<float>::quiet_NaN();
    config.ssaoIntensityUVE = 5.0F;
    config.ssaoPowerUVE = 0.0F;
    config.ssaoQualityUVE = 255U;
    config.renderResolutionScaleUVE = 2.0;
    config.minimumRenderResolutionScaleUVE = 2.0;
    config.renderResolutionTargetFrameTimeMillisecondsUVE = 1.0;

    EngineCoreUVE engine(config);
    engine.Init();
    const EngineConfigUVE& normalized = engine.GetConfigUVE();
    EXPECT_EQ(normalized.shaderCachePath, EngineConfigUVE{}.shaderCachePath);
    EXPECT_EQ(normalized.shaderCompilationModeUVE, ShaderCompilationModeUVE::AsynchronousOnDemandUVE);
    EXPECT_EQ(normalized.spatialUpscalingMethodUVE, SpatialUpscalingMethodUVE::BilinearUVE);
    EXPECT_EQ(normalized.toneMappingMethodUVE, ToneMappingMethodUVE::AcesUVE);
    EXPECT_EQ(normalized.postProcessAAMethodUVE, PostProcessAAMethodUVE::NoneUVE);
    EXPECT_EQ(normalized.screenSpaceAAQualityUVE, 2U);
    EXPECT_FLOAT_EQ(normalized.ssaoRadiusUVE, 0.5F);
    EXPECT_FLOAT_EQ(normalized.ssaoIntensityUVE, 1.0F);
    EXPECT_FLOAT_EQ(normalized.ssaoPowerUVE, 1.0F);
    EXPECT_EQ(normalized.ssaoQualityUVE, 2U);
    EXPECT_DOUBLE_EQ(normalized.renderResolutionScaleUVE, kMaximumRenderResolutionScaleUVE);
    EXPECT_DOUBLE_EQ(normalized.minimumRenderResolutionScaleUVE, kMinimumRenderResolutionScaleUVE);
    EXPECT_DOUBLE_EQ(normalized.renderResolutionTargetFrameTimeMillisecondsUVE,
                     kDefaultRenderResolutionTargetFrameTimeMillisecondsUVE);
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringUVETest, ValidSettingsOverrideAnInvalidDirectBaseWithoutPostValidation) {
    SettingsTestDirectoryUVE directory;
    EngineConfigUVE config = MakeSettingsTestConfigUVE(directory.GetPathUVE());
    config.renderResolutionScaleUVE = 0.25;
    config.minimumRenderResolutionScaleUVE = 0.25;
    config.renderResolutionTargetFrameTimeMillisecondsUVE = std::numeric_limits<double>::quiet_NaN();
    ASSERT_TRUE(WriteTextFileUVE(
        config.projectSettingsFilePath,
        R"({"rendering":{"resolution":{"scale":0.8,"minimumScale":0.6,"targetFrameTimeMilliseconds":20.0},"ssao":{"radius":1.25}}})"));

    EngineCoreUVE engine(config);
    engine.Init();
    EXPECT_DOUBLE_EQ(engine.GetConfigUVE().renderResolutionScaleUVE, 0.8);
    EXPECT_DOUBLE_EQ(engine.GetConfigUVE().minimumRenderResolutionScaleUVE, 0.6);
    EXPECT_DOUBLE_EQ(engine.GetConfigUVE().renderResolutionTargetFrameTimeMillisecondsUVE, 20.0);
    EXPECT_FLOAT_EQ(engine.GetConfigUVE().ssaoRadiusUVE, 1.25F);
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringUVETest, DisplaySleepPolicyIsForwardedToHeadlessWindowManager) {
    SettingsTestDirectoryUVE directory;
    EngineConfigUVE config = MakeSettingsTestConfigUVE(directory.GetPathUVE());
    config.allowDisplaySleepUVE = false;

    EngineCoreUVE engine(config);
    engine.Init();
    auto* const windowManager = dynamic_cast<UVE::Window::NullWindowManagerUVE*>(
        &engine.GetServicesUVE().GetWindowManagerUVE());
    ASSERT_NE(windowManager, nullptr);
    EXPECT_FALSE(windowManager->IsDisplaySleepAllowedUVE());
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringUVETest, EditorPlaySceneNameUpdatesHeadlessWindowTitleAndClearsIt) {
    SettingsTestDirectoryUVE directory;
    EngineConfigUVE config = MakeSettingsTestConfigUVE(directory.GetPathUVE());
    config.windowTitle = "UniVex";
    config.projectDisplayNameUVE = "Demo Project";
    config.windowTitleFormatUVE = "{projectName}";
    config.appendSceneNameInEditorPlayModeUVE = true;

    EngineCoreUVE engine(config);
    engine.Init();
    auto* const windowManager = dynamic_cast<UVE::Window::NullWindowManagerUVE*>(
        &engine.GetServicesUVE().GetWindowManagerUVE());
    ASSERT_NE(windowManager, nullptr);
    EXPECT_EQ(windowManager->GetWindowTitleUVE(), "Demo Project");

    EXPECT_TRUE(engine.SetEditorPlaySceneNameUVE("MainHall"));
    EXPECT_EQ(windowManager->GetWindowTitleUVE(), "Demo Project - MainHall");
    EXPECT_TRUE(engine.SetEditorPlaySceneNameUVE(""));
    EXPECT_EQ(windowManager->GetWindowTitleUVE(), "Demo Project");

    const std::string previousTitle{windowManager->GetWindowTitleUVE()};
    const std::string overlongScene(Window::kMaximumWindowTitleBytesUVE + 1U, 'x');
    EXPECT_FALSE(engine.SetEditorPlaySceneNameUVE(overlongScene));
    EXPECT_EQ(windowManager->GetWindowTitleUVE(), previousTitle);
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringUVETest, InvalidCommandLineValueFallsThroughToLoadedPlatformFile) {
    SettingsTestDirectoryUVE directory;
    EngineConfigUVE config = MakeSettingsTestConfigUVE(directory.GetPathUVE());
    config.platformSettingsFilePath.clear(); // Exercise the generated platforms/<target>/ path.
    config.commandLineArgs = {"--rendering.shadows.mapResolution", "1536"};
    ASSERT_TRUE(WriteTextFileUVE(config.projectSettingsFilePath,
                                 R"({"rendering":{"shadows":{"mapResolution":4096}}})"));
    ASSERT_TRUE(WriteTextFileUVE(config.settingsFilePath,
                                 R"({"rendering":{"shadows":{"mapResolution":2048}}})"));
    const std::filesystem::path platformPath = GetPlatformSettingsFilePathUVE(config);
    ASSERT_TRUE(std::filesystem::create_directories(platformPath.parent_path()));
    ASSERT_TRUE(WriteTextFileUVE(platformPath, R"({"rendering":{"shadows":{"mapResolution":1024}}})"));

    EngineCoreUVE engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetConfigUVE().shadowMapResolution, 1024U);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Core::Tests
