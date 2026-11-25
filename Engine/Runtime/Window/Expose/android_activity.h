// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

// What the UniVex activity (Engine/App/Android/java/com/univex/app/UniVexActivity.java) reports to
// native code about the screen it runs full screen on.

#include <optional>
#include <string>
#include <vector>

struct ANativeActivity;

namespace UVE::Window {

/// The edges of the surface a display cutout covers, in surface pixels. Interface laid out inside
/// them stays visible; the surface beyond them is still drawn (backgrounds, the game view).
struct AndroidSafeArea final {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int imeBottom = 0;

    friend bool operator==(const AndroidSafeArea&, const AndroidSafeArea&) = default;
};

/// Well-known Android external storage directories reported by UniVexActivity.
struct AndroidStoragePaths final {
    std::string publicStorageRoot;
    std::string publicDocuments;
    std::string publicDownloads;
    std::string appExternalDocuments;
};

/// Reads the safe area the activity last reported. All zero when the activity is not a UniVex
/// activity or Java cannot be reached. Call it from the app thread when the window or its layout
/// changes, not every frame: it attaches the thread to Java for the read.
[[nodiscard]] AndroidSafeArea ReadAndroidSafeArea(ANativeActivity* activity);

/// True while an external hardware mouse, touchpad, or non-virtual alphabetic keyboard is attached.
[[nodiscard]] bool HasAndroidHardwareInput(ANativeActivity* activity);

/// True when the app currently has permission to read/write shared public external storage.
[[nodiscard]] bool HasAndroidAllFilesAccess(ANativeActivity* activity);

/// Opens the Android All Files Access settings page (API 30+) or requests legacy storage access.
void RequestAndroidAllFilesAccess(ANativeActivity* activity);

/// Queries Android's external storage paths (public storage root, Documents, Downloads, and app external Documents).
[[nodiscard]] AndroidStoragePaths ReadAndroidStoragePaths(ANativeActivity* activity);

/// The character a key event types (android.view.KeyEvent.getUnicodeChar), or 0 for none (a
/// function key, or Java unreachable). Attaches the thread to Java for the call.
[[nodiscard]] unsigned int AndroidKeyCharacter(ANativeActivity* activity, int action, int keyCode, int metaState);

/// Opens Android's file picker (UniVexActivity.pickFile). False when the activity has none.
[[nodiscard]] bool StartAndroidFilePick(ANativeActivity* activity);
/// The outcome of the last pick: nothing while it runs, an empty string when it was cancelled,
/// "error:<reason>" when it failed, else the path of the picked document copied into the app's
/// cache. Returned once.
[[nodiscard]] std::optional<std::string> TakeAndroidPickedFile(ANativeActivity* activity);
/// Opens Android's multi-select file picker (UniVexActivity.pickFiles). False when unavailable.
[[nodiscard]] bool StartAndroidMultiFilePick(ANativeActivity* activity);
/// Opens Android's folder tree picker (UniVexActivity.pickFolder). False when unavailable.
[[nodiscard]] bool StartAndroidFolderPick(ANativeActivity* activity);
/// The outcome of the last multi-file or folder pick: nullopt while running, empty string when
/// cancelled, "error:<reason>" on failure, else '\x1E'-separated staged cache paths. Returned once.
[[nodiscard]] std::optional<std::string> TakeAndroidPickedPaths(ANativeActivity* activity);

/// Reads plain text from Android's system clipboard (UniVexActivity.getClipboardText), or empty
/// optional when the activity does not provide it.
[[nodiscard]] std::optional<std::string> ReadAndroidClipboard(ANativeActivity* activity);
/// Copies plain text to Android's system clipboard (UniVexActivity.setClipboardText).
void WriteAndroidClipboard(ANativeActivity* activity, const char* text);

/// Returns a .uvproject path passed to UniVexActivity via Intent extra "project" once, or empty.
[[nodiscard]] std::string TakeAndroidLaunchProject(ANativeActivity* activity);

/// Shows or hides the soft keyboard via UniVexActivity's IME InputConnection view. Returns false
/// if the activity is not a UniVexActivity (caller may fall back to ANativeActivity_showSoftInput).
[[nodiscard]] bool SetAndroidSoftKeyboard(ANativeActivity* activity, bool visible);

/// Drains queued IME events ("T:<utf8>" or "K:<androidKeyCode>") emitted by UniVexActivity's
/// InputConnection since the previous call.
[[nodiscard]] std::vector<std::string> DrainAndroidImeEvents(ANativeActivity* activity);

} // namespace UVE::Window
