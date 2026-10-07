// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Window {

inline constexpr std::uint32_t kMaximumWindowIconAxisUVE = 8192U;

struct WindowIconUVE final {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<std::uint8_t> rgba8;

    [[nodiscard]] bool operator==(const WindowIconUVE&) const = default;
};

inline constexpr std::size_t kMaximumWindowTitleBytesUVE = 256U;

/// Describes the window WindowManagerUVE creates. Passed once at construction; there is no
/// "resize the desc" API — width/height change through the OS (user drag, SetFullscreenUVE()) and
/// are read back via IWindowManagerUVE::GetWidthUVE()/GetHeightUVE(), not by mutating this struct.
/// Width and height are validated against the shared kMaximumDisplayModeAxisUVE cap before backend
/// creation narrows them to GLFW's signed integer dimensions.
struct WindowDescUVE {
    std::string title = "UniVex Engine";
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    bool vsyncEnabled = true; ///< Legacy Off/On request used when `vsyncModeExplicit` is false.
    bool vsyncModeExplicit = false;
    Platform::VSyncModeUVE vsyncMode = Platform::VSyncModeUVE::On;
    Platform::WindowModeUVE mode = Platform::WindowModeUVE::Windowed;
    bool borderless = false;
    bool alwaysOnTop = false;
    bool transparent = false;
    std::uint32_t minimumWidth = 0U;
    std::uint32_t minimumHeight = 0U;
    std::uint32_t maximumWidth = 0U;
    std::uint32_t maximumHeight = 0U;
    bool initialPositionSpecified = false;
    std::int32_t initialPositionX = 0;
    std::int32_t initialPositionY = 0;
    std::string monitorName;
    bool highDpiAware = true;
    bool perMonitorScaling = true;
    double contentScaleOverride = 0.0;
    Platform::StretchModeUVE stretchMode = Platform::StretchModeUVE::Disabled;
    Platform::AspectPolicyUVE aspectPolicy = Platform::AspectPolicyUVE::Keep;
    bool integerOnlyScaling = false;
    /// Handheld orientation request and its permitted set. Desktop GLFW has no API to rotate a
    /// display, so it retains these values without enforcing them; a handheld backend must apply
    /// them through its platform lifecycle/manifest integration.
    Platform::DisplayOrientationUVE orientation = Platform::DisplayOrientationUVE::Auto;
    std::vector<Platform::DisplayOrientationUVE> allowedOrientations{
        Platform::DisplayOrientationUVE::Landscape, Platform::DisplayOrientationUVE::Portrait};
    std::uint32_t focusedFrameRateCap = 0U;
    std::uint32_t unfocusedFrameRateCap = 0U;
    bool allowDisplaySleep = true;
    std::uint32_t cursorImageWidth = 0U;
    std::uint32_t cursorImageHeight = 0U;
    std::vector<std::uint8_t> cursorRgba8;
    std::uint32_t cursorHotspotX = 0U;
    std::uint32_t cursorHotspotY = 0U;
    bool cursorVisible = true;
    bool cursorConfinedToWindow = false;

    /// Requested OpenGL context version (WindowManagerUVE's real GLFW3 backend requests Core
    /// Profile at exactly this version via glfwWindowHint before creating the window). The
    /// production default is OpenGL 4.6 Core per the approved architecture decision. This is
    /// configurable — not hardcoded — because GL context availability is a real driver/platform
    /// fact: this project's own development sandbox (Mesa llvmpipe under Xvfb) caps at 4.5 Core
    /// and fails to create a 4.6 context (confirmed by direct testing), so verification here uses
    /// an explicit {4, 5} override without touching the shipped default a real GPU driver targets.
    std::uint32_t glVersionMajor = 4;
    std::uint32_t glVersionMinor = 6;

    /// Optional icon images in RGBA8 order. Backends that do not expose native icons may ignore
    /// these; the GLFW desktop backend applies them during window creation.
    std::vector<WindowIconUVE> icons;
};

} // namespace UVE::Window
