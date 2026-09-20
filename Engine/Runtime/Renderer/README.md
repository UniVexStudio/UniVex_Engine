# Engine/Runtime/Renderer

Placeholder — future restructuring stage. See `UNIVEX_Engine_Scratch_Rebuild_Architecture_Prompt` and the repo restructuring plan for what lands here.

> **⚠ Naming-collision note (2026-09-17 audit):** code with this
> responsibility already exists elsewhere in the tree — see below — so do NOT start a
> second implementation in this folder. Either move/rename that code here deliberately,
> or delete this placeholder if the existing location is the accepted permanent home.
- `Engine/Runtime/RHI/RenderSystems` already owns the renderer
  (`render_system_uve.cpp`, `renderer_3d_uve.cpp`, `render_graph_uve.cpp`).
