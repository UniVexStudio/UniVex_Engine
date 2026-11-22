// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// What the UniVex activity (Engine/App/Android/java/com/univex/app/UniVexActivity.java) reports to
// native code about the screen it runs full screen on.

#include <optional>
#include <string>

struct ANativeActivity;

namespace UVE::Window {

/// The edges of the surface a display cutout covers, in surface pixels. Interface laid out inside
/// them stays visible; the surface beyond them is still drawn (backgrounds, the game view).
struct AndroidSafeAreaUVE final {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;

    friend bool operator==(const AndroidSafeAreaUVE&, const AndroidSafeAreaUVE&) = default;
};

/// Reads the safe area the activity last reported. All zero when the activity is not a UniVex
/// activity or Java cannot be reached. Call it from the app thread when the window or its layout
/// changes, not every frame: it attaches the thread to Java for the read.
[[nodiscard]] AndroidSafeAreaUVE ReadAndroidSafeAreaUVE(ANativeActivity* activity);

/// The character a key event types (android.view.KeyEvent.getUnicodeChar), or 0 for none (a
/// function key, or Java unreachable). Attaches the thread to Java for the call.
[[nodiscard]] unsigned int AndroidKeyCharacterUVE(ANativeActivity* activity, int action, int keyCode, int metaState);

/// Opens Android's file picker (UniVexActivity.pickFile). False when the activity has none.
[[nodiscard]] bool StartAndroidFilePickUVE(ANativeActivity* activity);
/// The outcome of the last pick: nothing while it runs, an empty string when it was cancelled or
/// failed, else the path of the picked document copied into the app's cache. Returned once.
[[nodiscard]] std::optional<std::string> TakeAndroidPickedFileUVE(ANativeActivity* activity);

} // namespace UVE::Window
