# Engine/Runtime/VFX — Scaffolding

Scaffolding for future visual effects runtime.

**Current status:** Empty. Particle runtime currently lives in:
- `Engine/Runtime/Scene/Expose/uve/scene/particle_runtime_uve.h` + `i_particle_runtime_uve.h`
- `Engine/Runtime/RHI/RenderSystems/Expose/uve/render/particle_draw_command_uve.h`, `particle_render_bridge_uve.h`

**What will land here (per ROADMAP 1.2 & 8):**
- [ ] Particle-effect authoring tool (runtime exists, no dedicated editor UI)
- [ ] GPU particle simulation (compute shader)
- [ ] Foliage/vegetation instancing and wind-animation
- [ ] Water rendering (ocean/lake shader with reflection+refraction)
- [ ] Volumetric fog / volumetric lighting
- [ ] Decal rendering (decal scene-node exists as descriptor only)
- [ ] Generic VFX graph / material effects

**Why keep scaffolding?**
- VFX is distinct from Scene and RenderSystems — deserves its own module when particle system grows.

When first file lands, use `Expose/uve/vfx/<name>_uve.h` + `Internal/<name>_uve.cpp`.

See ROADMAP 1.2, 1.1, and SCENE_NODES_ROADMAP.

