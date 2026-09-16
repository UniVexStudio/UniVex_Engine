# Engine/Runtime/Core/Types — Scaffolding

Scaffolding for future core type definitions.

**Current status:** Empty. Core types currently live in individual modules:
- `Math` has `Vector3UVE`, `Matrix4x4UVE`, `QuaternionUVE`, etc.
- `Object` has base object?
- `Types` would be for truly engine-wide primitive types: `EntityId`, `Guid`, `Handle`, `Result`, `OptionalRef`, etc.

**Why keep?**
- Reserves namespace for types that don't belong to Math but are used everywhere (e.g., `EntityUVE` currently in Scene, but could be core).

**Plan:** Keep as scaffolding. When a type is needed that is used across >3 modules and doesn't fit Math/Memory, put it here.

Currently low priority.

