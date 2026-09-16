# Engine/Runtime/Gameplay — Scaffolding

Scaffolding for the future gameplay framework layer (ROADMAP 7.2).

**Current status:** Empty. No Actor/Pawn/Controller model yet — gameplay is currently raw ECS entities + components + visual scripting.

**What will land here (per ROADMAP):**
- [ ] Formal actor/pawn/controller-style object model above raw ECS (something gameplay programmers author against directly)
- [ ] Input-action-mapping layer (logical actions like "Jump" -> any physical input, rebinding)
- [ ] Gameplay tag / gameplay-attribute system (health, stamina, status effects) as reusable framework
- [ ] Cinematic/sequencer tool for cutscenes (camera, animation, audio, gameplay events on shared timeline)
- [ ] Trigger/event graph layer for lightweight level scripting ("on overlap, do X") distinct from full visual scripting

**Why keep scaffolding?**
- This is one of the largest missing systems for a "real game" — keeping the folder reserves the namespace and makes ROADMAP tracking explicit.

When first real file lands, use `Expose/uve/gameplay/<name>_uve.h` + `Internal/<name>_uve.cpp`.

See `ROADMAP.md` 7.2 and `SCENE_NODES_ROADMAP.md`.

