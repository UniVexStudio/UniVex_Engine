# Engine/Runtime/RHI

The render hardware interface: a stable base abstraction plus its concrete backends and
the systems built on top of it, following the base-plus-siblings pattern documented in
`.claude/skills/engine-layout/SKILL.md`.

- `RHI/` — the base: interfaces every backend implements.
- `OpenGL/`, `Vulkan/`, `Null/` — concrete backends, siblings of the base.
- `Shader/` — the built-in shader library, compiled from `Shader/built_in/`.
- `RenderSystems/` — the renderer built on top of the base abstraction.

Each of these is its own module with its own `CMakeLists.txt`, added individually in the
root `CMakeLists.txt`. There is no umbrella `RHI/CMakeLists.txt` — the folder is a grouping
for readability, not a build unit of its own.
