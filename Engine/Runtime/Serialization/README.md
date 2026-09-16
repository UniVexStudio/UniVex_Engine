# Engine/Runtime/Serialization — Scaffolding

Scaffolding for a future generic serialization module.

**Current status:** Empty. Real serialization currently lives in:
- `Engine/Runtime/Scene/Internal/scene_serializer_uve.cpp` (1478 LOC) — JSON per-component ToJson/FromJson, hierarchy remapping, skips derived WorldTransform
- `Engine/Runtime/Asset/` — `uve_file_envelope_uve.h` (asset envelope format)

**Why keep this folder?**
- Scene serializer is scene-specific (entity/component JSON).
- A future generic Serialization module would own: binary serializer, versioning, reflection, network replication serialization, save-game version migration (currently in Save module).

**Plan:**
- Keep as scaffolding.
- When first real file lands, decide whether to extract Scene's JSON logic here or keep Scene-specific and make this module binary/network-focused.
- Use `Expose/uve/serialization/<name>_uve.h` + `Internal/<name>_uve.cpp`.

See ROADMAP section 12 and section 8.

