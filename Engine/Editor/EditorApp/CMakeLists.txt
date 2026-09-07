# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The editor shell's first interactive slice: a real desktop window built on
# Engine/Runtime/Window + Engine/Runtime/Input, driving Engine/Editor/Viewport's
# existing ViewportRenderPass/WorldUveEntitySource bridge instead of Viewport's own
# raw-GLFW standalone demo app. See src/main.cpp for what this does and does not
# cover - it is a scaffold, not a full editor with panels.
#
# univex_viewport_engine_bridge already PUBLIC-links univex_viewport_app_support
# (-> univex_viewport_gl -> OpenGL::GL/GLEW::GLEW) and uve_world (-> uve_scene,
# uve_entity, uve_memory, uve_events, uve_debug) transitively - only uve_window and
# uve_input are new link dependencies this target needs directly.

add_executable(univex_editor src/main.cpp)
target_link_libraries(univex_editor PRIVATE
    univex_viewport_engine_bridge
    uve_window
    uve_input
)
target_compile_features(univex_editor PRIVATE cxx_std_20)
uve_set_warnings(univex_editor)
