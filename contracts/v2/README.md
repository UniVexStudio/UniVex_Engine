# UniVex Hub <-> Engine Contracts (v2)

UniVex Hub (`UniVexStudio/UniVex_Hub`) and UniVex Engine (`UniVexStudio/UniVex_Engine`) are built and released from separate repositories with no git submodule between them. These four documents define the versioned v2 boundary that both repositories test and preserve:

1. [Engine Feed & `.uvpackage` Contract](engine_package_contract.md) — `engine_feed.json`, release tags, `.uvpackage` / `.sha256` naming, `engine.json` manifest schema, and platform archive layout.
2. [`.uvproject` & Template Contract](uvproject_contract.md) — `.uvproject` JSON schema (including `rendererProfile`), `template.json`, and `starter_scene.uvstarter`.
3. [Desktop Launch Contract](desktop_launch_contract.md) — `uve_editor` CLI flags, version probing, and installation directory layout.
4. [Android In-Process Host ABI](android_host_abi.md) and [`univex_android_editor_abi.h`](univex_android_editor_abi.h) — `UveEditorRun` / `UveEditorAbiVersion` (v2) C ABI with backward-compatible v1 fallback, ELF symbol isolation, `UniVexActivity` JNI callbacks, and Android 14+ non-writable `.so` loading.

Breaking changes to any of these contracts require bumping the contract/schema version and updating both repositories together.
