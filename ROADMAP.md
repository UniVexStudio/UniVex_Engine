# UniVex Engine Roadmap

**Roadmap v1 is complete** (archived in git history). All core runtime, rendering, physics, audio, networking, asset pipeline, UI, editor tooling, packaging, and CI quality gates are shipped and verified across Linux and Windows.

For current subsystem documentation, see [docs/ARCHITECTURE_GUIDE.md](docs/ARCHITECTURE_GUIDE.md) and [docs/GETTING_STARTED.md](docs/GETTING_STARTED.md).

---

## Roadmap v2 Backlog

### 1. Gameplay & Scripting
- [ ] UVScript: `wait` inside regular `fn` functions
- [ ] In-editor script debugger (breakpoints, stepping, live values)
- [ ] AI crowd simulation for large numbers of agents
- [ ] Entity Editor: Scripting and Signals tabs, compile diagnostics list, and bottom dock (Content, Timeline, Anim Graph)
- [ ] Live C++ gameplay hot-reloading without restarting the editor

### 2. Platform & Native Graphics Backends
- [ ] Native graphics backends for target OSes (DX12 / Metal) alongside OpenGL and Vulkan 1.3
- [ ] Verified macOS desktop CI build and test runner
- [ ] Hardware OpenXR runtime integration and WebGL2/WebGPU browser deployment
- [ ] Console platform SDK integration

### 3. Hardware Profiling & Quality Gates
- [ ] Physical low-memory (3 GB) mobile device memory and thermal benchmarks
- [ ] Dedicated nightly ASan/UBSan sanitizer CI run
