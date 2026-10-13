# Engine/Runtime/Objects/2D

The 2D scene nodes. The first real 2D kind — Sprite2D, a textured quad — lives here as a
per-kind `NodeDefinition` `.h` + `.cpp` pair (`Expose/uve/objects/2d/<name>_uve.h` +
`Internal/<name>_uve.cpp`), matching the convention established in
`Engine/Runtime/Objects/3D`: each file holds that kind's creation recipe (components to
attach, authored defaults, default entity name), while the component structs themselves
stay in `Engine/Runtime/Component` — one truth per concept.

Sprite2D v1 is scene data, not rendering: `Sprite2DComponentUVE` (texture GUID, pixel
source-rect, flip flags, modulate + alpha, offset) plus `ComposeSprite2DQuadUVE`, a
fail-closed composer that turns the component + a texture size into corners and UVs (y-down
pixels, v top-down) for whatever 2D renderer arrives later. A zero GUID means a flat
modulate quad; a zero-area source-rect means the whole texture. The kind is registered in
the Scene node registry (`SceneObjectKindUVE::Sprite2D`, opening the `2D` object category)
and is creatable from the editor's Add-Node list. The second kind, Camera2D, is the 2D
scrolling camera - the view a 2D scene is authored through (`Camera2DComponentUVE`: zoom,
offset, `current`; like Sprite2D, component-only apply with the transform attached by the
creation shell). There is no Viewport2D marker: the level's outliner Viewport is the 3D
one (`SceneObjectKindUVE::Viewport3D`, renamed when 2D arrived), and the 2D view is shaped
by whichever Camera2D is current - nothing would stamp a second viewport slot.
Renderer2D v1 renders them: one renderer per viewport backend renders its own
Camera2D into its own targets through `RenderFrameToTargetUVE` (the Main-editor / Entity-Editor
split, mirroring the 3D viewport architecture) - painter-sorted, NDC view-culled, and
fail-closed per sprite, with per-sprite diagnostics for the future viewport overlay.
`SCENE_NODES_ROADMAP.md` at the repo
root tracks the full checklist of 2D node types still to build (animated sprites, 2D
meshes, polygons, lights, audio, etc.) — each new kind gets its own file pair here.
