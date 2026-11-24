# Android In-Process Host ABI Contract (v1)

Android applications do not spawn a second native GUI process. UniVex Hub (`com.univex.hub`, `libunivex_hub.so`) runs inside `com.univex.app.UniVexActivity` (`NativeActivity`), closes its own window when the user opens a project, loads the installed engine's `uve_editor` shared library with `dlopen`, and transfers control of `android_app*` to the editor until it returns.

## 1. C Entry Point (`univex_android_editor_abi.h`)

The Engine's Android editor library (`uve_editor` inside `univex-engine-*-android-<abi>.uvpackage`) exports:

```c
int UveEditorRunUVE(struct android_app* app, int argc, char** argv);
```

- Symbol name: `"UveEditorRunUVE"` (`UVE_ANDROID_EDITOR_ENTRY_SYMBOL`).
- `argv[0]` is the absolute path to the extracted `uve_editor` library inside the installed engine directory; the editor locates `prism_assets/` and `shaders/` beside `argv[0]`.
- Remaining `argv[1..argc-1]` follow the desktop launch contract (`--project <path>`, optional `--project-registry <path>`).
- Optional version symbol: `uint32_t UveEditorAbiVersionUVE(void)`. When exported, the Hub verifies it returns `UVE_ANDROID_EDITOR_ABI_VERSION` (`1`) before calling `UveEditorRunUVE`. When absent (for compatibility with v1 packages published before the optional symbol existed), the Hub treats the library as ABI v1.
- The Hub keeps the `dlopen` handle loaded for the remainder of the process (`RTLD_NOW | RTLD_LOCAL`) and never calls `dlclose`, because editor static destructors or worker threads may outlive return.

## 2. Strict ELF Symbol Isolation

Both `libunivex_hub.so` (in `UniVex_Hub`) and `uve_editor` (`libuve_editor.so`, in `UniVex_Engine`) statically link their own C++ runtime (`-static-libstdc++`), PRISM UI, and FreeType. To prevent ODR / global symbol collisions across `dlopen`:

- Compile with `-fvisibility=hidden -fvisibility-inlines-hidden`.
- Link with `-Wl,-Bsymbolic`, `-Wl,-z,max-page-size=16384` (16 KB page alignment), and a linker `--version-script` that hides all local symbols:
  - `libunivex_hub.so` exports only `ANativeActivity_onCreate`.
  - `uve_editor` exports only `UveEditorRunUVE` (and optional `UveEditorAbiVersionUVE`) plus NDK glue (`ANativeActivity_onCreate`).
- Verified in CI with `llvm-nm -D --defined-only`.

## 3. Android 14+ Safer DCL File Permissions

Before calling `dlopen` on an extracted `uve_editor` binary inside app-private storage, the Hub removes all write permission bits (mode `0555` / `0444`, non-writable by owner, group, and others). During uninstall or atomic replacement, owner write permissions are temporarily restored before deletion.

## 4. `UniVexActivity` Java / JNI Contract

`com.univex.app.UniVexActivity` (hosted in the Hub APK) exposes these stable fields and methods to both the Hub and the dynamically loaded Engine Editor via JNI:

- **Fields**: `safeInsetLeft`, `safeInsetTop`, `safeInsetRight`, `safeInsetBottom`, `imeInsetBottom`, `hardwareInputConnected`.
- **File & Folder Pickers (SAF)**: `pickFile()`, `takePickedFile()`, `pickFiles()`, `pickFolder()`, `takePickedPaths()`.
- **IME & Input**: `setSoftKeyboardVisible(boolean)`, `drainImeEvents()`, `getKeyCharacter(int, int, int)`.
- **Clipboard**: `getClipboardText()`, `setClipboardText(String)`.
- **Storage & Launch**: `takeLaunchProject()`, `getPublicStorageRoot()`, `getPublicDocumentsDir()`, `getPublicDownloadsDir()`, `getAppExternalDocumentsDir()`, `hasAllFilesAccess()`, `requestAllFilesAccess()`.
- **Orientation**: `android:screenOrientation="sensorLandscape"`.
- **Logcat smoke markers** (tag `UniVex`): `UVE_HUB_STARTED`, `UVE_HUB_RENDERED`, `UVE_HUB_EDITOR_STARTED`, `UVE_EDITOR_STARTED`, `UVE_EDITOR_RENDERED`, `UVE_EDITOR_STOPPED`, `UVE_HUB_EDITOR_STOPPED`.
