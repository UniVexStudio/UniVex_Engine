# Editor Viewport module

The editor viewport's own layers, drawn around the scene the engine's renderer draws:

- **Orbit camera** (`univex/camera`): orbit, pan, dolly, fly, framing and the eased snap to an axis.
- **Infinite grid** (`InfiniteGridRenderer`): one fullscreen triangle per frame, each pixel intersecting its own view ray with the ground plane. Four decades of spacing are cross-faded, so zooming never pops. The hit point is written to depth, so the grid sits behind the scene correctly.
- **Overlay geometry** (`univex/gizmo`, `GizmoRenderer`): bones, collision wireframes, camera frustums and the other object helpers, built as `GizmoMesh` lines and triangles. The lines are drawn as screen-space quads.
- **Selection outline** (`SelectionOutlineRenderer`): the selected objects' outline band.
- **Studio backdrop** (`StudioBackdropRenderer`): the sky, ground and faded floor of the preview windows (Motion Montage, Widget Blueprint, Retarget).

`app/viewport_render_pass` puts these together for one frame. The transform gizmo and the navigation gizmo in the corner belong to the editor shell (PRISM), which draws them and drives them.

The shader sources live in `shaders/` and are embedded at build time through the `*_shader_sources.h.in` templates.
