# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One real .h + .cpp pair per CanvasLayer-family scene-node kind, matching the convention
# Engine/Runtime/Nodes/3D established. The four UI kinds promoted into the Scene node registry
# (Canvas, UI Text, UI Image, UI Button) each define a NodeDefinition here — the kind's creation
# recipe (components to attach, authored defaults, default entity name) — while their component
# structs stay in Engine/Runtime/Component (one truth per concept). uve_component supplies the
# component structs + EntityUVE, uve_entity supplies IEntityManagerUVE for the Apply functions.

add_library(uve_nodes_canvas_layer STATIC
    Internal/canvas_uve.cpp
    Internal/ui_text_uve.cpp
    Internal/ui_image_uve.cpp
    Internal/ui_button_uve.cpp
)
target_include_directories(uve_nodes_canvas_layer PUBLIC Expose)
target_link_libraries(uve_nodes_canvas_layer PUBLIC uve_math uve_component uve_entity)
target_compile_features(uve_nodes_canvas_layer PUBLIC cxx_std_20)
