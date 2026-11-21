// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "prism/Widget.h"

namespace UVE::Core {
class EngineCoreUVE;
}

namespace UVE::LauncherUI {

/// PRISM's fonts, themes and locales ship beside every launcher executable under this name.
inline constexpr std::string_view kPrismAssetsDirectoryNameUVE = "prism_assets";
/// Process exit code when the window or its interface cannot start.
inline constexpr int kLauncherStartupFailedExitCodeUVE = 1;

struct LauncherWindowDescUVE final {
    /// The window's name for the operating system (taskbar, window switcher).
    std::string title;
    /// Whether the title bar repeats the mark and title. Launchers keep it clean: their own content
    /// already shows the brand, so the bar holds only the drag strip and the window buttons.
    bool showTitleInChrome = false;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    /// PRISM's fonts, themes and locales (normally kPrismAssetsDirectoryNameUVE beside the executable).
    std::filesystem::path prismAssetsDirectory;
    std::filesystem::path logFile;
    std::vector<std::string> commandLine;
    /// Android: the activity's surface to draw into (desktop builds open a window of their own), and
    /// the engine's built-in shaders unpacked from the APK.
    void* nativeSurface = nullptr;
    std::filesystem::path shaderDirectory;
    /// Runs before every frame with the running engine; returning false ends the loop. Android drives
    /// the activity's event queue here.
    std::function<bool(Core::EngineCoreUVE&)> beforeFrame;
    /// Automation: stop after this many frames, then save the window to `screenshot` if set.
    std::optional<int> frameLimit;
    std::filesystem::path screenshot;
};

/// Opens a borderless window with PRISM's own title bar in the shared UniVex palette, shows the
/// widget tree `buildRoot` returns, and runs until the window closes. `tick` runs once a frame before
/// the interface is drawn. Returns the process exit code.
[[nodiscard]] int RunLauncherWindowUVE(const LauncherWindowDescUVE& desc,
                                       const std::function<prism::Ref<prism::Widget>()>& buildRoot,
                                       const std::function<void()>& tick);

/// Closes the launcher window at the end of the current frame.
void RequestLauncherCloseUVE();

} // namespace UVE::LauncherUI
