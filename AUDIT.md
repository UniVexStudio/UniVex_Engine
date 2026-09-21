# UniVex Engine — Code Audit

**Commit audited:** `5174e37` (master)
**Date:** 2026-09-20
**Scope:** Full repository — 1,053 files; 489 `.cpp` and 422 `.h`; 103,282 lines of engine code, 68,116 lines of test code, 5,085 lines of vendored third-party code.

This audit supersedes the previous `AUDIT.md` (362 lines, dated 2026-09-17, removed in `677bfef`). Its conclusions are **not** carried forward. Every finding below was re-derived from the current tree.

## Status legend

| | Meaning |
|---|---|
| 🟢 | **VERIFIED / PASS** — directly verified with supporting evidence |
| 🟡 | **WARNING / REVIEW** — concern requiring review; not a confirmed bug |
| 🟠 | **PARTIAL / INCOMPLETE** — implemented but incomplete, stubbed, or missing integration |
| 🔴 | **FAIL / CRITICAL** — confirmed bug, broken build/test, or demonstrated defect |
| 🔵 | **INFORMATION** — factual observation; neither pass nor failure |
| 🟣 | **UNVERIFIED** — cannot be verified in this environment |

## Method

Whole-tree automated analysis followed by manual reading of every candidate it surfaced. Not every one of the 911 source files was read line by line; that is not claimed. Automated passes: full build, test suite in two execution modes, `cppcheck` across 236 translation units, a normalized duplicate-block detector, a never-included-header scan, an orphan-source scan against CMake, and a marker scan for TODO/FIXME/HACK/XXX. Findings marked 🟢 or 🔴 each cite reproducible evidence.

**Environment limitation:** the audit container has no GPU and no Vulkan driver. Results dependent on real devices are marked 🟣 and cross-checked against GitHub Actions, where a CPU Vulkan driver (lavapipe) is present.

---

## 1. Executive summary

| Area | Status | Summary |
|---|---|---|
| Build health | 🟢 | Clean configure and build; **0 errors, 0 warnings** across 55 static libraries and 8 executables |
| Test suite (serial) | 🟢 | **2,519 tests, 100% passed** |
| Test suite (parallel) | 🟢 | Scratch-path collisions fixed; 10/10 clean `-j` runs |
| Parallel threading abort | 🟠 | Pre-existing, ~1/30 under `-j`; keeps CI serial (§2b) |
| Continuous integration | 🟢 | Green on this commit, including 39/39 real Vulkan device cases |
| Code duplication | 🟡 | One helper duplicated 17×; a serialization helper set duplicated 3–5× with divergent safety |
| Dead code | 🟠 | One unused header; no orphan sources |
| Stub / TODO debt | 🟢 | **Zero** TODO/FIXME/HACK/XXX markers outside vendored code |
| Static analysis | 🔵 | 9 raised; 8 verified false positives, 1 partially valid |

**Overall assessment.** The engine is in good structural condition. Every source file is wired into the build, there are no orphan modules, no byte-identical duplicate files, and no stub or TODO debt in first-party code. The one confirmed defect is in the test harness, not the engine: the suite cannot run in parallel. The duplication findings are maintenance risk rather than active bugs, with one exception where copy-paste has already produced divergent overflow guards.

---

## 2. 🟢 RESOLVED — Test suite is not parallel-safe

**Status:** 🟢 VERIFIED — was 🔴 FAIL; fixed in this repository, evidence below
**Affected:** `Test/RHI/Shader/shader_manager_uve_tests.cpp`, `Test/Engine/engine_core_uve_tests.cpp`, `Test/Pack/project_launcher_uve_tests.cpp`, `Test/Pack/project_packager_uve_tests.cpp`, `Test/Core/Platform/editor_project_package_uve_tests.cpp`

### Evidence

| Execution mode | Result |
|---|---|
| `ctest -j1` | 2,519 tests, **100% passed**, 0 failed |
| `ctest -j$(nproc)` run 1 | 2 failed |
| `ctest -j$(nproc)` run 2 | 0 failed |
| `ctest -j$(nproc)` run 3 | 1 failed |
| `ctest -j$(nproc)` run 4 | 0 failed |

Failing cases observed across runs:
- `ShaderManagerUVETest.HotReload_FileChangeOnDisk_TriggersRecompileWithNewResolvedSource`
- `ShaderManagerUVETest.HotReload_SeparateFragmentStage_RebuildsSameProgram`
- `EngineCoreUVETest.LevelStreamer3D_NearViewerLoadsContentFarViewerUnloadsItHysteresisHoldsBetween`

### Root cause

`Test/CMakeLists.txt` registers tests with `gtest_discover_tests`, which gives **each test case its own process**. Several fixtures derive scratch paths that are not unique across processes, and each one deletes its path on entry and on teardown. Concurrent cases therefore delete each other's files mid-run.

Three distinct mechanisms, all verified in the current tree:

**(a) Current-directory-relative fixed names.** All test processes share one working directory.

```cpp
// Test/RHI/Shader/shader_manager_uve_tests.cpp:70-71
const std::filesystem::path tempDirectory = "uve_shader_manager_tests_shaders";
const std::filesystem::path tempCacheDirectory = "uve_shader_manager_tests_cache";
```

```cpp
// Test/Engine/engine_core_uve_tests.cpp:2569
const std::filesystem::path kStreamerTestLevelPath = "uve_engine_core_streamer_test_level.uvescene";
```

`kStreamerTestLevelPath` is shared by 6 test cases. Each calls `WriteStreamerTestLevelFileUVE()`, which begins with `std::filesystem::remove(kStreamerTestLevelPath)` (line 2574), and each holds a `StreamerTestCleanupUVE` whose destructor removes the same path (line 2597). `Test/Engine/engine_core_uve_tests.cpp:94-97` shares fixed relative names for the log, settings, and asset-database files across the whole `EngineCoreUVETest` suite.

**(b) A process-local counter presented as a uniqueness guarantee.**

```cpp
// Test/Pack/project_launcher_uve_tests.cpp:23-30
[[nodiscard]] std::filesystem::path MakeUniqueTestDirectoryUVE(const std::string& label) {
    static std::atomic<unsigned int> nextId{0U};
    const std::filesystem::path directory = std::filesystem::temp_directory_path() /
                                            ("uve_project_launcher_test_" + label + "_" +
                                             std::to_string(nextId.fetch_add(1U)));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    return directory;
}
```

The `static` counter is per-process. Because every test case is a separate process, each restarts at `0` and produces the **same** directory name. The function name asserts a property the implementation does not provide. The identical pattern appears in `project_packager_uve_tests.cpp:17-25` and, inline, in `editor_project_package_uve_tests.cpp:17`.

**(c) Broader exposure.** Roughly 25 fixed `uve_*_tests_*` scratch names are shared between test cases across the suite, covering the save-game, checkpoint, script-asset-loader, project-file-index and shader-binary-cache fixtures.

### Impact

CI currently runs `ctest` serially, so this does not block the pipeline today — but it forces serial execution (44s for the Test step) and any future move to `-j` will produce random red builds. More importantly, a genuinely flaky or racy engine defect would be indistinguishable from this noise.

### Resolution

`Test/Support/test_scratch_uve.h` now gives every test process a scratch directory under
`temp_directory_path() / "uve_tests" / <pid>_<steady-clock ns>` and makes it the working
directory, via a GoogleTest global environment registered in all four test executables. Because
`gtest_discover_tests` gives each case its own process, every current-directory-relative path in
the suite became per-case isolated with no edits at those call sites.

Paths that a working-directory change cannot reach were rerouted explicitly:
`ScratchRootUVE()` for the absolute `temp_directory_path()` sites (9 files), and
`MakeTestCaseDirectoryUVE()` — which also keys on the running test's name, so a developer running
one executable directly still gets per-case isolation — for the three fixtures that used the
process-local counter. That counter and its `MakeUniqueTestDirectoryUVE` wrapper are gone.

**Evidence after the fix:** `ctest -j$(nproc)` run **10 consecutive times, 0 failures**
(baseline over four runs: 2, 0, 1, 0). `ctest -j1` still 2519/2519. Build clean, 0 warnings.
No scratch files are left in the build directory. Twelve tests that had never executed now run —
see §9.

CI deliberately still runs serially. Not because of scratch paths, which are fixed, but because
removing the serial constraint exposes a separate defect — see §2b.

### Related observation

An earlier parallel run in this session produced a SEGFAULT in `EngineCoreUVETest.CollisionLifecycle_UpdatesReportBeforeScriptTickEachFrame`. It did **not** reproduce on the current tree across four parallel runs, nor in 60 consecutive isolated executions, nor under `gdb`. Whether it shares the root cause above or is an independent race is **🟣 UNVERIFIED**. It remains **🟣 UNVERIFIED**: the scratch-path defect is now fixed and the noise floor is clear, and it did not reappear across the 10 parallel verification runs, but absence over 10 runs is not proof that an intermittent race is gone.

---

## 2b. 🟠 Parallel runs expose a pre-existing threading abort

**Status:** 🟠 PARTIAL — reproducible and bounded, root cause not identified
**Affected:** `Test/RHI/RenderSystems/renderer_3d_uve_tests.cpp`,
`Test/RHI/RenderSystems/viewport_manager_uve_tests.cpp` (varies by run)

With §2 fixed and the suite able to run under `ctest -j`, roughly **1 run in 30** aborts a single
Renderer3D or ViewportManager case with:

```
terminate called without an active exception
```

That message means a `std::thread` was destroyed while still joinable. The affected case differs
between runs, so it is not specific to one test.

**This is not a regression from §2.** A build of unmodified master, with none of the scratch-path
changes, fails at the same rate under the same load: 1/30 at `-j4` and 1/30 at `-j16`. The defect
predates this work and was simply unreachable while the suite could not run in parallel at all.
In isolation the affected tests pass 40/40.

**Root cause is not established.** `ThreadPoolUVE`'s destructor
(`Engine/Runtime/Core/Threading/Internal/thread_pool_uve.cpp:57-66`) joins every worker and looks
correct. An initial hypothesis that thread creation was failing under load was checked and
rejected — the container's thread limit is 64313, nowhere near exhausted.

**Impact:** this is what keeps `.github/workflows/ci.yml` serial. Enabling `-j` would give roughly
a 3% chance of a spurious red build per run, which would reintroduce the exact "random red builds"
problem §2 removed, only from a different cause.

**Recommended next action:** reproduce under a debugger or with `-fsanitize=thread` to find which
thread is destroyed unjoined, fix it, then enable `-j"$(nproc)"` in CI. The test suite itself is
ready for it.

---

## 3. 🟡 Duplication — `IsFiniteVectorUVE` reimplemented 17 times

**Status:** 🟡 WARNING
**Affected:** 17 translation units across 8 modules

The same finite-check on `Math::Vector3UVE` is defined independently in 17 files. Two textual variants exist, differing only in parameter name:

```cpp
[[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& value) noexcept {   // 13 files
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
[[nodiscard]] bool IsFiniteVectorUVE(const Math::Vector3UVE& vector) noexcept {  // 4 files
    return std::isfinite(vector.x) && std::isfinite(vector.y) && std::isfinite(vector.z);
}
```

Modules affected: Animation (1), Asset (3), RHI/RenderSystems (7), Physics (7), Scene (1), Core/Math (1), Scripting (2).

The module that should own this helper already exports a sibling — `Engine/Runtime/Core/Math/Expose/uve/math/quaternion_uve.h:39` declares `IsFiniteUVE(const QuaternionUVE&)` publicly — but no `Vector3UVE` equivalent is exported, so each consumer rolled its own. `Engine/Runtime/Core/Math/Internal/quaternion_uve.cpp` contains a private copy of the Vector3 version.

Fragmentation extends beyond this function: `plane_uve.h:12` defines `IsFiniteScalarUVE`, and `vector3_uve.h:76` and `aabb_uve.h:37` each define a local `IsFiniteFloatUVE` lambda.

Member functions named `IsFiniteVectorUVE` in `editor_uve.cpp` and `mobile_gesture_recognizer_uve.cpp` operate on different types with different semantics and are **not** part of this finding.

**Recommended next action:** export `IsFiniteUVE(const Vector3UVE&)` from `Engine/Runtime/Core/Math/Expose/uve/math/vector3_uve.h` and replace the 17 local definitions. No behavioural change is expected — all 17 are semantically identical.

---

## 4. 🟡 Duplication — binary serialization helpers with divergent overflow guards

**Status:** 🟡 WARNING — latent, not currently exploitable
**Affected:** `Engine/Runtime/Asset/Internal/{asset_bundle,mesh_asset,texture_asset,audio_asset}_uve.cpp`, `Engine/Runtime/Save/Internal/{save_game_system,save_payload_compression}_uve.cpp`

A set of byte-buffer helpers is copy-pasted across the Asset and Save modules:

| Helper | Definitions |
|---|---|
| `AppendBytesUVE` | 5 |
| `AppendUint32UVE`, `AppendUint64UVE`, `ReadUint32FromBufferUVE` | 4 each |
| `ReadUint64FromBufferUVE` | 3 |

The bodies are identical, but the **bounds guards have already diverged** between the two idioms in the tree:

```cpp
// Asset/Internal/asset_bundle_uve.cpp:62,72 and Save/Internal/save_game_system_uve.cpp:97,107
if (offset + sizeof(outValue) > buffer.size()) return false;   // can overflow for a large offset

// Asset/Internal/audio_asset_uve.cpp:55,64
if (offset > buffer.size() || buffer.size() - offset < 2U) return false;   // overflow-safe
```

The first form computes `offset + sizeof(outValue)` before comparing, which wraps if `offset` is near `SIZE_MAX`. In practice `offset` is always produced by walking the buffer from zero, so the wrap is **not reachable today** — this is classified 🟡, not 🔴. It is recorded because it demonstrates the concrete cost of the duplication: the same operation now has two guards in the codebase and only one is correct for all inputs.

**Recommended next action:** consolidate into one internal header shared by both modules, standardising on the `audio_asset` overflow-safe form.

---

## 5. 🟡 Duplication — `SelectKernelSourceUVE` in three compute systems

**Status:** 🟡 WARNING — acknowledged in-source
**Affected:** `Engine/Runtime/RHI/RenderSystems/Internal/{frustum_cull_compute,frustum_cull_indirect,mesh_skin_compute}_uve.cpp`

The GLSL-versus-SPIR-V kernel selection helper is defined identically in three compute systems. This duplication is deliberate and documented — `frustum_cull_indirect_uve.cpp:27` states *"See the identical helper in frustum_cull_compute_uve.cpp"*. It is recorded for completeness rather than as a defect; a shared internal header would remove it without behavioural risk.

---

## 6. 🟠 Dead code — unused `ComponentUVE` concept

**Status:** 🟠 PARTIAL / INCOMPLETE
**Affected:** `Engine/Runtime/Component/Expose/uve/component/component_uve.h` (23 lines)

The header defines a concept that is never referenced:

```cpp
template <typename T>
concept ComponentUVE = std::is_object_v<T> && !std::is_pointer_v<T>;
```

A repository-wide scan for `#include` of this header returns zero results, and no constrained template uses the concept. It is the **only** never-included header in `Engine/` — every other header has at least one consumer.

The header carries a substantive rationale comment explaining why the design is a concept rather than an empty base class (empty-base brace-elision divergence between GCC and Clang during placement construction). That rationale is worth preserving regardless of the outcome.

**Recommended next action:** either constrain the component-facing templates with it, or delete the header and move the rationale to the relevant call site. It should not remain as-is.

---

## 7. 🟡 `setjmp` clobber risk in the JPEG decoder

**Status:** 🟡 WARNING — theoretical, defensive fix only
**Affected:** `Engine/Runtime/Asset/Internal/jpeg_metadata_uve.cpp:47`

```cpp
auto* const state = new (std::nothrow) JpegDecodeStateUVE{};
```

`state` is a non-`volatile` automatic variable that is read in the `longjmp` recovery path (line 52 onward). The C standard leaves such objects indeterminate after `longjmp` if their value changed between `setjmp` and the jump. `state` is `const` and never reassigned, so the code is correct on the compilers in use; `cppcheck` flags it as `legacyUninitvar` and GCC's `-Wclobbered` targets this class of code.

The surrounding implementation is otherwise careful and was verified in detail: the pixel budget is validated before decoding (`ValidateJpegRgba8PixelBudgetUVE`, overflow-safe via `pixelCount > maximumBytes / 4`), decoder dimensions are cross-checked against the parsed metadata, and `malloc`/`free` and `new`/`delete` are correctly paired across all seven exit paths with ownership of the output buffer transferred exactly once.

The six near-identical manual cleanup blocks in this function are **not** a finding: RAII destructors cannot be relied upon across `longjmp`, so explicit cleanup is the correct pattern here.

**Recommended next action:** declare `state` as `volatile`-qualified pointer. Low priority.

---

## 8. 🔵 Static analysis results

`cppcheck 2.x`, `--enable=warning,performance,portability`, `--std=c++20`, 236 translation units, vendored code excluded. Nine findings raised; each was read against the source.

| Finding | Count | Verdict |
|---|---|---|
| `containerOutOfBounds` in `audio_asset_uve.cpp` | 7 | **False positive** — the guard `offset > buffer.size() \|\| buffer.size() - offset < N` is correct; when `offset == size()` the second clause yields `0 < N` and returns early |
| `returnDanglingLifetime` in `scene_serializer_uve.cpp:1285` | 1 | **False positive** — `std::vector<std::byte>{ptr, ptr + n}` *copies* the range; `payloadText` is alive throughout construction |
| `legacyUninitvar` in `jpeg_metadata_uve.cpp:47` | 1 | **Partially valid** — see §7 |

No new findings were introduced relative to the pre-`#84` baseline.

---

## 9. 🟢 Build, test and CI verification

### Build — 🟢 VERIFIED

```
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DUVE_BUILD_TESTS=ON   → exit 0
cmake --build build --parallel $(nproc)                                  → exit 0
```

**0 errors, 0 warnings.** 55 static libraries, 8 executables, 44 modules registered in the root `CMakeLists.txt`.

Required system packages: `libglfw3-dev`, `libglew-dev`, `libgl1-mesa-dev`, `libglu1-mesa-dev`, `zlib1g-dev`, `libjpeg-dev`, `ninja-build`. `nlohmann_json` v3.11.3 and GoogleTest are fetched by CMake. `libpng` and OpenAL are **not** required — no `find_package` references them.

### Tests — 🟢 VERIFIED (serial)

`ctest -j1` → **2,519 tests, 100% passed, 0 failed.** See §2 for parallel behaviour.

### Continuous integration — 🟢 VERIFIED

Workflow `.github/workflows/ci.yml`, [run 35518768371](https://github.com/UniVexStudio/UniVex_Engine/actions/runs/35518768371) on this commit: **all steps green.** The tree at `5174e37` is byte-identical to the tree CI also exercised in run 35518187444, where the test step reported 2,519 passed with 12 skipped.

CI covers what this container cannot: it installs `mesa-vulkan-drivers` (lavapipe) and runs under `xvfb`, and the workflow fails the build if the Vulkan device cases skip themselves. On this commit that step reported **`Vulkan device cases executed: 39`** with `[ PASSED ] 39 tests`.

### Structural checks — 🟢 VERIFIED

- **Orphan sources:** none. Every `.cpp` under `Engine/` is referenced by a `CMakeLists.txt`.
- **Byte-identical duplicate files:** none, by MD5 across all `.h` and `.cpp`.
- **TODO / FIXME / HACK / XXX markers:** **zero** outside `Engine/Runtime/UI/thirdparty/`.
- **Math boundary check:** `Engine/Tools/check_math_boundary.py` passes, enforcing the documented `univex::math` / `UVE::Math` separation. It runs as a CI step.

---

## 10. 🟣 Coverage not verifiable in this environment

**158** of 2,519 tests skip in the audit container for lack of a GPU, a display, and a Vulkan
driver. Before the §2 work the figure was 170; the twelve `BuiltInShaderParityUVETest` cases now
execute and pass.

| Suite | Skipped |
|---|---|
| `GlRenderDeviceUVETest` | 74 |
| `VulkanRenderDeviceUVETest` | 39 |
| `WindowManagerUVETest` | 13 |
| `ComputeWorkloadsVulkanUVETest` | 7 |
| `EngineCoreUVETest` (windowed paths) | 6 |
| Remaining GL compute suites | 19 |

Under `xvfb`, which is what CI uses, the same tree skips only **47**.

**This is a limitation of the audit environment, not a gap in the project.** On CI all 39 Vulkan
device cases execute and pass. Device-dependent behaviour is therefore 🟣 UNVERIFIED *here* and
🟢 VERIFIED *on CI*. Any statement that the Vulkan backend is untested would be incorrect.

A correction to the previous revision of this document, which reported 350 skips and a
205-test remainder: those figures came from a `grep` that counted each skipped test twice.
`ctest`'s own count is authoritative and is what the table above uses.

---

## 11. 🔵 Informational observations

- **`actions/checkout@v4` deprecation.** CI emits `Node.js 20 is deprecated ... actions/checkout@v4`. Non-blocking; the runner forces Node 24. A version bump resolves it.
- **Vulkan guard threshold.** `.github/workflows/ci.yml` requires at least 17 Vulkan cases to execute, while 39 actually run. The floor is deliberately conservative so that legitimately gated cases do not break the build, while still catching the real failure mode (driver absent → 0 execute).
- **Resolved since the previous audit.** `MakeRootRelativePathUVE` was previously duplicated between `project_file_index_uve.cpp` and `project_change_watcher_uve.cpp`; it is now consolidated in `Engine/Runtime/Asset/Internal/project_tree_helpers_uve.cpp`. Font `.inc` byte blobs are no longer committed — they are generated at build time by `Engine/Tools/embed_file.py` from a single canonical `.ttf` per face.

---

## 12. Prioritised recommendations

| # | Action | Status addressed | Priority |
|---|---|---|---|
| 1 | ~~Give test fixtures per-process unique scratch paths~~ — **done**, see §2 | 🟢 §2 | — |
| 2 | Find the unjoined thread behind the parallel abort, then enable `-j` in CI | 🟠 §2b | High |
| 3 | Keep watching for the `CollisionLifecycle` SEGFAULT under `-j` | 🟣 §2 | Medium |
| 4 | Export `IsFiniteUVE(const Vector3UVE&)`; remove 17 local copies | 🟡 §3 | Medium |
| 5 | Consolidate binary helpers on the overflow-safe guard | 🟡 §4 | Medium |
| 6 | Resolve the unused `ComponentUVE` concept | 🟠 §6 | Low |
| 7 | Bump `actions/checkout` | 🔵 §11 | Low |
| 8 | Qualify the JPEG `state` pointer as `volatile` | 🟡 §7 | Low |

No production code was modified in the course of this audit.
