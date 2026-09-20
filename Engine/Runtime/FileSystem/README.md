# Engine/Runtime/FileSystem

Placeholder — future restructuring stage. See `UNIVEX_Engine_Scratch_Rebuild_Architecture_Prompt` and the repo restructuring plan for what lands here.

> **⚠ Naming-collision note (2026-09-17 audit):** code with this
> responsibility already exists elsewhere in the tree — see below — so do NOT start a
> second implementation in this folder. Either move/rename that code here deliberately,
> or delete this placeholder if the existing location is the accepted permanent home.
- `Engine/Runtime/Asset` already owns filesystem-flavored code: `file_system_uve.cpp`,
  `project_file_index_uve.cpp`, `project_change_watcher_uve.cpp`, `uve_file_envelope_uve.cpp`.
