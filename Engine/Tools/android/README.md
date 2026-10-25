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

- Native event polling blocks while paused. A resumed surface draws before focus arrives;
  the engine applies its existing unfocused frame cap and input remains focus-gated.
- Pause, focus loss and surface destruction cancel all active touch contacts and clear
  gesture history, so an interrupted contact cannot become a tap.
- Surface destruction detaches into a 1x1 EGL pbuffer while retaining the context and engine.
- Surface recreation reattaches the native window and resumes the same scene.
- A lost/unrecoverable EGL context stops the runtime with a clear error. It does not pretend
  to restore game state after GPU context loss. Full context-loss state restoration remains
  a separate requirement.
- Application destroy shuts the engine down before releasing EGL and native-window references.
- Shader assets are copied into private application storage; no external-storage permission is requested.

GLES requires version 3.0. Optional compute paths require actual device capabilities.
Desktop polygon wireframe is refused on GLES. Android pthread affinity operations are
explicitly unsupported and the capability report reflects that; callers can choose a fallback. Buffer readback uses GLES map/unmap and
reports failure when either operation fails. Rendering targets are bounded to 720p pixels;
this is a resource policy, not an FPS guarantee or proof of 3 GB device readiness.

A reproducible smoke check is available for a booted test emulator/device:

```sh
python3 Engine/Tools/android/smoke_runtime.py --adb /path/to/adb --serial emulator-5554 --apk /tmp/univex-android/univex-runtime-x86_64.apk --report /tmp/android-smoke.json --screenshot /tmp/android-runtime.png
```

It requires actual GLES cube draws, touch receipt and engine consumption, and a pause/resume that
keeps the same engine - plus, when the platform does destroy the window on the way to the home
screen, that the same engine reattaches the surface instead of starting over. Android is allowed to
keep the window, and on this emulator it sometimes does, so the check is "detached implies
reattached" rather than "detached", and the resumed scene has to draw either way. It also requires a
screen that is not empty (see "A black screen" below), and `--label demo` names the run in the
failure annotation, which is how a red check says which of the two apks was black and why without
anybody opening the log.

CI does not build the apk. It is a runtime with no editor in it, and building it for two ABIs plus
watching it draw on an emulator cost about half an hour of every pull request - a price worth paying
when the mobile runtime is what you are working on, and not otherwise. Build it locally with the two
commands above and check it with `smoke_runtime.py`; both run anywhere the toolchain from the first
section is installed, and neither needs anything from CI.

## How an apk is built

There is no Gradle in this project. `build_apk.py` is the whole build, and it is worth reading in the
order it runs, because every step is a way an apk can come out wrong:

1. **Native library.** CMake with the NDK's own toolchain file, `ANDROID_ABI` set from `--abi`,
   `ANDROID_PLATFORM=android-28`, `UVE_BUILD_TESTS=OFF`, `CMAKE_BUILD_TYPE=Release`, building the one
   target `univex_android`. The result is `libunivex_android.so`, which the manifest names through
   `android.app.lib_name`.
2. **Assets.** `Engine/Runtime/RHI/Shader/built_in` is staged as `assets/shaders`, because the runtime
   loads its shader sources from files rather than from the binary. With `--cooked-project`, the
   project's `runtimeFiles` list is copied into `assets/project` and written to
   `assets/project-files.txt`, which is the index the runtime extracts at startup.
3. **Resources.** `aapt2 link` with `Engine/App/Android/AndroidManifest.xml` and the staged asset
   directory produces an unsigned apk. There is no Java source (`hasCode=false`); the activity is the
   platform's `android.app.NativeActivity`.
4. **Libraries.** The stripped `libunivex_android.so` and the NDK's `libc++_shared.so` are written into
   `lib/<abi>/`, and every staged asset that `aapt2` skipped (it drops dotfiles such as `.uvassetdb`)
   is added to the archive. A missing cooked file fails the build here rather than the phone later.
5. **Alignment and signature.** `zipalign -P 16` for 16 KB page alignment, a generated debug keystore
   (`android`/`android`) made with `keytool` if absent, then `apksigner sign` and `apksigner verify`.
   The key belongs to the build directory: this is local test signing, not a release signature.

The output is `<build-dir>/univex-runtime-<abi>.apk`. Anything that is not that file - a stale
`--build-dir`, the other ABI's library, an unaligned archive - is a bug in the packaging step, which
is why the workflow unpacks both apks and checks that `lib/<abi>/libunivex_android.so` is the one
inside.

## A black screen

"Installed the apk and the phone shows nothing" has exactly one debugging surface on Android: logcat.
Every engine log line is written to logcat under the tag `UniVex` (`ConsoleSinkUVE` routes to
`__android_log_write` on Android; a process's stdout does not appear in logcat, so the desktop
`std::cout` path is invisible there). Read it with:

```sh
adb logcat -d -s UniVex:V '*:S'
```

The markers in `main.cpp` say how far startup got:

- `UVE_ANDROID_INIT_BEGIN` / `UVE_ANDROID_INIT_DONE` - the engine and its GLES context exist.
- `UVE_ANDROID_LOAD_DONE` - resources and scene loading finished.
- `UVE_ANDROID_COOKED_PROJECT_LOADED` - the packed project launched (cooked apks only).
- `UVE_ANDROID_INITIALIZED`, then `UVE_ANDROID_RENDERED` - the first frame was presented.
- `UVE_ANDROID_FAILED <reason>` - startup stopped, and the string after it is why. The window stays
  black after this line by definition, because there is no engine left to draw into it.
- `UVE_ANDROID_COOKED_PROJECT_REJECTED <reason>` - the packed project could not be prepared or
  activated. The runtime does not die on this: it falls back to its own touch-cube scene, so the
  phone shows something and the difference between "the runtime is broken" and "the content is
  broken" is visible both on the screen and in the log. On a cooked apk the smoke script treats this
  as a failure, because the apk exists to prove the project loads.

The first marker missing tells you which stage failed. If `UVE_ANDROID_FAILED` never appears and
`UVE_ANDROID_RENDERED` does, the engine is drawing and the frame is empty or invisible: no camera,
geometry outside the frustum, or a clear colour the device shows as black. If `UVE_ANDROID_FAILED`
appears before `UVE_ANDROID_INITIALIZED`, it is startup - missing shader assets, or a GLES driver that
would not give the engine a usable context or config.

Two builds exist because those two failures are indistinguishable from a phone:

```sh
# The runtime's own touch-cube scene. Black here means the runtime or the driver.
python3 Engine/Tools/android/build_apk.py --sdk "$ANDROID_HOME" --build-dir /tmp/univex-demo --abi arm64-v8a

# The same runtime with Samples/CookedRuntimeSmoke packed in. Renders here, black in the demo?
# That is the content, not the engine.
python3 Engine/Tools/android/build_apk.py --sdk "$ANDROID_HOME" --build-dir /tmp/univex-cooked \
    --native-build-dir /tmp/univex-demo/native \
    --cooked-project Samples/CookedRuntimeSmoke/sample.uvproject --abi arm64-v8a
```

`smoke_runtime.py` fails a run whose device screen has no content, and greps for `UVE_ANDROID_FAILED`.
Brightness alone would not catch it - a frame containing only the clear colour is uniformly dark but
not black - so the check asks for contrast across a 10x10 sample grid: a frame with geometry in it
has pixels that differ from the background, an empty one does not. That check is what turns "the
phone is black" into a failed run with a reason instead of another install.

## Cooked project content

`build_apk.py --cooked-project /path/to/cooked/game.uvproject` embeds the exact runtime file index
produced by `uve_pack --cook android`. Native startup streams those files into private storage and
loads the manifest's startup scene with its rooted GUID catalog. The camera/material/texture content
comes from the project. Without that option, the original touch demo remains available.

Application saves, logs, shader cache and derived data use the native sandbox's separate
`user-data` directory, so replacing extracted cooked content does not erase saved data. Extraction
is bounded to 512 MiB per file, 1 GiB in total and 100,000 files, with relative-path validation.

Use `smoke_runtime.py --expect-cooked-project` with the generated camera/cube workload fixture to
require evidence that the actual project launched. The smoke still verifies rendered pixels, native
touch consumption and pause/surface recovery. It is a correctness check, not physical-device
memory or frame-rate certification. See `Engine/Tools/performance/ASSET_WORKLOAD.md` for the
cooking profiles, residency budget contract and repeatable RAM/loading measurements.
