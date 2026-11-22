// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// The activity side of a UniVex app on Android (the Hub, and the editor the Hub starts): it owns the
// activity's callbacks while attached, feeds touches and the surface to the GLFW stand-in
// (android_glfw.h), and pumps the activity's events between frames.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

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

/// Touch gestures, turned into what the editor and PRISM read from a mouse: one finger presses the
/// left button immediately on ACTION_DOWN for instant UI press feedback (cancelled cleanly if held
/// still into a long-press right click or joined by a second finger), two+ fingers drag with the
/// middle button (Pan) while pinching smoothly sends continuous wheel deltas (Dolly Zoom).
struct TouchGestureUVE final {
    int fingers = 0;
    /// GLFW mouse button the gesture holds down (-1: none yet).
    int button = -1;
    /// True while one finger has stayed inside touch slop and may still fire a long-press right click.
    bool longPressArmed = false;
    /// The gesture ended early (long press fired or fingers lifted from a multi-finger gesture); the rest is ignored.
    bool finished = false;
    float startX = 0.0F;
    float startY = 0.0F;
    float lastX = 0.0F;
    float lastY = 0.0F;
    float pinchDistance = 0.0F;
    std::int64_t startMs = 0;
};

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
    TouchGestureUVE touch;
    /// The system Back key acts as Escape (the editor: it closes menus and dialogs) instead of
    /// leaving the app (the Hub).
    bool backIsEscape = false;
    /// The soft keyboard is up because the interface asked for typed text.
    bool keyboardShown = false;
    /// Remaining frames to keep draining the IME queue after keyboardShown turns false.
    int imeDrainTailFrames = 0;
    /// Timestamp (steady ms) of the last touch, key, IME, or lifecycle interaction (for smart idle).
    std::int64_t lastInteractionMs = 0;
    /// Surface pixels per density-independent pixel (density / 160 dpi).
    float pixelsPerDp = 1.0F;
    /// A system file pick in progress (StartFilePickUVE): what to call with the file, what to call
    /// when a pick fails or is rejected, and the name endings it accepts.
    std::function<void(const std::filesystem::path&)> pendingPick;
    std::function<void(const std::string&)> pendingPickError;
    std::vector<std::string> pickSuffixes;
};

/// Opens Android's file picker; `onPicked` runs (from BeforeFrameUVE) with the chosen file, copied
/// into the app's cache, when it matches `suffixes` (or `onError` when the pick fails or the file
/// is not an accepted package). False when no picker opened.
[[nodiscard]] bool StartFilePickUVE(ActivityStateUVE& state, const std::vector<std::string>& suffixes,
                                    std::function<void(const std::filesystem::path&)> onPicked,
                                    std::function<void(const std::string&)> onError = {});

/// Takes over the activity's callbacks for `state`, and tells this library's copies of the GLFW
/// stand-in and the HTTP client about the activity (Java VM, screen density, current surface).
void AttachUVE(ActivityStateUVE& state, android_app* app);

/// Waits until the activity has a surface. False once the activity is closing.
[[nodiscard]] bool WaitForWindowUVE(ActivityStateUVE& state);

/// Handles pending events; blocks while the app is in the background. When `idleWaitMs > 0` and
/// no recent interaction is active, waits up to `idleWaitMs` ms for the next event (waking
/// immediately on any touch, key, or lifecycle event). False once the activity is closing.
[[nodiscard]] bool PumpEventsUVE(ActivityStateUVE& state, int idleWaitMs = 0);

/// A per-frame step for LauncherWindowDescUVE::beforeFrame and the editor's loop: picks up the
/// engine's window manager, pumps events and reports the first drawn frame as `renderedMarker` in
/// logcat. False when the app should stop.
[[nodiscard]] bool BeforeFrameUVE(ActivityStateUVE& state, Core::EngineCoreUVE& core, const char* renderedMarker,
                                  int idleWaitMs = 0);

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
