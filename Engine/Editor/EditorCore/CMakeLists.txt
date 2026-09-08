# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Editor Foundation v1 is intentionally an upper-layer module: this may compose Engine/Runtime's
# Engine, Scripting, and Plugins services, but no Engine/Runtime module may depend on it (matches
# the reference module's own boundary comment verbatim).
#
# An earlier, pre-survey scaffolding stage speculatively pre-created a whole Unreal-style editor
# UI taxonomy here (AssetBrowser/ContentBrowser/Inspector/Outliner/Menus/SceneEditor/
# EditorWidgets/VisualScripting/Tools/Theme/Panels/Gizmos placeholder subfolders, sibling to this
# one) before the actual reference structure was ever surveyed. Removed as part of this port,
# same reasoning as Engine/Runtime/Scripting's own Pins/Graph/Compiler/etc. cleanup: the real
# reference engine/editor/ module is one flat library (EditorUVE, EditorBridgeUVE/
# EditorBridgeStdioUVE, DeveloperConsoleUVE, EditorThemeUVE, EditorUiAssetsUVE,
# MeshThumbnailRendererUVE, InspectorDrawerRegistryUVE, EditorToolSessionUVE,
# RegressionHarnessUVE) with no internal sub-target split, and this port follows that real
# structure rather than the earlier speculative guess.
#
# EditorUVE/EditorBridgeUVE compose uve_core, uve_scripting, and uve_plugins directly - all three
# are now real (EngineCoreUVE and the full Scripting module both landed this session).
#
# ImGui's GLFW backend (imgui_impl_glfw, vendored below) installs its own GLFW key/mouse/scroll/
# char callbacks on the native window via ImGui_ImplGlfw_InitForOpenGL(window, true) - the
# `true` requests install_callbacks, which chains to whatever callback WindowManagerUVE already
# installed at window-creation time rather than replacing it (ImGui's own backend saves the
# previous callback pointer and calls it after its own logic). Since EditorUVE only initializes
# ImGui after receiving an already-constructed, already-valid Window::IWindowManagerUVE&
# (confirmed in editor_uve.cpp: it checks IsValidUVE()/GetNativeWindowHandleUVE() != nullptr
# first), WindowManagerUVE's own callbacks are always installed first - no conflict, no extra
# wiring needed. This resolves the ImGui/GLFW callback-ordering question flagged earlier this
# session as unresolved.

find_package(OpenGL REQUIRED)
include(FetchContent)

# Pinned Dear ImGui source, following this repo's own established GLFW/GoogleTest FetchContent
# precedent (matches the reference module's own approach exactly, including its own comment
# below on why `docking` rather than a tagged release).
#
# `docking` (not a tagged release) rather than a `v1.91.x` tag: the editor's panels need
# DockSpace/DockBuilder, which do not exist in the non-docking mainline headers. Upstream has not
# folded the docking branch into mainline, so this tracks its HEAD rather than a pinned commit
# deliberately - re-resolve GIT_SHALLOW's fetched tip to pick up upstream docking-branch fixes;
# if a specific commit ever needs pinning for reproducibility, replace GIT_TAG's value with that
# commit hash directly.
FetchContent_Declare(
    uve_imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG docking
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(uve_imgui)
if(NOT uve_imgui_POPULATED)
    FetchContent_Populate(uve_imgui)
endif()

add_library(uve_editor_imgui STATIC
    ${uve_imgui_SOURCE_DIR}/imgui.cpp
    ${uve_imgui_SOURCE_DIR}/imgui_draw.cpp
    ${uve_imgui_SOURCE_DIR}/imgui_tables.cpp
    ${uve_imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${uve_imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${uve_imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(uve_editor_imgui SYSTEM PUBLIC
    ${uve_imgui_SOURCE_DIR}
    ${uve_imgui_SOURCE_DIR}/backends
)
target_link_libraries(uve_editor_imgui PRIVATE glfw OpenGL::GL)
target_compile_features(uve_editor_imgui PUBLIC cxx_std_20)
# ImGui is third-party source with its own deliberate warning profile - this project's
# warning-as-error policy stays active for every UVE source target, while this pinned dependency
# compiles without project-specific diagnostics (matches the reference module's own policy).
target_compile_options(uve_editor_imgui PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang>:-w>)

add_library(uve_editor STATIC
    src/editor_bridge_stdio_uve.cpp
    src/editor_bridge_uve.cpp
    src/developer_console_uve.cpp
    src/editor_tool_session_uve.cpp
    src/editor_theme_uve.cpp
    src/editor_ui_assets_uve.cpp
    src/mesh_thumbnail_renderer_uve.cpp
    src/editor_uve.cpp
    src/inspector_drawer_registry_uve.cpp
    src/regression_harness_uve.cpp
)
target_include_directories(uve_editor PUBLIC include)
target_compile_features(uve_editor PUBLIC cxx_std_20)
target_link_libraries(uve_editor
    PUBLIC uve_core uve_scripting uve_plugins
    PRIVATE uve_editor_imgui nlohmann_json::nlohmann_json glfw
)
uve_set_warnings(uve_editor)
