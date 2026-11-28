# Engine/Plugins

The one plugin module, shared by the runtime and the editor. It sits beside `Runtime/` and `Editor/` and holds both kinds of extension:

| File | What it is |
|---|---|
| `native_plugin_host.{h,cpp}` | Loads a native plugin (a shared library with the `UveNativePluginEntry` C entry point) and calls its initialize / shutdown. |
| `plugin_registry.{h,cpp}` | Manifest registry and registration scopes for native plugins. |
| `plugin_manifest_validation.{h,cpp}` | ABI negotiation and capability policy checks for native plugin manifests. |
| `blueprint_plugin_api.{h,cpp}` | Blueprint extensions: API versioning, `IBlueprintPluginExtension`, `.uvbpplugin` manifests, dependency-ordered loading and custom node registration. |

`Tests/` holds the tests; `Tests/Fixtures/` the test plugin libraries built by `uvbt.py`.
