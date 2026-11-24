# Contributing to UniVex Engine

Thank you for your interest in contributing to the **UniVex Engine**! This document provides guidelines, coding standards, and procedures for submitting code, issues, and documentation to the project.

---

## 1. Core Principles & Architecture

- **No Third-Party Commercial Engine Names**: By engine policy, do not reference or benchmark proprietary commercial game engines by name in codebase documentation, comments, commit messages, or PRs. Refer to architectural concepts generically (e.g., "current-generation standards", "analytic two-bone IK", "forward+ clustered shading").
- **Strict Module Boundary Discipline**: Keep runtime dependencies cleanly separated. Core modules (`uve_core`, `uve_math`, `uve_memory`, `uve_entity`) must not depend on higher-level systems (`uve_render`, `uve_scene`, `uve_editor`).
- **One Truth per Concept**: Component definitions belong in `Engine/Runtime/Component`. Node definitions and object recipes belong in `Engine/Runtime/Objects`. Do not duplicate component structs across directories.
- **Survey Before Authoring**: Review existing implementations and `docs/ARCHITECTURE_GUIDE.md` before introducing new systems or abstractions.

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
   - [ ] Filled out all sections of `.github/PULL_REQUEST_TEMPLATE.md`.

---

## 5. Contributor License Agreement (CLA)

By submitting a Pull Request, patch, commit, asset, or other contribution ("Contribution") to UniVex Studios for inclusion in UniVex Engine or UniVex Hub, you agree to the following terms:

1. **You Retain Ownership**: You keep ownership of the copyright in your original Contribution.
2. **Copyright License Grant**: You grant UniVex Studios a perpetual, worldwide, non-exclusive, irrevocable, royalty-free, transferable, and sublicensable license to reproduce, prepare derivative works of, publicly display, publicly perform, distribute, and relicense your Contribution (in whole or in part) under the [UniVex Engine & Hub EULA](LICENSE) and any future license terms adopted by UniVex Studios.
3. **Patent License Grant**: You grant UniVex Studios and recipients of software distributed by UniVex Studios a perpetual, worldwide, non-exclusive, irrevocable, royalty-free patent license to make, use, sell, offer to sell, import, and otherwise transfer your Contribution, covering only those patent claims necessarily infringed by your Contribution alone or in combination with the project to which it was submitted.
4. **Original Work & Authority**: You represent that your Contribution is your original creation (or that you have sufficient rights and authorization from your employer or copyright holder to submit it under these terms) and does not knowingly infringe third-party intellectual property rights.
5. **Provided "AS IS"**: Unless required by applicable law or agreed to in writing, you provide your Contribution on an "AS IS" basis, without warranties or conditions of any kind.

