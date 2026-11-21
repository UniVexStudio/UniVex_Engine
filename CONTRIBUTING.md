# Contributing to UniVex Engine

Thank you for your interest in contributing to the **UniVex Engine**! This document provides guidelines, coding standards, and procedures for submitting code, issues, and documentation to the project.

---

## 1. Core Principles & Architecture

- **No Third-Party Commercial Engine Names**: By engine policy, do not reference or benchmark proprietary commercial game engines by name in codebase documentation, comments, commit messages, or PRs. Refer to architectural concepts generically (e.g., "current-generation standards", "analytic two-bone IK", "forward+ clustered shading").
- **Strict Module Boundary Discipline**: Keep runtime dependencies cleanly separated. Core modules (`uve_core`, `uve_math`, `uve_memory`, `uve_entity`) must not depend on higher-level systems (`uve_render`, `uve_scene`, `uve_editor`).
- **One Truth per Concept**: Component definitions belong in `Engine/Runtime/Component`. Node definitions and object recipes belong in `Engine/Runtime/Objects`. Do not duplicate component structs across directories.
- **Survey Before Authoring**: Review existing implementations, `docs/ARCHITECTURE_GUIDE.md`, and `ROADMAP.md` before introducing new systems or abstractions.

---

## 2. C++23 Coding Standards

- **Standard**: The entire engine compiles under **C++23** (`cxx_std_23`).
- **Warnings as Errors**: All code must build cleanly with zero warnings (`-Wall -Wextra -Wpedantic` on GCC/Clang, `/W4 /WX` on MSVC).
- **Naming Conventions**:
  - Types and classes end with `UVE` (e.g., `TransformComponentUVE`, `PhysicsConstraintSystemUVE`).
  - Public functions and member methods end with `UVE` (e.g., `SolveUVE()`, `AddDistanceConstraintUVE()`).
  - Constants use `k` prefix with camelCase and end with `UVE` (e.g., `kConstraintEpsilonUVE`, `kMaximumConstraintsUVE`).
  - Member variables use trailing underscore `m_name` or `name_`.
- **Safety & Resource Management**:
  - Use `[[nodiscard]]` on all pure queries, validation helpers, factories, and return values that indicate success/failure or mutation codes.
  - Mark functions `noexcept` whenever they do not throw or allocate.
  - Prefer value semantics and explicit lifecycle owners over raw shared pointers or unowned pointers.
  - Never allow raw array buffer overruns; use `std::span` or `std::array` with explicit bounds checking.
  - Ensure all math operations guard against non-finite floats (`NaN`, `Inf`) using `Math::IsFiniteUVE()`.

---

## 3. Testing Discipline

- **GoogleTest Suite**: Every feature with non-trivial logic must be locked with unit or integration tests in the module's `Tests/` directory (automatically compiled into `UniVexTests` via `Engine/Tests/CoreTests.Build.cs`).
- **Coverage Standard**: Tests must verify:
  1. Default and valid configurations.
  2. Degenerate and invalid configurations (out-of-range, null/invalid handles, infinite/NaN inputs).
  3. Bounded limits and capacity saturation (e.g., `kMaximumConstraintsUVE`).
  4. Generation safety and stale handle rejection where generational handles are used.
- Build and run with UVBT: `./uvbt build UniVexTests Linux Debug && LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a ./.uvbt/bin/Linux/Debug/UniVexTests`.

---

## 4. Pull Request & Commit Process

1. **Branching**: Create focused feature branches named `feature/<short-description>` or `fix/<short-description>`.
2. **Conventional Commits**: Format commit messages according to Conventional Commits:
   - `feat(subsystem): short description`
   - `fix(subsystem): short description`
   - `docs(subsystem): short description`
   - `test(subsystem): short description`
3. **Checklist Before Submitting**:
   - [ ] Verified against the existing test suite (`./.uvbt/bin/Linux/Debug/UniVexTests` and `uve_prism_tests`).
   - [ ] All new tests pass cleanly.
   - [ ] Zero compiler warnings (`UVBT_WERROR=1`).
   - [ ] Updated `ROADMAP.md` if applicable.
   - [ ] Filled out all sections of `.github/PULL_REQUEST_TEMPLATE.md`.
