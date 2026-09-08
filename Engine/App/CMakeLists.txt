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

add_executable(uve_runtime src/main.cpp)
target_link_libraries(uve_runtime PRIVATE uve_core)
target_compile_features(uve_runtime PRIVATE cxx_std_20)
uve_set_warnings(uve_runtime)

add_executable(uve_editor_app src/editor/main.cpp)
set_target_properties(uve_editor_app PROPERTIES OUTPUT_NAME uve_editor)
target_link_libraries(uve_editor_app PRIVATE uve_editor)
target_compile_features(uve_editor_app PRIVATE cxx_std_20)
uve_set_warnings(uve_editor_app)
