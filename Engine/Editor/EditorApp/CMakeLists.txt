# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# Viewport smoke harness - NOT the editor (2026-09-17 audit re-label, AUDIT.md section
# 5.3). The real, maintained Editor application is `uve_editor_app` in Engine/App;
# this executable predates it: a real desktop window built on
# Engine/Runtime/Window + Engine/Runtime/Input, driving Engine/Editor/Viewport's
# existing ViewportRenderPass/WorldUveEntitySource bridge instead of Viewport's own
# raw-GLFW standalone demo app. See Internal/main.cpp for what this does and does not
# cover - it is a scaffold, not a full editor with panels. Its remaining job is
# automated validation: `--max-frames N` runs a known number of real frames then exits,
# which suits headless (Xvfb) smoke testing far better than the full editor. All new
# editor work goes to Engine/App's uve_editor_app; do not grow this one.
#
# univex_viewport_engine_bridge already PUBLIC-links univex_viewport_app_support
# (-> univex_viewport_gl -> OpenGL::GL/GLEW::GLEW) and uve_world (-> uve_scene,
# uve_entity, uve_memory, uve_events, uve_logging) transitively - only uve_window and
# uve_input are new link dependencies this target needs directly.

add_executable(univex_editor Internal/main.cpp)
target_link_libraries(univex_editor PRIVATE
    univex_viewport_engine_bridge
    uve_window
    uve_input
)
target_compile_features(univex_editor PRIVATE cxx_std_20)
uve_set_warnings(univex_editor)
