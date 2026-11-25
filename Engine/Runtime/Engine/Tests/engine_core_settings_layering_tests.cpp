// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "engine_core.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <gtest/gtest.h>

#include "engine_project_settings.h"
#include "null_window_manager.h"
#include "window_title_format.h"

namespace UVE::Core::Tests {
namespace {

class SettingsTestDirectory final {
public:
    SettingsTestDirectory() {
        static std::atomic<std::uint64_t> sequence{0U};
        const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
        m_path = std::filesystem::temp_directory_path() /
                 ("uve_engine_core_settings_" + std::to_string(timestamp) + "_" +
                  std::to_string(sequence.fetch_add(1U, std::memory_order_relaxed)));
        std::filesystem::create_directories(m_path);
    }

    ~SettingsTestDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] const std::filesystem::path& GetPath() const noexcept { return m_path; }

private:
    std::filesystem::path m_path;
};

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << text;
    return output.good();
}

[[nodiscard]] EngineConfig MakeSettingsTestConfig(const std::filesystem::path& directory) {
    EngineConfig config{};
    config.enableConsoleLogging = false;
    config.logFilePath = directory / "engine.log";
    config.threadPoolWorkerCount = 1U;
    config.projectSettingsFilePath = directory / "project.uvsettings";
    config.settingsFilePath = directory / "user.uvsettings";
    config.platformSettingsFilePath = directory / "platform.uvsettings";
    config.inputMapFilePath = directory / "project.uvinput";
    config.assetDatabaseFilePath = directory / "assets.uvassetdb";
    config.headless = true;
    return config;
}

TEST(EngineCoreSettingsLayeringTest, LoadsAllLayersAndAppliesCommandLineBeforeCoreStartupCompletes) {
    SettingsTestDirectory directory;
    EngineConfig config = MakeSettingsTestConfig(directory.GetPath());
    config.headless = false; // The presence-only CLI flag must resolve into EngineConfig.
    config.autoSaveIntervalSeconds = 777.0; // Keep the caller's base when no setting layer supplies it.
    config.commandLineArgs = {"--physics.common.ticksPerSecond", "120",
                              "--rendering.shadows.mapResolution", "512", "--headless"};
    ASSERT_TRUE(WriteTextFile(
        config.projectSettingsFilePath,
        R"({"physics":{"common":{"ticksPerSecond":30,"maxFrameTime":0.5,"maxStepsPerFrame":16}},"rendering":{"shadows":{"mapResolution":4096}}})"));
    ASSERT_TRUE(WriteTextFile(
        config.settingsFilePath,
        R"({"physics":{"common":{"ticksPerSecond":90,"maxFrameTime":0.4}},"rendering":{"shadows":{"mapResolution":2048}}})"));
    ASSERT_TRUE(WriteTextFile(config.platformSettingsFilePath,
                                 R"({"rendering":{"shadows":{"mapResolution":1024}}})"));

    EngineCore engine(config);
    engine.Init();
    EXPECT_DOUBLE_EQ(engine.GetConfig().fixedUpdateFps, 120.0);
    EXPECT_DOUBLE_EQ(engine.GetConfig().maxDeltaTimeSeconds, 0.4);
    EXPECT_EQ(engine.GetConfig().maxFixedStepsPerFrame, 16);
    EXPECT_EQ(engine.GetConfig().shadowMapResolution, 512U);
    EXPECT_TRUE(engine.GetConfig().headless);
    EXPECT_DOUBLE_EQ(engine.GetConfig().autoSaveIntervalSeconds, 777.0);
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringTest, DisplaySleepPolicyIsForwardedToHeadlessWindowManager) {
    SettingsTestDirectory directory;
    EngineConfig config = MakeSettingsTestConfig(directory.GetPath());
    config.allowDisplaySleep = false;

    EngineCore engine(config);
    engine.Init();
    auto* const windowManager = dynamic_cast<UVE::Window::NullWindowManager*>(
        &engine.GetServices().GetWindowManager());
    ASSERT_NE(windowManager, nullptr);
    EXPECT_FALSE(windowManager->IsDisplaySleepAllowed());
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringTest, EditorPlaySceneNameUpdatesHeadlessWindowTitleAndClearsIt) {
    SettingsTestDirectory directory;
    EngineConfig config = MakeSettingsTestConfig(directory.GetPath());
    config.windowTitle = "UniVex";
    config.projectDisplayName = "Demo Project";
    config.windowTitleFormat = "{projectName}";
    config.appendSceneNameInEditorPlayMode = true;

    EngineCore engine(config);
    engine.Init();
    auto* const windowManager = dynamic_cast<UVE::Window::NullWindowManager*>(
        &engine.GetServices().GetWindowManager());
    ASSERT_NE(windowManager, nullptr);
    EXPECT_EQ(windowManager->GetWindowTitle(), "Demo Project");

    EXPECT_TRUE(engine.SetEditorPlaySceneName("MainHall"));
    EXPECT_EQ(windowManager->GetWindowTitle(), "Demo Project - MainHall");
    EXPECT_TRUE(engine.SetEditorPlaySceneName(""));
    EXPECT_EQ(windowManager->GetWindowTitle(), "Demo Project");

    const std::string previousTitle{windowManager->GetWindowTitle()};
    const std::string overlongScene(Window::kMaximumWindowTitleBytes + 1U, 'x');
    EXPECT_FALSE(engine.SetEditorPlaySceneName(overlongScene));
    EXPECT_EQ(windowManager->GetWindowTitle(), previousTitle);
    engine.Shutdown();
}

TEST(EngineCoreSettingsLayeringTest, InvalidCommandLineValueFallsThroughToLoadedPlatformFile) {
    SettingsTestDirectory directory;
    EngineConfig config = MakeSettingsTestConfig(directory.GetPath());
    config.platformSettingsFilePath.clear(); // Exercise the generated platforms/<target>/ path.
    config.commandLineArgs = {"--rendering.shadows.mapResolution", "1536"};
    ASSERT_TRUE(WriteTextFile(config.projectSettingsFilePath,
                                 R"({"rendering":{"shadows":{"mapResolution":4096}}})"));
    ASSERT_TRUE(WriteTextFile(config.settingsFilePath,
                                 R"({"rendering":{"shadows":{"mapResolution":2048}}})"));
    const std::filesystem::path platformPath = GetPlatformSettingsFilePath(config);
    ASSERT_TRUE(std::filesystem::create_directories(platformPath.parent_path()));
    ASSERT_TRUE(WriteTextFile(platformPath, R"({"rendering":{"shadows":{"mapResolution":1024}}})"));

    EngineCore engine(config);
    engine.Init();
    EXPECT_EQ(engine.GetConfig().shadowMapResolution, 1024U);
    engine.Shutdown();
}

} // namespace
} // namespace UVE::Core::Tests
