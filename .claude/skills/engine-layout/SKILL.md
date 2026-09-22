---
name: engine-layout
description: Rules for where code goes in this engine's source tree. Use when adding a new module or subsystem, adding a second backend to a subsystem (RHI, audio, physics, parsers), deciding whether a header belongs in Expose/ or Internal/, writing or changing a module's CMakeLists.txt and its target_link_libraries, or reviewing a change that creates new directories.
---

# Engine layout rules

The directory tree is not filing. It is the architecture in a form the compiler and the
build system can enforce. `Internal/` is a promise that nothing outside the module reads
those headers. `RHI/Vulkan/` next to `RHI/Null/` announces that RHI is one interface with
several interchangeable implementations. Put code in the wrong folder and the rule it was
supposed to obey quietly stops existing.

This skill records where things go in **this** repository, and is honest about the places
where our tree does not yet match its own rules.

## The tree we actually have

```
Engine/
  Runtime/      ships in a packaged game (Core, Scene, Physics, RHI, Asset, ...)
  Editor/       editor-only (EditorCore, Viewport, EditorApp)
  App/          the executables' entry points
  Shaders/      shader source
  ThirdParty/   vendored dependencies, grouped by purpose
  CMake/        shared CMake helpers (warnings, build config, format support)
  Tools/        repo scripts (boundary checks, embedding), not a code bucket
Test/           the test suite, one directory per module
```

A module is a directory with a `CMakeLists.txt` and two source folders:

```
Engine/Runtime/Config/
  CMakeLists.txt
  Expose/uve/config/...    public headers  -> target_include_directories(PUBLIC Expose)
  Internal/...             .cpp and headers nobody outside may include
```

## Which bucket does new code go in?

Ask what the code is for, not what it is about:

- Runs in a shipped game → `Engine/Runtime/<Module>/`
- Exists only so a human can author content → `Engine/Editor/<Module>/`
- Is an executable's `main` and its wiring → `Engine/App/`
- Is someone else's code we vendored → `Engine/ThirdParty/<Category>/`
- Is a repo script, not part of the build product → `Engine/Tools/`

If a thing is needed by both the runtime and the editor, it belongs in `Runtime/`. The
editor is allowed to depend on the runtime; the runtime is never allowed to depend on the
editor.

## `Expose/` vs `Internal/`

One question decides it: **does anything outside this module include this header?**

- Yes → `Expose/uve/<area>/<name>_uve.h`. This is now a contract. Changing it is a
  change to every consumer, so it deserves a doc comment explaining the invariant, not
  just the signature.
- No → `Internal/`. This includes headers, not only `.cpp` files. A header that exists to
  split up a large implementation belongs in `Internal/`, and putting it in `Expose/`
  "just in case" turns a private detail into something you can no longer change freely.

Naming follows the existing convention: files `*_uve.h` / `*_uve.cpp`, types
`PascalCaseUVE`. A GL-free helper that only exists to make something testable is still a
legitimate `Expose/` header when another module or a test needs it — `AxisPaletteApply.h`
is the worked example, and its own comment explains why it was split out.

## Subsystems with more than one backend

When a subsystem has several interchangeable implementations, it decomposes into one base
module plus sibling backend modules. `Engine/Runtime/RHI/` already does this:

```
Engine/Runtime/RHI/
  RHI/        the base: interfaces every backend implements   (target uve_rhi)
  Null/       headless backend
  OpenGL/     backend                                          (target uve_rhi_opengl)
  Vulkan/     backend                                          (target uve_rhi_vulkan)
  Shader/     shader handling
  RenderSystems/
```

Two invariants keep this honest, and both are easy to break by accident:

1. **The base never depends on a backend.** Dependencies point from each backend to the
   base, never back. The moment the base includes a backend header, the abstraction is
   decoration.
2. **Consumers depend on the base only.** A renderer links `uve_rhi`. It does not link
   `uve_rhi_vulkan`, and it does not include a Vulkan header.

`Audio/` and `Physics/` are flat today (one `Expose/` + `Internal/` pair each) because
each has a single implementation. That is fine. If a second backend arrives, grow into the
shape above rather than adding `#ifdef`s to the existing module.

## Expressing dependency intent in CMake

We use plain CMake, so intent lives in the `PUBLIC` / `PRIVATE` keyword. Choose it by
asking where the dependency's types appear:

- **PUBLIC** — a header in `Expose/` names the dependency's types, so every consumer needs
  its include path too.
- **PRIVATE** — only `Internal/` uses it. Consumers must not see it, and must not
  accidentally compile against it.

The reference example for confining a third-party library is `Engine/Runtime/Config`:
`nlohmann_json` is linked `PRIVATE` and reaches exactly one `.cpp` behind a PIMPL, so the
JSON library never appears in any consumer's include path. Copy that shape when pulling in
a new external dependency — decide up front whether it is allowed to leak, and if not,
make one translation unit the only place that sees it.

**Known gap:** we have no way to express a *runtime* dependency — a backend loaded at
startup rather than linked. Every RHI backend is a `STATIC` library today. If loadable
backends are wanted later, that is a build-system feature to design deliberately. Do not
improvise a private version of it in one module's `CMakeLists.txt`.

## What a module ships besides code

- `CMakeLists.txt` — required.
- `README.md` — worth writing, answering four things in order: what the module does, why
  it is separate rather than folded into a neighbour, what it depends on, and what it
  exposes. 11 of the 26 modules under `Runtime/` and `Editor/` have one today; a new
  module should.
- Tests. `Engine/Editor/Viewport/tests/` is currently the **only** module with its own
  co-located tests. Everything else lives under `Test/<Module>/`. Follow the surrounding
  convention — put tests in `Test/<Module>/` unless the module already has its own
  harness — but understand the cost: a module and its tests are two places, so moving or
  deleting one is not a single atomic change.

## Layering rules this codebase has already paid for

**`EditorCore` must not depend on `Engine/Editor/Viewport`.** There are zero references
tree-wide and that is deliberate. EditorCore stays free of Viewport, GL and windowing
types; state crosses one way, from EditorCore to the viewport, through
`ViewportOverlayStateUVE`, and the host application applies it. The reasoning is written
at `Engine/App/Internal/editor/main.cpp:105-115`.

When you need the editor UI to drive something the viewport owns, add a field to that
overlay state and push it down. Do not add an include.

## Before you add a directory

1. Which bucket, by what the code is *for*?
2. Is this genuinely a new module, or a folder inside an existing one? Prefer the second —
   a module is a unit of ownership, not a unit of tidiness.
3. Does any existing module already own this responsibility? Search before creating. The
   tree has been bitten by this: `Engine/Runtime/Networking/` is an empty placeholder
   whose own README warns that `Engine/Runtime/Network/` holds the real code, precisely to
   stop a second implementation starting there.
4. For each header: does anything outside the module include it? That answers
   `Expose/` vs `Internal/`.
5. For each dependency: do `Expose/` headers name its types? That answers PUBLIC vs
   PRIVATE.
6. Is this a second implementation of something? Then it is a sibling backend, not an
   `#ifdef`.

## Known gaps in our tree

Stated so this skill does not describe the repository as tidier than it is:

- **Two visibility tiers, not three.** We have `Expose/` and `Internal/` only. There is no
  tier for headers shared between engine modules but outside any public surface —
  `ViewportOverlayStateUVE` is that shape and currently lives fully public.
- **Tests are not co-located** except in Viewport.
- **No `CHANGELOG.md` anywhere**, and README coverage is 11 of 26 modules.
- **No runtime/plugin dependency mechanism**, as above.

A 2026-09-17 audit stamped six empty, unwired directories that duplicated a real module's
name or responsibility — `Runtime/Networking`, `Runtime/Renderer`, `Runtime/FileSystem`,
`Runtime/Serialization`, `Engine/Shaders`, `Engine/Config` — each with a note pointing at
the real code and warning against starting a second implementation there. All six have
since been deleted; the pattern is recorded here so a future placeholder gets the same
treatment (resolve or delete) rather than sitting for months.

---

The framing of a source tree as enforceable architecture, and the lifecycle / visibility /
backend-pluralism / discoverability axes, are drawn from "Engine Architecture and Directory
Layout: A Principal Engineer's Guide" by Mallory Scotton
(https://docs.graphical-playground.com/blog/engine-architecture-layout). The rules above
are our own, written for this repository.
