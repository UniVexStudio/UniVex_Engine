# Deep audit — bugs, errors, and unwired code

Date: 2026-10-03. Branch `arena/01a0ffb2-univex-engine` at `6fa4679`.
Every number below came from a command run against this tree; the commands are quoted.

---

## 1. The subset build had never applied the project's own warning flags

`Engine/CMake/UveCompilerWarnings.cmake` gives every target a strict set — `-Wall -Wextra
-Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wnon-virtual-dtor -Woverloaded-virtual
-Wold-style-cast -Wcast-align -Werror`. The audit subset driver applied **none** of it:

```
grep -n "uve_set_warnings" /tmp/subset/CMakeLists.txt   → no match
```

So the 229 translation units the subset compiles had never been warning-checked at all. Added the
project set (minus `-Werror`, so everything is visible) plus 15 checks the project does not enable:
`-Wduplicated-cond -Wlogical-op -Wnull-dereference -Warray-bounds=2 -Wstrict-overflow=4 -Wundef
-Wcast-qual -Wredundant-decls -Wextra-semi -Wformat=2 -Wsuggest-override -Wuseless-cast
-Wdouble-promotion -Walloc-zero -Walloca -Wduplicated-branches -Wrestrict -Wstringop-overflow=4
-Wtrampolines -Wvector-operation-performance -Wplacement-new=2`.

Confirmed the flags really landed, not silently dropped:

```
grep -o "Wstrict-overflow=4\|Wduplicated-cond\|Wsuggest-override\|Wuseless-cast" \
     CMakeFiles/uve_audit_subset_tests.dir/flags.make   → all four present
```

Full clean rebuild (`--clean-first`):

```
1 warning in 229 translation units.
```

The one warning is benign — a `-Wstrict-overflow` note on `kDepth - i` in
`Test/Integration/Scene/scene_graph_uve_tests.cpp:379`, the compiler stating an optimisation
assumption about a test loop bound. Not a defect.

**Verdict: warning-clean.**

---

## 2. ASan + UBSan: 341 tests, zero findings

Built the whole subset with `-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1` in a
separate build directory, then ran every test:

```
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=0 \
ASAN_OPTIONS=detect_leaks=1:abort_on_error=0 ./uve_audit_subset_tests

[==========] 341 tests from 44 test suites ran.
[  PASSED  ] 341 tests.
grep -cE "runtime error:|ERROR: AddressSanitizer" → 0
```

No undefined behaviour, no out-of-bounds, no use-after-free, no leaks on any path the tests reach.

**Caveat, stated plainly:** this covers what the tests exercise. Code with no test — the 24
translation units that cannot even compile in this sandbox, and any untested branch inside a tested
file — is not covered by this result.

---

## 3. Unwired code

Two scans over every `Engine/**/Expose/**/*.h`, counting which other files mention each declared
symbol. Both scans were refined after the first pass produced false positives; the definitions used
here are the ones that survived checking.

### 3a. Orphan declarations — **10** (originally reported as 44; that figure was wrong)

> **Correction, 2026-10-03.** The original figure of 44 came from a scan that excluded the
> declaring header from its own hit count. That silently misreported every symbol whose only use
> is *inside its own header*, which is a normal and correct thing for a header to do. Two verified
> examples:
>
> - `BufferTagUVE` — used as the template argument on `buffer_handle_uve.h:15`
>   (`using BufferHandleUVE = ResourceHandleUVE<BufferTagUVE>;`)
> - `MakeDefaultLightUVE` — called by the default member initializer on `light_3d_uve.h:30`
>
> Re-running with the rule "an orphan is a symbol whose **only** occurrence in the whole repo is
> its own declaration" gives **10**. The table below is the original, superseded list; the
> corrected list is the one that follows it.

#### Corrected list — 10, each verified by `grep` to have exactly 1 hit repo-wide

| Symbol | Header | What it is |
|---|---|---|
| `IsCommandPaletteOpenUVE` | `editor/editor_uve.h` | inline accessor, public editor API |
| `IsEditorPreferencesOpenUVE` | `editor/editor_uve.h` | inline accessor, public editor API |
| `IsInputMapOpenUVE` | `editor/editor_uve.h` | inline accessor, public editor API |
| `IsProjectSettingsOpenUVE` | `editor/editor_uve.h` | inline accessor, public editor API |
| `UnifiedTimeAdvanceResultUVE` | `animation/time_pose_contract_uve.h` | public type |
| `GetMaximumSamplesUVE` | `audio/wav_pcm16_decoder_uve.h` | accessor, public API |
| `EvaluateGamepadConnectionTransitionUVE` | `input/i_gamepad_input_system_uve.h` | adapter-facing helper |
| `GetMutableUVE` | `object/type_metadata_uve.h` | accessor, public API |
| `GetUniformsUVE` | `rhi_shader/shader_program_uve.h` | accessor, public API |
| `GetStageUVE` | `rhi_shader/shader_source_uve.h` | accessor, public API |

These are **public API with no in-tree consumer**, which is the normal shape for an engine
library: the consumer is game code outside this repo. Note the four `editor_uve.h` accessors have
no matching `Set*OpenUVE` at all (0 hits), so they are not half of an unfinished pair. This is not
a defect and no wiring is owed.

#### Superseded original list — do not act on this

| Symbol | Header |
|---|---|
| `AnimationDriverObjectDefinitionUVE` | `Objects/Animation/.../abstract_animation_objects_3d_uve.h` |
| `BoneModifier3DObjectDefinitionUVE` | same |
| `LightEmitter3DObjectDefinitionUVE` | `Objects/3D/.../abstract_objects_3d_uve.h` |
| `PhysicsObject3DObjectDefinitionUVE` | `Objects/3D/Physics/.../abstract_physics_objects_3d_uve.h` |
| `RenderInstance3DObjectDefinitionUVE` | `Objects/3D/.../abstract_objects_3d_uve.h` |
| `SolidBody3DObjectDefinitionUVE` | `Objects/3D/Physics/.../abstract_physics_objects_3d_uve.h` |
| `SurfaceInstance3DObjectDefinitionUVE` | `Objects/3D/.../abstract_objects_3d_uve.h` |
| `UnifiedTimeAdvanceResultUVE` | `Animation/.../time_pose_contract_uve.h` |
| `EvaluateGamepadConnectionTransitionUVE`, `GamepadConnectionTransitionUVE` | `Input/.../i_gamepad_input_system_uve.h` |
| `GetFallbackLocaleUVE` | `Localization/.../localization_uve.h` |
| `IsLoopingUVE`, `GetMaximumSamplesUVE` | `Audio/.../wav_pcm16_decoder_uve.h` |
| `IsFiniteScalarUVE` | `Core/Math/.../plane_uve.h` |
| `LogToActiveInstanceUVE` | `Core/Logging/.../logging_macros_uve.h` |
| `MakeDefaultBodyUVE`, `MakeDefaultColliderUVE`, `MakeDefaultLightUVE` | `Objects/3D{,/Physics}` |
| `PrefabOverrideConflictUVE` | `Component/.../prefab_instance_component_uve.h` |
| `ReleaseIfOwnedUVE` | `Asset/.../asset_handle_uve.h` |
| `NativePluginManifestDiagnosticUVE`, `NativePluginVersionUVE` | `Plugins/...` |
| `IsSceneRootObjectDefinitionValidUVE` | `Scene/.../scene_root_uve.h` |
| `GetMutableUVE`, `TypeMetadataNumericRangeUVE` | `Objects/Core/.../type_metadata_uve.h` |
| `TextureFormatBlockInfoUVE`, `TextureFormatBytesPerPixelUVE`, `IsVertexAttributeFormatValidUVE` | `RHI/.../render_resource_descs_uve.h` |
| `BufferTagUVE`, `PipelineTagUVE`, `ShaderTagUVE`, `TextureTagUVE` | `RHI/.../*_handle_uve.h` |
| `EditorProjectVersionUVE` | `Platform/.../editor_project_package_uve.h` |
| `GetStageUVE`, `GetUniformsUVE` | `RHI/Shader/...` |
| `InvokeForEachCallbackUVE` | `Entity/.../i_entity_manager_uve.h` |
| `IsCommandPaletteOpenUVE`, `IsEditorPreferencesOpenUVE`, `IsInputMapOpenUVE`, `IsProjectSettingsOpenUVE`, `Progress` | `Editor/EditorCore/.../editor_uve.h` |
| `IsAxisChannelValidUVE`, `IsAxisRgbValidUVE`, `StudioColor` | `Editor/Viewport/...` |

Most are accessors or tag structs that a later increment would use. They are not bugs, but they are
public surface that nothing exercises, so nothing protects them from rot.

### 3b. Tested but never wired — 57

Declared public, exercised by a test, **zero production callers**. Grouped by header:

| Header | count | examples |
|---|---|---|
| `Editor/EditorCore/.../editor_uve.h` | 15 | `GetViewportGridCellSizeUVE`, `GetColorPickerPreferencesUVE`, `GetSettingsRegistryUVE` |
| `Runtime/Input/.../i_mobile_input_system_uve.h` | 7 | `EvaluateMobileInputPolicyUVE`, `EvaluateTouchLifecycleTransitionUVE`, `MobileLifecycleStateUVE` |
| `RHI/RenderSystems/.../compute_dispatch_desc_uve.h` | 5 | `MakeComputeUniformBoolUVE`, `...FloatUVE`, `...IntUVE`, `...Matrix4x4UVE`, `...Vector3UVE` |
| `Runtime/.../core/engine_core_uve.h` | 3 | `GetConfigUVE`, `GetLastCollisionLifecycleReportUVE`, `GetLocalizationServiceUVE` |
| `Localization/.../localization_uve.h` | 3 | `GetActiveLocaleUVE`, `GetStringTableCountUVE`, `GetTranslationCountUVE` |
| `Network/.../reliable_packet_window_uve.h` | 3 | `GetElapsedSecondsUVE`, `GetRetryCountUVE`, `IsConfiguredUVE` |
| `Objects/Core/.../type_metadata_uve.h` | 3 | `GetPropertyValueUVE`, `SetPropertyValueUVE`, `IsSerializedUVE` |
| `RHI/.../render_resource_descs_uve.h` | 3 | `CalculateTextureUploadByteCountUVE`, `GetTextureMipExtentUVE`, `MaximumTextureMipLevelCountUVE` |
| `Audio/.../wav_pcm16_decoder_uve.h` | 2 | `GetCursorSampleUVE`, `GetTotalSamplesUVE` |

The whole `i_mobile_input_system_uve.h` policy layer and the whole reliable-packet retry layer are
in this bucket: written, tested, never called.

### 3c. A concrete duplication with drift risk — the 7 abstract base definitions

The 69-member `*ObjectDefinitionUVE` family splits cleanly:

```
62 types → 3-7 files reference them   (declaration + Apply function + registry + test)
 7 types → 1 file: their own header only
```

Those 7 are the abstract bases (`RenderInstance3D`, `SurfaceInstance3D`, `LightEmitter3D`,
`PhysicsObject3D`, `SolidBody3D`, `BoneModifier3D`, `AnimationDriver`). Their header comment
explains the intent — *"None of these abstract kinds is created directly"* — and the `Apply*BaseUVE`
**functions** beside them are very much alive (4, 8 and 4 files respectively).

But each struct exists only to hold a `typeName` constant, and **nothing reads it**. The same
string is typed a second time as a literal in `scene_component_metadata_uve.cpp`:

```
abstract_objects_3d_uve.h:32         typeName = "LightEmitter3D";
scene_component_metadata_uve.cpp:984     "component.light_emitter", "LightEmitter3D", ...
```

All 7 pairs were checked and **currently agree** — so this is a latent drift risk, not a live bug.
An earlier pass of mine reported 4 of them as drifted; that was my own regex failing on the
multi-line `MakeEntryUVE(` calls. They are in sync.

The fix is small: have the metadata table read `LightEmitter3DObjectDefinitionUVE::typeName`
instead of restating the string, which wires the structs up and makes drift impossible.

---

## 4. What this audit could not reach

24 translation units cannot be compiled in this sandbox — missing `GL/glew.h`, `miniaudio.h`,
`vulkan/vulkan_core.h`, `stb_truetype.h`, a generated `.inc`, and a few Viewport/Shader headers with
relative includes. `deb.debian.org` is blocked, so the headers cannot be installed.

```
357 non-built TU  →  333 syntax-clean   24 blocked   0 real errors
```

None of the 24 is a file this branch modified (checked with `comm` against the 148-file change list).
They are unverified, not known-bad.

---

## 5. Recommended, in order

1. **Wire the 7 abstract `typeName` constants into `scene_component_metadata_uve.cpp`.** Removes a
   real duplication, makes the 7 orphan structs live, and prevents a name drifting between the
   object definition and the inspector.
2. **Do not wire the mobile-input or reliable-packet layers.** Both were reported here as "tested,
   coherent, zero callers — wire up or remove." Checking the actual code shows neither is an
   unfinished integration:
   - The mobile `Evaluate*` helpers are written for Android/iOS adapters, and
     `Engine/Runtime/Window/CMakeLists.txt:6-7` records that the Android backend was deliberately
     not ported. `find . -iname "*android*"` returns nothing.
   - `Engine/Runtime/Network` is three files and the engine contains **no socket code at all**
     (`grep -rlE "socket\(|sendto\(|recvfrom\(|sys/socket.h" Engine` returns nothing). The module
     is a pure wire-format library; its own doc comments say it owns no socket, timer, peer, or
     transport lifecycle.
   Both headers now carry a scope-boundary comment saying so, added in the same pass as this
   correction.
3. **No orphan-accessor sweep is owed.** The 44 figure was a scanning error; the real count is 10,
   and all 10 are public API for consumers outside this repo. Leave them alone.
