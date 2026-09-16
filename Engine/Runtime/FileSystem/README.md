# Engine/Runtime/FileSystem — Scaffolding

Scaffolding placeholder for a future dedicated OS filesystem abstraction.

**Current status:** Empty. Real filesystem/VFS work currently lives in `Engine/Runtime/Asset/`:
- `Asset/Expose/uve/asset/file_system_uve.h` / `i_file_system_uve.h`
- Mounting, shader source real directory, project file index, change watcher

**Why keep this folder?**
- Asset's file_system is asset-oriented (project-relative, GUID-based).
- A future low-level `FileSystem` module would own OS-level concerns: absolute paths, sandboxing, platform-specific storage APIs (Android assets, iOS bundle, console save APIs), async file IO, file locking.

**Plan:**
- Keep as scaffolding until Asset's FileSystem needs to be split.
- When first real file lands, it gets `Expose/uve/filesystem/<name>_uve.h` + `Internal/<name>_uve.cpp` pair, matching `Nodes/3D` convention.
- Decide then whether to merge with `Asset/FileSystem` or keep separate layers (low-level OS vs high-level asset VFS).

See `ROADMAP.md` section 8 and `UNIVEX_Engine_Scratch_Rebuild_Architecture_Prompt`.

