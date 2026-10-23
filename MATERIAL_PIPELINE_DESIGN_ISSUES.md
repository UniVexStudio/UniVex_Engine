# Material Pipeline — Design Issues

This document records what the material pipeline **promises** an author and what it
**actually does** today, for everything the `MaterialAssetUVE` surface exposes: the
authored preset kinds, the extended PBR block, the shading path, the asset-health
check and the hot-reload path.

It exists because a material is the one asset type an author touches constantly, and
the failure mode of an over-eager schema is silent: a control that serialises, round
trips and shows up in the Inspector looks identical to one that shades the surface.
Nothing in the UI tells the author which of those two it is. The audit below does.

Two of the findings were cheap and unambiguous enough to fix in the same change that
writes this document; they are marked **fixed** and carry the test that now pins
them. Everything else is recorded, not implemented — this is a report, not a
feature list to be smuggled into a bug fix.

## How the audit was run

For every field of `Asset::MaterialAssetUVE`
(`Engine/Runtime/Asset/Expose/uve/asset/material_asset_uve.h`) and every enum it
uses, the field name was searched across `Engine/**` (`.cpp` and `.h`) and the hits
were split into three buckets:

| Bucket | Meaning |
|---|---|
| **Consumer** | `Engine/Runtime/RHI`, `Engine/Runtime/Asset` importers, `RenderSystems` — something reads the field to produce shading, GPU state or an imported value. |
| **Bookkeeping** | `material_asset_uve.cpp` — validation, JSON load/save. Proves the field round trips; proves nothing about rendering. |
| **Authoring only** | `Engine/Editor/**` — the Inspector draws a widget that writes the field. |

A field whose only hits are *Bookkeeping* and *Authoring only* is marked `[ ]` below:
it is authored and persisted, and nothing in the runtime reads it.

Status markers follow the convention the other planning documents use:

- `[x]` — wired end to end, with the code path named.
- `[~]` — partially wired; the note says exactly which half is missing.
- `[ ]` — authored and persisted, but no runtime consumer.

## Summary

| # | Issue | Status |
|---|---|---|
| 1 | `dielectricSpecular` is never read; f0 is hardcoded to 0.04 in the lit shader, and the field's own default (0.5) is not a dielectric f0 | `[ ]` |
| 2 | Separate `metallicTexture` / `roughnessTexture` are imported and saved, then dropped — only the packed ORM map reaches the shader | `[ ]` |
| 3 | Four authored concepts of "alpha" (`blendMode`, `alphaSource`, `albedoAlpha`, `isTransparent`) collapse into one bool; the alpha channel never reaches the framebuffer | `[~]` |
| 4 | Pipeline state comes from `isTransparent` alone; `cullMode`, `depthTestEnabled`, `depthWriteEnabled`, `depthDrawMode`, `renderPriority` are ignored by the 3D renderer | `[~]` |
| 5 | `useCustomShaderCode` / `customShader` are not wired: only `vertexShader`+`fragmentShader` GUIDs select a program | `[~]` |
| 6 | Primitives receive shadows but never cast them, and `receiveShadows` cannot switch reception off | `[~]` |
| 7 | The asset-health check knew `.uvmat` but not `.uvmaterial`, so Inspector-created materials were never checked | **fixed** |
| 8 | The asset-health check still cannot map `.uvtable`, `.uvanim`, `.uvanimlib`, `.uvfont`, `.uvaudio`, `.uvskel` | `[ ]` |
| 9 | A reload announcement is not the reload: invalidating on it alone could cache the payload being replaced | **fixed** |
| 10 | The remaining extended block (parallax, clearcoat, rim, subsurface, anisotropy, refraction, triplanar, fades, layers, layered materials) is authoring only | `[ ]` |

## 1. `dielectricSpecular` is authored, never read

`MaterialAssetUVE::dielectricSpecular` defaults to `0.5F` and is validated as a unit
interval, saved, loaded and shown in the Inspector. No other code in `Engine` reads
it.

The lit shader does not take it as a uniform; it hardcodes the dielectric
reflectance in three places:

- `Engine/Runtime/RHI/Shader/Internal/built_in_shaders_uve.cpp:1703` —
  `vec3 ambientBaseReflectance = mix(vec3(0.04), albedo, metallic);`
- same file `:1788` — `vec3 baseReflectance = mix(vec3(0.04), albedo, metallic);`
- same file `:1821` — `vec3 baselineAmbient = max(ambientColor, vec3(0.04)) * albedo * ambientOcclusion;`

Two separate problems:

1. **Dead control.** Every dielectric surface in the project renders at 4%
   reflectance no matter what the author sets.
2. **The default is not a dielectric f0.** `0.5` is the value a conductor's
   reflectance approaches, not a dielectric's. If the field were wired up as
   declared today, every newly created material would become a mirror. The
   engine's own treatment of "no specular data" is 0.04 (`:1703`), so the field's
   default and the renderer's fallback disagree by an order of magnitude.

Wiring it means one more uniform (`uDielectricSpecular`) consumed by all three
sites, plus resetting the default to `0.04F`. Because it changes the look of every
existing `.uvmat` that leaves the field at its old default, it belongs in its own
increment with a migration note, not in a bug fix.

## 2. Separate metallic and roughness maps are dropped

`MaterialAssetUVE` carries three metallic/roughness inputs:

| Field | Written by | Read by the renderer |
|---|---|---|
| `metallic` (scalar) | Inspector, glTF/OBJ/MTL import | yes — `uMetallic` |
| `roughness` (scalar) | Inspector, glTF/OBJ/MTL import | yes — `uRoughness` |
| `metallicRoughnessTexture` (packed: G roughness, B metallic) | Inspector, glTF import | yes — bound to slot 3, `.g`/`.b` sampled |
| `metallicTexture` | Inspector, `obj_material_import_uve.cpp:273-275` | **no** |
| `roughnessTexture` | Inspector | **no** |

`ResolveMaterialGpuResourcesUVE` resolves exactly five texture slots — albedo,
normal, AO, packed metallic/roughness, emissive
(`Engine/Runtime/RHI/RenderSystems/Internal/renderer_3d_uve.cpp:1916-1924`) — and
`BindMaterialTexturesUVE` binds exactly those five. The shader's only metallic and
roughness sources are `uMetallic`/`uRoughness` multiplied by the packed map's green
and blue channels
(`built_in_shaders_uve.cpp:1690-1691`).

Consequence: an author who assigns a separate roughness map gets a surface that
still uses the scalar. The map is imported, saved and previewed in the Inspector as
if it applied. The realistic outcomes are either (a) finish the binding — two more
slots, two more uniforms, work out the priority against the packed map — or (b)
remove the fields, since the packed form already covers the data. Leaving them
authored-but-inert is the only option that is definitely wrong.

## 3. Four authored concepts of alpha, one bool of behaviour

The material exposes:

- `isTransparent` — documented as driving opaque/transparent bucketing;
- `blendMode` (`Opaque` / `Masked`(cutout) / `Blend` …);
- `alphaSource` (`AlbedoAlpha`, `FromTexture`, …);
- `albedoAlpha` (a scalar the glTF importer fills from the base-colour alpha,
  `gltf_material_import_uve.cpp:253`);
- `alphaCutoff`, `alphaHashingEnabled`.

What the pipeline does:

| Knob | Effect today |
|---|---|
| `isTransparent` | The only one that changes anything: it selects the blended pipeline variant and turns depth writes off (`renderer_3d_uve.cpp:1989`, `:1993-1996`). |
| `alphaCutoff` | Works: `discard` above 0 (`built_in_shaders_uve.cpp:1641`). |
| `albedoAlpha`, `alphaSource`, `blendMode`, `alphaHashingEnabled` | Nothing. All four are `[ ]`. |

More importantly, the fragment shader's output alpha is **not** the material's:

```glsl
vec3 albedo = albedoSample.rgb * uAlbedoColor;         // .a dropped
float surfaceOpacity = clamp(uSurfaceOpacity, 0.0, 1.0); // from the surface instance
FragColor = vec4(finalColor, surfaceOpacity);
```

`uSurfaceOpacity` is the *surface instance's* fade weight
(`renderer_3d_uve.cpp:2860`, `:2953`, `:3337`), not the material's. A material with
`isTransparent = true` therefore blends at whatever object opacity the instance
carries — 1.0 by default, i.e. fully opaque, with depth writes off. Per-texel
transparency (a gradient alpha card, tinted glass) cannot be expressed at all, and
a glTF material with `baseColorFactor.a = 0.5` renders opaque.

The blending mode is straight alpha (`PipelineBlendModeUVE::SourceAlphaOver`) and no
premultiplied option exists, so even after wiring the alpha channel there is no way
to author an additive or premultiplied material — the modes a particle/VFX material
needs.

## 4. Pipeline state is derived from one bool

`ResolveMaterialGpuResourcesUVE` builds the program description as:

```cpp
programDesc.depthTestEnabled = true;
programDesc.depthWriteEnabled = !material->isTransparent;
// blended variant:
blendedDesc.depthWriteEnabled = false;
blendedDesc.blendMode = PipelineBlendModeUVE::SourceAlphaOver;
blendedDesc.depthCompare = DepthCompareUVE::LessOrEqual;
```

Nothing reads `material->cullMode`, `depthTestEnabled`, `depthWriteEnabled`,
`depthDrawMode` or `renderPriority`. Those four are consumed only by the Editor's
own overlay/mesh preview path and by the importers
(`obj_material_import_uve.cpp`, `mtl_material_converter_uve.cpp`), which is why
they *look* alive: the asset round trips and the importers even derive them from
the source format.

Notes on what is missing concretely:

- **Culling.** There is no per-material cull mode; back-face culling is the pipeline
  default and `MaterialCullModeUVE::None` cannot be honoured. Two-sided foliage or
  cloth is not expressible.
- **Depth.** `depthWriteEnabled` cannot be turned off for an opaque material
  (useful for depth-prepass-authored geometry), and `depthTestEnabled` cannot be
  turned off at all.
- **Ordering.** `renderPriority` has no consumer, so there is no way to order two
  transparent materials against each other beyond the renderer's depth sort.

## 5. The custom shader path is not wired

Three separate things claim to select shading:

| Field | Meaning | Consumed by |
|---|---|---|
| `vertexShader` + `fragmentShader` | `.uvshader` GUID pair | `ResolveMaterialGpuResourcesUVE` — the only pair that builds a program |
| `useCustomShaderCode` | "this material supplies its own code" | nothing in `Engine/Runtime` |
| `customShader` | `.uvshader` GUID | nothing in `Engine/Runtime` |

The Inspector's **New Shader Material** preset sets `kind = Shader` and
`useCustomShaderCode = true` and offers a picker for `customShader`
(`editor_panel_inspector_metadata_uve.cpp:1105-1116`, `:1376`). A material created
that way has no `vertexShader`/`fragmentShader` GUIDs, so the renderer takes its
"Standard material" branch and shades it with the built-in lit program. The UI
promises a custom shader; the renderer draws the default one.

This is the most misleading item in this document, because the failure is invisible:
the material *does* render, and it renders correctly — just never with the shader
the author selected.

## 6. Primitives receive shadows but never cast them

Primitive geometry is drawn only in the main colour pass
(`renderer_3d_uve.cpp:4541-4542`); the shadow passes record mesh items only
(`RecordShadowPassUVE`). So a primitive is lit by shadows cast by meshes but never
enters a shadow map itself.

The material's `receiveShadows` field (default `true`) is `[ ]` as well: there is no
way to exempt a material from shadow reception. The mesh component's own
receive/cast flags are separate and do work for meshes.

Both belong to a single "primitive participates fully in the shadow pipeline" item
that needs a primitive entry in the shadow pass, its own instanced batch source and
the culling decision that goes with it.

## 7. Asset-health check: `.uvmaterial` — **fixed here**

`ExpectedKindUVE` in `Engine/Runtime/ProjectCheck/Internal/project_checker_uve.cpp`
mapped file extensions to `AssetKindUVE`s so the project check could decode and
validate each supported file. It knew `.uvmat` (what the importers write) but not
`.uvmaterial` (what the Inspector's *New Material* writes).

Every material an author created from the Inspector was therefore skipped by the
check: never decoded, never validated, never reported as unregistered. A corrupt
one stayed invisible until something tried to draw with it.

Fixed by accepting both spellings, with
`ProjectCheckerUVETest.RunUVE_ChecksMaterialsWrittenByTheInspectorAsWellAsImportedOnes`
covering decode, kind mismatch and registration for each spelling.

## 8. Asset-health check: five kinds still unmapped

`AssetKindUVE` declares fifteen kinds. `ExpectedKindUVE` maps nine. Unmapped, with
the extensions the asset layer actually writes:

| Kind | Extension | Written by |
|---|---|---|
| `DataTable` | `.uvtable` | `data_table_importer_uve.cpp` |
| `Animation` | `.uvanim` | `asset_importer_uve.h` |
| `AnimationLibrary` | `.uvanimlib` | `uve_file_envelope_uve.h:30` |
| `Audio` | `.uvaudio` | WAV → `.uvaudio` bridge |
| `Font` | `.uvfont` | `ttf_importer_uve.h` |
| `Skeleton` | `.uvskel` | skeleton import |

The check silently skips all six, so a malformed animation clip or audio asset is
reported by nothing until it is loaded. This is the same class of hole as §7 and is
a per-extension table addition plus tests, not a design decision.

## 9. Reload invalidation: announcement versus completion — **fixed here**

Two events exist and they mean different things:

- `Asset::AssetReloadedEventUVE` — "a reload was triggered". Queued by
  `HotReloadUVE::PollUVE` (`Engine/Runtime/Asset/Internal/hot_reload_uve.cpp:150`)
  immediately after `AssetManagerUVE::ReloadUVE()`, which only *submits* the load to
  a worker thread.
- `Asset::AssetLoadCompletedEventUVE` — "the load finished, success or failure".
  Queued by `ExecuteLoadUVE` after the payload swap
  (`asset_manager_uve.cpp:212`). Its own header
  (`asset_reloaded_event_uve.h`) says to subscribe to this one for the outcome.

`Renderer3DUVE` subscribed only to the first and dropped its GPU caches there. When
the announcement wins the race against the worker — the common case, since queuing
an event costs less than reading and parsing a file — the renderer rebuilt its
material cache from the payload that was about to be replaced, and no second event
ever arrived. An author who swapped a primitive material's albedo map kept drawing
the old one for the rest of the session. Value edits were unaffected (they are
uploaded per frame from the payload), which is why the bug looked intermittent and
material-specific.

Fixed by keeping the announcement invalidation for responsiveness and re-invalidating
on the completion of any reload the renderer had cache state for. A failed reload
deliberately does **not** invalidate: the manager is failure-atomic and keeps the
last-known-good payload, so the caches are still current.

Note on `primitiveMaterialHandles`, the map the report was originally about: it
needs no invalidation. It stores a GUID plus a ref count
(`AssetHandleUVE`), and `TryGetUVE()` re-reads the manager's current payload every
call — there is nothing in it to go stale. The stale site was `materialCache`,
which holds resolved payload-derived state (pinned program, resolved texture
handles, source GUIDs).

Pinned by
`Renderer3DUVETest.AssetLoadCompletedEventUVE_ForAPrimitiveMaterial_RedrawsWithTheInstalledValues`,
which draws the exact ordering — announcement, a frame on the old payload, then the
swap — and asserts that the primitive moves off the shared albedo map.

## 10. The rest of the extended block

These fields are authored, persisted and validated, and have no runtime consumer.
Grouped by what a real implementation would have to touch:

| Group | Fields | Blocked on |
|---|---|---|
| Parallax | `heightTexture`, `heightScale`, `parallaxMinLayers`, `parallaxMaxLayers` | A height-map fetch plus ray-march in the lit shader; the material already validates the range. |
| Clearcoat | `clearcoatAmount`, `clearcoatRoughness` | A second specular lobe; needs its own BRDF evaluation, not just a uniform. |
| Rim | `rimAmount`, `rimTint` | Cheap — a view-dependent term in the fragment shader. |
| Subsurface | `subsurfaceStrength` | A wrap-diffuse approximation plus a translucency term. |
| Anisotropy | `anisotropyAmount`, `uv2Scale`, `uv2Offset` | A tangent-frame anisotropy term plus a second UV set (the mesh vertex layout has one UV set today). |
| Refraction | `refractionAmount` | A scene-colour or transmission render target, which the renderer does not currently keep bound at material-draw time. |
| Triplanar | `triplanarEnabled`, `worldSpaceTriplanar`, `triplanarBlendSharpness` | Three projected samples per texture slot; the binding path assumes one UV. |
| Distance fades | `proximityFadeDistance`, `distanceFadeNear`, `distanceFadeFar` | A per-frame camera-distance uniform; note the surface instance already has its own visibility-range fade, so these two would have to be reconciled rather than stacked. |
| Layer/layering | `renderLayerMask`, `receiveShadows`, `disableAmbientLight`, `disableFog` | Material-level participation in lighting and culling; `renderLayerMask` overlaps the render-instance component's own layers. |
| Layered materials | `parentMaterial`, `nextPassMaterial` | Layered/stacked material evaluation; the renderer's cache is keyed by a single GUID. |
| Vertex colour | `useVertexColorAsAlbedo`, `vertexColorIsSrgb` | A vertex-colour attribute in `MeshVertexUVE` and the albedo path. |
| Billboard | `billboardMode` | The comment describes rewriting the model matrix at draw; nothing reads it. |
| Normal flip | `normalFlipGreen` | One sign flip in the normal sampling path. |
| AO on direct light | `aoAffectsDirectLight` | AO is applied to ambient only today (`built_in_shaders_uve.cpp:1821`). |
| Texture tint/tiling | `textureTint`, `textureTileX`, `textureTileY`, `uv2Scale`, `uv2Offset` | Superseded in practice by `uvScale`/`uvOffset`, which *are* uploaded as `uUvScale`/`uUvOffset`. Two overlapping ways to say the same thing is its own design smell. |
| `MaterialTextureSlotUVE` | `MaterialTextureSlotUVE` (channel/tint/scale per slot) | Struct exists with per-slot UV channel, channel source, tint and scale/offset; the renderer binds five GUIDs and ignores every per-slot attribute. |

## What does work

For an accurate picture of the pipeline's real reach, the `[x]` list:

| Feature | Where |
|---|---|
| Albedo colour + albedo texture | `uAlbedoColor`, slot 0 |
| Normal map + `normalScale` | slot 1, `uNormalScale` |
| AO map + `occlusionStrength` (ambient only) | slot 2, `uOcclusionStrength` |
| Metallic + roughness scalars, packed ORM multiplicatively | `uMetallic`, `uRoughness`, slot 3 |
| Emissive colour + energy + texture | `uEmissiveColor`, `uEmissiveEnergy`, slot 4 |
| `uvScale` / `uvOffset` | `uUvScale`, `uUvOffset` |
| `unshaded`, `shadingMode` (lit / unlit / normals / roughness debug) | `uUnshaded`, `uShadingMode` |
| `alphaCutoff` (cutout) | `uAlphaCutoff` with `discard` |
| `isTransparent` (blended pass, no depth write) | pipeline variant |
| `vertexShader` + `fragmentShader` GUID pair | program built from the two `.uvshader` sources |
| Instancing eligibility | `VertexSourceSupportsInstancingUVE` scans the vertex source |
| Reload invalidation | announcement **and** completion (§9) |

## Suggested order of work

1. **§3 + §4 (alpha and pipeline state)** — they are the same file and the same
   decision: what a material is allowed to say about its own blend/depth/cull state.
   `albedoAlpha`, `alphaSource` and `blendMode` should either be honoured together
   or removed together.
2. **§5 (custom shader)** — either honour `customShader` or stop offering the
   preset. Silent under-delivery is the worst of the three outcomes.
3. **§2 (separate metallic/roughness)** — bind or delete the fields.
4. **§1 (`dielectricSpecular`)** — wire it and fix the default to `0.04`, with a
   migration note for existing assets.
5. **§8 (checker table)** — mechanical, and it closes the same hole §7 did.
6. **§6 (primitive shadow casting)** — the only item here that needs a new pass
   entry rather than a new uniform.
7. **§10** — take the cheap ones (`rimTint`, `normalFlipGreen`, `aoAffectsDirectLight`,
   `receiveShadows`) before the ones that need new shader structure (clearcoat,
   parallax, refraction, triplanar, layered materials).
