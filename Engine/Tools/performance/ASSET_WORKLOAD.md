# Cooked content and demand-loading measurements

Build `uve_pack`, `uve_runtime`, and `uve_asset_workload` with CMake. Run from the repository:

```sh
build/Engine/Runtime/Pack/uve_asset_workload --create-fixture /tmp/asset-source
build/Engine/Runtime/Pack/uve_pack --project /tmp/asset-source/sample.uvproject --runtime build/Engine/App/uve_runtime --output /tmp/asset-cooked --cook android
python3 Engine/Tools/performance/measure_process.py --runs 3 --timeout 120 --output /tmp/asset-rss.json -- build/Engine/Runtime/Pack/uve_asset_workload /tmp/asset-cooked/.uvassetdb /tmp/asset-cooked /tmp/asset-load.json
```

`--cook windows` caps texture axes at 2048 and defaults CPU asset residency to 256 MiB.
`--cook android` caps texture axes at 1024 and defaults residency to 128 MiB.
Raw native textures generate mipmaps; RGBA8 textures use portable UASTC when the encoder is built.
An encoder-free build writes valid raw mip chains. Already-compressed oversized assets decode before
resizing. HDR metadata and precision remain intact. Profiles never change GUIDs.

The cooker consumes **imported native assets**, not original model/image authoring formats. Run the
existing importer first. It validates the native assets, includes scripts/shader includes/application
resources, omits raw authoring files, and writes a relative `.uvassetdb` plus `cooked.json`.
Unused authoring-source registry entries are omitted; runtime GUIDs targeting those sources are rejected.
Registered missing native files or missing GUID dependencies fail the cook. All native assets are retained;
this is not unused-content reachability stripping or world partitioning. Output must be fresh.

Launch a relocated package with `uve_runtime --project game.uvproject`. The cooked catalog resolves
against the package root, independently of working directory or the original author's machine.
The engine's existing asynchronous `LoadUVE<T>(guid, database)` is the demand-loading API. Handles
pin data; duplicate requests share a record. Release all handles and collect to unload. A finite
budget rejects a candidate that cannot fit, leaving a failed handle/diagnostic; release unused
handles and call `ReloadUVE` (or release the failed handle, collect, then request it again).
Failed replacements keep last-known-good data. A loader under a finite budget must supply a size
callback. Built-in mesh, texture, shader, material, audio, animation and table loaders supply one.

Counted residency includes payload object/container capacities and nested allocations. Small-string
capacity is conservatively counted even when inline. It excludes allocator bookkeeping, transient
file/decode buffers, in-flight candidates, renderer/audio copies, GPU allocations, and other systems.
This is an **asset admission budget**, not a process RAM limit. Concurrent completions serialize
admission. Lowering the limit preserves pinned data and blocks further admissions until room exists;
unknown-sized live data must be released before enabling a finite limit.

The workload generates eight 2048x1024 native RGBA textures and a camera/cube scene. It loads and
releases textures sequentially under a 16 MiB budget, verifies zero pre-load/final residency, and
writes per-asset latency and peak counted bytes. `measure_process.py` separately measures child
process peak RSS using Linux wait4. Repeated runs are warm filesystem-cache measurements, not cold
storage throughput. Neither report certifies physical 3 GB hardware, frame time, GPU usage, or a
large real game. Run representative game scenes on actual reference hardware for those claims.

For Android:

```sh
python3 Engine/Tools/android/build_apk.py --sdk /path/to/sdk --build-dir /tmp/android-build --abi arm64-v8a --cooked-project /tmp/asset-cooked/sample.uvproject
```

The builder adds a bounded file index and cooked content. Native startup streams files into private
storage with a 64 KiB copy buffer, validates relative paths and extraction bounds, then prepares and
loads the packaged startup scene. No source project, network access or external-storage permission
is needed. Without `--cooked-project`, the existing demo remains available. APK signing is still the
local debug workflow; store/release signing is separate.

The portable Windows editor distribution also includes `uve_pack.exe`, `uve_runtime.exe` and
`uve_asset_workload.exe`. Cooked Windows exports copy runtime DLLs colocated with the supplied
runtime executable. Supply the runtime from that portable distribution (or stage its dependent
DLLs beside a custom-built runtime); the packager does not search arbitrary system directories.
Windows CI cooks the fixture and launches the exported runtime after relocating the source.
