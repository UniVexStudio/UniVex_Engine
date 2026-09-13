# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# A minimal, additive extern "C" boundary (uve_engine_capi) letting a non-C++ host - initially a
# C# assembly embedded into this same process via .NET's native hosting API (hostfxr) - drive
# EngineCoreUVE's lifecycle and read the same real viewport GL texture
# Engine/App/src/editor/main.cpp's ViewportPanelBackendUVE already produces for the C++ ImGui
# editor.
#
# STATIC, not SHARED: every static library in this repo is compiled without -fPIC (they only ever
# link into executables), and linking them all into a real .so here failed at link time
# (`relocation R_X86_64_TPOFF32 ... can not be used when making a shared object`) - the fix would
# be a project-wide `CMAKE_POSITION_INDEPENDENT_CODE ON`, a much larger, non-additive change to
# every one of this repo's ~440 targets just for one new module. Instead, this library links
# directly into Engine/App/src/editor_cs's hosting executable (built with ENABLE_EXPORTS so its
# own dynamic symbol table, including every uve_capi_* function transitively pulled in from here,
# is visible to the process at runtime) - the managed side resolves its `DllImport("uve_engine_capi")`
# P/Invoke calls against `NativeLibrary.GetMainProgramHandle()` via a `DllImportResolver` instead
# of a real loaded shared object. Same net effect (native -> managed via hostfxr, managed -> native
# via P/Invoke both reach the one live EngineCoreUVE instance in this process, zero IPC, zero
# cross-context GL sharing since both run on the same OS thread) without the global build-flag change.
#
# Does not wrap EditorUVE/EditorBridgeUVE yet - only EngineCoreUVE plus a minimal viewport render
# pass (mirroring ViewportPanelBackendUVE, minus its ImGui/EditorUVE-selection-specific wiring).
# That lands once a managed panel actually needs the bridge's snapshot/dispatch surface (Scene
# Hierarchy/Inspector, in a later slice) - this first slice only has to prove the native-hosting
# call path and the shared-GL-context viewport texture handoff both work end-to-end.

add_library(uve_engine_capi STATIC
    src/uve_engine_capi.cpp
)
target_include_directories(uve_engine_capi PUBLIC include)
target_compile_features(uve_engine_capi PUBLIC cxx_std_20)
target_link_libraries(uve_engine_capi PUBLIC uve_core univex_viewport_engine_bridge)
uve_set_warnings(uve_engine_capi)
