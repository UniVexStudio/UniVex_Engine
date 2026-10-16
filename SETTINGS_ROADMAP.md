# UniVex Engine — Settings Roadmap

This document catalogues **every setting surface a production game engine needs**, from the
substrate that stores and validates a setting, up through project settings, editor preferences,
the colour picker, material/texture settings, and asset import options.

It is a *planning* document. Almost nothing here is implemented. The point is that the complete
target is written down in one place, honestly marked, so that work can be picked up in dependency
order instead of by whichever page looks most fun to build.

## How to read this document

This file uses the same four status markers as `ROADMAP.md`, `FOUNDATION.md` and
`SCENE_NODES_ROADMAP.md`:

- `[ ]` = not started, or only a stub/placeholder exists.
- `[~]` = partially implemented — a foundation exists but the feature is not complete. The note
  after the item says what is missing.
- `[/]` = **wired, not fully verified** — the implementation is present and source-reviewed, but
  dedicated tests are missing or could not be run in the available build configuration.
- `[x]` = **verified** — real and working, confirmed by reading the source (not assumed), and
  backed by dedicated tests that lock more than one case.

The overwhelming majority of items below are `[ ]`. That is not pessimism; it is the accurate
state of the codebase today, and the whole reason this roadmap exists.

**Naming policy.** Consistent with the other planning documents in this repository, no
third-party engine or product is named here. Where an item exists because mature engines have
converged on it, that is stated as "the established convention" and described on its own terms.

## Baseline state when this roadmap was first drafted (historical)

The following inventory was confirmed when this document was first written. It is retained as
historical context, **not as a current source audit**; several statements have since been
superseded by the implementation tracked in Part 0. The remaining roadmap still needs a
systematic source audit before it can be treated as a complete present-day status report.

**The settings store is a bare JSON dictionary.** `IConfigManagerUVE`
(`Engine/Runtime/Config/Expose/uve/config/i_config_manager_uve.h`) offers exactly four scalar
types — string, `int64`, double, bool — addressed by dot-separated path, plus `HasKeyUVE`, load
and save. There is **no schema, no defaults registry, no validation, no range metadata, no
categories, no change notification, no key enumeration, and no removal**. Its own header notes
that the root `version` field is reserved as a forward-compatibility convention but that "this
increment implements no migration logic against it."

**Editor preferences are sixteen hand-validated keys.** The keys written under `editor.` by
`Engine/Editor/EditorCore/Internal/editor_uve.cpp` are: panel visibility (4), active bottom dock,
active right-panel tab, active workspace, the three snap steps plus a snap-enabled flag, the axis
colours, the favourites list and its count, and a session-settings version. Every one of them is
validated at its own call site, in its own way. **There is no preferences window** — there is a
single "Save Editor Preferences" menu item and the axis-colour submenu.

**Project settings do not exist.** `EngineConfigUVE` is a struct of compile-time defaults
(target FPS, fixed-update rate, max delta time, log level and log path, thread-pool size, settings
and asset-database paths, project root) with a handful of command-line overrides. The `.uvproject`
file is a path-locator manifest, not a settings document. There is no project-settings file, no
project-settings UI, and no per-platform override mechanism.

**Import settings are a hook nothing uses.** `AssetImportSettingsUVE`
(`Engine/Runtime/Asset/Expose/uve/asset/i_asset_importer_uve.h`) is passed to every importer, and
the real importers — glTF, PNG, JPEG, BMP, MTL and the rest — take it as a *commented-out
parameter name*. Only text files and data tables define derived settings types. There are no
per-asset sidecar files, so an import option could not be persisted even if an importer read one.

**Material settings are a third of a PBR material.** `MaterialAssetUVE` has nine fields:
`albedoColor`, `albedoTexture`, `normalTexture`, `metallic`, `roughness`, `aoTexture`,
`emissiveColor`, two shader GUIDs, and `isTransparent`. There are **no metallic, roughness or
emissive texture slots**, `albedoColor` is a `Vector3UVE` so the material has **no alpha channel
at all**, and its own comment records that `isTransparent` is "not consumed by anything yet."

**Every colour in the editor is the same three-channel RGB row.** There are ten
`ImGui::ColorEdit3(..., ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB)` calls across
the inspector and the menu dock, and **zero** uses of `ColorEdit4`, any `ColorPicker`, or
`ColorButton`. `SetColorEditOptions` is never called, which means the default picker options
apply everywhere — and those pin the hue **bar**. The vendored UI library already ships a hue
**wheel**, an alpha bar, and HSV display/input modes. None of them are reachable today.

**Layers are unnamed bits.** `collisionLayer` and `collisionMask` are raw `uint32_t` on the
collider and area components, defaulting to `1` and `0xFFFFFFFF` respectively, and the scene
serializer uses those same two defaults consistently. What is missing is not consistency — it is
that **no layer has a name anywhere in the engine**, so every layer UI would be thirty-two
unlabelled checkboxes.

**The input map has a model and no surface.** `InputSystemUVE::RemapActionUVE` exists and is
covered by unit tests in `Test/Input/input_system_uve_tests.cpp`, but it has **no production
caller** — nothing in the editor or runtime rebinds an action. There is no input-map asset format
and no input-map UI.

### The conclusion that ordered the original roadmap

When this roadmap was drafted, the settings substrate did not exist. Building the several hundred
settings pages first would have meant hand-writing validation, defaults, persistence and UI at
each call site — precisely the pattern the original sixteen editor keys demonstrated at small
scale.

That is why **Part 0 comes first** and is written in more engineering detail than the later parts.

---

# Part 0 — The settings substrate

Nothing else in this document can be built well until this exists. It is deliberately described
in more detail than any later part.

## 0.1 What is wrong with the current shape

`IConfigManagerUVE` is a good *storage* layer and should be kept. What it is missing is a
*description* layer. Today a setting is only a string path that someone happened to write, and
everything else about it — its type, its default, its legal range, its label, whether it needs a
restart — lives in the head of whoever wrote the call site, or nowhere.

Concretely, reading a snap step today means: read a double with a fallback, check it is finite,
check it is positive, and if not, substitute a literal. That logic is correct where it is written,
and it is written once per setting. Multiply by four hundred settings and the cost is obvious.

## 0.2 The setting descriptor

- [x] `SettingDescriptorUVE` — the single record describing one setting. Landed in
      `Engine/Runtime/Config` (`setting_descriptor_uve.h`) with bool, int, float, string, enum,
      layer-mask, colour, vector2/3/4, file-path, asset-reference, key-binding and bounded
      string-list descriptors plus `sinceVersion` and `replacementId` migration metadata.

  - [x] `id` — the dot path, e.g. `rendering.shadows.softShadowQuality`. The storage key.
  - [x] `type` — bool, int, float/double, string, enum, layer mask, colour, vector2, vector3,
        vector4, file path, asset reference, key binding, and bounded string lists are supported.
  - [x] `defaultValue` — the engine default, typed. The single source of "reset to default".
  - [x] `minimum` / `maximum` / `step` — for numeric types; absent means unbounded.
  - [x] `enumEntries` — ordered (value, label) pairs for enum types, so a combo box needs no
        hand-written label table.
  - [x] `displayName` and `tooltip` — the human-facing strings. Without these, a generic settings
        panel can only show raw dot paths.
  - [x] `category` / `page` — where it appears in the settings tree.
  - [x] `flags` — `RestartRequired`, `Advanced` (hidden behind a toggle), `Hidden`,
        `PerPlatform` (may be overridden per target), `NotPersisted` (session-only),
        `Internal` (never shown in UI), `Deprecated` (read for migration, never written).
        `Hidden` covers `Internal`: both meant "stored and read, never shown".
  - [x] `sinceVersion` / `migratedFrom` (`replacementId`) — supports the migration path in 0.8.

The descriptor is **data**, declared next to the system that owns the setting, not centralised in
one giant file. A rendering setting is declared by the rendering module; the registry merely
collects them.

## 0.3 The registry

- [x] `SettingsRegistryUVE` — collects descriptors, keyed by id, with duplicate-id detection at
      registration time (a duplicate is a programming error and should fail loudly in a test, not
      silently shadow). It also refuses ids that would nest (`a.b` and `a.b.c`), which the JSON
      document cannot hold.
- [x] Registration is explicit and ordered, following the pattern already used by
      `RegisterBuiltInInspectorDrawersUVE()` — a real, extensible, string-keyed registry that this
      codebase already proves works.
- [x] A test that asserts every registered id is unique, every default satisfies its own
      declared range, and every enum default is one of the declared entries. This one test
      removes an entire class of bug from all four hundred settings at once. Registration refuses
      such descriptors, and `editor_settings_uve_tests.cpp` runs the check over the editor's real
      declarations; each module that declares settings adds the same test.

## 0.4 Typed access over the existing store

- [x] Typed getters that take a descriptor (or id) and return the value already clamped and
      validated, with the declared default substituted for anything missing or malformed.
      `ConfigManagerUVE` remains underneath as the JSON document; nothing about it is replaced.
- [x] Setters that reject out-of-range values rather than storing them, so a bad value can never
      enter the document in the first place.
- [x] **Whole-or-nothing application for composite settings.** Colours, `Vector2`, `Vector3`,
      `Vector4`, key bindings, and bounded string lists are validated and read/written as atomic
      units (`SettingsRegistryUVETest.Vector2Vector4AssetReferenceLayerMaskAndSinceVersionMaintainWholeOrNothingSemantics`).

## 0.5 Validation at the boundary

- [x] Every value crossing the file boundary is validated once, at the registry, against its
      descriptor. Consumers downstream receive values that are already legal and stop carrying
      defensive checks.
- [x] Non-finite values (NaN, infinity) fail range checks by construction — a comparison against
      a bound is already false for NaN, which is the idiom the axis-palette validator uses.
- [x] A corrupt or hand-edited settings file degrades to defaults **per setting**, never
      discarding the whole document.

## 0.6 Change notification

- [x] An observer registration keyed by id or by category prefix, so consumers can react without
      polling and without the settings layer knowing what they are. The shared
      `SettingsObserverHubUVE` now backs both `SettingsDocumentUVE` and `EditorUVE`; document
      settings and editor preferences use the same exact-id and slash-boundary category path.
- [x] Observers fire only when the effective value changes, not on every write. Document Set,
      Reset, and successful Load notifications report the complete before/after value; editor
      setting writes and live viewport preference setters likewise suppress no-op notifications.
      Invalid writes and storage-only cleanup do not produce false change events.
- [x] The callback thread and re-entrancy contract is documented: callbacks run synchronously on
      the owner's thread after the value is committed. The owner is not thread-safe, callbacks must
      stay on that thread, nested mutations dispatch nested events, and unsubscribing an observer
      before its turn skips it in the current dispatch.

## 0.7 Layering and override order

- [x] Resolution order, lowest to highest priority: **engine default → project setting → user
      preference → per-platform override → command-line override**. `SettingsStackUVE` resolves the
      chain, and `EngineCoreUVE::Init()` attaches all four stores before dependent systems initialize.
      The CPU-buildable settings target tests provenance, layer filtering, invalid-value fallthrough
      and preservation of the caller's base config. Headless EngineCore integration tests load
      project, user and platform files, resolve command-line precedence, and exercise both generated
      and explicit platform paths through actual startup.
- [x] Command-line values for registered EngineConfig settings use setting ids directly:
      `--<setting.id> <value>`; a presence-only bool means true, and `--headless` is the hidden
      `NotPersisted` CLI-only setting. Typed conversion and descriptor validation run in the
      CPU-buildable settings target, with tests for bool, integer, float, vector and enum values,
      invalid overrides, and command-line precedence. Focused `EngineCore::Init()` integration tests
      also run in the headless CPU target; GL-rendering tests remain desktop-only.
- [x] A query for *where a value came from*. `SettingsStackUVE::ResolveUVE` returns the effective,
      descriptor-validated value together with its `SettingValueSourceUVE`; invalid high-priority
      values fall through. Dedicated tests cover every precedence level, provenance, and invalid
      command-line fallback.
- [x] Per-layer save targets: user preferences never write into the project file, and vice versa.
      Editor preferences go to `.uvsettings`, project settings to `project.uvsettings`
      (committed; the ignore rule for `*.uvsettings` makes an exception for it).

## 0.8 Versioning and migration

- [x] A root-level integer `version` for `SettingsDocumentUVE`. Missing-version files are version 0;
      successful loads record the current version, while malformed or newer-than-supported versions
      fail closed without replacing the in-memory document.
- [x] Forward migration callbacks registered for version N run on a candidate store before it is
      committed. A failed callback leaves the existing document, path and dirty state unchanged;
      unregistered steps are identity migrations that still advance the version.
- [x] A `Deprecated` descriptor can name its live `replacementId`. Reads can fall back to a valid old
      value; load migrates it to the new id and removes the alias, while a valid new value wins.
      Editor setting renames now use this shared registry path.
- [x] Version 0, 1, 2 and current-version fixtures verify forward migration, serialization, alias
      cleanup, and rejection of future, malformed and failed migrations.

## 0.9 Enumeration, search and the generic panel

This is the payoff, and it is why the descriptor carries display strings.

- [x] Enumerate all descriptors, filtered by category, flags, or a search string matching **id,
      display name and tooltip**. Also the category, and every word of a multi-word search must
      match (`MatchesSettingSearchUVE`).
- [x] A generic settings panel that renders a page purely from descriptors: a category tree on
      the left, the matching settings on the right, each rendered by its type. The editor's
      **Editor Preferences** window (Menu > File), `editor_panel_preferences_uve.cpp`.
- [x] Per-type row renderers written **once** — bool, slider, drag, combo, layer mask, colour,
      vector2/3/4, path, asset reference, key binding — instead of per setting (`editor_panel_preferences_uve.cpp`).
- [x] A modified-from-default indicator and a per-setting revert control, both of which are free
      once `defaultValue` is in the descriptor. Also a "Modified only" filter, a dot on every
      category holding a change, the default in each row's tooltip, and a confirmed "Reset to
      Defaults" for what is shown.
- [x] An "advanced settings" toggle that reveals `Advanced`-flagged entries. It appears only
      when some setting is `Advanced`.
- [x] A restart-required notice for `RestartRequired` entries: a "(restart)" tag and a tooltip
      line, plus a visible notice listing pending changes. It compares against the window's initial
      values, updates after edits and disappears when a setting is restored.

Note the existing inspector search box matches **drawer ids only**, not property names, so typing
a property name hides everything. A registry-backed search fixes that same class of problem for
settings, and the inspector should eventually share the mechanism.

## 0.10 Project settings file

- [x] A project settings document, separate from user preferences, checked into version control
      alongside the project. `SettingsDocumentUVE`, owned by `EngineCoreUVE` and reached through
      `EngineServicesUVE::GetProjectSettingsUVE()`; edited in the **Project Settings** window
      (Menu > File), which saves when it closes, on Save, and when the editor shuts down.
- [x] Deterministic key ordering on write, so the file produces clean diffs rather than
      reshuffling on every save. The store's JSON objects keep keys sorted.
- [x] Only non-default values written, so the file stays readable and defaults can change in a
      later engine version without rewriting every project. Setting a default removes the key
      (`IConfigManagerUVE::RemoveKeyUVE` prunes the objects it empties).

## 0.11 Substrate work items, in dependency order

1. [x] `SettingDescriptorUVE` and the registry, with the uniqueness/range/default test.
2. [x] Typed validated access over `ConfigManagerUVE`.
3. [x] Migrate the existing editor preferences onto descriptors, moving typed storage and bounds
       into the registry while keeping domain-specific conversion and one-time legacy migration in
       the editor (`RegisterEditorSettingsUVE`, verified by `EditorSettingsUVETest` and
       `EditorLayerMaskUVETest`).
4. [x] Change notification. The shared `SettingsObserverHubUVE` backs document and editor
       settings, with exact-id/category subscriptions, effective-value events, owner-scoped handles,
       and dedicated document/editor regression tests.
5. [x] The generic settings panel with type-based row renderers (the Editor Preferences window).
6. [x] Layering, override order, and the "where did this value come from" query. `SettingsStackUVE`
       resolves defaults/project/user/platform/command-line and reports the winner; EngineCore
       attaches all four stores before constructing dependent systems. CPU-buildable tests cover
       typed command-line conversion, platform-path selection, layer attachment, provenance,
       invalid-value fallthrough and mapping to `EngineConfigUVE`. Focused `EngineCore::Init()`
       tests load project/user/platform files and exercise command-line precedence and fallback in
       headless mode.
7. [x] The project settings file and its layer.
8. [x] Versioning and migration. `SettingsDocumentUVE` versions its root document, runs registered
       forward migrations transactionally, and migrates Deprecated aliases to their replacement ids.

---

# Part 1 — Project settings

Settings that belong to the *project* and ship with the game. One line each; everything in this
part is `[ ]` unless noted. The project settings file exists (0.10); a setting here is done when
it is declared in it and something reads it.

## 1.1 Application and metadata

- [x] Product name, short name, description, product version string and build number. Stored as
      optional, bounded `.uvproject` product metadata for older-project compatibility; the packager
      uses the product name (falling back to the project display name) and reports its version/build.
      Codec round-trip, defaults and validation have dedicated tests.
- [x] Company / publisher name and copyright line. Stored and validated with the `.uvproject`
      application settings and included in the packager's product report.
- [x] Unique reverse-domain application identifier per target. Target-specific identifiers are
      validated, resolved into runtime configuration, and used as the single-instance lock identity;
      legacy projects without one fall back to their project ID.
- [x] Application icon assets per target and pixel size. Package validation and copying retain the
      full icon set; current-target PNGs are decoded and applied to the live window. `.ico`/`.icns`
      assets are retained for host/native packaging, while the current GLFW path consumes PNG.
- [x] Main scene to load on launch. The manifest's content-relative startup scene is validated,
      loaded, and its camera activated before the game loop.
- [x] Splash/boot image, background colour, fade, minimum display time and skip policy. Standalone
      startup imports supported source images to derived user data, renders through ordered UI image
      batches, and skips display on headless/no-window runs.
- [x] Boot logo behaviour in editor play mode. Project settings can skip it (default) or show a
      timed/fading, Escape-skippable viewport overlay with the configured image/title.
- [x] Quit-on-last-window-closed behavior is mapped from the package to EngineCore's run policy.
- [x] Single-instance enforcement uses a per-application lock in user data; duplicate launches fail
      cleanly, and the lock releases on shutdown.
- [x] Runtime `--help`/`-h` documents `--project` and every registered engine setting flag.
- [x] Custom user-data directory name and portable/per-user policy. Runtime resolves platform data
      roots and places caches, settings, logs, saves and crash output under the selected directory.
- [x] Crash handler enable/directory and HTTPS symbol endpoint are stored, validated and consumed.
      Windows writes minidumps; POSIX writes an async-safe signal report. The endpoint is exposed
      for host integration without automatic background symbol uploads.

## 1.2 Display and window

- [x] Default window width and height. Defaults are 1280x720 in project application settings and
      `EngineConfigUVE`; package/runtime mapping is covered by tests.
- [x] Window mode: windowed, maximised, fullscreen, exclusive fullscreen, borderless (`window.display.mode`, validated and tested).
- [x] Resizable, borderless, always-on-top, transparent background (`window.display.{resizable,borderless,alwaysOnTop,transparent}`).
- [x] Minimum and maximum window size (`window.sizeLimits.{minimumWidth,minimumHeight,maximumWidth,maximumHeight}`).
- [x] Initial window position and which monitor to open on (`window.placement.{positionSpecified,x,y,monitor}`).
- [x] High-DPI awareness and per-monitor DPI scaling (`window.scaling.{highDpiAware,perMonitorScaling}`).
- [x] Content scale factor override. The shared 0-to-4 range is validated and the real/null window
      scale-query paths apply a positive override uniformly, with descriptor/settings coverage.
- [x] Stretch mode: disabled, canvas items, viewport; and aspect policy: ignore, keep, keep width,
      keep height, expand.
- [x] Integer-only scaling for pixel-art targets.
- [x] Orientation for handheld targets, and allowed orientation set (`window.handheld.{orientation,allowedOrientations}`).
- [x] V-sync mode: off, on, adaptive, mailbox (`window.performance.vsyncMode`).
- [x] Frame rate cap, separate caps for focused and unfocused windows (`window.performance.{focusedFrameRateCap,unfocusedFrameRateCap}`).
- [x] Allow display sleep / keep screen on (`window.performance.allowDisplaySleep`).
- [x] Mouse cursor: custom image, hotspot, visibility default, confine-to-window default (`window.cursor.*`).
- [x] Window title format, including whether the scene name is appended in editor play mode (`window.title.appendSceneNameInEditorPlayMode`).

## 1.3 Rendering

The largest group by far, and the one most dependent on the renderer maturing.

### Pipeline and general

- [x] Renderer backend selection and fallback order (`rendering.backend.preference`, per-platform and restart-required, verified in `EngineSettingsLayeringUVETest`).
- [x] Rendering method (`rendering.general.renderingMethod` -> `EngineConfigUVE::renderingMethodUVE`).
- [x] Colour space (`rendering.general.colorSpace` -> `EngineConfigUVE::colorSpaceModeUVE`).
- [x] HDR rendering enable, output transfer function, peak luminance, paper white (`rendering.hdr.{enabled,transferFunction,peakLuminanceNits,paperWhiteNits}`).
- [x] Render target format and precision (8/10/16-bit) (`rendering.general.renderTargetFormat`).
- [x] Default clear colour (`application.boot.backgroundColor` RGBA setting mapped into `EngineConfigUVE::backgroundColorUVE`).
- [x] Render scale and dynamic resolution (`rendering.general.{resolutionScale,dynamicResolution,minimumResolutionScale,targetFrameTimeMilliseconds}`).
- [x] Upscaling method and quality (spatial vs. temporal) (`rendering.upscaling.method`).
- [x] Multi-threaded rendering enable and command-buffer thread count (`rendering.threading.{multiThreadedRendering,commandBufferThreads}`).
- [x] Pipeline-cache location and precompilation at startup (`rendering.shaders.programCacheDirectory`).
- [x] Shader compilation mode (`rendering.shaders.compilationMode`).
- [x] GPU validation layers (debug builds) (`rendering.debug.vulkanValidationLayers`).

### Anti-aliasing

- [x] MSAA sample count, separately for 2D and 3D (`rendering.antialiasing.{msaa2dSamples,msaa3dSamples}`).
- [x] Post-process AA method (`rendering.antialiasing.postProcessMethod`).
- [x] Temporal AA sample count, jitter pattern, sharpening amount (`rendering.antialiasing.{taaSamples,taaJitterPattern,taaSharpening}`).
- [x] Screen-space AA quality (`rendering.antialiasing.screenSpaceQuality`).
- [x] Alpha-to-coverage for foliage-style materials (`rendering.antialiasing.alphaToCoverage`).

### Shadows

- [x] Shadow atlas size, and per-light quadrant subdivision (`rendering.shadows.atlasSize`).
- [x] Directional shadow resolution and split count (`rendering.shadows.{mapResolution,directionalSplitCount}`).
- [x] Cascade split distribution and shadow-distance fade (`rendering.shadows.defaultCascadeSplitBlend`).
- [x] Shadow filter quality: hard, soft, very soft, and sample counts per tier (`rendering.shadows.filter`).
- [x] Depth/normal/slope-angle bias defaults (`rendering.shadows.{defaultBias,defaultNormalBias}`).
- [x] Shadow maximum distance (`rendering.shadows.maxDistance` + per-light `DirectionalLight3D` controls).
- [x] 16-bit vs. 32-bit shadow depth format (`rendering.shadows.depthFormatBits`).
- [x] Point/spot shadow cube resolution (`rendering.shadows.pointSpotCubeResolution`).
- [x] Contact shadows enable, length, step count (`rendering.shadows.{contactShadowsEnabled,contactShadowsLength,contactShadowsSteps}`).

### Global illumination and reflections

- [x] Ambient light source: None, Flat Color, Sky, Environment Map (`WorldEnvironmentAmbientSourceUVE`).
- [x] Screen-space ambient occlusion (`rendering.ssao.{enabled,radius,intensity,power,quality,blur}`).
- [x] Screen-space indirect lighting: enable, quality, frame count (`rendering.gi.ssilEnabled`).
- [x] Screen-space reflections: enable, max steps, fade in/out, depth tolerance (`rendering.gi.{ssrEnabled,ssrMaxSteps}`).
- [x] Reflection probe atlas size and per-probe resolution (`rendering.gi.reflectionProbeAtlasSize` + `ReflectionProbe3D` capture tiers).
- [x] Voxel or signed-distance GI quality tier, bounce count, and update budget (`rendering.gi.voxelGiQuality` + `VoxelGI3D`).
- [x] Lightmap baking: resolution scale, bounce count, denoiser enable, directional lightmaps, texel density defaults (`rendering.gi.lightmapTexelDensity` + `LightmapGI3D`).
- [x] Light probe density defaults (`rendering.gi.lightProbeDefaultDensity` + `LightmapProbe3D`).

### Post-processing

- [x] Tonemapper (`rendering.postProcessing.toneMapper`).
- [x] Auto-exposure: enable, min/max luminance, adaptation speed up/down (`rendering.postProcessing.autoExposureEnabled`).
- [x] Bloom (`WorldEnvironment3DComponentUVE` enable, threshold, soft knee, intensity, and `bloomMipCount` 1..6).
- [x] Depth of field (`WorldEnvironment3DComponentUVE` manual/center autofocus, focus distance, aperture, quality).
- [x] Motion blur (`WorldEnvironment3DComponentUVE` camera-motion reprojection blur).
- [x] Vignette, chromatic aberration, film grain, and lens distortion (`WorldEnvironment3DComponentUVE`).
- [x] Colour grading: LUT slot, LUT size, and whether grading happens pre- or post-tonemap (`rendering.postProcessing.colorGradingLutAsset`).
- [x] Sharpening amount (`rendering.postProcessing.sharpeningAmount`).
- [x] Debanding / dithering enable (`rendering.postProcessing.ditheringEnabled`).
- [x] WorldEnvironment fog modes (`WorldEnvironmentFogModeUVE::{Linear,Exponential,Height}`).
- [x] Volumetric fog: enable, froxel resolution, depth distribution, temporal reprojection, anisotropy (`rendering.postProcessing.volumetricFogEnabled` + `FogVolume3D`).

### Geometry, culling and batching

- [x] Default near and far clip planes (`rendering.camera.{defaultNearClip,defaultFarClip}`).
- [x] Frustum culling enable and debug freeze (`rendering.culling.frustumCulling`).
- [x] Occlusion culling: enable, method, buffer resolution, bake settings (`rendering.culling.occlusionCullingEnabled` + `Occluder3D`).
- [x] LOD bias, minimum screen coverage, and crossfade mode (`rendering.lod.globalBias` + `LODGroup3D`).
- [x] Mesh LOD auto-generation defaults (`rendering.lod.autoGenerateDefault`).
- [x] Instancing thresholds and per-instance data budget (`rendering.batching.instancingMinCount`).
- [x] Static batching enable and vertex budget (`rendering.batching.staticBatchingEnabled`).
- [x] Indirect draw enable (`rendering.batching.indirectDrawEnabled`).
- [x] Draw-call and triangle budget warnings (`rendering.budgets.maxDrawCallsWarning`).

### Textures and sampling

- [x] Default texture filter: nearest, bilinear, trilinear, with or without mipmaps (`rendering.textures.defaultFilter`).
- [x] Default anisotropic filtering level (`rendering.textures.defaultAnisotropy`).
- [x] Global mipmap LOD bias (`rendering.textures.globalMipLodBias`).
- [x] Texture streaming enable, memory budget, mip bias under pressure (`rendering.textures.streamingEnabled`).
- [x] Maximum texture size per quality tier (`rendering.textures.maxSizePerTier`).
- [x] Default texture repeat mode: disabled, enabled, mirrored (`rendering.textures.defaultRepeatMode`).

### 2D rendering

- [x] Default canvas item filter and repeat (`rendering.2d.defaultCanvasFilter`).
- [x] Pixel snapping for 2D transforms and for vertices (`rendering.2d.pixelSnapTransforms`).
- [x] 2D shadow atlas size (`rendering.2d.shadowAtlasSize`).
- [x] Sprite sorting axis and default sort layer set (`rendering.2d.spriteSortAxis`).

### Environment

- [x] Default sky: none, colour, gradient, panorama, physical (`rendering.environment.defaultSkyMode`).
- [x] Sky resolution and update mode (realtime, once, on demand) (`rendering.environment.skyResolution`).
- [x] Default world environment resource for new scenes (`rendering.environment.defaultWorldEnvironmentAsset`).

## 1.4 Physics

- [x] Physics tick rate, and maximum substeps per frame. `physics.common.ticksPerSecond`,
      `physics.common.maxFrameTime` (the longest frame caught up on) and
      `physics.common.maxStepsPerFrame` (the timer's step cap, now `SetMaxStepsPerTickUVE`),
      applied at startup.
- [x] Engine selection per dimension, where more than one exists.
- [x] Default gravity vector and magnitude, 2D and 3D separately (`physics.3d.gravity` and `physics.2d.gravity`, registered and mapped into `EngineConfigUVE::{gravity, gravity2DUVE}`).
- [x] Default linear and angular damping (`physics.common.defaultLinearDamping`, `physics.common.defaultAngularDamping`).
- [x] Sleep threshold, sleep time, and whether sleeping is allowed by default (`physics.common.{sleepThreshold, sleepTimeSeconds, allowSleepingDefault}`).
- [x] Solver iteration counts: position and velocity (`physics.solver.{positionIterations, velocityIterations}`).
- [x] Contact offset, rest offset, penetration slop, max penetration recovery speed (`physics.contacts.{contactOffset, penetrationSlop}`).
- [x] Continuous collision detection mode default (`EngineConfigUVE::physicsCcdDefaultModeUVE`).
- [x] Broadphase algorithm and world bounds (`EngineConfigUVE::{physicsBroadphaseAlgorithmUVE, physicsBroadphaseHalfExtentUVE}`).
- [x] Default physics material: friction, bounce, combine modes (`EngineConfigUVE::{defaultPhysicsFrictionUVE, defaultPhysicsRestitutionUVE}`).
- [x] Layer/mask default for new bodies (`physics.layers.{defaultCollisionLayer, defaultCollisionMask}` registered via `MakeLayerMaskSettingUVE`).
- [x] Layer collision matrix — which layer collides with which (`layers.physics.1..32` and `layers.physics2d.1..32`).
- [x] Trigger/area callbacks enable, and whether triggers report on static bodies (`EngineConfigUVE::{triggerCallbacksEnabledUVE, triggersReportOnStaticBodiesUVE}`).
- [x] Raycast defaults: hit backfaces, hit triggers, max distance (`EngineConfigUVE::{raycastHitBackfacesUVE, raycastHitTriggersUVE, raycastDefaultMaxDistanceUVE}`).
- [x] Physics interpolation default (`physics.common.interpolationDefault` -> `EngineConfigUVE::physicsInterpolationDefaultUVE`).
- [x] Physics debug draw defaults (`EngineConfigUVE::physicsDebugDrawCollidersUVE`).
- [x] Deterministic mode and fixed random seed (`physics.simulation.deterministicMode` -> `EngineConfigUVE::{physicsDeterministicModeUVE, physicsFixedRandomSeedUVE}`).

## 1.5 Input

- [x] The input map itself: named actions, each with positive and negative bindings.
      `project.uvinput`, read by `EngineCoreUVE::Init()` into `InputMapDocumentUVE` and
      registered with the input system; `EngineServicesUVE::GetInputMapUVE()` reaches it.
- [x] Per-action deadzone, sensitivity, and whether it is analog or digital (`input.actions.defaultDeadzone` -> `EngineConfigUVE::{inputDefaultDeadzoneUVE, inputDefaultSensitivityUVE, inputAnalogByDefaultUVE}`).
- [x] Device classes: keyboard, mouse, gamepad, touch, pen (`EngineConfigUVE::inputEnabledDeviceMaskUVE`).
- [x] Gamepad button/axis remapping table and per-controller-model mapping database (`EngineConfigUVE::inputGamepadDatabasePathUVE`).
- [x] Gamepad deadzone shape (axial, radial, scaled radial) and inner/outer radius (`EngineConfigUVE::{inputGamepadDeadzoneShapeUVE, inputGamepadDeadzoneInnerUVE, inputGamepadDeadzoneOuterUVE}`).
- [x] Vibration/haptics enable and default strength (`input.haptics.enabled` -> `EngineConfigUVE::{inputHapticsEnabledUVE, inputHapticsStrengthUVE}`).
- [x] Mouse: raw input, acceleration, sensitivity, invert axes (`input.mouse.sensitivity` -> `EngineConfigUVE::{inputMouseRawUVE, inputMouseAccelerationUVE, inputMouseSensitivityUVE, inputMouseInvertXUVE, inputMouseInvertYUVE}`).
- [x] Touch: emulate mouse from touch, emulate touch from mouse, multi-touch limit (`EngineConfigUVE::{inputEmulateMouseFromTouchUVE, inputEmulateTouchFromMouseUVE, inputMaxTouchPointsUVE}`).
- [x] Text input: IME enable, on-screen keyboard behaviour (`EngineConfigUVE::{inputImeEnabledUVE, inputVirtualKeyboardAutoShowUVE}`).
- [x] Accumulated vs. per-frame input polling (`EngineConfigUVE::inputAccumulatedPollingUVE`).
- [x] Action-set / context switching (gameplay vs. menu vs. vehicle) (`EngineConfigUVE::inputDefaultActionSetUVE`).
- [x] Rebinding UI support, and where user rebinds are persisted (`EngineConfigUVE::inputUserRebindsPathUVE` + `InputMapDocumentUVE`).

## 1.6 Audio

- [x] Output driver selection, and fallback order (`EngineConfigUVE::audioDriverPreferenceUVE`).
- [x] Sample rate, buffer size / latency target, channel layout (mono, stereo, 5.1, 7.1) (`audio.output.sampleRate` -> `EngineConfigUVE::{audioSampleRateUVE, audioBufferSizeFramesUVE, audioChannelLayoutUVE}`).
- [x] Bus layout: master plus named buses, each with volume, mute, solo, bypass (`audio.buses.masterVolume` -> `EngineConfigUVE::audioMasterVolumeUVE`).
- [x] Per-bus effect chain defaults.
- [x] Default listener model, doppler factor, speed of sound (`EngineConfigUVE::{audioDopplerFactorUVE, audioSpeedOfSoundUVE}`).
- [x] 3D attenuation model, rolloff curve, min/max distance defaults (`EngineConfigUVE::{audioAttenuationModelUVE, audioDefaultMinDistanceUVE, audioDefaultMaxDistanceUVE}`).
- [x] Voice limit and voice-stealing policy (`audio.voices.maxVoices` -> `EngineConfigUVE::{audioMaxVoicesUVE, audioVoiceStealingPolicyUVE}`).
- [x] Streaming threshold — file size above which audio streams rather than loads (`EngineConfigUVE::audioStreamingThresholdKbUVE`).
- [x] Music vs. SFX vs. voice category volumes exposed to the player (`EngineConfigUVE::{audioMusicVolumeUVE, audioSfxVolumeUVE, audioVoiceVolumeUVE}`).
- [x] Mute on focus loss (`audio.window.muteOnFocusLoss` -> `EngineConfigUVE::audioMuteOnFocusLossUVE`).

## 1.7 Animation

- [x] Default blend time and blend mode (`animation.playback.defaultBlendTime` -> `EngineConfigUVE::{animationDefaultBlendTimeUVE, animationDefaultBlendModeUVE}`).
- [x] Animation update rate and whether it is fixed or frame-rate driven (`EngineConfigUVE::{animationUpdateRateHzUVE, animationFixedTimestepUVE}`).
- [x] Root motion enable default (`EngineConfigUVE::animationRootMotionDefaultUVE`).
- [x] Bone LOD / animation LOD distance tiers (`EngineConfigUVE::animationLodDistanceTiersUVE`).
- [x] Animation compression: tolerance, keyframe reduction (`EngineConfigUVE::{animationCompressionToleranceUVE, animationKeyframeReductionUVE}`).
- [x] Skinning method: CPU, GPU linear blend, dual quaternion; and max bone influences (`EngineConfigUVE::{animationSkinningMethodUVE, animationMaxBoneInfluencesUVE}`).
- [x] Animation state machine defaults (`EngineConfigUVE::animationStateMachineDefaultTransitionUVE`).
- [x] IK solver iteration defaults (`animation.ik.defaultSolverIterations` -> `EngineConfigUVE::animationIkDefaultIterationsUVE`).

## 1.8 Navigation

- [x] Navigation mesh bake defaults: cell size, cell height, agent radius/height/max slope/max
      climb (`navigation.bake.{cellSize, agentRadius}` -> `EngineConfigUVE::{navCellSizeUVE, navCellHeightUVE, navAgentRadiusUVE, navAgentHeightUVE, navAgentMaxSlopeDegreesUVE, navAgentMaxClimbUVE}`).
- [x] Region minimum size, merge size, edge max length and error (`EngineConfigUVE::{navRegionMinSizeUVE, navRegionMergeSizeUVE, navEdgeMaxLengthUVE, navEdgeMaxErrorUVE}`).
- [x] Detail sample distance and max error (`EngineConfigUVE::{navDetailSampleDistanceUVE, navDetailMaxErrorUVE}`).
- [x] Navigation layer names and cost per layer (`layers.navigation.1..32` via `LayerSetUVE::Navigation`).
- [x] Path post-processing: string pulling, smoothing, corridor optimisation (`EngineConfigUVE::{navStringPullingUVE, navPathSmoothingUVE, navCorridorOptimizationUVE}`).
- [x] Avoidance: enable, neighbour distance, time horizon, max neighbours (`navigation.avoidance.enabled` -> `EngineConfigUVE::{navAvoidanceEnabledUVE, navNeighborDistanceUVE, navTimeHorizonUVE, navMaxNeighborsUVE}`).
- [x] Async baking and per-frame bake budget (`EngineConfigUVE::{navAsyncBakingUVE, navBakeBudgetMsUVE}`).

## 1.9 Networking

- [x] Default transport, port, and bind address (`network.transport.defaultPort` -> `EngineConfigUVE::{networkDefaultPortUVE, networkBindAddressUVE}`).
- [x] Maximum connections, max packet size, and channel count (`EngineConfigUVE::{networkMaxConnectionsUVE, networkMaxPacketSizeBytesUVE, networkChannelCountUVE}`).
- [x] Tick rate, send rate, and interpolation/extrapolation buffer (`network.rates.tickRateHz` -> `EngineConfigUVE::{networkTickRateHzUVE, networkSendRateHzUVE, networkInterpolationBufferMsUVE}`).
- [x] Compression mode and threshold (`EngineConfigUVE::{networkCompressionEnabledUVE, networkCompressionThresholdBytesUVE}`).
- [x] Encryption enable and certificate paths (`EngineConfigUVE::{networkEncryptionEnabledUVE, networkCertificatePathUVE}`).
- [x] Connection timeout and keepalive interval (`EngineConfigUVE::{networkTimeoutSecondsUVE, networkKeepaliveIntervalSecondsUVE}`).
- [x] Replication defaults: authority model, relevancy distance, priority (`EngineConfigUVE::{networkRelevancyDistanceMetresUVE, networkDefaultPriorityUVE}`).
- [x] Lag compensation window (`network.lagCompensation.windowMilliseconds` -> `EngineConfigUVE::networkLagCompensationWindowMsUVE`).
- [x] Network simulation for testing: latency, jitter, packet loss, duplication (`EngineConfigUVE::{networkSimLatencyMsUVE, networkSimJitterMsUVE, networkSimPacketLossPercentUVE, networkSimDuplicatePercentUVE}`).

## 1.10 Localisation

- [x] Locale list, default locale, and fallback chain (`localisation.defaultLocale` -> `EngineConfigUVE::{localisationDefaultLocaleUVE, localisationFallbackLocaleUVE, localisationSupportedLocalesUVE}`).
- [x] Translation file locations and format (`EngineConfigUVE::localisationTranslationsDirUVE`).
- [x] Pseudolocalisation toggle, with expansion factor and accent/bracket options (`localisation.pseudolocalisationEnabled` -> `EngineConfigUVE::{pseudolocalisationEnabledUVE, pseudolocalisationExpansionUVE}`).
- [x] Right-to-left layout support (`EngineConfigUVE::rightToLeftLayoutUVE`).
- [x] Per-locale font override (`EngineConfigUVE::localisationDefaultFontAssetUVE`).
- [x] Number, date and currency formatting mode (`EngineConfigUVE::localisationFormattingModeUVE`).
- [x] Whether untranslated strings are reported as warnings at build time (`EngineConfigUVE::warnOnUntranslatedStringsUVE`).

## 1.11 File system and paths

- [x] Project root, asset root, and import cache directory (`EngineConfigUVE::{projectRootDirectoryUVE, projectContentRootUVE, derivedArtifactCacheRootUVE}`).
- [x] User data directory and save-game directory (`paths.saveGameDirectory` -> `EngineConfigUVE::{userDataDirectoryPathUVE, saveDirectoryPath}`).
- [x] Ignored paths for the asset scanner.
- [x] Case-sensitivity policy for asset paths (`EngineConfigUVE::caseSensitiveAssetPathsUVE`).
- [x] Maximum path length warnings (`EngineConfigUVE::maxPathLengthWarningUVE`).
- [x] Pack/archive mount order and priority (`EngineConfigUVE::packMountPriorityUVE`).
- [x] Log file path and rotation policy (`paths.logFilePath` -> `EngineConfigUVE::{logFilePath, enableConsoleLogging}`).

## 1.12 Layer names

- [x] Named 3D physics layers (32). `layers.physics.1` to `.32` in the project settings (`LayerSetUVE::Physics`).
- [x] Named 2D physics layers (32). `layers.physics2d.1` to `.32` in the project settings (`LayerSetUVE::Physics2D`).
- [x] Named render/visibility layers (32). `layers.render.1` to `.32`: mesh visibility, render instance, light and decal cull masks (`LayerSetUVE::Render`).
- [x] Named navigation layers (32). `layers.navigation.1` to `.32` in the project settings (`LayerSetUVE::Navigation`).
- [x] Named avoidance layers (32). `layers.avoidance.1` to `.32` in the project settings (`LayerSetUVE::Avoidance`).
- [x] A shared layer-name editor UI, one implementation reused by all of the above. The
      Project Settings window's Layers pages, reached from any mask's "Edit Names...".
- [x] A layer-mask property row that shows names instead of bits — this is the single change that
      makes layers usable at all. The row reads "Default, Player +2", "All" or "None"; it opens a
      two-column checklist of all 32 layers with All, None, Invert and Edit Names, and its tooltip
      lists every layer in the mask with the raw value.

## 1.13 Quality tiers

- [x] Named quality presets (for example: low, medium, high, ultra) as a first-class concept (`quality.activePreset` enum `Low`/`Medium`/`High`/`Ultra`).
- [x] Each preset is a set of overrides over the rendering/physics/audio settings above.
- [x] Per-platform default preset (`quality.activePreset` carries `kSettingFlagPerPlatformUVE`).
- [x] Runtime preset switching, and which settings require a restart to take effect (`EngineConfigUVE::allowRuntimeQualitySwitchingUVE`).
- [x] Automatic tier detection from detected hardware, with a manual override (`EngineConfigUVE::autoDetectQualityTierUVE`).

## 1.14 Per-platform overrides

- [x] A per-target override layer, so a setting can differ on desktop, handheld and console-class
      targets without duplicating the whole document (`SettingsStackUVE` + `GetPlatformSettingsFilePathUVE`).
- [x] A UI that shows the base value, which targets override it, and the effective value per
      target (`SettingResolutionUVE::source`).
- [x] Feature tags per target, so code can query capabilities rather than platform names (`platform.targetFeatureTags` -> `EngineConfigUVE::platformFeatureTagsUVE`).

## 1.15 Packaging and build

- [x] Export/build presets, named and reusable (`packaging.defaultPresetName` -> `EngineConfigUVE::packagingDefaultPresetUVE`).
- [x] Target platform, architecture, and build configuration per preset (`EngineConfigUVE::{packagingTargetArchUVE, packagingBuildConfigUVE}`).
- [x] Included/excluded asset filters per preset (`EngineConfigUVE::{packagingIncludeFiltersUVE, packagingExcludeFiltersUVE}`).
- [x] Compression mode for packaged assets (`EngineConfigUVE::packagingCompressionModeUVE`).
- [x] Debug symbols: generate, strip, upload (`EngineConfigUVE::packagingDebugSymbolsPolicyUVE`).
- [x] Code signing identity and provisioning per platform (`EngineConfigUVE::packagingCodeSigningIdentityUVE`).
- [x] Asset encryption enable and key source (`packaging.encryptAssets` -> `EngineConfigUVE::packagingEncryptAssetsUVE`).
- [x] Embedded vs. loose asset packs (`EngineConfigUVE::packagingPackModeUVE`).
- [x] Post-build command hooks (`EngineConfigUVE::packagingPostBuildCommandUVE`).
- [x] Version and build-number auto-increment policy (`EngineConfigUVE::packagingAutoIncrementBuildUVE`).

---

# Part 2 — Editor settings

Settings that belong to the *person using the editor*, not the project. These persist to user
preferences and are never checked in with the game.

This is where the engine is furthest along — and "furthest along" still means sixteen keys and no
settings window.

## 2.1 Interface and theme

- [x] Theme selection, and custom theme files (`editor.interface.{theme,customThemePath}`).
- [x] Base colour, accent colour, contrast, and whether the theme is derived or hand-authored (`editor.interface.{baseColor,accentColor,contrast}`).
- [x] Editor UI scale: automatic from DPI, or a manual factor (`editor.interface.uiScale`).
- [x] Main font, monospace font, font size, and font hinting/anti-aliasing mode (`editor.interface.{mainFont,monospaceFont,fontSize}`).
- [x] Font oversampling and subpixel positioning (`editor.interface.fontOversampling`).
- [x] Icon set and icon saturation (`editor.interface.iconSaturation`).
- [x] Border size, corner radius, and spacing density (compact / comfortable) (`editor.interface.{borderSize,cornerRadius,spacingDensity}`).
- [x] Animation speed for UI transitions, and a "reduce motion" option (`editor.interface.{animationSpeed,reduceMotion}`).
- [x] Tooltip delay and whether tooltips are shown at all (`editor.interface.{showTooltips,tooltipDelaySeconds}`).
- [x] Editor language (`editor.interface.language`).
- [x] Touchscreen-friendly hit targets (`editor.interface.touchscreenHitTargets`).

## 2.2 Layout, docking and workspaces

- [x] Dock layout persistence, and named saved layouts (`editor.layout.namedLayout`).
- [x] Reset-to-default-layout command (`editor.layout.resetRequested`).
- [x] Panel visibility for the scene, inspector, viewport and bottom dock (`editor.panels.{showScene,showInspector,showViewport,showBottomDock}`).
- [x] Active workspace and active bottom-dock tab (`editor.workspace.active` and `editor.bottomDock.active`).
- [x] Active right-panel tab (`editor.rightPanel.activeTab`).
- [x] Per-panel size and split-ratio persistence (`editor.layout.{leftSplitRatio,rightSplitRatio,bottomSplitRatio}`).
- [x] Floating window support and multi-monitor placement memory (`editor.layout.floatingWindowsEnabled`).
- [x] Tab position, tab close buttons, and tab overflow behaviour (`editor.layout.tabCloseButtons`).
- [x] Distraction-free / fullscreen editing mode (`editor.layout.distractionFreeMode`).

## 2.3 Scene tree and hierarchy panel

The hierarchy is one of the two panels a user looks at constantly.

- [x] Auto-expand on selection, and auto-scroll the selected node into view (`editor.hierarchy.revealSelection`).
- [x] Expand-all / collapse-all depth limit (`editor.hierarchy.expandDepthLimit`).
- [x] Persist expansion state per scene across sessions (`editor.hierarchy.persistExpansion`).
- [x] Row height, indent width, and whether indent guides are drawn (`editor.hierarchy.{rowHeight,indentWidth,treeLines}`).
- [x] Object-type icons: show, hide, or colour-code by type (`editor.hierarchy.{showObjectIcons,colorCodeObjectIcons}`).
- [x] Show node type name alongside the node name (`editor.hierarchy.showObjectTypes`).
- [x] Show component badges on the row (`editor.hierarchy.showComponentBadges`).
- [x] Visibility toggle column: show, hide, or show on hover (`editor.hierarchy.visibilityColumn`).
- [x] Lock / unselectable column (`editor.hierarchy.showLockColumn`).
- [x] Filter behaviour: match name only, or name plus type plus component; case sensitivity; whether ancestors of a match are kept visible (`editor.hierarchy.filterMode`).
- [x] Sort mode: scene order (authoritative), alphabetical, or by type (`editor.hierarchy.sortMode`).
- [x] Drag-and-drop reparent: enable, and whether a confirmation is required for large subtrees (`editor.hierarchy.{dragReparent,confirmLargeSubtreeReparent}`).
- [x] Multi-selection behaviour: rubber-band, range select, and whether children follow the parent (`editor.hierarchy.childrenFollowParentSelect`).
- [x] Colour tags / node groups, and whether tag colour tints the row (`editor.hierarchy.tintRowsByTagColor`).
- [x] Warning and error badges (`editor.hierarchy.showWarningBadges`).
- [x] Double-click action: rename, focus in viewport, or open script (`editor.hierarchy.doubleClickAction`).
- [x] Rename mode: inline edit vs. dialog, and name-collision policy (`editor.hierarchy.renameCollisionPolicy`).
- [x] Show the scene root specially (`editor.hierarchy.styleSceneRootRow`).

## 2.4 Scene node context menu (right-click)

- [x] Which actions appear, and in what order (`editor.contextMenu.actionOrder`).
- [x] Add child node, add sibling node, instantiate scene as child.
- [x] Attach / detach / open script (`editor.contextMenu.enableScriptActions`).
- [x] Add component, remove component, copy component values, paste component values (`editor.contextMenu.enableComponentClipboard`).
- [x] Cut, copy, paste, duplicate, delete, with a configurable duplicate-name suffix pattern (`editor.contextMenu.duplicateNameSuffix`).
- [x] Rename, and change node type where the conversion is legal (`editor.contextMenu.enableChangeNodeType`).
- [x] Reparent to selection, reparent keeping global transform (toggle) (`editor.contextMenu.reparentKeepGlobalTransform`).
- [x] Move up / move down / move to top / move to bottom in sibling order.
- [x] Save branch as a reusable scene, and make a local instance editable (`editor.contextMenu.enableSaveBranchAsScene`).
- [x] Focus in viewport, frame selection, align view to node, align node to view.
- [x] Lock / unlock, show / hide, toggle selectable.
- [x] Copy node path, copy node identifier (`editor.contextMenu.enableCopyNodePath`).
- [x] Expand / collapse subtree.
- [x] Whether the context menu also selects the node it opened on (`editor.contextMenu.selectOnRightClick`).
- [x] Long-press equivalent for touch input, with a configurable hold duration (`editor.contextMenu.longPressHoldSeconds`).
- [x] Whether dangerous entries (delete subtree) require confirmation (`editor.contextMenu.confirmDeleteSubtree`).

## 2.5 Menu bar and commands

- [x] A command registry: every editor action has an id, a label, a category, and an optional default shortcut (`editor_commands_uve.cpp`).
- [x] Command palette: enable, fuzzy-match mode, recent-command memory depth (`editor.commands.paletteHistoryDepth`).
- [x] Shortcut bindings as a real settings page: searchable, per-command, with conflict detection and a reset-to-default per binding.
- [x] Shortcut profiles, so a user can switch between binding sets (`editor.commands.shortcutProfile`).
- [x] Menu bar customisation: which top-level menus appear (`editor.commands.visibleTopMenus`).
- [x] Recent files / recent projects list length, and whether it is cleared on exit (`editor.commands.{recentProjectsLimit,clearRecentsOnExit}`).
- [x] Toolbar contents and button size (`editor.commands.toolbarButtonSize`).
- [x] Confirmation prompts: which destructive actions ask first (`editor.commands.confirmDestructiveActions`).
- [x] "Save Editor Preferences" and automatic save on change (`editor.commands.autoSavePreferencesOnChange`).

## 2.6 Viewport and 3D editing

The group with the most real backing today.

- [x] Snap enabled, translate step, rotate step (degrees) and scale step (`editor.viewport.snap.*`).
- [x] Gizmo axis colours (`editor.viewport.axisColors.*`).
- [x] Grid: visible, size, subdivisions, extent/fade distance, colour, and which plane(s) are drawn (`editor.viewport.grid.{visible,opacity,cellSize,subdivisions,fadeDistance}`).
- [x] Grid follows the camera vs. fixed at origin (`editor.viewport.grid.followCamera`).
- [x] Gizmo size in pixels, gizmo opacity, and whether the gizmo is hidden during drag (`editor.viewport.gizmo.{sizePixels,opacity,hideDuringDrag}`).
- [x] Gizmo mode memory: whether the active transform mode persists across sessions (`editor.viewport.gizmo.persistMode`).
- [x] Local vs. global transform space default (`editor.viewport.gizmo.defaultLocalSpace`).
- [x] Pivot mode: individual origins, median point, or active object (`editor.viewport.gizmo.pivotMode`).
- [x] Navigation scheme presets, and per-scheme modifier assignments for orbit, pan and zoom (`editor.viewport.navigation.scheme`).
- [x] Orbit sensitivity, pan sensitivity, zoom sensitivity, and invert toggles per axis (`editor.viewport.navigation.{orbitSensitivity,panSensitivity,zoomSensitivity,invertOrbitY}`).
- [x] Zoom style: dolly vs. field-of-view, and zoom-to-cursor (`editor.viewport.navigation.zoomToCursor`).
- [x] Inertia / smoothing amount for camera motion (`editor.viewport.navigation.cameraInertia`).
- [x] Freelook: enable, activation modifier, base speed, speed scaling with scroll, acceleration (`editor.viewport.freelook.{baseSpeed,acceleration}`).
- [x] Default camera field of view, near plane, far plane (`editor.viewport.camera.{defaultFovDegrees,nearClip,farClip}`).
- [x] Orthographic view presets and the shortcut to each.
- [x] Frame-selected padding and animation duration (`editor.viewport.camera.{framePadding,frameDurationSeconds}`).
- [x] Selection outline: colour, thickness, and whether it draws through geometry (`editor.viewport.selectionOutline.*`).
- [x] Selection box / rubber-band select mode: touch vs. enclose (`editor.viewport.selection.boxEncloseOnly`).
- [x] View modes: wireframe, unshaded, overdraw, lighting-only, normals, and per-buffer debug views (`editor.viewport.viewMode`).
- [x] Viewport overlays: statistics, frame time, draw calls, triangle count, and their corner (`editor.viewport.overlays.{showStats,corner}`).
- [x] Orientation gizmo: size, corner, and whether clicking it snaps the view (`editor.viewport.orientationGizmo.{sizePixels,snapOnClick}`).
- [x] Preview environment when the scene has none (`editor.viewport.previewEnvironmentAsset`).
- [x] Viewport render scale for low-end machines (`editor.viewport.renderScale`).
- [x] Multiple viewports (1 / 2 / 3 / 4 panes) and per-pane settings (`editor.viewport.paneCount`).

## 2.7 Inspector

- [x] Collapsible, component-grouped sections with persisted expansion state (`editor.inspector.folds`).
- [x] Property-row helpers used by every drawer (`editor_panel_inspector_uve.cpp`).
- Property search: the Inspector's search box was removed on purpose - the Inspector shows only the node's own class chain.
- [x] Label width / name column ratio, and word-wrapping of long labels (`editor.inspector.{labelColumnRatio,wrapLongLabels}`).
- [x] Float display precision, and drag step per property (`editor.inspector.floatPrecision`).
- [x] Degrees vs. radians display for angles (`editor.inspector.anglesInDegrees`).
- [x] Show advanced / internal properties toggle (`editor.inspector.showAdvancedProperties`).
- [x] Show modified-from-default markers, and per-property revert.
- [x] Multi-object editing, with mixed-value indication (`editor.inspector.enableMultiObjectEditing`).
- [x] Copy / paste property values, and copy property path.
- [x] Favourite properties pinned to the top (`editor.inspector.pinnedFavoriteProperties`).
- [x] Default colour-picker shape and colour-picker mode (`editor.inspector.{defaultColorPickerShape,defaultColorPickerMode}`).
- [x] Open resources in a sub-inspector vs. a new panel (`editor.inspector.openResourcesInSubInspector`).
- [x] Auto-refresh rate while the game is running (`editor.inspector.playModeRefreshHz`).
- [x] Section ordering, with the universal node section last.
- Add Component search: the Add Component control was removed from the Inspector on purpose - a node's components come from its type. Not planned in this form.

## 2.8 Script and shader editor

- [x] Font, font size, line height, and ligatures (`editor.codeEditor.{fontSize,lineHeight,ligatures}`).
- [x] Line numbers, relative line numbers, and the gutter contents (`editor.codeEditor.{showLineNumbers,relativeLineNumbers}`).
- [x] Tab size, spaces vs. tabs, and auto-indent mode (`editor.codeEditor.{tabSize,insertSpaces}`).
- [x] Word wrap mode and wrap guide columns (`editor.codeEditor.{wordWrap,wrapGuideColumn}`).
- [x] Syntax highlighting colour set, and per-token overrides (`editor.codeEditor.syntaxTheme`).
- [x] Current-line highlight, matching-bracket highlight, and whitespace visibility (`editor.codeEditor.{highlightCurrentLine,showWhitespace}`).
- [x] Code completion: enable, delay, minimum prefix length, auto-insert behaviour (`editor.codeEditor.{codeCompletionEnabled,completionDelayMs}`).
- [x] Signature help and inline documentation popups (`editor.codeEditor.signatureHelpEnabled`).
- [x] Auto-brace and auto-quote completion (`editor.codeEditor.autoCloseBrackets`).
- [x] Code folding, and fold-on-open regions (`editor.codeEditor.codeFoldingEnabled`).
- [x] Minimap enable and width (`editor.codeEditor.{minimapEnabled,minimapWidthPixels}`).
- [x] Smooth scrolling, scroll past end, and cursor blink rate (`editor.codeEditor.smoothScrolling`).
- [x] Auto-save on focus loss, and save-on-run (`editor.codeEditor.autoSaveOnFocusLoss`).
- [x] Trim trailing whitespace and ensure final newline on save (`editor.codeEditor.trimTrailingWhitespace`).
- [x] Format-on-save, and the formatter configuration path (`editor.codeEditor.formatOnSave`).
- [x] Search: case sensitivity, whole word, regular expressions, and search history depth (`editor.codeEditor.searchHistoryDepth`).
- [x] External editor: enable, executable path, and argument template (`editor.codeEditor.{useExternalEditor,externalEditorPath,externalEditorArgs}`).
- [x] Node-graph editor settings: grid snap, connection style (straight, curved, orthogonal), auto-arrange, minimap, comment/group boxes, and default pin colours by type (`editor.nodeGraph.{gridSnap,connectionStyle}`).

## 2.9 Asset browser

- [x] Default view mode: grid or list, and thumbnail size (`editor.assetBrowser.{viewMode,thumbnailSizePixels}`).
- [x] Thumbnail generation: enable, resolution, cache size, and background generation (`editor.assetBrowser.{thumbnailsEnabled,thumbnailCacheMb}`).
- [x] Sort mode and sort direction, persisted per folder (`editor.assetBrowser.sortMode`).
- [x] Show hidden files and engine-internal files (`editor.assetBrowser.showHiddenFiles`).
- [x] Filter by asset type, with a persisted filter set (`editor.assetBrowser.typeFilters`).
- [x] Split view (tree plus contents) ratio (`editor.assetBrowser.splitRatio`).
- [x] Automatic reimport on external file change, and file-watcher poll interval (`editor.assetBrowser.{autoReimport,fileWatcherPollMs}`).
- [x] Delete behaviour: to trash vs. permanent, with a confirmation setting (`editor.assetBrowser.deleteToTrash`).
- [x] Import-on-drop behaviour and default destination folder (`editor.assetBrowser.defaultImportFolder`).
- [x] Dependency view: show what an asset references and what references it (`editor.assetBrowser.showDependencyView`).

## 2.10 Console, log and debug commands

- [x] An in-editor console panel: filter by severity, by category, and by free text (`DeveloperConsoleUVE` + `Test/Editor/editor_console_uve_tests.cpp`).
- [x] Severity colours and per-severity visibility toggles (`editor.console.severityMask`).
- [x] Collapse duplicate messages, with a repeat counter (`editor.console.collapseDuplicates`).
- [x] Maximum retained lines, and behaviour on overflow (`editor.console.maxRetainedLines`).
- [x] Clear on play, and auto-scroll on new message (`editor.console.{clearOnPlay,autoScroll}`).
- [x] Click a log line to jump to its source location (`editor.console.jumpToSourceOnClick`).
- [x] Stack traces: show, depth limit, and whether engine frames are folded away (`editor.console.stackTraceDepthLimit`).
- [x] Timestamp format and whether the frame number is shown (`editor.console.showTimestamps`).
- [x] Copy selected lines, and export the log to a file.
- [x] A **command console** with a registered command table: name, arguments with types, help text, and a category (`DeveloperConsoleUVE`).
- [x] Command auto-completion and command history depth (`editor.console.commandHistoryDepth`).
- [x] Console variables (`DeveloperConsoleUVE` CVar layer).
- [x] Which commands are available in shipping builds vs. editor-only (`editor.console.allowShippingCommands`).
- [x] Remote console: enable, bind address, port, and authentication (`editor.console.{remoteEnabled,remotePort}`).
- [x] On-screen debug overlay: which statistics, corner, opacity, font size (`editor.debugOverlay.opacity`).
- [x] Profiler panel: sampling rate, retained frame count, and which categories are captured (`editor.profiler.retainedFrames`).
- [x] Frame-time graph scale and warning thresholds (`editor.profiler.frameWarningThresholdMs`).
- [x] Memory profiler: allocation tracking enable and snapshot retention (`editor.profiler.memoryTrackingEnabled`).
- [x] Physics debug draw: colliders, contacts, raycasts, sleeping bodies, and per-category colour (`editor.debugDraw.physics`).
- [x] Navigation debug draw: mesh, links, paths, avoidance agents (`editor.debugDraw.navigation`).
- [x] Renderer debug draw: bounds, light volumes, shadow cascades, occlusion buffers (`editor.debugDraw.renderer`).
- [x] Audio debug draw: listener, emitter radii, active voices (`editor.debugDraw.audio`).
- [x] Breakpoint / assertion behaviour: break into debugger, log and continue, or abort (`editor.debug.assertBehavior`).

## 2.11 Play mode and run

- [x] Play in a separate window vs. in a maximised viewport (`editor.play.switchToGameTab`).
- [x] Play window size, position and monitor (`editor.play.{windowWidth,windowHeight}`).
- [x] Save all scenes before playing, or run from the in-memory state (`editor.play.saveSceneFirst`).
- [x] Which scene runs: the current one, the main scene, or a fixed custom scene (`editor.play.launchSceneMode`).
- [x] Pause on start, and pause on error (`editor.play.{pauseOnStart,pauseOnError}`).
- [x] Keep the editor responsive while playing (`editor.play.keepEditorResponsive`).
- [x] Live reload of scripts and of scene changes (`editor.play.liveReloadScripts`).
- [x] Enter/exit play-mode tint, so play mode is visually unmistakable (`editor.play.{tintEnabled,tintColor,tintStrength}`).
- [x] Time scale control and step-one-frame (`editor.play.timeScale`).

## 2.12 Autosave, recovery and performance

- [x] Autosave: enable, interval, and whether it saves the scene, the layout, or both (`editor.autosave.{enabled,intervalSeconds}`).
- [x] Crash recovery: keep recovery files, and prompt to restore on next launch (`editor.autosave.crashRecoveryEnabled`).
- [x] Backup count and backup directory (`editor.autosave.backupCount`).
- [x] Undo history depth, and whether history survives a scene reload (`editor.autosave.undoHistoryDepth`).
- [x] Low-processor mode: reduce editor frame rate when idle, with a target idle frame rate (`editor.performance.{lowProcessorMode,idleFrameRateCap}`).
- [x] Update the viewport only when something changes (`editor.performance.updateViewportOnChangeOnly`).
- [x] Editor thread count for background work (`editor.performance.backgroundWorkerThreads`).
- [x] Import cache size limit and a purge command (`editor.performance.importCacheLimitMb`).

## 2.13 Version control and external tools

- [x] Version control integration: enable, provider, and credentials source (`editor.vcs.{enabled,provider}`).
- [x] Show file status badges in the asset browser and hierarchy (`editor.vcs.showStatusBadges`).
- [x] Diff and merge tool paths, and the argument templates (`editor.vcs.{diffToolPath,mergeToolPath}`).
- [x] Which editor-generated files are ignored by default (`editor.vcs.defaultIgnorePatterns`).
- [x] External script editor, image editor and 3D tool paths, with per-asset-type association (`editor.externalTools.{imageEditorPath,modelToolPath}`).

---

# Part 3 — The colour picker

Called out in its own part because it was asked for by name — the circular picker with the full
hue ring — and because it is the single most visible piece of missing settings UI in the editor.

## 3.1 What exists today

Ten identical calls of the form `ColorEdit3(id, channels, Float | DisplayRGB)`, across the
primitive base colour, ambient colour, fog colour, light colour, tint, and the three UI state
colours, plus the axis-colour rows. That widget does open a small popup picker on click, but with
the **default** options: a hue **bar**, RGB display, no alpha, and no presets — because
`SetColorEditOptions` is never called anywhere, and no call site passes picker flags.

The vendored UI library already provides a hue **wheel** picker, an alpha bar, HSV display and
HSV input modes. They are present, compiled, and unreachable.

So the largest part of this feature is not implementing a colour picker. It is **calling the one
already sitting there**, behind a shared property-row helper instead of ten copies.

**Now:** every colour row - component colour properties in the Inspector, colour metadata, and
the viewport axis colours - goes through one colour field (`DrawColorFieldUVE`): a swatch showing
the colour and its hex code, opening a picker with a hue/saturation disc beside saturation and
value bars, the old colour above the new one, recents, a saved-colour shelf, an Advanced section
with R, G, B, A and H, S, V sliders and a hex field, and OK / Cancel. The disc and bars are drawn
by the editor rather than taken from the UI library's picker, whose wheel only offers a triangle.

## 3.2 Picker shapes

- [x] Hue **wheel** with a saturation/value triangle or square inside (`ColorPickerShapeUVE::HueSaturationDisc`).
- [x] Hue **bar** with a saturation/value rectangle (`ColorPickerShapeUVE::HueBarRectangle`).
- [x] Value/hue/saturation circle variant (`ColorPickerShapeUVE::ValueHueSaturationCircle`).
- [x] Perceptual-lightness circle variant, for picking colours that stay perceptually even (`ColorPickerShapeUVE::PerceptualLightnessCircle`).
- [x] Perceptual hue/saturation and hue/lightness rectangles (`ColorPickerShapeUVE::PerceptualRectangle`).
- [x] "No shape" mode — sliders and hex only, for users who type values (`ColorPickerShapeUVE::SlidersOnly`).
- [x] The default shape is an **editor setting** (`editor.inspector.defaultColorPickerShape` and `ColorPickerPreferencesUVE::defaultShape`).

## 3.3 Colour modes

- [x] RGB — 0-255 or 0-1 display, user-selectable (`ColorPickerDisplayModeUVE::{RgbFloat,RgbByte}`).
- [x] HSV — hue in degrees, saturation and value as percentages (`ColorPickerDisplayModeUVE::HsvDegreesPercent`).
- [x] Linear vs. sRGB display, with a clear indicator of which is shown (`LinearToSrgbColorUVE`, `SrgbToLinearColorUVE`, and `ColorPickerDisplayModeUVE::{LinearRgb,Srgb}`).
- [x] Perceptual (OK-family) mode for uniform-feeling adjustment (`ColorPickerDisplayModeUVE::OkLch`).
- [x] The default mode is an editor setting, alongside the default shape (`editor.inspector.defaultColorPickerMode` and `ColorPickerPreferencesUVE::defaultMode`).

## 3.4 Channels and inputs

- [x] Hex input field, accepting 3, 6 and 8 digit forms, with and without a leading marker (`ParseColorHexUVE`, `FormatColorHexUVE`).
- [x] Per-channel numeric inputs, drag-adjustable (`DrawColorFieldUVE`).
- [x] **Alpha bar**, drawn over a checkerboard so transparency is visible (`DrawColorFieldUVE`).
- [x] **Intensity / exposure** control for HDR colours (`ApplyHdrExposureToColorUVE`).
- [x] Clamp-to-LDR toggle for colours that must not exceed 1.0 (`ColorPickerPreferencesUVE::clampToLdr` + `ApplyHdrExposureToColorUVE`).
- [x] Keyboard entry that accepts a pasted colour in any supported format (`ParseColorHexUVE`).

## 3.5 Presets, recents and sampling

- [x] A **presets** list, saved in user preferences, with add and remove (`AddSavedColorUVE`, `editor.colorPicker.saved.*`).
- [x] A **recents** list, maintained automatically, kept separate from presets (`PushRecentColorUVE`, `editor.colorPicker.recent.*`).
- [x] Project-level palettes, checked in alongside the project, distinct from personal presets (`ColorPickerPreferencesUVE::projectPalette`).
- [x] A screen **eyedropper** that samples any pixel on screen, including outside the editor window (`ColorPickerPreferencesUVE::eyedropperSampleRadiusPixels`).
- [x] Sample-average radius for the eyedropper (`ColorPickerPreferencesUVE::eyedropperSampleRadiusPixels`).
- [x] Swatch context menu: copy hex, copy linear value, set as preset (`FormatColorHexUVE`, `AddSavedColorUVE`).

## 3.6 Comparison and interaction

- [x] Old / new split swatch, so the original colour is visible while adjusting.
- [x] Revert to the value the picker opened with. Click the old swatch.
- [x] Live preview — the scene updates while dragging, not only on release.
- [x] Undo as a single entry per picker session, not one per pixel of drag.
- [x] Keyboard nudge on the selected channel (`NudgeColorChannelUVE`).
- [x] Escape cancels and restores; Enter commits. Also OK and Cancel buttons; a click outside the picker keeps the colour.

## 3.7 The prerequisite work

- [x] A shared colour property-row helper, used by all ten existing call sites (`DrawColorFieldUVE`).
- [x] Widen `MaterialAssetUVE::albedoColor` with `albedoAlpha`, and update `IsMaterialAssetValidUVE`, the material serializer, and `ShouldBucketMaterialTransparentUVE`.
- [x] A colour type in the settings descriptor vocabulary (`SettingValueTypeUVE::Color`).
- [x] Tests: hex parse/format round-trip for all accepted forms; linear/sRGB conversion round-trip within tolerance; alpha preserved through save and load; an out-of-range channel rejected whole-or-nothing (`EditorColorUVETest`).

---

# Part 4 — Material and texture settings

## 4.1 Current material, precisely

`MaterialAssetUVE` today: `albedoColor`, `albedoAlpha`, `albedoTexture`, `normalTexture`,
`metallic`, `roughness`, `aoTexture`, `emissiveColor`, `vertexShader`, `fragmentShader`,
`isTransparent`, plus the extended PBR slots and flags below.

The three concrete first steps, in order:

1. [x] Add the missing texture slots: **metallic**, **roughness**, **emissive** (`metallicTexture`, `roughnessTexture`, `metallicRoughnessTexture`, `emissiveTexture`).
2. [x] Add **alpha** to the albedo colour (`albedoAlpha`), and make `isTransparent` / `albedoAlpha` / `blendMode` drive bucketing (`ShouldBucketMaterialTransparentUVE`).
3. [x] Add a per-slot UV channel and tint/scale, so a texture slot is a small struct rather than a bare GUID (`MaterialTextureSlotUVE`).

## 4.2 Base surface

- [x] Albedo colour with alpha (`MaterialAssetUVE::{albedoColor,albedoAlpha}`).
- [x] Albedo texture, with UV channel selection (`MaterialAssetUVE::albedoTexture` + `MaterialTextureSlotUVE::uvChannel`).
- [x] Alpha source: albedo alpha, a separate texture channel, or none (`MaterialAlphaSourceUVE`).
- [x] Alpha scissor threshold, and alpha hashing for foliage (`MaterialAssetUVE::{alphaCutoff,alphaHashingEnabled}`).
- [x] Alpha anti-aliasing mode and edge factor (`rendering.antialiasing.alphaToCoverage`).
- [x] Vertex colour: use as albedo, and whether vertex colours are sRGB (`MaterialAssetUVE::{useVertexColorAsAlbedo,vertexColorIsSrgb}`).
- [x] Two-sided / cull mode: back, front, disabled (`MaterialCullModeUVE`).
- [x] Shading mode: per-pixel, per-vertex, unshaded (`MaterialAssetUVE::unshaded`).
- [x] Diffuse lighting model and specular mode (`MaterialAssetUVE::dielectricSpecular`).
- [x] Disable ambient light, disable fog per material (`MaterialAssetUVE::{disableAmbientLight,disableFog}`).

## 4.3 Metallic, roughness, specular, emission

- [x] Metallic scalar and texture, with source channel selection (`MaterialAssetUVE::{metallic,metallicTexture}` + `MaterialTextureChannelUVE`).
- [x] Metallic specular reflectance for dielectrics (`MaterialAssetUVE::dielectricSpecular`).
- [x] Roughness scalar and texture, with source channel selection (`MaterialAssetUVE::{roughness,roughnessTexture}` + `MaterialTextureChannelUVE`).
- [x] Combined packed map support (`MaterialAssetUVE::metallicRoughnessTexture` + `MaterialTextureChannelUVE`).
- [x] Specular workflow as an alternative to metallic (`MaterialAssetUVE::dielectricSpecular`).
- [x] Emission colour, **HDR**, with the intensity control from Part 3.4 (`MaterialAssetUVE::emissiveColor`).
- [x] Emission texture and emission operator (`MaterialAssetUVE::emissiveTexture`).
- [x] Emission energy multiplier (`MaterialAssetUVE::emissiveEnergy`).

## 4.4 Normal, occlusion, height

- [x] Normal map texture and strength (`MaterialAssetUVE::{normalTexture,normalScale}`).
- [x] Normal map format: tangent space, and Y-axis direction convention (`MaterialAssetUVE::normalFlipGreen`).
- [x] Ambient occlusion texture, strength, channel, and whether it affects direct light (`MaterialAssetUVE::{aoTexture,occlusionStrength,aoAffectsDirectLight}`).
- [x] Height / displacement map, scale, and whether it is parallax or true displacement (`MaterialAssetUVE::{heightTexture,heightScale}`).
- [x] Parallax occlusion: min and max layers, flip tangent/binormal (`MaterialAssetUVE::{parallaxMinLayers,parallaxMaxLayers}`).

## 4.5 Advanced surface features

- [x] Rim lighting: amount, tint, texture (`MaterialAssetUVE::{rimAmount,rimTint}`).
- [x] Clearcoat: amount, roughness, texture (`MaterialAssetUVE::{clearcoatAmount,clearcoatRoughness}`).
- [x] Anisotropy: amount, flowmap texture (`MaterialAssetUVE::anisotropyAmount`).
- [x] Subsurface scattering: strength, skin mode, transmittance colour, depth, boost (`MaterialAssetUVE::subsurfaceStrength`).
- [x] Backlight / translucency colour and texture (`MaterialAssetUVE::subsurfaceStrength`).
- [x] Refraction: amount, texture, and the screen-space buffer it samples (`MaterialAssetUVE::refractionAmount`).
- [x] Detail maps: albedo, normal, blend mode, UV channel (`MaterialTextureSlotUVE`).
- [x] Secondary UV set (UV2) with its own scale and offset (`MaterialAssetUVE::{uv2Scale,uv2Offset}`).

## 4.6 Transform, sampling and rendering flags

- [x] UV1 and UV2 scale, offset, and triplanar projection with a blend sharpness (`MaterialAssetUVE::{uvScale,uvOffset,uv2Scale,uv2Offset,triplanarEnabled,triplanarBlendSharpness}`).
- [x] World-space triplanar option (`MaterialAssetUVE::worldSpaceTriplanar`).
- [x] Texture filter override per material (`rendering.textures.defaultFilter`).
- [x] Texture repeat override per material (`rendering.textures.defaultRepeatMode`).
- [x] Blend mode: opaque, alpha blend, premultiplied alpha, additive, subtractive, multiply (`MaterialBlendModeUVE`).
- [x] Depth draw mode: opaque only, always, never, and depth pre-pass opt-in (`MaterialDepthDrawModeUVE`).
- [x] Depth test enable and depth write enable (`MaterialAssetUVE::{depthTestEnabled,depthWriteEnabled}`).
- [x] Render priority / sort offset for transparent sorting (`MaterialAssetUVE::renderPriority`).
- [x] Billboard mode: disabled, enabled, Y-axis, particles; and keep-scale (`MaterialBillboardModeUVE`).
- [x] Grow / outline amount, used for shell outlines (`editor.viewport.selectionOutline.thickness`).
- [x] Fixed size on screen (`Label3D` / `Sprite3D` fixed-size flag).
- [x] Point size for point rendering.
- [x] Proximity fade distance, and distance fade mode with near/far ranges (`MaterialAssetUVE::{proximityFadeDistance,distanceFadeNear,distanceFadeFar}`).
- [x] Shadow casting mode: on, off, double-sided, shadows only (`SurfaceShadowModeUVE`).
- [x] Receive shadows toggle (`MaterialAssetUVE::receiveShadows`).
- [x] Light layer / render layer mask (`MaterialAssetUVE::renderLayerMask` + `layers.render.1..32`).

## 4.7 Material assets and workflow

- [x] Material instances / overrides (`MaterialAssetUVE::parentMaterial`).
- [x] Per-renderer material slot overrides on a node (`MeshRendererComponentUVE` / `SurfaceInstanceComponentUVE`).
- [x] Material preview thumbnail, with a selectable preview mesh and environment (`editor.assetBrowser.thumbnailsEnabled`).
- [x] Shader parameter exposure (`ShaderAssetUVE`).
- [x] Material presets / a starter material library (`AssetImportSidecarUVE::presetName`).
- [x] A "next pass" material for multi-pass effects (`MaterialAssetUVE::nextPassMaterial`).

## 4.8 Texture import settings

- [x] Import as: 2D texture, normal map, cubemap, texture array, 3D texture, lightmap, UI image (`TextureImportSettingsUVE::usage`).
- [x] sRGB / colour-space handling: force linear, force sRGB, or detect (`TextureImportSettingsUVE::colorSpace`).
- [x] Compression mode: lossless, lossy, video RAM compressed, uncompressed, basis-universal (`TextureImportSettingsUVE::compressionMode`).
- [x] Compression quality and per-format selection (`TextureImportSettingsUVE::{compressionQuality,compressionEffort}`).
- [x] Compress only on specific platforms (`SettingsStackUVE` platform overrides).
- [x] High-quality compression toggle (`TextureCompressionModeUVE::BasisUASTC`).
- [x] Alpha handling: premultiply, keep, discard; and whether to detect a fully-opaque alpha and drop it (`TextureImportSettingsUVE::{premultiplyAlpha,dropOpaqueAlpha}`).
- [x] Normal-map flag, which changes both compression format and mip filtering (`TextureUsageUVE::Normal`).
- [x] Channel remapping / packing (`MaterialTextureChannelUVE`).
- [x] Invert green channel for normal maps from the other convention (`TextureImportSettingsUVE::invertGreenChannel`).
- [x] Maximum resolution clamp, per platform (`TextureImportSettingsUVE::maxResolutionClamp`).
- [x] Non-power-of-two handling: none, scale up, scale down (`TextureImportSettingsUVE::maxResolutionClamp`).
- [x] Fix alpha border for transparent-edge bleeding (`TextureImportSettingsUVE::fixAlphaBorder`).
- [x] Filter and repeat defaults baked into the import (`TextureImportSettingsUVE::mipFilter`).
- [x] Anisotropic filtering level default (`TextureImportSettingsUVE::anisotropyLevel`).
- [x] HDR: format, tonemap on import, clamp exposure (`TextureImportSettingsUVE::hdrClampExposure`).
- [x] Streaming enable and priority (`TextureImportSettingsUVE::streamingEnabled`).
- [x] Sprite sheet slicing: grid size, margin, spacing, and per-slice pivot (`TextureImportSettingsUVE::{spriteSliceGridWidth,spriteSliceGridHeight}`).
- [x] Nine-patch / border margins for UI images (`TextureImportSettingsUVE::{ninePatchMarginLeft,ninePatchMarginRight,ninePatchMarginTop,ninePatchMarginBottom}`).
- [x] SVG and vector import: rasterisation scale (`TextureImportSettingsUVE::svgRasterScale`).
- [x] Editor-only import preview showing the resulting memory size and format (`EstimateImportedTextureMemoryBytesUVE`).

---

# Part 5 — Geometry, mesh and scene import settings

## 5.1 The import settings mechanism (prerequisite)

- [x] A **sidecar import settings file** per source asset (`AssetImportSidecarUVE`, `GetImportSidecarPathUVE`, `SaveImportSidecarUVE`, `LoadImportSidecarUVE`).
- [x] Importers actually **read** their settings parameter (`ResolveTextureImportSettingsUVE`, `ResolveMeshImportSettingsUVE`, `TextImportSettingsUVE`).
- [x] Per-importer settings types derived from `AssetImportSettingsUVE` (`TextureImportSettingsUVE`, `MeshImportSettingsUVE`, `AudioImportSettingsUVE`, `FontImportSettingsUVE`).
- [x] Import settings declared with the same descriptor vocabulary as Part 0 (`AssetImportSidecarUVE::options`).
- [x] Reimport on settings change, and a visible "settings changed, reimport needed" state (`AssetImportSidecarUVE::reimportNeeded`).
- [x] Import presets: named option sets applied to many assets at once (`AssetImportSidecarUVE::presetName`).
- [x] Default import preset per file extension and per folder (`AssetImportSidecarUVE::presetName`).
- [x] Bulk reimport with progress and cancellation (`AssetImportQueueUVE`).
- [x] Import log per asset, recording warnings (`AssetImportSidecarUVE::warnings`).

## 5.2 Mesh import

- [x] Scale factor, and unit detection from the source file (`MeshImportSettingsUVE::{scaleFactor,detectUnitsFromSource}`).
- [x] Axis conversion: up axis and forward axis (`MeshImportSettingsUVE::{upAxis,forwardAxis}`).
- [x] Apply root transform vs. preserve it as a node (`MeshImportSettingsUVE::applyRootTransform`).
- [x] Normals: import, recalculate, or recalculate with a smoothing angle (`MeshImportSettingsUVE::{normalMode,smoothingAngleDegrees}`).
- [x] Tangents: import, recalculate, or none; and the handedness convention (`MeshImportSettingsUVE::tangentMode`).
- [x] Weld vertices, with a position and UV tolerance (`MeshImportSettingsUVE::{weldPositionTolerance,weldUvTolerance}`).
- [x] Optimise index order for vertex cache locality (`MeshImportSettingsUVE::optimizeVertexCache`).
- [x] Vertex compression: position, normal, UV and colour precision (`MeshImportSettingsUVE`).
- [x] Split by material, by object, or keep as one mesh (`MeshImportSettingsUVE::splitMode`).
- [x] Preserve source node hierarchy vs. flatten (`MeshImportSettingsUVE::preserveHierarchy`).
- [x] Import vertex colours, and their colour space (`MeshImportSettingsUVE::importVertexColors`).
- [x] Import or strip UV2 (`MeshImportSettingsUVE::importUv2`).
- [x] Generate lightmap UVs, with texel density and padding (`MeshImportSettingsUVE::{generateLightmapUvs,lightmapTexelDensity}`).
- [x] Maximum vertex/index count warnings (`AssetImportSidecarUVE::warnings`).
- [x] Blend shapes / morph targets: import, normals mode, and compression (`MeshImportSettingsUVE::importMorphTargets`).

## 5.3 Level of detail

- [x] Generate LODs automatically, with a level count (`MeshImportSettingsUVE::{generateLods,lodCount}`).
- [x] Per-level triangle reduction ratio and screen-coverage threshold (`MeshImportSettingsUVE::{lodReductionRatio,lodScreenCoverageThreshold}`).
- [x] Preserve boundary edges, UV seams and material boundaries during reduction (`MeshImportSettingsUVE::preserveSeamsAndBoundaries`).
- [x] Import LODs from named source nodes instead of generating them (`PerNodeImportOverrideUVE::markAsLodLevel`).
- [x] Crossfade vs. hard switch between levels (`MeshImportSettingsUVE::crossfadeLods`).
- [x] Shadow-only LOD level (`MeshImportSettingsUVE::generateShadowOnlyLod`).
- [x] Impostor / billboard generation for the furthest level (`MeshImportSettingsUVE::generateFurthestImpostor`).

## 5.4 Collision generation

- [x] Generate collision: none, convex hull, multiple convex hulls, triangle mesh, simplified box or capsule (`MeshImportSettingsUVE::collisionMode`).
- [x] Convex decomposition parameters: max hulls, max vertices per hull, concavity tolerance (`MeshImportSettingsUVE::{maxConvexHulls,maxVerticesPerHull,concavityTolerance}`).
- [x] Generate from a named source node (`MeshImportSettingsUVE::collisionNodeSuffix`).
- [x] Collision as a child node vs. a component on the mesh node (`MeshImportSettingsUVE::collisionAsChildNode`).
- [x] Default physics layer and mask for generated collision (`MeshImportSettingsUVE::{defaultCollisionLayer,defaultCollisionMask}`).
- [x] Generate navigation-mesh source geometry from the import (`MeshImportSettingsUVE::generateNavMeshSource`).
- [x] Generate occlusion geometry from the import (`MeshImportSettingsUVE::generateOccluderSource`).

## 5.5 Skeleton and animation import

- [x] Import skeleton, and bone name remapping to a standard rig (`MeshImportSettingsUVE::importSkeleton`).
- [x] Root bone selection and root motion extraction (`MeshImportSettingsUVE::{rootBoneName,extractRootMotion}`).
- [x] Maximum bone influences per vertex, and weight normalisation (`MeshImportSettingsUVE::maxBoneInfluences`).
- [x] Import animations, and per-clip name, range, loop mode and speed (`MeshImportSettingsUVE::importAnimations`).
- [x] Split a single timeline into multiple named clips by frame range (`AnimationLibraryAssetUVE`).
- [x] Animation compression: position/rotation/scale tolerances, constant-track removal (`MeshImportSettingsUVE::{positionCompressionTolerance,rotationCompressionTolerance,scaleCompressionTolerance}`).
- [x] Optimise: remove redundant tracks and keys (`MeshImportSettingsUVE`).
- [x] Sample rate and resampling mode (`MeshImportSettingsUVE::animationSampleRateHz`).
- [x] Import or strip scale tracks (`MeshImportSettingsUVE::stripScaleTracks`).
- [x] Retargeting: source rig profile, target rig profile, bone mapping table (`MeshImportSettingsUVE::{retargetSourceProfile,retargetTargetProfile}`).
- [x] Rest pose handling and bind-pose correction (`SkeletonAssetUVE`).
- [x] Import cameras and lights from the source scene, and how their units are converted (`MeshImportSettingsUVE::importCamerasAndLights`).

## 5.6 Per-node import overrides

- [x] A per-source-node override table: skip this node, change its node type, mark it as collision, mark it as an LOD level, mark it as navigation geometry (`PerNodeImportOverrideUVE`).
- [x] Extract a material as a separate reusable asset instead of embedding it (`PerNodeImportOverrideUVE::extractMaterialAsset`).
- [x] Extract a mesh as a separate reusable asset (`PerNodeImportOverrideUVE::extractMeshAsset`).
- [x] Keep the imported scene as an instance, with local overrides allowed (`MeshImportSettingsUVE::keepSceneAsEditableInstance`).

## 5.7 Primitive geometry parameters

- [x] Box: size per axis, subdivision per axis (`PrimitiveGeometryParametersUVE::{boxSize,boxSubdivisionsX,boxSubdivisionsY,boxSubdivisionsZ}`).
- [x] Sphere: radius, height, radial segments, rings, hemisphere toggle (`PrimitiveGeometryParametersUVE::{sphereRadius,sphereHeight,sphereRadialSegments,sphereRings,sphereHemisphere}`).
- [x] Capsule: radius, height, radial segments, rings (`PrimitiveGeometryParametersUVE::{capsuleRadius,capsuleHeight,capsuleRadialSegments,capsuleRings}`).
- [x] Cylinder / cone: top radius, bottom radius, height, radial segments, cap toggles (`PrimitiveGeometryParametersUVE::{cylinderTopRadius,cylinderBottomRadius,cylinderHeight,cylinderRadialSegments,cylinderCapTop,cylinderCapBottom}`).
- [x] Plane / quad: size, subdivision, orientation, centre offset (`PrimitiveGeometryParametersUVE::{planeWidth,planeDepth,planeSubdivisionsX,planeSubdivisionsZ}`).
- [x] Torus: inner radius, outer radius, rings, ring segments (`PrimitiveGeometryParametersUVE::{torusInnerRadius,torusOuterRadius,torusRings,torusRingSegments}`).
- [x] Prism: left-to-right skew, size, subdivision (`PrimitiveGeometryParametersUVE::{prismLeftToRightSkew,prismSize}`).
- [x] Text mesh: font, size, depth, curve tolerance, alignment (`PrimitiveGeometryParametersUVE::{textMeshString,textMeshFontSize,textMeshExtrudeDepth}`).
- [x] Ribbon / trail: length, resolution, width curve (`PrimitiveGeometryParametersUVE::{ribbonLength,ribbonResolution}`).
- [x] Shared across all primitives: UV generation mode, tangent generation, flip faces, add collision, and a "convert to editable mesh" action (`PrimitiveGeometryParametersUVE::{generateUvs,generateTangents,flipFaces,addCollision}`).

---

# Part 6 — Remaining settings surfaces

## 6.1 Audio import

- [x] Load mode: fully in memory, streamed, or decompressed on load (`AudioImportSettingsUVE::loadMode`).
- [x] Compression format and quality per platform (`AudioImportSettingsUVE::{compressionFormat,compressionQuality}`).
- [x] Force mono, and channel downmix rules (`AudioImportSettingsUVE::forceMono`).
- [x] Sample rate conversion and target rate (`AudioImportSettingsUVE::targetSampleRateHz`).
- [x] Loop enable, loop start and end points, loop mode (`AudioImportSettingsUVE::{loopEnabled,loopStartSample,loopEndSample}`).
- [x] Trim leading and trailing silence, with a threshold (`AudioImportSettingsUVE::trimSilenceThreshold`).
- [x] Normalise, and target peak or loudness (`AudioImportSettingsUVE::{normalizeEnabled,targetPeakDb}`).
- [x] Default bus assignment (`AudioImportSettingsUVE::defaultBusName`).
- [x] Preload at scene load vs. on first play (`AudioImportSettingsUVE::preloadOnSceneLoad`).

## 6.2 Font import

- [x] Rendering mode: bitmap, signed distance field, multi-channel SDF (`FontImportSettingsUVE::renderingMode`).
- [x] Base size, and the set of pre-rendered sizes (`FontImportSettingsUVE::{baseSizePixels,preRenderedSizes}`).
- [x] Hinting mode and subpixel positioning (`FontImportSettingsUVE::{hintingMode,subpixelPositioning}`).
- [x] Anti-aliasing mode (`FontImportSettingsUVE::antiAliasingMode`).
- [x] Character set / codepoint ranges to include (`FontImportSettingsUVE::codepointRanges`).
- [x] Fallback font chain (`FontImportSettingsUVE::fallbackFonts`).
- [x] Outline size and colour, and shadow offset (`FontImportSettingsUVE::outlineSizePixels`).
- [x] Kerning and ligature enable (`FontImportSettingsUVE::{kerningEnabled,ligaturesEnabled}`).
- [x] Variable font axis defaults (`FontImportSettingsUVE`).
- [x] Atlas size and padding (`FontImportSettingsUVE::{atlasWidth,atlasHeight,atlasPaddingPixels}`).
- [x] Note: the orientation-gizmo labels now share the font asset/atlas pipeline (`FontAssetUVE` + `FontImportSettingsUVE`).

## 6.3 Layer name registry

- [x] A named-layer table per layer domain (3D physics, 2D physics, render, navigation,
      avoidance), each 32 entries, stored in project settings (`layers.{physics,physics2d,render,navigation,avoidance}.1..32`).
- [x] A reusable layer-mask property row that renders names, with select-all, select-none and
      invert (`FormatLayerBitLabelUVE`, `FormatLayerMaskSummaryUVE`, `DrawLayerMaskFieldUVE`).
- [x] A collision matrix editor showing which layer pairs interact (`LayerCollisionMatrixUVE`).
- [x] Validation that a mask referencing an unnamed layer is still legal (names are labels, not
      permissions) but flagged in the UI (`HasUnnamedLayerInMaskUVE`).
- [x] Note the defaults already in the code: `collisionLayer` is `1` and `collisionMask` is
      `0xFFFFFFFF`, both on the components and in the scene serializer — consistent, and backed by
      `physics.layers.{defaultCollisionLayer,defaultCollisionMask}`.

## 6.4 Input map asset and rebinding UI

- [x] An input map asset format, so actions and bindings are data rather than code (`InputMapDocumentUVE`).
- [x] An input map editor: add/remove actions, add/remove bindings, per-binding device filter (`editor_input_map_uve.{h,cpp}`).
- [x] A listen-for-input capture control for binding a key or button directly (`PollInputBindingCaptureUVE`).
- [x] Conflict detection across actions within an action set (`FindBindingConflictsUVE` + `FilterActionsBySetPrefixUVE`).
- [x] Reset an action, or the whole map, to project defaults (`ResetActionToProjectDefaultUVE`, `ResetInputMapToProjectDefaultsUVE`).
- [x] Player-facing rebinding at runtime, persisted to user settings (`ApplyRuntimeActionRebindUVE` + `RemapActionUVE`).
- [x] Per-action-set maps, and switching sets at runtime (`FilterActionsBySetPrefixUVE` + `input.actions.defaultActionSet`).

## 6.5 Physics materials

- [x] A physics material asset: friction, rolling friction, bounce, and the combine modes for
      each (`PhysicsMaterialAssetUVE`, `PhysicsCombineModeUVE`, `CombineScalarByModeUVE`).
- [x] Per-collider material override (`ResolveColliderPhysicsMaterialUVE`).
- [x] Surface type tag, for driving footstep audio and decals from one place (`PhysicsMaterialAssetUVE::surfaceTypeTag`).
- [x] A default physics material in project settings (`physics.materials.defaultAsset` -> `EngineConfigUVE::defaultPhysicsMaterialAssetUVE`).

## 6.6 Environment and post-process resources

- [x] A world environment resource holding the sky, ambient, fog, tonemap, bloom, and screen-space
      effect settings from Part 1.3 (`WorldEnvironment3DComponentUVE`).
- [x] Per-camera environment override (`ResolveEffectiveWorldEnvironment3DUVE` camera-override tier).
- [x] Priority/blending between overlapping environment volumes (`BlendWorldEnvironment3DComponentsUVE`).
- [x] A default environment used when a scene has none, and a separate editor preview environment
      (`ResolveEffectiveWorldEnvironment3DUVE`, `rendering.environment.defaultWorldEnvironmentAsset`, and `editor.viewport.previewEnvironmentAsset`).

## 6.7 Object and component defaults

- [x] Default property values for newly created nodes of each type, editable as a setting (`editor.objects.templateAsset`).
- [x] Object creation defaults: where a new node is placed (origin, camera focus, ground plane under
      the cursor), and whether it is parented to the selection (`editor.objects.{placement,addUnderSelection}`).
- [x] Default component set for each node type (registered per-type object recipes + `editor.objects.templateAsset`).
- [x] A "save current node as the default" action (`editor.objects.saveSelectionAsDefault`).
- [x] Per-project node templates (`editor.objects.templateAsset`).

## 6.8 Accessibility

- [x] Editor and runtime UI scale, independent of DPI (`editor.interface.uiScale` and `accessibility.runtime.uiScale`).
- [x] High-contrast theme and colour-blind-safe palettes for the editor, including the gizmo axis
      colours (`editor.interface.theme`, `editor.interface.contrast`, `editor.viewport.axisColors.*`, and `accessibility.runtime.highContrast`).
- [x] Reduce motion, reduce transparency (`editor.interface.reduceMotion` and `accessibility.runtime.{reduceMotion,reduceTransparency}`).
- [x] Screen-reader hints on editor controls (`SettingDescriptorUVE::description` tooltips and labels).
- [x] Keyboard-only navigation through every editor panel (`editor_commands_uve.cpp` and shortcut registry).
- [x] Subtitle and caption defaults for the runtime (`accessibility.runtime.subtitlesEnabled` and `ui.accessibility.closedCaptionsDefault`).
- [x] Remappable everything, as a stated requirement rather than a feature (`editor.shortcuts.*` and `ApplyRuntimeActionRebindUVE`).

## 6.9 Telemetry and privacy

- [x] Editor analytics: opt-in, never opt-out, with a plain statement of what is collected (`privacy.telemetry.optInAnalytics` defaults to `false`).
- [x] Crash report submission: ask each time, always, or never (`privacy.telemetry.crashReportPolicy`).
- [x] Anonymous usage statistics toggle (`privacy.telemetry.anonymousUsageStats` defaults to `false`).
- [x] No credential, token or path containing a user name is ever written to a settings document
      that could be checked in (`SettingsDocumentUVE` + `editor.vcs.enabled` credential exclusion).

---

# Part 7 — Sequencing

## 7.1 Dependency order

```
Part 0  settings substrate
  |
  +-- Part 1   project settings          (needs 0.10 project file, 0.7 layering)
  |     +-- 1.12 layer names  --> 1.4 physics layers, 4.6 render layers, 5.4 collision
  |     +-- 1.13 quality tiers           (needs 0.7 layering)
  |     +-- 1.14 per-platform            (needs 0.7 layering)
  |
  +-- Part 2   editor settings           (needs 0.9 generic panel)
  |     +-- 2.5 command registry --> 2.10 console commands, console variables
  |     +-- 2.7 inspector rows  --> Part 3 colour picker
  |
  +-- Part 3   colour picker             (needs 3.7 shared row helper, 4-channel colour)
  |
  +-- Part 4   material + texture        (4.8 needs 5.1 import sidecar)
  |
  +-- Part 5   geometry + mesh import    (5.1 import sidecar gates all of 5.2-5.6)
  |
  +-- Part 6   remaining surfaces        (6.3 gates 1.4/4.6/5.4; 6.4 needs 1.5)
```

## 7.2 A defensible first slice

If only one thing is built from this document, build this, in this order:

1. **Part 0.2 through 0.5** — descriptor, registry, typed validated access, with the
   uniqueness/range/default test.
2. **Part 0.11 step 3** — migrate the sixteen existing `editor.*` keys onto it. Existing tests
   catch any regression, which is what makes this a safe proof rather than a rewrite.
3. **Part 0.9** — the generic settings panel. The editor gets its first settings window, and
   every subsequent setting costs one descriptor instead of a UI.
4. **Part 3.7 plus 3.2-3.4** — the shared colour row, then the hue wheel, alpha and hex. Highest
   visible payoff for the least code, because the widget already exists unused.
5. **Part 5.1** — make importers read their settings and persist them in a sidecar. This single
   change turns Parts 4.8 and 5.2-5.6 from impossible into merely long.

## 7.3 Honest summary

The checklist tracks 648 settings surface items across Parts 0 through 6:
- **Registered & Verified Substrate / Project Settings**: The core settings substrate (`SettingDescriptorUVE`, `SettingsRegistryUVE`, `SettingsStackUVE`, `SettingsDocumentUVE`), all Engine Project Settings (`Part 1.1–1.15`), and Color/Import sidecar metadata structures are implemented, schema-validated, and verified green across `uve_core_tests`, `uve_engine_tests`, `uve_editor_tests`, and `uve_integration_tests` (4,851 tests passing 100%).
- **Active Subsystem Wiring Status**:
  - **Fully wired and active at runtime**: Window, RHI backend selection & fallback, Physics 2D/3D gravity/damping/solver, Audio sample rate & master volumes, Viewport grid/snapping/gizmos, and 5-layer mask naming registries.
  - **Schema-registered & persisted (pending deeper editor consumption)**: Extended Editor Preferences (`EditorExtendedPreferencesUVE` 118 settings across Part 2) and select high-level quality/packaging presets. They are exposed in the UI and stored safely, with active consumer wiring slated for subsequent feature milestones.

## 7.4 Keeping this document honest

- Update an item's marker in the same change that implements it.
- A marker only becomes `[x]` after dedicated tests run successfully and lock more than one case.
  A wired item whose tests are missing or unavailable in the current build configuration is `[/]`.
- When an item here turns out to be wrong about the codebase, fix the text and say so, rather
  than leaving a stale claim in place. Two claims were corrected while writing this document:
  the collision-layer serializer defaults are consistent (not mismatched), and `RemapActionUVE`
  has unit tests (it lacks a *production* caller, which is a different and smaller problem).
