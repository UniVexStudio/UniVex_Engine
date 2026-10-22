# Build Variants — Debug, Development, Release, Shipping

A build variant is not "which optimization flag". It is a statement about **what the
binary is allowed to do at runtime**: whether a failed invariant can stop it, and which
severities of diagnostic it can emit. Those answers change what code the compiler is
allowed to throw away, so they have to be decided once, early, and carried in the
preprocessor — not inferred later from whether a call happens to look hot.

This document describes the four variants this engine builds as, who decides which one
is in effect, exactly what each one strips, and the compile-time invariants that make a
contradictory variant impossible to build.

| | `Debug` | `Development` | `Release` | `Shipping` |
|---|---|---|---|---|
| CMake build type | `Debug` | `RelWithDebInfo` | `Release` | `MinSizeRel` |
| `UVE_DEBUG` / assertions | on | on | off | off |
| `UVE_SHIPPING` | 0 | 0 | 0 | 1 |
| Runtime default log level | `Trace` (0) | `Debug` (1) | `Info` (2) | `Warning` (3) |
| Statements stripped at compile time | none | none | none | `UVE_TRACE`, `UVE_INFO` |
| Typical use | day-to-day development, all tests | playtesting, profiling with symbols | internal / press builds | the artifact that ships |

## Who decides the variant

`Engine/CMake/UveBuildConfiguration.cmake` owns the decision. Two inputs can describe a
build — CMake's conventional `CMAKE_BUILD_TYPE`, and this engine's `UVE_BUILD_CONFIGURATION`
— and the precedence is:

1. **An explicitly requested profile wins**, and the CMake build type follows it.
   `-DUVE_BUILD_CONFIGURATION=Shipping` produces shipping semantics regardless of what
   CMake's type vocabulary happens to be called.
2. **Otherwise the profile is derived from `CMAKE_BUILD_TYPE`.** `Debug` → `Debug`,
   `RelWithDebInfo` → `Development`, `Release` → `Release`, `MinSizeRel` → `Shipping`.
   This is the path a tool outside the repo takes when it passes a conventional flag.
3. **Otherwise both default to `Debug`**, with a warning if `CMAKE_BUILD_TYPE` was set to
   something with no profile equivalent.

Earlier this module ignored `CMAKE_BUILD_TYPE` entirely and forced it back to `Debug`, so
`-DCMAKE_BUILD_TYPE=Release` silently produced an unoptimized binary with assertions
enabled. That is the worst possible failure mode: every test passes, and the artifact
ships wrong. The rule above exists specifically so that the conventional flag keeps its
conventional meaning while a named profile still outranks it.

The chosen profile is then published to every translation unit as definitions:
`UVE_BUILD_CONFIGURATION_<NAME>=1`, `UVE_SHIPPING=0|1`,
`UVE_PROFILE_ASSERTIONS_ENABLED=0|1`, `UVE_PROFILE_DEFAULT_LOG_LEVEL=<0..5>` and
`UVE_COMPILED_LOG_LEVEL=<threshold>`.

## Assertions follow the profile, not `NDEBUG`

`UVE_DEBUG` — which gates `UVE_ASSERT` — is defined as
`UVE_PROFILE_ASSERTIONS_ENABLED` whenever the profile module configured the build. Only
integrations that build these headers outside this module fall back to the historical
`NDEBUG` rule.

That matters because `NDEBUG` is a compiler-library convention, not a statement about the
product. CMake's stock `RelWithDebInfo` flags define `NDEBUG`, so a `Development` build
(optimized, symbols, **assertions on** — the profile people actually playtest with) would
otherwise strip exactly the checks that make playtesting useful. `Shipping` sets
`NDEBUG` *and* turns assertions off; the two agree there, which is why nobody notices the
distinction until a playtest build goes quiet.

A disabled assertion still type-checks its condition (`(void)sizeof(cond)`) but never
evaluates it, so call sites need no `#if UVE_DEBUG` guard to stay warning-clean and
side-effect-free.

## Compile-time log stripping

`Release` and everything below it keep every log statement and filter by severity at
runtime. `Shipping` sets `UVE_COMPILED_LOG_LEVEL=3`, and
`Engine/Runtime/Core/Logging/Expose/uve/logging/logging_macros_uve.h` uses it to remove
statements **in the preprocessor**:

| Macro | Stripped when `UVE_COMPILED_LOG_LEVEL >` | At `Shipping` (3) |
|---|---|---|
| `UVE_TRACE` | 0 | stripped |
| `UVE_INFO` | 2 | stripped |
| `UVE_WARNING` | 3 | kept |
| `UVE_ERROR` | 4 | kept |
| `UVE_FATAL` | never | kept |

The thresholds are derived from the runtime filter, not invented: a statement is stripped
exactly when the runtime level would have discarded it anyway. So stripping changes what
is *in the binary*, never what a running build would have *printed* — in shipping,
`Trace` and `Info` are gone and `Warning` and above still reach the log. Warnings are kept
deliberately: they are rare, they are the last signal before an error, and a build that
can still describe what went wrong is worth more than the few hundred bytes of format
strings.

The difference from a runtime filter is not cosmetic. A runtime filter still emits the
format string, its literal fragments and the call, so a shipping build would carry every
`INFO` line it is guaranteed never to print — both binary size and strings that can be
scraped out of the executable.

**Stripped calls do not evaluate their arguments.** That is the point (no string
formatting, no branch), but it has a consequence worth stating: a value computed *only*
for a log line becomes unused in shipping. Mark it `[[maybe_unused]]` rather than
reintroducing the call. Code whose side effects matter must not hide them in a log
argument.

## The header that makes contradictions impossible

`Engine/Runtime/Platform/Expose/uve/platform/build_variant_uve.h` is included by
`platform_uve.h`, so every translation unit that can assert or log validates the variant
it was actually compiled under. It publishes four compile-time facts:

- `UVE::Build::ProfileUVE` / `kProfileUVE` — the profile, with stable numeric values
  (`Debug=0`, `Development=1`, `Release=2`, `Shipping=3`) so it can be written into a
  crash report or save header without a string table.
- `kAssertionsEnabledUVE` — true when `UVE_ASSERT` evaluates its condition.
- `kCompiledOutLoggingUVE` — true when this profile strips statements at compile time.
- `kCompiledLogLevelUVE` — the threshold, or 0 when nothing is stripped.

and turns four mistakes into build errors:

1. **No profile defined** — `#error`, because guessing a profile is worse than failing.
2. **More than one profile defined** — every `#if` downstream would silently take
   whichever branch came first.
3. **Shipping with assertions enabled** — silent, costs frame time, and surfaces only as a
   crash dialog in a player's face.
4. **A strip threshold that disagrees with the runtime default** — below it, the build
   deletes messages it claims it can print; above it, it keeps paying for statements it
   never emits.

## Building

```bash
# Named profile (wins over everything else)
cmake -S . -B build-shipping -G Ninja -DUVE_BUILD_CONFIGURATION=Shipping
# Conventional flag (profile derived from it)
cmake -S . -B build-release  -G Ninja -DCMAKE_BUILD_TYPE=Release
# Defaults to Debug, with tests
cmake -S . -B build -G Ninja -DUVE_BUILD_TESTS=ON
```

## What is verified, and what is not

`Test/Core/Platform/build_variant_uve_tests.cpp` locks the published facts against each
other: exactly one profile selected, the compile-time constants agreeing with the
definitions, shipping never carrying live assertions, the strip threshold equalling the
runtime default, distinct stable profile names, and log macros still evaluating their
arguments in profiles that do not strip. The build module itself is verified by configuring
`-DCMAKE_BUILD_TYPE=Release` and confirming the resulting profile, build type and defines;
and the log macros are verified out of band by compiling probe translation units that
reference undeclared identifiers — such a probe fails in `Debug`, `Development` and
`Release` (the argument is evaluated) and compiles only under `Shipping` (the call was
removed before the compiler saw it).

The `Release` profile is also built and tested end to end, not just configured: the whole
engine and the core test binary compile at `-O3 -DNDEBUG` with assertions off, and the same
test suite passes there. That run is what surfaced the three things an unexercised profile
hides, and each is now handled at its source rather than by weakening a rule:

- Two death tests (`LinearArenaUVEDeathTest`) had nothing to observe once assertions are
  compiled out, and failed by not dying. They now skip when `kAssertionsEnabledUVE` is
  false, which is the honest outcome: the property is real, this profile simply cannot
  demonstrate it.
- GCC 12 reports false positives only in optimized builds, because that is the first time
  the standard library's string and vector code is inlined. Two call sites were rewritten
  into a form the optimizer can bound (`crash_reporter_uve.cpp` appends instead of using a
  chained `operator+`, `animation_graph_uve.cpp` fills an optional with an `if` instead of a
  conditional expression), and `-Wno-restrict` is applied only for GCC < 13, the compiler
  that has the bug. No warning class is disabled project-wide.

Two things this does **not** cover, and they are separate roadmap rows rather than
oversights:

- **Assets are not stripped or cooked per variant.** A shipping build still contains
  whatever source-format assets the project carries; that is the asset-cooking row.
- **There is no per-platform packaging or code signing.** Variants decide the contents of
  a build; installers and store bundles are the platform-store row.
