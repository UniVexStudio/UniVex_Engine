// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "null_window_manager.h"

#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "window_title_format.h"

namespace UVE::Window::Tests {
namespace {

TEST(NullWindowManagerTest, IsValid_AlwaysTrue) {
    NullWindowManager windowManager;
    EXPECT_TRUE(windowManager.IsValid());
}

TEST(NullWindowManagerTest, Construction_BookkeepsWidthHeightVSyncFromDesc) {
    WindowDesc desc;
    desc.width = 1920;
    desc.height = 1080;
    desc.vsyncEnabled = false;
    NullWindowManager windowManager(desc);

    EXPECT_EQ(windowManager.GetWidth(), 1920U);
    EXPECT_EQ(windowManager.GetHeight(), 1080U);
    EXPECT_FALSE(windowManager.IsVSyncEnabled());
}

TEST(NullWindowManagerTest, Construction_UsesTypedDisplayPolicyAndContentScaleOverride) {
    WindowDesc desc;
    desc.mode = Platform::WindowMode::Maximized;
    desc.vsyncModeExplicit = true;
    desc.vsyncMode = Platform::VSyncMode::Mailbox;
    desc.contentScaleOverride = 1.75;
    NullWindowManager windowManager(desc);

    EXPECT_EQ(windowManager.GetWindowMode(), Platform::WindowMode::Maximized);
    EXPECT_FALSE(windowManager.IsFullscreen());
    EXPECT_EQ(windowManager.GetVSyncMode(), Platform::VSyncMode::Mailbox);
    EXPECT_TRUE(windowManager.IsVSyncEnabled());
    float scaleX = 0.0F;
    float scaleY = 0.0F;
    windowManager.GetContentScale(scaleX, scaleY);
    EXPECT_FLOAT_EQ(scaleX, 1.75F);
    EXPECT_FLOAT_EQ(scaleY, 1.75F);
}

TEST(NullWindowManagerTest, InvalidContentScaleOverrideFallsBackToDefaultScale) {
    WindowDesc desc;
    desc.contentScaleOverride = std::numeric_limits<double>::infinity();
    NullWindowManager windowManager(desc);

    float scaleX = 0.0F;
    float scaleY = 0.0F;
    windowManager.GetContentScale(scaleX, scaleY);
    EXPECT_FLOAT_EQ(scaleX, 1.0F);
    EXPECT_FLOAT_EQ(scaleY, 1.0F);
}

TEST(NullWindowManagerTest, DefaultDesc_MatchesWindowDescDefaults) {
    NullWindowManager windowManager;
    EXPECT_EQ(windowManager.GetWidth(), WindowDesc{}.width);
    EXPECT_EQ(windowManager.GetHeight(), WindowDesc{}.height);
    EXPECT_TRUE(windowManager.IsVSyncEnabled());
    EXPECT_TRUE(WindowDesc{}.cursorVisible);
    EXPECT_FALSE(WindowDesc{}.cursorConfinedToWindow);
    EXPECT_TRUE(windowManager.IsDisplaySleepAllowed());
}

TEST(NullWindowManagerTest, WindowTitle_PreservesInitialTitleAndRejectsInvalidUpdates) {
    WindowDesc desc;
    desc.title = "Initial title";
    NullWindowManager windowManager(desc);
    EXPECT_EQ(windowManager.GetWindowTitle(), "Initial title");

    windowManager.SetWindowTitle("Editor Play - MainHall");
    EXPECT_EQ(windowManager.GetWindowTitle(), "Editor Play - MainHall");

    const std::string previousTitle{windowManager.GetWindowTitle()};
    windowManager.SetWindowTitle("");
    windowManager.SetWindowTitle(std::string(kMaximumWindowTitleBytes + 1U, 'x'));
    std::string embeddedNullTitle{"invalid"};
    embeddedNullTitle.push_back('\0');
    embeddedNullTitle += "title";
    windowManager.SetWindowTitle(embeddedNullTitle);
    EXPECT_EQ(windowManager.GetWindowTitle(), previousTitle);
}

TEST(NullWindowManagerTest, DisplaySleepPolicy_RetainsDescriptorAndRuntimeRequestsWithoutOSEffects) {
    NullWindowManager defaultWindowManager;
    EXPECT_TRUE(defaultWindowManager.IsDisplaySleepAllowed());

    WindowDesc desc;
    desc.allowDisplaySleep = false;
    NullWindowManager windowManager(desc);
    EXPECT_FALSE(windowManager.IsDisplaySleepAllowed());
    windowManager.SetDisplaySleepAllowed(true);
    EXPECT_TRUE(windowManager.IsDisplaySleepAllowed());
    windowManager.SetDisplaySleepAllowed(false);
    EXPECT_FALSE(windowManager.IsDisplaySleepAllowed());
}

TEST(NullWindowManagerTest, PollEventsAndSwapBuffers_NeverCrash) {
    NullWindowManager windowManager;
    windowManager.PollEvents();
    windowManager.SwapBuffers();
    windowManager.PollEvents();
    SUCCEED();
}

TEST(NullWindowManagerTest, IsCloseRequested_AlwaysFalse) {
    NullWindowManager windowManager;
    EXPECT_FALSE(windowManager.IsCloseRequested());
    windowManager.PollEvents();
    EXPECT_FALSE(windowManager.IsCloseRequested());
}

TEST(NullWindowManagerTest, SetVSyncEnabled_RoundTrips) {
    NullWindowManager windowManager;
    windowManager.SetVSyncEnabled(false);
    EXPECT_FALSE(windowManager.IsVSyncEnabled());
    windowManager.SetVSyncEnabled(true);
    EXPECT_TRUE(windowManager.IsVSyncEnabled());
}

TEST(NullWindowManagerTest, SetVSyncMode_RoundTripsAllModesAndIgnoresInvalidValues) {
    NullWindowManager windowManager;
    for (const Platform::VSyncMode mode : {Platform::VSyncMode::Off,
                                              Platform::VSyncMode::On,
                                              Platform::VSyncMode::Adaptive,
                                              Platform::VSyncMode::Mailbox}) {
        windowManager.SetVSyncMode(mode);
        EXPECT_EQ(windowManager.GetVSyncMode(), mode);
        EXPECT_EQ(windowManager.IsVSyncEnabled(), mode != Platform::VSyncMode::Off);
    }

    const Platform::VSyncMode previousMode = windowManager.GetVSyncMode();
    windowManager.SetVSyncMode(static_cast<Platform::VSyncMode>(255U));
    EXPECT_EQ(windowManager.GetVSyncMode(), previousMode);
}


TEST(NullWindowManagerTest, SetFullscreen_RoundTrips) {
    NullWindowManager windowManager;
    EXPECT_FALSE(windowManager.IsFullscreen());
    windowManager.SetFullscreen(true);
    EXPECT_TRUE(windowManager.IsFullscreen());
    EXPECT_EQ(windowManager.GetWindowMode(), Platform::WindowMode::Fullscreen);
    windowManager.SetFullscreen(false);
    EXPECT_FALSE(windowManager.IsFullscreen());
    EXPECT_EQ(windowManager.GetWindowMode(), Platform::WindowMode::Windowed);
}

TEST(NullWindowManagerTest, SetWindowMode_RoundTripsAllModesAndIgnoresInvalidValues) {
    NullWindowManager windowManager;
    for (const Platform::WindowMode mode : {Platform::WindowMode::Windowed,
                                               Platform::WindowMode::Maximized,
                                               Platform::WindowMode::Fullscreen,
                                               Platform::WindowMode::ExclusiveFullscreen,
                                               Platform::WindowMode::Borderless}) {
        windowManager.SetWindowMode(mode);
        EXPECT_EQ(windowManager.GetWindowMode(), mode);
        EXPECT_EQ(windowManager.IsFullscreen(),
                  mode == Platform::WindowMode::Fullscreen ||
                      mode == Platform::WindowMode::ExclusiveFullscreen);
    }

    const Platform::WindowMode previousMode = windowManager.GetWindowMode();
    const bool previousFullscreen = windowManager.IsFullscreen();
    windowManager.SetWindowMode(static_cast<Platform::WindowMode>(255U));
    EXPECT_EQ(windowManager.GetWindowMode(), previousMode);
    EXPECT_EQ(windowManager.IsFullscreen(), previousFullscreen);
}

TEST(NullWindowManagerTest, EnumerateMonitors_ReturnsEmpty) {
    NullWindowManager windowManager;
    EXPECT_TRUE(windowManager.EnumerateMonitors().empty());
}

TEST(NullWindowManagerTest, GetNativeWindowHandle_ReturnsNullptr) {
    NullWindowManager windowManager;
    EXPECT_EQ(windowManager.GetNativeWindowHandle(), nullptr);
}

TEST(NullWindowManagerTest, GetBackendName_ReturnsNull) {
    NullWindowManager windowManager;
    EXPECT_EQ(windowManager.GetBackendName(), "Null");
}

} // namespace
} // namespace UVE::Window::Tests
