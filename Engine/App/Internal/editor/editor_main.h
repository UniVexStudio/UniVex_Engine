// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <filesystem>
#include <functional>

namespace UVE::Core {
class EngineCoreUVE;
}

namespace UVE::App {

/// What the platform hands the editor beyond its command line. A desktop process passes nothing:
/// the editor opens its own window and finds its data beside the executable. On Android the Hub
/// runs the editor in its own process (engine_install.h, kEditorAndroidEntryPointUVE) and passes the
/// activity's surface, the engine package's data folders and a step that drives the activity.
struct EditorPlatformUVE final {
    /// The surface to draw into instead of a window of the editor's own (Android).
    void* nativeSurface = nullptr;
    /// PRISM's fonts, themes and locales; empty: prism_assets beside the executable.
    std::filesystem::path prismAssetsDirectory;
    /// The engine's built-in shader sources; empty: the engine's default location.
    std::filesystem::path shaderDirectory;
    /// Persistent shader binary/cache directory; empty: engine default.
    std::filesystem::path shaderCacheDirectory;
    /// Folder for the editor's log when no project gives it one; empty: the working directory.
    std::filesystem::path logDirectory;
    /// Safe area insets {left, top, right, bottom} in PRISM logical units (cutouts + soft keyboard).
    std::function<std::array<float, 4>()> safeArea;
    /// Runs before every frame; returning false closes the editor.
    std::function<bool(Core::EngineCoreUVE&)> beforeFrame;
};

/// Runs the editor until it closes and returns the process exit code. `argv[0]` names the editor
/// executable (or library); the options are the editor's command line (see editor/main.cpp).
[[nodiscard]] int RunEditorUVE(int argc, char** argv, const EditorPlatformUVE& platform);

} // namespace UVE::App
