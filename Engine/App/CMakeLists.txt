# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Mirrors the reference engine's own top-level engine/app/ module: the real UniVex Engine and
# Editor launcher executables. Engine/Runtime/ProjectCheck ported the third reference executable
# (uve_project_check) early, deferring these two until uve_core and uve_editor were real -
# both landed this session, so this closes that deferred note.
#
# uve_runtime: a proof-of-life entry point. By default opens a real GLFW3 window with an OpenGL
# render device and drives a full Init -> Load -> N x (BeginFrame/Update/LateUpdate/Render/
# EndFrame) -> Shutdown cycle through EngineCoreUVE. `--headless` falls back to
# NullWindowManagerUVE/NullRenderDeviceUVE (no display required). argv[1..] forwards to
# EngineConfigUVE::commandLineArgs, parsed by CommandLineUVE.
#
# uve_editor_app (binary name uve_editor, matching the reference's own OUTPUT_NAME): the real
# standalone UniVex Editor Foundation v1 entry point. Drives EngineCoreUVE's lifecycle by hand
# (Init()/Load()/TickFrameUVE()) rather than through RunUVE(), wrapping it in its own exception
# boundary that mirrors RunUVE()'s documented one. `--bridge-stdio` runs a framed JSON-RPC bridge
# server instead of native ImGui/GLFW presentation - the mode this project's own tooling drives
# the editor through non-interactively.
#
# This target also wires the real Viewport panel: EditorUVE (Engine/Editor/EditorCore) exposes only
# a generic, GL/ImGui-agnostic render-callback hook (SetViewportPanelRendererUVE) - the concrete
# rendering lives here, since Engine/App is the top-level layer allowed to depend on the
# Engine/Editor/Viewport sibling module directly (EditorCore itself must not, per its own
# upper-layer-but-not-cross-editor-module boundary). Links univex_viewport_engine_bridge (grid +
# orbit camera + gizmos + real-entity proxy cubes, transitively pulling in GLEW/OpenGL) and
# uve_editor_imgui directly (not just through uve_editor's own PRIVATE link of it) so this file's
# own <imgui.h> calls - reading mouse input for the orbit camera - have their include path.

add_executable(uve_runtime src/main.cpp)
target_link_libraries(uve_runtime PRIVATE uve_core uve_pack_lib)
target_compile_features(uve_runtime PRIVATE cxx_std_20)
uve_set_warnings(uve_runtime)

add_executable(uve_editor_app src/editor/main.cpp)
set_target_properties(uve_editor_app PROPERTIES OUTPUT_NAME uve_editor)
target_link_libraries(uve_editor_app PRIVATE uve_editor univex_viewport_engine_bridge uve_editor_imgui)
target_compile_features(uve_editor_app PRIVATE cxx_std_20)
uve_set_warnings(uve_editor_app)

#
# uve_editor_app_cs (binary name uve_editor_cs): Phase 0 of the C#/ImGui.NET editor migration -
# see Engine/App/src/editor_cs/main.cpp's own top comment and the plan checkpoint "C# (ImGui.NET)
# editor migration - Phase 0". Entirely additive: uve_runtime/uve_editor_app above are untouched,
# and this whole block is skipped (with a status message, not an error) when no .NET SDK is found,
# so a checkout without .NET still configures and builds every other target exactly as before.
#
# Locates the .NET SDK's native-hosting headers/libs (nethost.h/hostfxr.h/coreclr_delegates.h,
# libnethost) by finding the `dotnet` executable and searching its own installation tree for the
# `Microsoft.NETCore.App.Host.<rid>` pack - the same pack `dotnet publish` itself resolves from,
# so this stays correct across SDK versions/RIDs without hardcoding a path or a version number.
find_program(UVE_DOTNET_EXECUTABLE dotnet)
if(UVE_DOTNET_EXECUTABLE)
    # `dotnet` on PATH is commonly a symlink (e.g. /usr/bin/dotnet -> ../lib/dotnet/dotnet on this
    # Ubuntu apt package) - resolve it first, or the derived root points at /usr/bin instead of
    # the real install tree that actually contains packs/.
    get_filename_component(UVE_DOTNET_EXECUTABLE_REAL "${UVE_DOTNET_EXECUTABLE}" REALPATH)
    get_filename_component(UVE_DOTNET_ROOT "${UVE_DOTNET_EXECUTABLE_REAL}" DIRECTORY)
    file(GLOB UVE_DOTNET_HOST_NATIVE_DIRS
        "${UVE_DOTNET_ROOT}/packs/Microsoft.NETCore.App.Host.*/*/runtimes/*/native"
    )
    set(UVE_DOTNET_HOST_NATIVE_DIR "")
    foreach(UVE_CANDIDATE_DIR ${UVE_DOTNET_HOST_NATIVE_DIRS})
        if(EXISTS "${UVE_CANDIDATE_DIR}/nethost.h")
            set(UVE_DOTNET_HOST_NATIVE_DIR "${UVE_CANDIDATE_DIR}")
            break()
        endif()
    endforeach()

    if(UVE_DOTNET_HOST_NATIVE_DIR)
        message(STATUS "UVE: found .NET native hosting headers at ${UVE_DOTNET_HOST_NATIVE_DIR} - building uve_editor_app_cs")

        # `dotnet publish` produces EditorCoreCs.dll + its .deps.json/.runtimeconfig.json here;
        # Engine/App/src/editor_cs/main.cpp's hostfxr embedding loads the assembly from this exact
        # path (passed through as the UVE_EDITOR_CS_PUBLISH_DIR compile definition below).
        set(UVE_EDITOR_CS_PUBLISH_DIR "${CMAKE_BINARY_DIR}/Engine/Editor/EditorCoreCs/publish")
        add_custom_target(uve_editor_cs_managed
            COMMAND "${UVE_DOTNET_EXECUTABLE}" publish
                    "${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCoreCs/EditorCoreCs.csproj"
                    -c Release -r linux-x64 --self-contained false
                    -o "${UVE_EDITOR_CS_PUBLISH_DIR}"
            COMMENT "Publishing EditorCoreCs (managed editor host, Phase 0)"
            VERBATIM
        )

        add_executable(uve_editor_app_cs src/editor_cs/main.cpp)
        set_target_properties(uve_editor_app_cs PROPERTIES
            OUTPUT_NAME uve_editor_cs
            # Exports this executable's own dynamic symbol table (uve_capi_* included, pulled in
            # from the statically-linked uve_engine_capi below) so the managed side's
            # DllImport("uve_engine_capi") resolves against NativeLibrary.GetMainProgramHandle()
            # instead of a real loaded .so - see uve_engine_capi's own CMakeLists.txt comment for
            # why it is STATIC rather than SHARED.
            ENABLE_EXPORTS ON
        )
        # SYSTEM: these are Microsoft's own vendored headers (e.g. coreclr_delegates.h's own
        # UNMANAGEDCALLERSONLY_METHOD macro uses an old-style cast) - matches how this repo already
        # treats the vendored Dear ImGui headers in Engine/Editor/EditorCore/CMakeLists.txt, keeping
        # this project's own warnings-as-errors policy scoped to this project's own source.
        target_include_directories(uve_editor_app_cs SYSTEM PRIVATE "${UVE_DOTNET_HOST_NATIVE_DIR}")
        target_compile_definitions(uve_editor_app_cs PRIVATE
            UVE_EDITOR_CS_PUBLISH_DIR="${UVE_EDITOR_CS_PUBLISH_DIR}"
        )
        target_link_libraries(uve_editor_app_cs PRIVATE
            uve_engine_capi
            "${UVE_DOTNET_HOST_NATIVE_DIR}/libnethost.a"
            ${CMAKE_DL_LIBS}
        )
        target_compile_features(uve_editor_app_cs PRIVATE cxx_std_20)
        uve_set_warnings(uve_editor_app_cs)
        add_dependencies(uve_editor_app_cs uve_editor_cs_managed)
    else()
        message(STATUS "UVE: .NET SDK found but no Microsoft.NETCore.App.Host pack - skipping uve_editor_app_cs")
    endif()
else()
    message(STATUS "UVE: no .NET SDK found (dotnet not on PATH) - skipping uve_editor_app_cs")
endif()
