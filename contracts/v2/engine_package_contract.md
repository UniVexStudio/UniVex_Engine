# Engine Feed & `.uvpackage` Contract (v1)

## 1. Engine Feed Configuration (`engine_feed.json`)

Shipped beside the Hub executable (and unpacked into Android private app storage):

```json
{
  "format": "uvenginefeed",
  "schemaVersion": 1,
  "apiRoot": "https://api.github.com",
  "owner": "UniVexStudio",
  "repository": "UniVexReleases"
}
```

- `format` must equal `"uvenginefeed"`.
- `schemaVersion` must equal `1`.
- The Hub queries `{apiRoot}/repos/{owner}/{repository}/releases` without credentials (public feed).
- Draft releases are ignored. Prereleases map to the `beta` channel; non-prereleases map to `stable`.

## 2. Release Tag & Asset Naming

- Engine release tags use `v<major>.<minor>.<patch>[+build.<N>][-beta]` (for example `v0.1.2`, `v0.1.2+build.0-beta`).
- Hub releases use `hub-v<major>.<minor>.<patch>[+build.<N>][-beta]` so Hub and Engine tags never collide.
- Engine package assets follow `<prefix><displayVersion>-<target><extension>`:
  - Prefix: `univex-engine-`
  - Display version: `<major>.<minor>.<patch>` when `build == 0`, or `<major>.<minor>.<patch>+build.<N>` when `build > 0`
  - Targets: `linux`, `windows`, `android-arm64-v8a`, `android-x86_64`
  - Extension: `.uvpackage` (gzip-compressed ustar tar archive)
- Each package is paired with `<packageName>.sha256` containing its lowercase hex SHA-256 digest.

## 3. Package Manifest (`engine.json`)

Every `.uvpackage` archive contains `engine.json` at its root:

```json
{
  "format": "uvengine",
  "schemaVersion": 1,
  "version": "0.1.2+build.0",
  "channel": "beta",
  "target": "linux"
}
```

- `format`: `"uvengine"`
- `schemaVersion`: `1`
- `version`: canonical engine version string
- `channel`: `"stable"` or `"beta"`
- `target`: target identifier matching the archive platform

## 4. Platform Archive Contents

| Target | Required Root Entries | Notes |
| --- | --- | --- |
| `linux` | `engine.json`, `uve_editor`, `uve_runtime`, `uve_pack`, `prism_assets/`, `shaders/`, optional `lib/` | Non-system shared libraries bundled under `lib/` (`$ORIGIN/lib`). |
| `windows` | `engine.json`, `uve_editor.exe`, `uve_runtime.exe`, `uve_pack.exe`, `prism_assets/`, `shaders/`, `*.dll` | Required MinGW-w64 / MSYS2 runtime DLLs bundled beside `.exe`. |
| `android-arm64-v8a`, `android-x86_64` | `engine.json`, `uve_editor` (ELF shared library, mode `0555`), `libunivex_android.so` (mode `0555`), `prism_assets/`, `shaders/` | `uve_editor` is loaded in-process by UniVex Hub via `dlopen`. |

## 5. Installed Directory Layout

Installed engines are extracted atomically into `<userData>/UniVex/Engines/<version>/` (using `.staging/` inside `Engines/` during download and verification). On Android, extracted `.so` files are set read-only before `dlopen` to comply with Android 14+ Safer DCL.
