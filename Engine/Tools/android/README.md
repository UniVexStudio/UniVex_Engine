# Android native runtime

The NativeActivity entry point uses EngineCoreUVE, the EGL/GLES window backend and the
existing mobile input system. Its built-in validation scene contains a cube which changes
colour while the screen is touched. It is a runtime validation app, not the desktop editor.

Required on Linux: Java 17, Python 3, CMake 3.24+, Ninja, Android SDK platform 35,
build tools 35.0.0 and NDK 29.0.14206865. Install SDK components with `sdkmanager`.
NDK 29 is required for the floating-point charconv support used by the engine.

```sh
python3 Engine/Tools/android/build_apk.py --sdk /path/to/android-sdk --build-dir /tmp/univex-android --abi arm64-v8a
adb install -r /tmp/univex-android/univex-runtime-arm64-v8a.apk
adb shell am start -n com.univex.runtime/android.app.NativeActivity
```

Use `--abi x86_64` for an x86 emulator. `--native-build-dir` reuses an existing native
CMake build. `--dependency-source-cache` can point at another build's `_deps` directory
so a second ABI reuses downloaded FetchContent sources. Each ABI still needs its own build.
The script verifies APK signatures and 16 KB ZIP alignment. The generated debug key belongs
to this build directory. This is local test signing, not store-ready release signing.

Lifecycle behavior:

- Native event polling blocks while inactive; frames stop when paused or unfocused.
- Pause, focus loss and surface destruction cancel all active touch contacts.
- Surface destruction detaches into a 1x1 EGL pbuffer while retaining the context and engine.
- Surface recreation reattaches the native window and resumes the same scene.
- A lost/unrecoverable EGL context stops the runtime with a clear error. It does not pretend
  to restore game state after GPU context loss. Full context-loss state restoration remains
  a separate requirement.
- Application destroy shuts the engine down before releasing EGL and native-window references.
- Shader assets are copied into private application storage; no external-storage permission is requested.

GLES requires version 3.0. Optional compute paths require actual device capabilities.
Desktop polygon wireframe is refused on GLES. Buffer readback uses GLES map/unmap and
reports failure when either operation fails. Rendering targets are bounded to 720p pixels;
this is a resource policy, not an FPS guarantee or proof of 3 GB device readiness.

A reproducible smoke check is available for a booted test emulator/device:

```sh
python3 Engine/Tools/android/smoke_runtime.py --adb /path/to/adb --serial emulator-5554 --apk /tmp/univex-android/univex-runtime-x86_64.apk --report /tmp/android-smoke.json --screenshot /tmp/android-runtime.png
```

It requires actual GLES cube draws, touch receipt and engine consumption, pause/resume,
and surface reattachment without a second engine initialization. It records no device FPS claim.
The debug APK is debuggable for native diagnostics and uses private application storage.

CI builds arm64 and x86_64 APKs on every normal CI run and exercises x86_64 on a KVM emulator. Uploads follow the same opt-in artifact controls
as desktop CI (see `.github/CI_ARTIFACTS.md`). The manual checkbox enables an APK download.
