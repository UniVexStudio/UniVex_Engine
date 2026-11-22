// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The activity side of a UniVex app on Android (the Hub, and the editor the Hub starts): it owns the
// activity's callbacks while attached, feeds touches and the surface to the GLFW stand-in
// (android_glfw.h), and pumps the activity's events between frames.

#include <cstdint>
#include <filesystem>
#include <string>

#include "android_activity.h"

struct android_app;
struct AAssetManager;

namespace UVE::Window {
class AndroidWindowManagerUVE;
}
namespace UVE::Core {
class EngineCoreUVE;
}

namespace UVE::AndroidHost {

/// Log tag every UniVex app on Android writes under (logcat -s UniVex).
inline constexpr const char* kLogTagUVE = "UniVex";
/// Written by build_apk.py: every file under the APK's assets/, one relative path per line.
inline constexpr const char* kAssetIndexUVE = "asset-index.txt";

/// What the attached app knows about its activity.
struct ActivityStateUVE final {
    android_app* app = nullptr;
    bool resumed = false;
    /// The running engine's window manager, once its loop has started.
    Window::AndroidWindowManagerUVE* windowManager = nullptr;
    /// The screen edges display cutouts cover, read again whenever the window or its layout changes.
    Window::AndroidSafeAreaUVE safeArea;
    /// Frames started since attaching; the first is on screen once the second starts.
    std::uint64_t framesStarted = 0;
};

/// Takes over the activity's callbacks for `state`, and tells this library's copies of the GLFW
/// stand-in and the HTTP client about the activity (Java VM, screen density, current surface).
void AttachUVE(ActivityStateUVE& state, android_app* app);

/// Waits until the activity has a surface. False once the activity is closing.
[[nodiscard]] bool WaitForWindowUVE(ActivityStateUVE& state);

/// Handles pending events; blocks while the app is in the background. False once the activity is
/// closing.
[[nodiscard]] bool PumpEventsUVE(ActivityStateUVE& state);

/// A per-frame step for LauncherWindowDescUVE::beforeFrame and the editor's loop: picks up the
/// engine's window manager, pumps events and reports the first drawn frame as `renderedMarker` in
/// logcat. False when the app should stop.
[[nodiscard]] bool BeforeFrameUVE(ActivityStateUVE& state, Core::EngineCoreUVE& core, const char* renderedMarker);

/// Lets go of the surface before the engine that drew to it shuts down.
void DetachEngineUVE(ActivityStateUVE& state);

/// Sets HOME and the working directory to the app's private storage, where the engine's relative
/// paths (settings, caches, logs) and the user's projects and engines live.
void EnterDataDirectoryUVE(const android_app* app);

/// Copies every asset the APK's index lists into `destination`, replacing what an older APK left.
[[nodiscard]] bool UnpackAssetsUVE(AAssetManager* assets, const std::filesystem::path& destination);

/// Keeps the activity alive until Android destroys it (after ANativeActivity_finish).
void DrainUntilDestroyedUVE(android_app* app);

} // namespace UVE::AndroidHost
