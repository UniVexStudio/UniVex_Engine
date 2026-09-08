# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The visual node-graph scripting system: ScriptGraphUVE (the graph data model), the built-in
# node library, the small value types (Vector2/Vector3/Rotation/Transform/Collection),
# ScriptCompilerIrUVE/ScriptBytecodeUVE/ScriptVmUVE (compile-to-bytecode + a bytecode VM),
# ScriptRuntimeUVE/ScriptEntityQueryAdapterUVE/ScriptComponentRuntimeOwnershipUVE/
# ScriptGraphRuntimeBindingUVE (wiring a compiled graph to a live Scene entity),
# ScriptGraphPersistenceUVE/ScriptGraphCanvasPersistenceUVE/ScriptAssetLoaderUVE/
# ScriptHotReloadUVE (on-disk graph serialization + asset loading + change-triggered recompile),
# ScriptGraphCanvasUVE/ScriptGraphEditorBackendUVE (the editor-facing graph-canvas model), and
# ScriptDebuggerUVE/ScriptSourceMappingUVE (breakpoints and bytecode-to-node source mapping).
#
# Ported as a single flat module matching the reference's own uve_scripting CMakeLists.txt
# exactly - one undivided static library, no internal sub-target split. An earlier, much
# earlier-stage scaffold in this repo speculatively pre-created Pins/Graph/Compiler/VM/Runtime/
# Reflection/Nodes placeholder subfolders here (before this module's real reference structure was
# ever surveyed) - removed as part of this port since they named a split that doesn't exist in
# the actual reference codebase and wasn't grounded in any verified real-Unreal precedent either;
# every other module ported this session follows the real reference's own module boundaries, and
# this one does too.
#
# Dependency closure survey: only uve/asset/i_file_system_uve.h, uve/math/*, and
# uve/scene/{components/script_component_uve.h, entity_uve.h, i_entity_manager_uve.h} outside its
# own headers - all already ported (Asset, Math, Component, Scene/Entity). nlohmann/json.hpp is
# the only third-party dependency beyond stdlib, matching the reference's own PRIVATE link.

add_library(uve_scripting STATIC
    src/script_graph_uve.cpp
    src/script_builtin_nodes_uve.cpp
    src/script_vector3_value_uve.cpp
    src/script_vector2_value_uve.cpp
    src/script_rotation_value_uve.cpp
    src/script_transform_value_uve.cpp
    src/script_collection_value_uve.cpp
    src/script_compiler_ir_uve.cpp
    src/script_bytecode_uve.cpp
    src/script_vm_uve.cpp
    src/script_entity_query_adapter_uve.cpp
    src/script_runtime_uve.cpp
    src/script_graph_persistence_uve.cpp
    src/script_graph_canvas_persistence_uve.cpp
    src/script_graph_editor_backend_uve.cpp
    src/script_graph_canvas_uve.cpp
    src/script_debugger_uve.cpp
    src/script_hot_reload_uve.cpp
    src/script_graph_runtime_binding_uve.cpp
    src/script_component_runtime_ownership_uve.cpp
    src/script_asset_loader_uve.cpp
    src/script_source_mapping_uve.cpp
)
target_include_directories(uve_scripting PUBLIC include)
target_link_libraries(uve_scripting PUBLIC uve_scene PRIVATE nlohmann_json::nlohmann_json)
target_compile_features(uve_scripting PUBLIC cxx_std_20)
uve_set_warnings(uve_scripting)
