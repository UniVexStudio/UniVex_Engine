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
- `[/]` = **wired, not fully verified** — a real system runs it, confirmed by reading the actual
  implementation, but it lacks the dedicated test coverage needed to call it verified.
- `[x]` = **verified** — real and working, confirmed by reading the source (not assumed), and
  backed by dedicated tests that lock more than one case.

The overwhelming majority of items below are `[ ]`. That is not pessimism; it is the accurate
state of the codebase today, and the whole reason this roadmap exists.

**Naming policy.** Consistent with the other planning documents in this repository, no
third-party engine or product is named here. Where an item exists because mature engines have
converged on it, that is stated as "the established convention" and described on its own terms.

## Current state, honestly stated

The following were confirmed by reading the source while writing this document.

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
and asset-database paths, project root) with a handful of command-line overrides. The `.uveditor`
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

### The conclusion that orders this entire document

Every settings page described in Parts 1 through 6 sits on top of a settings system that does not
exist. Building pages first means hand-writing validation, defaults, persistence and UI for each
of several hundred settings, at their own call sites — which is precisely the pattern the sixteen
existing editor keys already demonstrate at small scale.

So **Part 0 comes first**, and it is the only part written in full engineering detail.

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

- [ ] `SettingDescriptorUVE` — the single record describing one setting.
  - [ ] `id` — the dot path, e.g. `rendering.shadows.softShadowQuality`. The storage key.
  - [ ] `type` — bool, int, float/double, string, enum, colour, vector2/3/4, key binding, asset
        reference, file path, layer mask, string list.
  - [ ] `defaultValue` — the engine default, typed. The single source of "reset to default".
  - [ ] `minimum` / `maximum` / `step` — for numeric types; absent means unbounded.
  - [ ] `enumEntries` — ordered (value, label) pairs for enum types, so a combo box needs no
        hand-written label table.
  - [ ] `displayName` and `tooltip` — the human-facing strings. Without these, a generic settings
        panel can only show raw dot paths.
  - [ ] `category` / `page` — where it appears in the settings tree.
  - [ ] `flags` — `RestartRequired`, `Advanced` (hidden behind a toggle), `Hidden`,
        `PerPlatform` (may be overridden per target), `NotPersisted` (session-only),
        `Internal` (never shown in UI), `Deprecated` (read for migration, never written).
  - [ ] `sinceVersion` / `migratedFrom` — supports the migration path in 0.8.

The descriptor is **data**, declared next to the system that owns the setting, not centralised in
one giant file. A rendering setting is declared by the rendering module; the registry merely
collects them.

## 0.3 The registry

- [ ] `SettingsRegistryUVE` — collects descriptors, keyed by id, with duplicate-id detection at
      registration time (a duplicate is a programming error and should fail loudly in a test, not
      silently shadow).
- [ ] Registration is explicit and ordered, following the pattern already used by
      `RegisterBuiltInInspectorDrawersUVE()` — a real, extensible, string-keyed registry that this
      codebase already proves works.
- [ ] A test that asserts every registered id is unique, every default satisfies its own
      declared range, and every enum default is one of the declared entries. This one test
      removes an entire class of bug from all four hundred settings at once.

## 0.4 Typed access over the existing store

- [ ] Typed getters that take a descriptor (or id) and return the value already clamped and
      validated, with the declared default substituted for anything missing or malformed.
      `ConfigManagerUVE` remains underneath as the JSON document; nothing about it is replaced.
- [ ] Setters that reject out-of-range values rather than storing them, so a bad value can never
      enter the document in the first place.
- [ ] **Whole-or-nothing application for composite settings.** The axis-palette setter added for
      the viewport already establishes this rule: a palette with one bad channel leaves *both*
      the gizmo and the grid untouched rather than half-written. Composite settings — colours,
      vectors, key bindings — should follow it uniformly.

## 0.5 Validation at the boundary

- [ ] Every value crossing the file boundary is validated once, at the registry, against its
      descriptor. Consumers downstream receive values that are already legal and stop carrying
      defensive checks.
- [ ] Non-finite values (NaN, infinity) fail range checks by construction — a comparison against
      a bound is already false for NaN, which is the idiom the axis-palette validator uses.
- [ ] A corrupt or hand-edited settings file degrades to defaults **per setting**, never
      discarding the whole document.

## 0.6 Change notification

- [ ] An observer registration keyed by id or by category prefix, so the viewport can react to
      `editor.viewport.*` without polling every frame and without the settings layer knowing what
      a viewport is.
- [ ] Observers fire only on an actual change in value, not on every write.
- [ ] Clear documentation of which thread observers run on — the store is already mutex-guarded
      and callable from any thread, so this is not optional detail.

## 0.7 Layering and override order

- [ ] Resolution order, lowest to highest priority: **engine default → project setting → user
      preference → per-platform override → command-line override**.
- [ ] `EngineConfigUVE`'s existing command-line overrides become the top layer of this stack
      rather than a separate mechanism.
- [ ] A query for *where a value came from*. Without it, "I changed the setting and nothing
      happened" is undebuggable, because a higher layer may be silently winning.
- [ ] Per-layer save targets: user preferences never write into the project file, and vice versa.

## 0.8 Versioning and migration

- [ ] A document version, following the existing session-settings version idiom in
      `editor_uve.cpp`, which already bumps a version integer alongside the format.
- [ ] Forward migration steps registered per version, so an old settings file is upgraded on load
      rather than discarded.
- [ ] `Deprecated` descriptors read old ids and write the new ones, then stop.
- [ ] A test that loads a fixture of each historical version and asserts the migrated result.

## 0.9 Enumeration, search and the generic panel

This is the payoff, and it is why the descriptor carries display strings.

- [ ] Enumerate all descriptors, filtered by category, flags, or a search string matching **id,
      display name and tooltip**.
- [ ] A generic settings panel that renders a page purely from descriptors: a category tree on
      the left, the matching settings on the right, each rendered by its type.
- [ ] Per-type row renderers written **once** — bool, slider, drag, combo, colour, vector, path,
      key binding — instead of per setting.
- [ ] A modified-from-default indicator and a per-setting revert control, both of which are free
      once `defaultValue` is in the descriptor.
- [ ] An "advanced settings" toggle that reveals `Advanced`-flagged entries.
- [ ] A restart-required notice for `RestartRequired` entries.

Note the existing inspector search box matches **drawer ids only**, not property names, so typing
a property name hides everything. A registry-backed search fixes that same class of problem for
settings, and the inspector should eventually share the mechanism.

## 0.10 Project settings file

- [ ] A project settings document, separate from user preferences, checked into version control
      alongside the project.
- [ ] Deterministic key ordering on write, so the file produces clean diffs rather than
      reshuffling on every save.
- [ ] Only non-default values written, so the file stays readable and defaults can change in a
      later engine version without rewriting every project.

## 0.11 Substrate work items, in dependency order

1. [ ] `SettingDescriptorUVE` and the registry, with the uniqueness/range/default test.
2. [ ] Typed validated access over `ConfigManagerUVE`.
3. [ ] Migrate the sixteen existing `editor.*` keys onto descriptors, deleting their hand-rolled
       validation. This is the proof the substrate works, on real settings, with existing tests
       to catch a regression.
4. [ ] Change notification.
5. [ ] The generic settings panel with type-based row renderers.
6. [ ] Layering, override order, and the "where did this come from" query.
7. [ ] The project settings file and its layer.
8. [ ] Versioning and migration.

---

# Part 1 — Project settings

Settings that belong to the *project* and ship with the game. One line each; everything in this
part is `[ ]` unless noted, because there is no project settings file yet.

## 1.1 Application and metadata

- [ ] Product name, short name, description, version string, build number.
- [ ] Company / publisher name, copyright line.
- [ ] Unique application identifier (reverse-domain style) per target.
- [ ] Application icon set, per platform and per size.
- [ ] Main scene to load on launch.
- [ ] Splash/boot screen image, background colour, fade time, minimum display time, whether it is
      skippable.
- [ ] Boot logo behaviour in editor play mode (usually skipped).
- [ ] Quit-on-last-window-closed behaviour.
- [ ] Single-instance enforcement.
- [ ] Command-line argument documentation surface (which arguments the shipped build accepts).
- [ ] Custom user data directory name, and whether it is per-user or portable.
- [ ] Crash handler enable, crash dump directory, symbol upload endpoint.

## 1.2 Display and window

- [ ] Default window width and height.
- [ ] Window mode: windowed, maximised, fullscreen, exclusive fullscreen, borderless.
- [ ] Resizable, borderless, always-on-top, transparent background.
- [ ] Minimum and maximum window size.
- [ ] Initial window position and which monitor to open on.
- [ ] High-DPI awareness and per-monitor DPI scaling.
- [ ] Content scale factor override.
- [ ] Stretch mode: disabled, canvas items, viewport; and aspect policy: ignore, keep, keep width,
      keep height, expand.
- [ ] Integer-only scaling for pixel-art targets.
- [ ] Orientation for handheld targets, and allowed orientation set.
- [ ] V-sync mode: off, on, adaptive, mailbox.
- [ ] Frame rate cap, separate caps for focused and unfocused windows.
- [ ] Allow display sleep / keep screen on.
- [ ] Mouse cursor: custom image, hotspot, visibility default, confine-to-window default.
- [ ] Window title format, including whether the scene name is appended in editor play mode.

## 1.3 Rendering

The largest group by far, and the one most dependent on the renderer maturing.

### Pipeline and general

- [ ] Renderer backend selection and fallback order.
- [ ] Rendering method: forward, forward-plus, deferred, mobile/low-end path.
- [ ] Colour space: linear vs. gamma working space.
- [ ] HDR rendering enable, output transfer function, peak luminance, paper white.
- [ ] Render target format and precision (8/10/16-bit).
- [ ] Default clear colour.
- [ ] Render scale / dynamic resolution, with minimum scale and target frame time.
- [ ] Upscaling method and quality (spatial vs. temporal).
- [ ] Multi-threaded rendering enable and command-buffer thread count.
- [ ] Pipeline cache location and precompilation at startup.
- [ ] Shader compilation mode: ahead-of-time, on-demand, async with placeholder.
- [ ] GPU validation layers enable (debug builds).

### Anti-aliasing

- [ ] MSAA sample count, separately for 2D and 3D.
- [ ] Post-process AA method: none, fast approximate, temporal.
- [ ] Temporal AA sample count, jitter pattern, sharpening amount.
- [ ] Screen-space AA quality.
- [ ] Alpha-to-coverage for foliage-style materials.

### Shadows

- [ ] Shadow atlas size, and per-light quadrant subdivision.
- [ ] Directional shadow resolution and split count.
- [ ] Cascade split distribution (logarithmic/linear blend), and fade-out range.
- [ ] Shadow filter quality: hard, soft, very soft, and sample counts per tier.
- [ ] Depth bias, normal bias, slope-scaled bias defaults.
- [ ] Shadow maximum distance.
- [ ] 16-bit vs. 32-bit shadow depth format.
- [ ] Point/spot shadow cube resolution.
- [ ] Contact shadows enable, length, step count.

### Global illumination and reflections

- [ ] Ambient light source: none, flat colour, sky, environment map.
- [ ] Screen-space ambient occlusion: enable, radius, intensity, power, quality tier, blur.
- [ ] Screen-space indirect lighting: enable, quality, frame count.
- [ ] Screen-space reflections: enable, max steps, fade in/out, depth tolerance.
- [ ] Reflection probe atlas size and per-probe resolution.
- [ ] Voxel or signed-distance GI quality tier, bounce count, and update budget.
- [ ] Lightmap baking: resolution scale, bounce count, denoiser enable, directional lightmaps,
      texel density defaults.
- [ ] Light probe density defaults.

### Post-processing

- [ ] Tonemapper: linear, filmic, ACES-style, or custom curve; white point; exposure.
- [ ] Auto-exposure: enable, min/max luminance, adaptation speed up/down.
- [ ] Bloom: enable, threshold, soft knee, intensity, mip count.
- [ ] Depth of field: enable, focus mode, aperture, bokeh shape, quality.
- [ ] Motion blur: enable, strength, sample count, per-object vs. camera-only.
- [ ] Chromatic aberration, vignette, film grain, lens distortion.
- [ ] Colour grading: LUT slot, LUT size, and whether grading happens pre- or post-tonemap.
- [ ] Sharpening amount.
- [ ] Debanding / dithering enable.
- [ ] Fog: mode (linear, exponential, height), colour, density, start/end, sun scatter.
- [ ] Volumetric fog: enable, froxel resolution, depth distribution, temporal reprojection,
      anisotropy.

### Geometry, culling and batching

- [ ] Default near and far clip planes.
- [ ] Frustum culling enable and debug freeze.
- [ ] Occlusion culling: enable, method, buffer resolution, bake settings.
- [ ] LOD bias, minimum screen coverage, and crossfade mode.
- [ ] Mesh LOD auto-generation defaults.
- [ ] Instancing thresholds and per-instance data budget.
- [ ] Static batching enable and vertex budget.
- [ ] Indirect draw enable.
- [ ] Draw-call and triangle budget warnings.

### Textures and sampling

- [ ] Default texture filter: nearest, bilinear, trilinear, with or without mipmaps.
- [ ] Default anisotropic filtering level.
- [ ] Global mipmap LOD bias.
- [ ] Texture streaming enable, memory budget, mip bias under pressure.
- [ ] Maximum texture size per quality tier.
- [ ] Default texture repeat mode: disabled, enabled, mirrored.

### 2D rendering

- [ ] Default canvas item filter and repeat.
- [ ] Pixel snapping for 2D transforms and for vertices.
- [ ] 2D shadow atlas size.
- [ ] Sprite sorting axis and default sort layer set.

### Environment

- [ ] Default sky: none, colour, gradient, panorama, physical.
- [ ] Sky resolution and update mode (realtime, once, on demand).
- [ ] Default world environment resource for new scenes.

## 1.4 Physics

- [ ] Physics tick rate, and maximum substeps per frame.
- [ ] Engine selection per dimension, where more than one exists.
- [ ] Default gravity vector and magnitude, 2D and 3D separately.
- [ ] Default linear and angular damping.
- [ ] Sleep threshold, sleep time, and whether sleeping is allowed by default.
- [ ] Solver iteration counts: position and velocity.
- [ ] Contact offset, rest offset, penetration slop, max penetration recovery speed.
- [ ] Continuous collision detection mode default.
- [ ] Broadphase algorithm and world bounds.
- [ ] Default physics material: friction, bounce, combine modes.
- [ ] Layer/mask default for new bodies (today `1` and `0xFFFFFFFF`, consistently, but unnamed).
- [ ] Layer collision matrix — which layer collides with which.
- [ ] Trigger/area callbacks enable, and whether triggers report on static bodies.
- [ ] Raycast defaults: hit backfaces, hit triggers, max distance.
- [ ] Physics interpolation default — the engine already has an interpolation mode enum wired to
      rendering, so this setting has real backing the moment a project file exists.
- [ ] Physics debug draw defaults (see Part 6.6).
- [ ] Deterministic mode and fixed random seed.

## 1.5 Input

- [ ] The input map itself: named actions, each with positive and negative bindings.
- [ ] Per-action deadzone, sensitivity, and whether it is analog or digital.
- [ ] Device classes: keyboard, mouse, gamepad, touch, pen.
- [ ] Gamepad button/axis remapping table and per-controller-model mapping database.
- [ ] Gamepad deadzone shape (axial, radial, scaled radial) and inner/outer radius.
- [ ] Vibration/haptics enable and default strength.
- [ ] Mouse: raw input, acceleration, sensitivity, invert axes.
- [ ] Touch: emulate mouse from touch, emulate touch from mouse, multi-touch limit.
- [ ] Text input: IME enable, on-screen keyboard behaviour.
- [ ] Accumulated vs. per-frame input polling.
- [ ] Action-set / context switching (gameplay vs. menu vs. vehicle).
- [ ] Rebinding UI support, and where user rebinds are persisted — note `RemapActionUVE` already
      exists and is unit-tested, with no production caller.

## 1.6 Audio

- [ ] Output driver selection, and fallback order.
- [ ] Sample rate, buffer size / latency target, channel layout (mono, stereo, 5.1, 7.1).
- [ ] Bus layout: master plus named buses, each with volume, mute, solo, bypass.
- [ ] Per-bus effect chain defaults.
- [ ] Default listener model, doppler factor, speed of sound.
- [ ] 3D attenuation model, rolloff curve, min/max distance defaults.
- [ ] Voice limit and voice-stealing policy.
- [ ] Streaming threshold — file size above which audio streams rather than loads.
- [ ] Music vs. SFX vs. voice category volumes exposed to the player.
- [ ] Mute on focus loss.

## 1.7 Animation

- [ ] Default blend time and blend mode.
- [ ] Animation update rate and whether it is fixed or frame-rate driven.
- [ ] Root motion enable default.
- [ ] Bone LOD / animation LOD distance tiers.
- [ ] Animation compression: tolerance, keyframe reduction, curve fitting.
- [ ] Skinning method: CPU, GPU linear blend, dual quaternion; and max bone influences.
- [ ] Animation state machine defaults — the state machine already exists and carries a layer
      mask identifier, so its defaults are a real settings surface.
- [ ] IK solver iteration defaults.

## 1.8 Navigation

- [ ] Navigation mesh bake defaults: cell size, cell height, agent radius/height/max slope/max
      climb.
- [ ] Region minimum size, merge size, edge max length and error.
- [ ] Detail sample distance and max error.
- [ ] Navigation layer names and cost per layer.
- [ ] Path post-processing: string pulling, smoothing, corridor optimisation.
- [ ] Avoidance: enable, neighbour distance, time horizon, max neighbours.
- [ ] Async baking and per-frame bake budget.

## 1.9 Networking

- [ ] Default transport, port, and bind address.
- [ ] Maximum connections, max packet size, and channel count.
- [ ] Tick rate, send rate, and interpolation/extrapolation buffer.
- [ ] Compression mode and threshold.
- [ ] Encryption enable and certificate paths.
- [ ] Connection timeout and keepalive interval.
- [ ] Replication defaults: authority model, relevancy distance, priority.
- [ ] Lag compensation window.
- [ ] Network simulation for testing: latency, jitter, packet loss, duplication.

## 1.10 Localisation

- [ ] Locale list, default locale, and fallback chain.
- [ ] Translation file locations and format.
- [ ] Pseudolocalisation toggle, with expansion factor and accent/bracket options.
- [ ] Right-to-left layout support.
- [ ] Per-locale font override.
- [ ] Number, date and currency formatting mode.
- [ ] Whether untranslated strings are reported as warnings at build time.

## 1.11 File system and paths

- [ ] Project root, asset root, and import cache directory.
- [ ] User data directory and save-game directory.
- [ ] Ignored paths for the asset scanner.
- [ ] Case-sensitivity policy for asset paths.
- [ ] Maximum path length warnings.
- [ ] Pack/archive mount order and priority.
- [ ] Log file path and rotation policy — `EngineConfigUVE` already carries a log path and console
      toggle, so this is the natural first project setting to migrate.

## 1.12 Layer names

- [ ] Named 3D physics layers (32).
- [ ] Named 2D physics layers (32).
- [ ] Named render/visibility layers (20 or 32, depending on the renderer's budget).
- [ ] Named navigation layers (32).
- [ ] Named avoidance layers (32).
- [ ] A shared layer-name editor UI, one implementation reused by all of the above.
- [ ] A layer-mask property row that shows names instead of bits — this is the single change that
      makes layers usable at all.

## 1.13 Quality tiers

- [ ] Named quality presets (for example: low, medium, high, ultra) as a first-class concept.
- [ ] Each preset is a set of overrides over the rendering/physics/audio settings above.
- [ ] Per-platform default preset.
- [ ] Runtime preset switching, and which settings require a restart to take effect.
- [ ] Automatic tier detection from detected hardware, with a manual override.

## 1.14 Per-platform overrides

- [ ] A per-target override layer, so a setting can differ on desktop, handheld and console-class
      targets without duplicating the whole document.
- [ ] A UI that shows the base value, which targets override it, and the effective value per
      target.
- [ ] Feature tags per target, so code can query capabilities rather than platform names.

## 1.15 Packaging and build

- [ ] Export/build presets, named and reusable.
- [ ] Target platform, architecture, and build configuration per preset.
- [ ] Included/excluded asset filters per preset.
- [ ] Compression mode for packaged assets.
- [ ] Debug symbols: generate, strip, upload.
- [ ] Code signing identity and provisioning per platform.
- [ ] Asset encryption enable and key source.
- [ ] Embedded vs. loose asset packs.
- [ ] Post-build command hooks.
- [ ] Version and build-number auto-increment policy.

---

# Part 2 — Editor settings

Settings that belong to the *person using the editor*, not the project. These persist to user
preferences and are never checked in with the game.

This is where the engine is furthest along — and "furthest along" still means sixteen keys and no
settings window.

## 2.1 Interface and theme

- [ ] Theme selection, and custom theme files.
- [ ] Base colour, accent colour, contrast, and whether the theme is derived or hand-authored.
- [ ] Editor UI scale: automatic from DPI, or a manual factor.
- [ ] Main font, monospace font, font size, and font hinting/anti-aliasing mode.
- [ ] Font oversampling and subpixel positioning.
- [ ] Icon set and icon saturation.
- [ ] Border size, corner radius, and spacing density (compact / comfortable).
- [ ] Animation speed for UI transitions, and a "reduce motion" option.
- [ ] Tooltip delay and whether tooltips are shown at all.
- [ ] Editor language.
- [ ] Touchscreen-friendly hit targets.

## 2.2 Layout, docking and workspaces

- [ ] Dock layout persistence, and named saved layouts.
- [ ] Reset-to-default-layout command.
- [/] Panel visibility for the scene, inspector, viewport and bottom dock — four booleans are
      already saved and restored under `editor.panels.*`; there is no UI listing them as settings
      and no dedicated round-trip test beyond the session-settings tests.
- [/] Active workspace and active bottom-dock tab are persisted under `editor.workspace.active`
      and `editor.bottomDock.active`.
- [/] Active right-panel tab is persisted under `editor.rightPanel.activeTab`.
- [ ] Per-panel size and split-ratio persistence.
- [ ] Floating window support and multi-monitor placement memory.
- [ ] Tab position, tab close buttons, and tab overflow behaviour.
- [ ] Distraction-free / fullscreen editing mode.

## 2.3 Scene tree and hierarchy panel

The hierarchy is one of the two panels a user looks at constantly. Several behaviours below are
already built in, but none of them is a setting yet - each is fixed in code.

- [~] Auto-expand on selection, and auto-scroll the selected node into view. When the active
      selection changes (picked in the viewport, or a node just added) the rows above it open and
      it is scrolled into view, once, so it can be collapsed again; missing: a setting to turn it
      off.
- [ ] Expand-all / collapse-all depth limit.
- [ ] Persist expansion state per scene across sessions.
- [ ] Row height, indent width, and whether indent guides are drawn.
- [~] Node-type icons: show, hide, or colour-code by type. Every row already draws an icon for its
      node kind (mesh, camera, light, environment, physics, audio, particle, script, plain node);
      missing: a setting to hide or recolour them.
- [ ] Show node type name alongside the node name.
- [~] Show component badges on the row (script attached, visibility off, locked). A script badge
      (with its path on hover) and the visibility eye sit in fixed columns at the row's right edge,
      and a long name is cut with "..." and shown whole on hover; missing: locked, and a setting.
- [~] Visibility toggle column: show, hide, or show on hover. Every row that can be hidden has
      an eye at its right edge; a click is one undoable edit that leaves the selection alone, and
      a node hidden only by its parent shows a dimmer eye; missing: the show-on-hover option.
- [ ] Lock / unselectable column.
- [~] Filter behaviour: match name only, or name plus type plus component; case sensitivity;
      whether ancestors of a match are kept visible. The Search Nodes box already matches the
      name case-insensitively, `type:` filters by node type, `root` lists the roots, ancestors of
      a match stay visible and the tree opens while a filter is active; missing: component
      matching and a setting to choose the mode.
- [ ] Sort mode: scene order (authoritative), alphabetical, or by type.
- [~] Drag-and-drop reparent: enable, and whether a confirmation is required for large subtrees.
      Dragging a row onto another reparents it, and dropping below the tree makes it a root;
      missing: the enable toggle and the large-subtree confirmation.
- [~] Multi-selection behaviour: rubber-band, range select, and whether children follow the
      parent. Ctrl+click toggles a row in the selection; missing: range select, rubber-band and
      the children-follow option.
- [ ] Colour tags / node groups, and whether tag colour tints the row.
- [~] Warning and error badges (missing script, broken reference, invalid transform). An amber
      badge lists each problem on hover: non-finite transform, invalid script path, no mesh, mesh
      or material missing from the project, Skeleton3D with no source; missing: error severity
      and per-node-type checks beyond these.
- [~] Double-click action: rename, focus in viewport, or open script. Double-click on the
      selected row renames it today; missing: the choice of action.
- [~] Rename mode: inline edit vs. dialog, and name-collision policy. Inline rename exists (F2 or
      double-click, Enter commits, Escape cancels), and a renamed row keeps its children open;
      missing: the dialog option and a name-collision policy.
- [~] Show the scene root specially. The scene root can no longer be dragged, and its
      right-click menu disables Duplicate and Delete with a tooltip saying why; missing: a
      distinct look for its row.

## 2.4 Scene node context menu (right-click)

The right-click floating toolbar on an entity already exists in the viewport and opens the script
graph. The full context-menu surface is much larger, and *which entries appear* is itself a
settings question.

- [ ] Which actions appear, and in what order — user-reorderable.
- [~] Add child node, add sibling node, instantiate scene as child. Each hierarchy row has a
      right-click menu with Add Child Node (the full node library); missing: add sibling and
      instantiate scene.
- [ ] Attach / detach / open script.
- [ ] Add component, remove component, copy component values, paste component values.
- [~] Cut, copy, paste, duplicate, delete, with a configurable duplicate-name suffix pattern.
      Duplicate and Delete are in the row's right-click menu and on Ctrl+D / Delete, both
      disabled on the scene root; missing: cut/copy/paste and the suffix setting.
- [~] Rename, and change node type where the conversion is legal. Rename is in the right-click
      menu (and F2); missing: change node type.
- [ ] Reparent to selection, reparent keeping global transform (toggle).
- [ ] Move up / move down / move to top / move to bottom in sibling order.
- [ ] Save branch as a reusable scene, and make a local instance editable.
- [~] Focus in viewport, frame selection, align view to node, align node to view. Focus in
      Viewport is in the row's menu and on F over the viewport, both through one request the host
      applies: the node becomes the orbit pivot, and a Marker3D flies the camera into its
      viewpoint; it is disabled, with a tooltip, for a node with no position (the scene root, a
      plain Node); missing: framing by bounds, and the two align commands.
- [~] Lock / unlock, show / hide, toggle selectable. Hide / Show is in the row's menu, the same
      undoable edit as the row's eye; missing: lock and selectable.
- [ ] Copy node path, copy node identifier.
- [x] Expand / collapse subtree. Expand Branch and Collapse Branch open or close a row and every
      row below it; rows inside a collapsed branch stay closed when the branch is opened again.
- [ ] Whether the context menu also selects the node it opened on (currently it does — keep it as
      an explicit, documented setting rather than incidental behaviour).
- [ ] Long-press equivalent for touch input, with a configurable hold duration.
- [ ] Whether dangerous entries (delete subtree) require confirmation.

## 2.5 Menu bar and commands

- [ ] A command registry: every editor action has an id, a label, a category, and an optional
      default shortcut. The menu bar and the command palette both render from it.
- [ ] Command palette: enable, fuzzy-match mode, recent-command memory depth.
- [ ] Shortcut bindings as a real settings page: searchable, per-command, with conflict detection
      and a reset-to-default per binding.
- [ ] Shortcut profiles, so a user can switch between binding sets.
- [ ] Menu bar customisation: which top-level menus appear.
- [ ] Recent files / recent projects list length, and whether it is cleared on exit.
- [ ] Toolbar contents and button size.
- [ ] Confirmation prompts: which destructive actions ask first.
- [/] "Save Editor Preferences" exists as a menu item, and an interactive editor now also saves
      its preferences when it shuts down; it should become an automatic save on change once the
      substrate lands.

## 2.6 Viewport and 3D editing

The group with the most real backing today.

- [/] Snap enabled, translate step, rotate step (degrees) and scale step are persisted under
      `editor.viewport.snap.*` with positive-finite validation on load.
- [x] Gizmo axis colours are user-configurable and persisted under `editor.viewport.axisColors.*`,
      applied whole-or-nothing to both the transform gizmo and the grid axis lines, with the grid
      taking a dimmed variant; covered by dedicated tests.
- [~] Grid: visible, size, subdivisions, extent/fade distance, colour, and which plane(s) are
      drawn. Visible and opacity (10-100%) are set from the grid button (click toggles,
      right-click opens the options) and saved under `editor.viewport.grid.*`, with a corrupt
      stored value falling back to the defaults. The plane follows the view: the ground normally,
      XY in Front/Back and ZY in Left/Right so a side view keeps a grid; missing: size,
      subdivisions, fade, colour, and a manual plane choice.
- [ ] Grid follows the camera vs. fixed at origin.
- [ ] Gizmo size in pixels, gizmo opacity, and whether the gizmo is hidden during drag.
- [ ] Gizmo mode memory: whether the active transform mode persists across sessions.
- [ ] Local vs. global transform space default.
- [ ] Pivot mode: individual origins, median point, or active object.
- [ ] Navigation scheme presets, and per-scheme modifier assignments for orbit, pan and zoom.
- [ ] Orbit sensitivity, pan sensitivity, zoom sensitivity, and invert toggles per axis.
- [ ] Zoom style: dolly vs. field-of-view, and zoom-to-cursor.
- [ ] Inertia / smoothing amount for camera motion.
- [ ] Freelook: enable, activation modifier, base speed, speed scaling with scroll, acceleration.
- [ ] Default camera field of view, near plane, far plane.
- [~] Orthographic view presets and the shortcut to each. The projection pill is a menu:
      Perspective / Orthographic, and Top, Bottom, Front, Back, Right, Left. A named view goes
      orthographic by itself and returns to perspective when orbited out; an orthographic chosen
      explicitly stays. The nav gizmo's balls use the same path. Shortcuts: Numpad 7/1/3 (Alt+7/1/3
      without a keypad) for Top/Front/Right, Ctrl for the opposite side, Numpad 5 / Alt+5 to switch
      projection; missing: user rebinding.
- [ ] Frame-selected padding and animation duration.
- [ ] Selection outline: colour, thickness, and whether it draws through geometry. The engine has
      **no selection outline today**, which is why the pivot dot is the only indication that a
      mesh-less node is selected.
- [ ] Selection box / rubber-band select mode: touch vs. enclose.
- [ ] View modes: wireframe, unshaded, overdraw, lighting-only, normals, and per-buffer debug
      views.
- [ ] Viewport overlays: statistics, frame time, draw calls, triangle count, and their corner.
- [ ] Orientation gizmo: size, corner, and whether clicking it snaps the view.
- [ ] Preview environment when the scene has none.
- [ ] Viewport render scale for low-end machines.
- [ ] Multiple viewports (1 / 2 / 3 / 4 panes) and per-pane settings.

## 2.7 Inspector

- [x] Collapsible, component-grouped sections with persisted expansion state. Every section is
      a collapsible header, a component that belongs to a node sits nested inside the node's own
      section, and related fields share collapsible sub-groups. Which ones are open is kept by
      key (section, nested component, sub-group) across selections and sessions, bounded to 256
      entries; covered by an editor test.
- [/] Property-row helpers used by every drawer, replacing the hand-drawn rows and the ten
      copy-pasted colour rows. Rows are drawn from the type metadata - label, tooltip, range,
      step, enum and colour - through one set of helpers.
- Property search: the Inspector's search box was removed on purpose - the Inspector shows only
      the node's own class chain. If search returns, it must match property names, not drawer
      ids.
- [ ] Label width / name column ratio, and word-wrapping of long labels.
- [~] Float display precision, and drag step per property. The drag step comes from each
      property's metadata range; missing: display precision.
- [~] Degrees vs. radians display for angles. Rotation is shown and edited in degrees and stored
      in radians; missing: the choice.
- [ ] Show advanced / internal properties toggle.
- [/] Show modified-from-default markers, and per-property revert. A revert button appears only
      on a row whose value differs from what a freshly added component holds, and resets it to
      that value; no dedicated test locks it yet.
- [ ] Multi-object editing, with mixed-value indication. With several nodes selected the
      Inspector says single-entity editing is unavailable.
- [~] Copy / paste property values, and copy property path. Right-click a section header for
      Copy Values, Paste Values (same component type only) and Reset to Defaults, each one undoable
      step; Transform copies only the local pose; missing: per-property copy/paste and the path.
- [ ] Favourite properties pinned to the top — a favourites list already exists in the preferences
      (capped at 128 entries) and could back this.
- [ ] Default colour-picker shape and colour-picker mode (see Part 3).
- [ ] Open resources in a sub-inspector vs. a new panel.
- [ ] Auto-refresh rate while the game is running.
- [x] Section ordering, with the universal node section last. The node's own section comes
      first, then its bases, then Transform and Visibility, then the Node section; editor tests
      assert the exact order for each node kind. Missing only: user reordering.
- Add Component search: the Add Component control was removed from the Inspector on purpose -
      a node's components come from its type. Not planned in this form.

## 2.8 Script and shader editor

- [ ] Font, font size, line height, and ligatures.
- [ ] Line numbers, relative line numbers, and the gutter contents.
- [ ] Tab size, spaces vs. tabs, and auto-indent mode.
- [ ] Word wrap mode and wrap guide columns.
- [ ] Syntax highlighting colour set, and per-token overrides.
- [ ] Current-line highlight, matching-bracket highlight, and whitespace visibility.
- [ ] Code completion: enable, delay, minimum prefix length, auto-insert behaviour.
- [ ] Signature help and inline documentation popups.
- [ ] Auto-brace and auto-quote completion.
- [ ] Code folding, and fold-on-open regions.
- [ ] Minimap enable and width.
- [ ] Smooth scrolling, scroll past end, and cursor blink rate.
- [ ] Auto-save on focus loss, and save-on-run.
- [ ] Trim trailing whitespace and ensure final newline on save.
- [ ] Format-on-save, and the formatter configuration path.
- [ ] Search: case sensitivity, whole word, regular expressions, and search history depth.
- [ ] External editor: enable, executable path, and argument template.
- [ ] Node-graph editor settings: grid snap, connection style (straight, curved, orthogonal),
      auto-arrange, minimap, comment/group boxes, and default pin colours by type.

## 2.9 Asset browser

- [ ] Default view mode: grid or list, and thumbnail size.
- [ ] Thumbnail generation: enable, resolution, cache size, and background generation.
- [ ] Sort mode and sort direction, persisted per folder.
- [ ] Show hidden files and engine-internal files.
- [ ] Filter by asset type, with a persisted filter set.
- [ ] Split view (tree plus contents) ratio.
- [ ] Automatic reimport on external file change, and file-watcher poll interval.
- [ ] Delete behaviour: to trash vs. permanent, with a confirmation setting.
- [ ] Import-on-drop behaviour and default destination folder.
- [ ] Dependency view: show what an asset references and what references it.

## 2.10 Console, log and debug commands

Explicitly requested. The engine has a log level and log sinks; it has no console surface.

- [ ] An in-editor console panel: filter by severity, by category, and by free text.
- [ ] Severity colours and per-severity visibility toggles.
- [ ] Collapse duplicate messages, with a repeat counter.
- [ ] Maximum retained lines, and behaviour on overflow.
- [ ] Clear on play, and auto-scroll on new message.
- [ ] Click a log line to jump to its source location.
- [ ] Stack traces: show, depth limit, and whether engine frames are folded away.
- [ ] Timestamp format and whether the frame number is shown.
- [ ] Copy selected lines, and export the log to a file.
- [ ] A **command console** with a registered command table: name, arguments with types, help
      text, and a category. Registration should reuse the same descriptor idea as Part 0 so that
      help, completion and validation are free.
- [ ] Command auto-completion and command history depth.
- [ ] Console variables (settings exposed as runtime-tweakable variables), with the same
      descriptor metadata — this is the direct payoff of Part 0.7's layering, since a console
      variable is just the top override layer.
- [ ] Which commands are available in shipping builds vs. editor-only.
- [ ] Remote console: enable, bind address, port, and authentication.
- [ ] On-screen debug overlay: which statistics, corner, opacity, font size.
- [ ] Profiler panel: sampling rate, retained frame count, and which categories are captured.
- [ ] Frame-time graph scale and warning thresholds.
- [ ] Memory profiler: allocation tracking enable and snapshot retention.
- [ ] Physics debug draw: colliders, contacts, raycasts, sleeping bodies, and per-category colour.
- [ ] Navigation debug draw: mesh, links, paths, avoidance agents.
- [ ] Renderer debug draw: bounds, light volumes, shadow cascades, occlusion buffers.
- [ ] Audio debug draw: listener, emitter radii, active voices.
- [ ] Breakpoint / assertion behaviour: break into debugger, log and continue, or abort.

## 2.11 Play mode and run

- [ ] Play in a separate window vs. in a maximised viewport.
- [ ] Play window size, position and monitor.
- [ ] Save all scenes before playing, or run from the in-memory state.
- [ ] Which scene runs: the current one, the main scene, or a fixed custom scene.
- [ ] Pause on start, and pause on error.
- [ ] Keep the editor responsive while playing.
- [ ] Live reload of scripts and of scene changes.
- [ ] Enter/exit play-mode tint, so play mode is visually unmistakable.
- [ ] Time scale control and step-one-frame.

## 2.12 Autosave, recovery and performance

- [ ] Autosave: enable, interval, and whether it saves the scene, the layout, or both.
- [ ] Crash recovery: keep recovery files, and prompt to restore on next launch.
- [ ] Backup count and backup directory.
- [ ] Undo history depth, and whether history survives a scene reload.
- [ ] Low-processor mode: reduce editor frame rate when idle, with a target idle frame rate.
- [ ] Update the viewport only when something changes.
- [ ] Editor thread count for background work (import, thumbnails, baking).
- [ ] Import cache size limit and a purge command.

## 2.13 Version control and external tools

- [ ] Version control integration: enable, provider, and credentials source (never stored in the
      settings document).
- [ ] Show file status badges in the asset browser and hierarchy.
- [ ] Diff and merge tool paths, and the argument templates.
- [ ] Which editor-generated files are ignored by default.
- [ ] External script editor, image editor and 3D tool paths, with per-asset-type association.

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

## 3.2 Picker shapes

- [ ] Hue **wheel** with a saturation/value triangle or square inside — the requested shape.
- [ ] Hue **bar** with a saturation/value rectangle — the current default; keep it as an option.
- [ ] Value/hue/saturation circle variant.
- [ ] Perceptual-lightness circle variant, for picking colours that stay perceptually even.
- [ ] Perceptual hue/saturation and hue/lightness rectangles.
- [ ] "No shape" mode — sliders and hex only, for users who type values.
- [ ] The default shape is an **editor setting** (Part 2.7), not a per-call-site decision.

## 3.3 Colour modes

- [ ] RGB — 0-255 or 0-1 display, user-selectable.
- [ ] HSV — hue in degrees, saturation and value as percentages.
- [ ] Linear vs. sRGB display, with a clear indicator of which is shown. This matters because the
      engine stores material colours linearly; showing a linear value labelled as if it were sRGB
      is a bug users cannot see.
- [ ] Perceptual (OK-family) mode for uniform-feeling adjustment.
- [ ] The default mode is an editor setting, alongside the default shape.

## 3.4 Channels and inputs

- [ ] Hex input field, accepting 3, 6 and 8 digit forms, with and without a leading marker.
- [ ] Per-channel numeric inputs, drag-adjustable.
- [ ] **Alpha bar**, drawn over a checkerboard so transparency is visible.
- [ ] **Intensity / exposure** control for HDR colours, so an emissive colour can exceed 1.0
      without the hue field becoming unusable. Required for emission (Part 4.3).
- [ ] Clamp-to-LDR toggle for colours that must not exceed 1.0.
- [ ] Keyboard entry that accepts a pasted colour in any supported format.

## 3.5 Presets, recents and sampling

- [ ] A **presets** list, saved in user preferences, with add and remove.
- [ ] A **recents** list, maintained automatically, kept separate from presets so that recents
      never overwrite a curated palette.
- [ ] Project-level palettes, checked in alongside the project, distinct from personal presets.
- [ ] A screen **eyedropper** that samples any pixel on screen, including outside the editor
      window.
- [ ] Sample-average radius for the eyedropper.
- [ ] Swatch context menu: copy hex, copy linear value, set as preset.

## 3.6 Comparison and interaction

- [ ] Old / new split swatch, so the original colour is visible while adjusting.
- [ ] Revert to the value the picker opened with.
- [ ] Live preview — the scene updates while dragging, not only on release.
- [ ] Undo as a single entry per picker session, not one per pixel of drag.
- [ ] Keyboard nudge on the selected channel.
- [ ] Escape cancels and restores; Enter commits.

## 3.7 The prerequisite work

- [ ] A shared colour property-row helper, used by all ten existing call sites. Nothing else in
      this part should be built before this, or it will need doing ten times.
- [ ] Widen `MaterialAssetUVE::albedoColor` from `Vector3UVE` to a four-channel colour, and update
      `IsMaterialAssetValidUVE`, the material serializer, and the renderer's opaque/transparent
      bucketing (which currently reads an `isTransparent` flag that nothing consumes).
- [ ] A colour type in the settings descriptor vocabulary (Part 0.2), so colour settings are
      declared like any other and the generic panel renders them.
- [ ] Tests: hex parse/format round-trip for all accepted forms; linear/sRGB conversion round-trip
      within tolerance; alpha preserved through save and load; an out-of-range channel rejected
      whole-or-nothing, matching the axis-palette rule.

---

# Part 4 — Material and texture settings

## 4.1 Current material, precisely

`MaterialAssetUVE` today: `albedoColor` (three channels), `albedoTexture`, `normalTexture`,
`metallic`, `roughness`, `aoTexture`, `emissiveColor`, `vertexShader`, `fragmentShader`,
`isTransparent`. Textures are referenced by asset GUID, never by path, which is the right
decision and should not change. An invalid GUID means "unset, use the flat value instead", which
is also right and removes the need for per-slot "has texture" booleans.

The three concrete first steps, in order:

1. [ ] Add the missing texture slots: **metallic**, **roughness**, **emissive**.
2. [ ] Add **alpha** to the albedo colour, and make `isTransparent` actually drive bucketing.
3. [ ] Add a per-slot UV channel and tint/scale, so a texture slot is a small struct rather than
       a bare GUID.

## 4.2 Base surface

- [ ] Albedo colour with alpha.
- [ ] Albedo texture, with UV channel selection.
- [ ] Alpha source: albedo alpha, a separate texture channel, or none.
- [ ] Alpha scissor threshold, and alpha hashing for foliage.
- [ ] Alpha anti-aliasing mode and edge factor.
- [ ] Vertex colour: use as albedo, and whether vertex colours are sRGB.
- [ ] Two-sided / cull mode: back, front, disabled.
- [ ] Shading mode: per-pixel, per-vertex, unshaded.
- [ ] Diffuse lighting model and specular mode.
- [ ] Disable ambient light, disable fog per material.

## 4.3 Metallic, roughness, specular, emission

- [ ] Metallic scalar and texture, with source channel selection.
- [ ] Metallic specular reflectance for dielectrics.
- [ ] Roughness scalar and texture, with source channel selection.
- [ ] Combined packed map support (for example metallic and roughness in separate channels of one
      texture), which is why per-slot channel selection matters.
- [ ] Specular workflow as an alternative to metallic, where the pipeline supports it.
- [ ] Emission colour, **HDR**, with the intensity control from Part 3.4.
- [ ] Emission texture and emission operator (add vs. multiply).
- [ ] Emission energy multiplier.

## 4.4 Normal, occlusion, height

- [ ] Normal map texture and strength.
- [ ] Normal map format: tangent space, and Y-axis direction convention.
- [ ] Ambient occlusion texture, strength, channel, and whether it affects direct light.
- [ ] Height / displacement map, scale, and whether it is parallax or true displacement.
- [ ] Parallax occlusion: min and max layers, flip tangent/binormal.

## 4.5 Advanced surface features

- [ ] Rim lighting: amount, tint, texture.
- [ ] Clearcoat: amount, roughness, texture.
- [ ] Anisotropy: amount, flowmap texture.
- [ ] Subsurface scattering: strength, skin mode, transmittance colour, depth, boost.
- [ ] Backlight / translucency colour and texture.
- [ ] Refraction: amount, texture, and the screen-space buffer it samples.
- [ ] Detail maps: albedo, normal, blend mode, UV channel.
- [ ] Secondary UV set (UV2) with its own scale and offset.

## 4.6 Transform, sampling and rendering flags

- [ ] UV1 and UV2 scale, offset, and triplanar projection with a blend sharpness.
- [ ] World-space triplanar option.
- [ ] Texture filter override per material.
- [ ] Texture repeat override per material.
- [ ] Blend mode: opaque, alpha blend, premultiplied alpha, additive, subtractive, multiply.
- [ ] Depth draw mode: opaque only, always, never, and depth pre-pass opt-in.
- [ ] Depth test enable and depth write enable.
- [ ] Render priority / sort offset for transparent sorting.
- [ ] Billboard mode: disabled, enabled, Y-axis, particles; and keep-scale.
- [ ] Grow / outline amount, used for shell outlines.
- [ ] Fixed size on screen.
- [ ] Point size for point rendering.
- [ ] Proximity fade distance, and distance fade mode with near/far ranges.
- [ ] Shadow casting mode: on, off, double-sided, shadows only.
- [ ] Receive shadows toggle.
- [ ] Light layer / render layer mask (requires named layers, Part 1.12).

## 4.7 Material assets and workflow

- [ ] Material instances / overrides: a child material that overrides only some properties of a
      parent, with a clear modified-from-parent indicator.
- [ ] Per-renderer material slot overrides on a node.
- [ ] Material preview thumbnail, with a selectable preview mesh and environment.
- [ ] Shader parameter exposure: a custom shader's uniforms appearing as inspector rows,
      generated from reflection rather than hand-written.
- [ ] Material presets / a starter material library.
- [ ] A "next pass" material for multi-pass effects.

## 4.8 Texture import settings

None of these exist — the image importers take their settings parameter commented out, and there
is no sidecar file to persist a choice in. Part 5.1 covers the sidecar mechanism these depend on.

- [ ] Import as: 2D texture, normal map, cubemap, texture array, 3D texture, lightmap, UI image.
- [ ] sRGB / colour-space handling: force linear, force sRGB, or detect.
- [ ] Compression mode: lossless, lossy, video RAM compressed, uncompressed, basis-universal.
- [ ] Compression quality and per-format selection (block compression family, ASTC block size).
- [ ] Compress only on specific platforms.
- [ ] High-quality compression toggle, with the build-time cost noted.
- [ ] Mipmaps: generate, mip filter, and mip count limit.
- [ ] Mipmap generation in linear vs. sRGB space.
- [ ] Alpha handling: premultiply, keep, discard; and whether to detect a fully-opaque alpha and
      drop it.
- [ ] Normal-map flag, which changes both compression format and mip filtering.
- [ ] Channel remapping / packing, so three greyscale maps become one RGB texture at import.
- [ ] Invert green channel for normal maps from the other convention.
- [ ] Maximum resolution clamp, per platform.
- [ ] Non-power-of-two handling: none, scale up, scale down.
- [ ] Fix alpha border for transparent-edge bleeding.
- [ ] Filter and repeat defaults baked into the import.
- [ ] Anisotropic filtering level default.
- [ ] HDR: format, tonemap on import, clamp exposure.
- [ ] Streaming enable and priority.
- [ ] Sprite sheet slicing: grid size, margin, spacing, and per-slice pivot.
- [ ] Nine-patch / border margins for UI images.
- [ ] SVG and vector import: rasterisation scale.
- [ ] Editor-only import preview showing the resulting memory size and format.

---

# Part 5 — Geometry, mesh and scene import settings

## 5.1 The import settings mechanism (prerequisite)

Nothing in this part can be persisted until this exists.

- [ ] A **sidecar import settings file** per source asset, checked in next to it, holding the
      chosen options and the importer version that produced the current result.
- [ ] Importers actually **read** their settings parameter. Today every real importer takes it as
      a commented-out name, so the hook is inert.
- [ ] Per-importer settings types derived from `AssetImportSettingsUVE`, following the two that
      already exist for text and data tables.
- [ ] Import settings declared with the same descriptor vocabulary as Part 0, so the import
      inspector is generated rather than hand-written per format.
- [ ] Reimport on settings change, and a visible "settings changed, reimport needed" state.
- [ ] Import presets: named option sets applied to many assets at once.
- [ ] Default import preset per file extension and per folder.
- [ ] Bulk reimport with progress and cancellation.
- [ ] Import log per asset, recording warnings (missing textures, degenerate triangles, unsupported
      features).

## 5.2 Mesh import

- [ ] Scale factor, and unit detection from the source file.
- [ ] Axis conversion: up axis and forward axis.
- [ ] Apply root transform vs. preserve it as a node.
- [ ] Normals: import, recalculate, or recalculate with a smoothing angle.
- [ ] Tangents: import, recalculate, or none; and the handedness convention.
- [ ] Weld vertices, with a position and UV tolerance.
- [ ] Optimise index order for vertex cache locality.
- [ ] Vertex compression: position, normal, UV and colour precision.
- [ ] Split by material, by object, or keep as one mesh.
- [ ] Preserve source node hierarchy vs. flatten.
- [ ] Import vertex colours, and their colour space.
- [ ] Import or strip UV2.
- [ ] Generate lightmap UVs, with texel density and padding.
- [ ] Maximum vertex/index count warnings.
- [ ] Blend shapes / morph targets: import, normals mode, and compression.

## 5.3 Level of detail

- [ ] Generate LODs automatically, with a level count.
- [ ] Per-level triangle reduction ratio and screen-coverage threshold.
- [ ] Preserve boundary edges, UV seams and material boundaries during reduction.
- [ ] Import LODs from named source nodes instead of generating them.
- [ ] Crossfade vs. hard switch between levels.
- [ ] Shadow-only LOD level.
- [ ] Impostor / billboard generation for the furthest level.

## 5.4 Collision generation

- [ ] Generate collision: none, convex hull, multiple convex hulls, triangle mesh, simplified box
      or capsule.
- [ ] Convex decomposition parameters: max hulls, max vertices per hull, concavity tolerance.
- [ ] Generate from a named source node (the established `-col`-suffix convention or equivalent).
- [ ] Collision as a child node vs. a component on the mesh node.
- [ ] Default physics layer and mask for generated collision (needs named layers, Part 1.12).
- [ ] Generate navigation-mesh source geometry from the import.
- [ ] Generate occlusion geometry from the import.

## 5.5 Skeleton and animation import

- [ ] Import skeleton, and bone name remapping to a standard rig.
- [ ] Root bone selection and root motion extraction.
- [ ] Maximum bone influences per vertex, and weight normalisation.
- [ ] Import animations, and per-clip name, range, loop mode and speed.
- [ ] Split a single timeline into multiple named clips by frame range.
- [ ] Animation compression: position/rotation/scale tolerances, constant-track removal.
- [ ] Optimise: remove redundant tracks and keys.
- [ ] Sample rate and resampling mode.
- [ ] Import or strip scale tracks.
- [ ] Retargeting: source rig profile, target rig profile, bone mapping table.
- [ ] Rest pose handling and bind-pose correction.
- [ ] Import cameras and lights from the source scene, and how their units are converted.

## 5.6 Per-node import overrides

- [ ] A per-source-node override table: skip this node, change its node type, mark it as
      collision, mark it as an LOD level, mark it as navigation geometry.
- [ ] Extract a material as a separate reusable asset instead of embedding it.
- [ ] Extract a mesh as a separate reusable asset.
- [ ] Keep the imported scene as an instance, with local overrides allowed.

## 5.7 Primitive geometry parameters

These are node properties rather than import options, but they are settings a user edits
constantly and they belong in the same inventory.

- [ ] Box: size per axis, subdivision per axis.
- [ ] Sphere: radius, height, radial segments, rings, hemisphere toggle.
- [ ] Capsule: radius, height, radial segments, rings.
- [ ] Cylinder / cone: top radius, bottom radius, height, radial segments, cap toggles.
- [ ] Plane / quad: size, subdivision, orientation, centre offset.
- [ ] Torus: inner radius, outer radius, rings, ring segments.
- [ ] Prism: left-to-right skew, size, subdivision.
- [ ] Text mesh: font, size, depth, curve tolerance, alignment.
- [ ] Ribbon / trail: length, resolution, width curve.
- [ ] Shared across all primitives: UV generation mode, tangent generation, flip faces, add
      collision, and a "convert to editable mesh" action.

---

# Part 6 — Remaining settings surfaces

## 6.1 Audio import

- [ ] Load mode: fully in memory, streamed, or decompressed on load.
- [ ] Compression format and quality per platform.
- [ ] Force mono, and channel downmix rules.
- [ ] Sample rate conversion and target rate.
- [ ] Loop enable, loop start and end points, loop mode.
- [ ] Trim leading and trailing silence, with a threshold.
- [ ] Normalise, and target peak or loudness.
- [ ] Default bus assignment.
- [ ] Preload at scene load vs. on first play.

## 6.2 Font import

- [ ] Rendering mode: bitmap, signed distance field, multi-channel SDF.
- [ ] Base size, and the set of pre-rendered sizes.
- [ ] Hinting mode and subpixel positioning.
- [ ] Anti-aliasing mode.
- [ ] Character set / codepoint ranges to include.
- [ ] Fallback font chain.
- [ ] Outline size and colour, and shadow offset.
- [ ] Kerning and ligature enable.
- [ ] Variable font axis defaults.
- [ ] Atlas size and padding.
- [ ] Note: the orientation-gizmo labels currently have no font atlas, so this work unblocks a
      known viewport gap as well.

## 6.3 Layer name registry

- [ ] A named-layer table per layer domain (3D physics, 2D physics, render, navigation,
      avoidance), each 32 entries, stored in project settings.
- [ ] A reusable layer-mask property row that renders names, with select-all, select-none and
      invert.
- [ ] A collision matrix editor showing which layer pairs interact.
- [ ] Validation that a mask referencing an unnamed layer is still legal (names are labels, not
      permissions) but flagged in the UI.
- [ ] Note the defaults already in the code: `collisionLayer` is `1` and `collisionMask` is
      `0xFFFFFFFF`, both on the components and in the scene serializer — consistent, but entirely
      unnamed.

## 6.4 Input map asset and rebinding UI

- [ ] An input map asset format, so actions and bindings are data rather than code.
- [ ] An input map editor: add/remove actions, add/remove bindings, per-binding device filter.
- [ ] A listen-for-input capture control for binding a key or button directly.
- [ ] Conflict detection across actions within an action set.
- [ ] Reset an action, or the whole map, to project defaults.
- [ ] Player-facing rebinding at runtime, persisted to user settings — `RemapActionUVE` already
      exists and is unit-tested, and would be the function this calls. It has no production caller
      today.
- [ ] Per-action-set maps, and switching sets at runtime.

## 6.5 Physics materials

- [ ] A physics material asset: friction, rolling friction, bounce, and the combine modes for
      each.
- [ ] Per-collider material override.
- [ ] Surface type tag, for driving footstep audio and decals from one place.
- [ ] A default physics material in project settings.

## 6.6 Environment and post-process resources

- [ ] A world environment resource holding the sky, ambient, fog, tonemap, bloom, and screen-space
      effect settings from Part 1.3, as an asset rather than as project-wide values.
- [ ] Per-camera environment override.
- [ ] Priority/blending between overlapping environment volumes.
- [ ] A default environment used when a scene has none, and a separate editor preview environment
      (Part 2.6).

## 6.7 Node and component defaults

- [ ] Default property values for newly created nodes of each type, editable as a setting.
- [ ] Node creation defaults: where a new node is placed (origin, camera focus, ground plane under
      the cursor), and whether it is parented to the selection.
- [~] Default component set for each node type. Each node type's recipe attaches its
      components (its own, its bases', and Node3D's); missing: editing that set as a setting.
- [ ] A "save current node as the default" action.
- [ ] Per-project node templates.

## 6.8 Accessibility

- [ ] Editor and runtime UI scale, independent of DPI.
- [ ] High-contrast theme and colour-blind-safe palettes for the editor, including the gizmo axis
      colours — which are already user-configurable, making this partly reachable today.
- [ ] Reduce motion, reduce transparency.
- [ ] Screen-reader hints on editor controls.
- [ ] Keyboard-only navigation through every editor panel.
- [ ] Subtitle and caption defaults for the runtime.
- [ ] Remappable everything, as a stated requirement rather than a feature.

## 6.9 Telemetry and privacy

- [ ] Editor analytics: opt-in, never opt-out, with a plain statement of what is collected.
- [ ] Crash report submission: ask each time, always, or never.
- [ ] Anonymous usage statistics toggle.
- [ ] No credential, token or path containing a user name is ever written to a settings document
      that could be checked in. This is a constraint on Part 0.10, not a preference.

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

Of the roughly 640 items in this document, **six are marked `[/]` or `[x]` today**: the
viewport snap steps and the gizmo axis colours, plus the handful of persisted panel-visibility
booleans. Everything else is `[ ]`.

That ratio is the finding. This engine does not have a settings system with some settings
missing; it has a JSON file with sixteen keys in it. Part 0 is not preparatory work before the
real work — Part 0 **is** the work, and Parts 1 through 6 are what it makes cheap.

## 7.4 Keeping this document honest

- Update an item's marker in the same change that implements it.
- A marker only becomes `[x]` when a dedicated test locks more than one case. "It runs" is `[/]`.
- When an item here turns out to be wrong about the codebase, fix the text and say so, rather
  than leaving a stale claim in place. Two claims were corrected while writing this document:
  the collision-layer serializer defaults are consistent (not mismatched), and `RemapActionUVE`
  has unit tests (it lacks a *production* caller, which is a different and smaller problem).
