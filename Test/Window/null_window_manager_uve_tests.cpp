// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/window/null_window_manager_uve.h"

#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "uve/window/window_title_format_uve.h"

namespace UVE::Window::Tests {
namespace {

TEST(NullWindowManagerUVETest, IsValidUVE_AlwaysTrue) {
    NullWindowManagerUVE windowManager;
    EXPECT_TRUE(windowManager.IsValidUVE());
}

TEST(NullWindowManagerUVETest, Construction_BookkeepsWidthHeightVSyncFromDesc) {
    WindowDescUVE desc;
    desc.width = 1920;
    desc.height = 1080;
    desc.vsyncEnabled = false;
    NullWindowManagerUVE windowManager(desc);

    EXPECT_EQ(windowManager.GetWidthUVE(), 1920U);
    EXPECT_EQ(windowManager.GetHeightUVE(), 1080U);
    EXPECT_FALSE(windowManager.IsVSyncEnabledUVE());
}

TEST(NullWindowManagerUVETest, Construction_UsesTypedDisplayPolicyAndContentScaleOverride) {
    WindowDescUVE desc;
    desc.mode = Platform::WindowModeUVE::Maximized;
    desc.vsyncModeExplicit = true;
    desc.vsyncMode = Platform::VSyncModeUVE::Mailbox;
    desc.contentScaleOverride = 1.75;
    NullWindowManagerUVE windowManager(desc);

    EXPECT_EQ(windowManager.GetWindowModeUVE(), Platform::WindowModeUVE::Maximized);
    EXPECT_FALSE(windowManager.IsFullscreenUVE());
    EXPECT_EQ(windowManager.GetVSyncModeUVE(), Platform::VSyncModeUVE::Mailbox);
    EXPECT_TRUE(windowManager.IsVSyncEnabledUVE());
    float scaleX = 0.0F;
    float scaleY = 0.0F;
    windowManager.GetContentScaleUVE(scaleX, scaleY);
    EXPECT_FLOAT_EQ(scaleX, 1.75F);
    EXPECT_FLOAT_EQ(scaleY, 1.75F);
}

TEST(NullWindowManagerUVETest, InvalidContentScaleOverrideFallsBackToDefaultScale) {
    WindowDescUVE desc;
    desc.contentScaleOverride = std::numeric_limits<double>::infinity();
    NullWindowManagerUVE windowManager(desc);

    float scaleX = 0.0F;
    float scaleY = 0.0F;
    windowManager.GetContentScaleUVE(scaleX, scaleY);
    EXPECT_FLOAT_EQ(scaleX, 1.0F);
    EXPECT_FLOAT_EQ(scaleY, 1.0F);
}

TEST(NullWindowManagerUVETest, DefaultDesc_MatchesWindowDescUVEDefaults) {
    NullWindowManagerUVE windowManager;
    EXPECT_EQ(windowManager.GetWidthUVE(), WindowDescUVE{}.width);
    EXPECT_EQ(windowManager.GetHeightUVE(), WindowDescUVE{}.height);
    EXPECT_TRUE(windowManager.IsVSyncEnabledUVE());
    EXPECT_TRUE(WindowDescUVE{}.cursorVisible);
    EXPECT_FALSE(WindowDescUVE{}.cursorConfinedToWindow);
    EXPECT_TRUE(windowManager.IsDisplaySleepAllowedUVE());
}

TEST(NullWindowManagerUVETest, WindowTitle_PreservesInitialTitleAndRejectsInvalidUpdates) {
    WindowDescUVE desc;
    desc.title = "Initial title";
    NullWindowManagerUVE windowManager(desc);
    EXPECT_EQ(windowManager.GetWindowTitleUVE(), "Initial title");

    windowManager.SetWindowTitleUVE("Editor Play - MainHall");
    EXPECT_EQ(windowManager.GetWindowTitleUVE(), "Editor Play - MainHall");

    const std::string previousTitle{windowManager.GetWindowTitleUVE()};
    windowManager.SetWindowTitleUVE("");
    windowManager.SetWindowTitleUVE(std::string(kMaximumWindowTitleBytesUVE + 1U, 'x'));
    std::string embeddedNullTitle{"invalid"};
    embeddedNullTitle.push_back('\0');
    embeddedNullTitle += "title";
    windowManager.SetWindowTitleUVE(embeddedNullTitle);
    EXPECT_EQ(windowManager.GetWindowTitleUVE(), previousTitle);
}

TEST(NullWindowManagerUVETest, DisplaySleepPolicy_RetainsDescriptorAndRuntimeRequestsWithoutOSEffects) {
    NullWindowManagerUVE defaultWindowManager;
    EXPECT_TRUE(defaultWindowManager.IsDisplaySleepAllowedUVE());

    WindowDescUVE desc;
    desc.allowDisplaySleep = false;
    NullWindowManagerUVE windowManager(desc);
    EXPECT_FALSE(windowManager.IsDisplaySleepAllowedUVE());
    windowManager.SetDisplaySleepAllowedUVE(true);
    EXPECT_TRUE(windowManager.IsDisplaySleepAllowedUVE());
    windowManager.SetDisplaySleepAllowedUVE(false);
    EXPECT_FALSE(windowManager.IsDisplaySleepAllowedUVE());
}

TEST(NullWindowManagerUVETest, PollEventsAndSwapBuffers_NeverCrash) {
    NullWindowManagerUVE windowManager;
    windowManager.PollEventsUVE();
    windowManager.SwapBuffersUVE();
    windowManager.PollEventsUVE();
    SUCCEED();
}

TEST(NullWindowManagerUVETest, IsCloseRequestedUVE_AlwaysFalse) {
    NullWindowManagerUVE windowManager;
    EXPECT_FALSE(windowManager.IsCloseRequestedUVE());
    windowManager.PollEventsUVE();
    EXPECT_FALSE(windowManager.IsCloseRequestedUVE());
}

TEST(NullWindowManagerUVETest, SetVSyncEnabledUVE_RoundTrips) {
    NullWindowManagerUVE windowManager;
    windowManager.SetVSyncEnabledUVE(false);
    EXPECT_FALSE(windowManager.IsVSyncEnabledUVE());
    windowManager.SetVSyncEnabledUVE(true);
    EXPECT_TRUE(windowManager.IsVSyncEnabledUVE());
}

TEST(NullWindowManagerUVETest, SetVSyncModeUVE_RoundTripsAllModesAndIgnoresInvalidValues) {
    NullWindowManagerUVE windowManager;
    for (const Platform::VSyncModeUVE mode : {Platform::VSyncModeUVE::Off,
                                              Platform::VSyncModeUVE::On,
                                              Platform::VSyncModeUVE::Adaptive,
                                              Platform::VSyncModeUVE::Mailbox}) {
        windowManager.SetVSyncModeUVE(mode);
        EXPECT_EQ(windowManager.GetVSyncModeUVE(), mode);
        EXPECT_EQ(windowManager.IsVSyncEnabledUVE(), mode != Platform::VSyncModeUVE::Off);
    }

    const Platform::VSyncModeUVE previousMode = windowManager.GetVSyncModeUVE();
    windowManager.SetVSyncModeUVE(static_cast<Platform::VSyncModeUVE>(255U));
    EXPECT_EQ(windowManager.GetVSyncModeUVE(), previousMode);
}


TEST(NullWindowManagerUVETest, SetFullscreenUVE_RoundTrips) {
    NullWindowManagerUVE windowManager;
    EXPECT_FALSE(windowManager.IsFullscreenUVE());
    windowManager.SetFullscreenUVE(true);
    EXPECT_TRUE(windowManager.IsFullscreenUVE());
    EXPECT_EQ(windowManager.GetWindowModeUVE(), Platform::WindowModeUVE::Fullscreen);
    windowManager.SetFullscreenUVE(false);
    EXPECT_FALSE(windowManager.IsFullscreenUVE());
    EXPECT_EQ(windowManager.GetWindowModeUVE(), Platform::WindowModeUVE::Windowed);
}

TEST(NullWindowManagerUVETest, SetWindowModeUVE_RoundTripsAllModesAndIgnoresInvalidValues) {
    NullWindowManagerUVE windowManager;
    for (const Platform::WindowModeUVE mode : {Platform::WindowModeUVE::Windowed,
                                               Platform::WindowModeUVE::Maximized,
                                               Platform::WindowModeUVE::Fullscreen,
                                               Platform::WindowModeUVE::ExclusiveFullscreen,
                                               Platform::WindowModeUVE::Borderless}) {
        windowManager.SetWindowModeUVE(mode);
        EXPECT_EQ(windowManager.GetWindowModeUVE(), mode);
        EXPECT_EQ(windowManager.IsFullscreenUVE(),
                  mode == Platform::WindowModeUVE::Fullscreen ||
                      mode == Platform::WindowModeUVE::ExclusiveFullscreen);
    }

    const Platform::WindowModeUVE previousMode = windowManager.GetWindowModeUVE();
    const bool previousFullscreen = windowManager.IsFullscreenUVE();
    windowManager.SetWindowModeUVE(static_cast<Platform::WindowModeUVE>(255U));
    EXPECT_EQ(windowManager.GetWindowModeUVE(), previousMode);
    EXPECT_EQ(windowManager.IsFullscreenUVE(), previousFullscreen);
}

TEST(NullWindowManagerUVETest, EnumerateMonitorsUVE_ReturnsEmpty) {
    NullWindowManagerUVE windowManager;
    EXPECT_TRUE(windowManager.EnumerateMonitorsUVE().empty());
}

TEST(NullWindowManagerUVETest, GetNativeWindowHandleUVE_ReturnsNullptr) {
    NullWindowManagerUVE windowManager;
    EXPECT_EQ(windowManager.GetNativeWindowHandleUVE(), nullptr);
}

TEST(NullWindowManagerUVETest, GetBackendNameUVE_ReturnsNull) {
    NullWindowManagerUVE windowManager;
    EXPECT_EQ(windowManager.GetBackendNameUVE(), "Null");
}

} // namespace
} // namespace UVE::Window::Tests
