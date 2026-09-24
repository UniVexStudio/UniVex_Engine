# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# One shared test executable (uve_core_tests) covers every module ported into the Test/Core/
# and Test/Math/ buckets so far - simpler than a separate GoogleTest executable per module while
# the engine is small. Its source list grows as each new module is ported (see the restructuring
# plan); it only ever links against modules that already exist in this repo.

include(FetchContent)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
FetchContent_MakeAvailable(googletest)

add_executable(uve_core_tests
    Support/test_scratch_uve.cpp
    Core/Platform/editor_project_package_uve_tests.cpp
    Core/Logging/logger_uve_tests.cpp
    Core/Logging/log_format_uve_tests.cpp
    Core/Memory/heap_allocator_uve_tests.cpp
    Core/Memory/pool_allocator_uve_tests.cpp
    Core/Memory/stack_allocator_uve_tests.cpp
    Core/Memory/memory_manager_uve_tests.cpp
    Core/Memory/allocator_utils_uve_tests.cpp
    Core/Memory/memory_alignment_uve_tests.cpp
    Core/Threading/job_counter_uve_tests.cpp
    Core/Threading/job_graph_uve_tests.cpp
    Core/Threading/thread_pool_uve_tests.cpp
    Core/Utilities/timer_uve_tests.cpp
    Core/Scheduling/frame_scheduler_uve_tests.cpp
    Core/Diagnostics/profiler_diagnostics_uve_tests.cpp
    Core/Events/event_system_uve_tests.cpp
    Core/Asset/uve_file_envelope_uve_tests.cpp
    Core/Asset/asset_database_uve_tests.cpp
    Core/Asset/asset_manager_uve_tests.cpp
    Core/Asset/file_system_uve_tests.cpp
    Core/Asset/mesh_asset_uve_tests.cpp
    Core/Asset/mesh_skinning_uve_tests.cpp
    Core/Asset/texture_asset_uve_tests.cpp
    Core/Asset/shader_asset_uve_tests.cpp
    Core/Asset/material_asset_uve_tests.cpp
    Core/Asset/asset_importer_uve_tests.cpp
    Core/Asset/bmp_importer_uve_tests.cpp
    Core/Asset/bmp_metadata_uve_tests.cpp
    Core/Asset/tga_importer_uve_tests.cpp
    Core/Asset/tga_metadata_uve_tests.cpp
    Core/Asset/png_importer_uve_tests.cpp
    Core/Asset/png_metadata_uve_tests.cpp
    Core/Asset/jpeg_importer_uve_tests.cpp
    Core/Asset/jpeg_metadata_uve_tests.cpp
    Core/Asset/obj_importer_uve_tests.cpp
    Core/Asset/obj_mesh_converter_uve_tests.cpp
    Core/Asset/obj_metadata_uve_tests.cpp
    Core/Asset/gltf_importer_uve_tests.cpp
    Core/Asset/gltf_mesh_converter_uve_tests.cpp
    Core/Asset/gltf_metadata_uve_tests.cpp
    Core/Asset/gltf_skeleton_uve_tests.cpp
    Core/Asset/mtl_importer_uve_tests.cpp
    Core/Asset/mtl_material_converter_uve_tests.cpp
    Core/Asset/mtl_metadata_uve_tests.cpp
    Core/Asset/hot_reload_uve_tests.cpp
    Core/Asset/asset_import_queue_uve_tests.cpp
    Core/Asset/project_change_watcher_uve_tests.cpp
    Core/Asset/project_file_index_uve_tests.cpp
    Core/Asset/resource_dependency_graph_uve_tests.cpp
    Core/Asset/animation_clip_asset_uve_tests.cpp
    Core/Asset/data_table_uve_tests.cpp
    Core/Asset/data_table_asset_uve_tests.cpp
    Core/Asset/data_table_asset_manager_uve_tests.cpp
    Core/Asset/data_table_asset_registration_uve_tests.cpp
    Core/Asset/data_table_importer_uve_tests.cpp
    Core/Asset/data_table_pipeline_uve_tests.cpp
    Core/Asset/data_table_registry_uve_tests.cpp
    Commandline/command_line_uve_tests.cpp
    Config/config_manager_uve_tests.cpp
    Config/settings_registry_uve_tests.cpp
    Network/reliable_packet_window_uve_tests.cpp
    Plugins/plugin_registry_uve_tests.cpp
    ProjectCheck/project_checker_uve_tests.cpp
    Input/input_system_uve_tests.cpp
    Input/mobile_input_system_uve_tests.cpp
    Input/mobile_gesture_system_uve_tests.cpp
    Input/mobile_gesture_recognizer_uve_tests.cpp
    RHI/null_render_device_uve_tests.cpp
    RHI/OpenGL/gl_render_device_uve_tests.cpp
    RHI/Vulkan/vulkan_render_device_uve_tests.cpp
    RHI/Shader/shader_binary_cache_uve_tests.cpp
    RHI/Shader/shader_diagnostics_parser_uve_tests.cpp
    RHI/Shader/shader_manager_uve_tests.cpp
    RHI/Shader/shader_preprocessor_uve_tests.cpp
    RHI/Shader/shader_program_uve_tests.cpp
    Window/adaptive_render_resolution_uve_tests.cpp
    Window/display_mode_validation_uve_tests.cpp
    Window/framebuffer_size_validation_uve_tests.cpp
    Window/monitor_info_validation_uve_tests.cpp
    Window/null_window_manager_uve_tests.cpp
    Window/window_desc_validation_uve_tests.cpp
    Window/window_manager_uve_tests.cpp
    Math/vector2_uve_tests.cpp
    Math/vector3_uve_tests.cpp
    Math/quaternion_uve_tests.cpp
    Math/matrix4x4_uve_tests.cpp
    Math/aabb_uve_tests.cpp
    Math/plane_uve_tests.cpp
    Math/frustum_uve_tests.cpp
)
target_link_libraries(uve_core_tests PRIVATE
    nlohmann_json::nlohmann_json
    uve_platform
    uve_logging
    uve_memory
    uve_threading
    uve_scheduling
    uve_diagnostics
    uve_math
    uve_utilities
    uve_events
    uve_asset
    uve_input
    uve_window
    uve_rhi
    uve_rhi_opengl
    uve_rhi_vulkan
    uve_rhi_null
    uve_rhi_shader
    uve_commandline
    uve_config
    uve_network
    uve_plugins
    uve_project_check_lib
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(uve_core_tests PRIVATE cxx_std_20)
target_compile_definitions(uve_core_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_core_tests)

# Support/ holds the suite's shared test-only helpers. Test/ itself goes on the include path
# so they are reached as "Support/<name>.h" from any subdirectory.
target_include_directories(uve_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# shader_binary_cache_uve_tests.cpp/shader_diagnostics_parser_uve_tests.cpp/
# shader_preprocessor_uve_tests.cpp unit-test uve_rhi_shader's module-private classes directly -
# this include path exposes only those module-private headers, never anything from
# Engine/Runtime/RHI/Shader/Internal as part of uve_rhi_shader's own public interface (matching
# uve_integration_tests' identical need for Engine/Runtime/Entity/Internal below).
target_include_directories(uve_core_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Runtime/RHI/Shader/Internal)

include(GoogleTest)
gtest_discover_tests(uve_core_tests)

#
# uve_integration_tests covers Entity/Component/Scene/World - the node system - since the
# scratch-rebuild architecture's Test/ tree has no dedicated Entity/Component/Scene/World
# folders, only Test/Integration (their behavior is inherently cross-module: hierarchy +
# ticking + serialization).

add_executable(uve_integration_tests
    Support/test_scratch_uve.cpp
    Nodes/3D/nodes_3d_uve_tests.cpp
    Nodes/3D/node_definitions_3d_uve_tests.cpp
    Nodes/CanvasLayer/node_definitions_canvas_layer_uve_tests.cpp
    Integration/Entity/archetype_uve_tests.cpp
    Integration/Entity/chunk_uve_tests.cpp
    Integration/Entity/entity_manager_uve_tests.cpp
    Integration/Localization/localization_uve_tests.cpp
    Integration/Scene/scene_graph_uve_tests.cpp
    Integration/Scene/node_common_components_uve_tests.cpp
    Integration/Scene/scene_component_metadata_uve_tests.cpp
    Integration/Scene/scene_serializer_uve_tests.cpp
    Integration/Scene/prefab_system_uve_tests.cpp
    Integration/Scene/prefab_maturity_uve_tests.cpp
    Integration/Scene/prefab_revision_policy_uve_tests.cpp
    Integration/Scene/particle_runtime_uve_tests.cpp
    Integration/Scene/scene_node_registry_uve_tests.cpp
    Integration/Animation/time_pose_contract_uve_tests.cpp
    Integration/Animation/animation_clip_uve_tests.cpp
    Integration/Animation/animation_tree_uve_tests.cpp
    Integration/Animation/animation_state_machine_uve_tests.cpp
    Integration/World/world_uve_tests.cpp
    Integration/Object/type_metadata_uve_tests.cpp
    Integration/Object/property_accessor_uve_tests.cpp
    Integration/Object/type_metadata_bridge_uve_tests.cpp
    Integration/Object/variant_uve_tests.cpp
    Integration/Scripting/script_asset_loader_uve_tests.cpp
    Integration/Scripting/script_graph_uve_tests.cpp
    Integration/Scripting/script_rotation_value_uve_tests.cpp
    Integration/Scripting/script_source_mapping_uve_tests.cpp
    Integration/Scripting/script_transform_value_uve_tests.cpp
    Integration/Scripting/script_vector3_value_uve_tests.cpp
    Physics/angular_dynamics_uve_tests.cpp
    Physics/area_overlap_lifecycle_tracker_uve_tests.cpp
    Physics/area_overlap_system_uve_tests.cpp
    Physics/character_controller_uve_tests.cpp
    Physics/collider_shape_uve_tests.cpp
    Physics/collision_lifecycle_tracker_uve_tests.cpp
    Physics/collision_system_uve_tests.cpp
    Physics/hinge_motor_limit_uve_tests.cpp
    Physics/physics_constraint_system_uve_tests.cpp
    Physics/physics_material_uve_tests.cpp
    Physics/physics_query_system_uve_tests.cpp
    Physics/physics_system_uve_tests.cpp
    Physics/raycast_system_uve_tests.cpp
    Physics/shape_cast_system_uve_tests.cpp
    Save/checkpoint_manager_uve_tests.cpp
    Save/save_game_system_uve_tests.cpp
    Save/save_payload_compression_uve_tests.cpp
    Save/save_revision_policy_uve_tests.cpp
    Audio/audio_attenuation_model_uve_tests.cpp
    Audio/audio_listener_orientation_validation_uve_tests.cpp
    Audio/audio_mixer_group_uve_tests.cpp
    Audio/audio_source_system_uve_tests.cpp
    Audio/audio_system_uve_tests.cpp
    Audio/null_audio_device_uve_tests.cpp
    Audio/miniaudio_audio_device_uve_tests.cpp
    Audio/pcm_gain_effect_uve_tests.cpp
    Audio/wav_metadata_uve_tests.cpp
    Audio/wav_pcm16_decoder_uve_tests.cpp
    Audio/wav_importer_uve_tests.cpp
    RHI/RenderSystems/camera_system_uve_tests.cpp
    RHI/RenderSystems/compute_system_uve_tests.cpp
    RHI/RenderSystems/particle_compute_simulation_uve_tests.cpp
    RHI/RenderSystems/frustum_cull_compute_uve_tests.cpp
    RHI/RenderSystems/frustum_cull_indirect_uve_tests.cpp
    RHI/RenderSystems/mesh_skin_compute_uve_tests.cpp
    RHI/RenderSystems/render_batch_uve_tests.cpp
    RHI/RenderSystems/compute_kernel_portability_uve_tests.cpp
    RHI/RenderSystems/compute_workloads_vulkan_uve_tests.cpp
    RHI/RenderSystems/light_system_uve_tests.cpp
    RHI/RenderSystems/mesh_renderer_uve_tests.cpp
    RHI/RenderSystems/mesh_render_eligibility_uve_tests.cpp
    RHI/RenderSystems/primitive_geometry_uve_tests.cpp
    RHI/RenderSystems/render_graph_uve_tests.cpp
    RHI/RenderSystems/render_queue_uve_tests.cpp
    RHI/RenderSystems/render_system_uve_tests.cpp
    RHI/RenderSystems/renderer_3d_uve_tests.cpp
    RHI/RenderSystems/viewport_manager_uve_tests.cpp
    RHI/RenderSystems/particle_render_bridge_uve_tests.cpp
    RHI/RenderSystems/particle_draw_command_uve_tests.cpp
    UI/ui_font_atlas_uve_tests.cpp
    UI/ui_runtime_uve_tests.cpp
)
target_link_libraries(uve_integration_tests PRIVATE
    uve_localization
    nlohmann_json::nlohmann_json
    uve_component
    uve_entity
    uve_scene
    uve_scripting
    uve_asset
    uve_memory
    uve_animation
    uve_world
    uve_object
    uve_physics
    uve_save
    uve_audio
    uve_render_systems
    uve_rhi_null
    # compute_system_uve_tests.cpp proves ComputeSystemUVE against a REAL GL device (program
    # compilation plus a byte-verified SSBO readback after a queued dispatch), skipping cleanly
    # when no display is available - so this executable needs the GL backend and the window
    # manager that brings up its context, exactly like uve_core_tests does for its own GL tests.
    # The raw <GL/gl.h> calls in that file resolve the same way engine_core_uve_tests.cpp's do:
    # OpenGL::GL and glfw propagate through the static-library dependency chain.
    uve_rhi_opengl
    # compute_workloads_vulkan_uve_tests.cpp runs CS4's particle simulation and CS5's culling
    # against a REAL Vulkan device (headless over VK_EXT_headless_surface), which is what proves
    # those workloads are backend-independent rather than GL-only - so this executable needs the
    # Vulkan backend too, alongside the GL one its other compute tests use.
    uve_rhi_vulkan
    uve_window
    uve_input
    uve_ui
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(uve_integration_tests PRIVATE cxx_std_20)
target_compile_definitions(uve_integration_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_integration_tests)

# Support/ holds the suite's shared test-only helpers. Test/ itself goes on the include path
# so they are reached as "Support/<name>.h" from any subdirectory.
target_include_directories(uve_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# archetype_uve_tests.cpp and chunk_uve_tests.cpp unit-test the module-private
# ArchetypeUVE/ChunkUVE Detail:: classes directly - this include path exposes only those
# module-private headers, never anything from Engine/Runtime/Entity/Internal as part of uve_entity's
# own public interface.
target_include_directories(uve_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Runtime/Entity/Internal)

gtest_discover_tests(uve_integration_tests)

#
# uve_engine_tests covers Engine/Runtime/Engine - EngineCoreUVE (the whole-engine bootstrap),
# EngineServicesUVE, VersionUVE, and EngineStateUVE. Kept as its own executable rather than
# folded into uve_core_tests or uve_integration_tests since EngineCoreUVE's own test suite
# exercises literally every already-ported subsystem at once (Asset/Audio/Input/Physics/Render/
# Save/Scene/Window) through one bootstrap object - it doesn't belong to either bucket, it sits
# on top of both. Linking uve_core is sufficient for every other dependency: uve_core PUBLIC-links
# the full set its own header/source files need (uve_platform, uve_logging, uve_utilities,
# uve_events, uve_memory, uve_threading, uve_commandline, uve_config, uve_asset, uve_scene,
# uve_physics, uve_input, uve_audio, uve_save, uve_window, uve_rhi, uve_rhi_shader,
# uve_rhi_opengl, uve_rhi_null, uve_render_systems), so all of those propagate transitively.
#
# engine_core_uve_tests.cpp includes <GL/gl.h> directly and calls raw glFinish/glReadBuffer/
# glReadPixels/glGetError in the handful of tests that exercise the real (non-headless) GL
# rendering path - this resolves the same way window_manager_uve_tests.cpp's own direct
# <GLFW/glfw3.h> use already does elsewhere in this file with no explicit glfw/OpenGL entry here:
# the actual link requirement (OpenGL::GL, glfw) propagates through the static-library dependency
# chain regardless of PRIVATE/PUBLIC (CMake still needs it to link the final executable), and the
# system GL/GLFW headers are already on the default compiler include path.
add_executable(uve_engine_tests
    Support/test_scratch_uve.cpp
    Engine/engine_state_uve_tests.cpp
    Engine/version_uve_tests.cpp
    Engine/engine_services_uve_tests.cpp
    Engine/engine_core_uve_tests.cpp
    Engine/script_gameplay_bindings_uve_tests.cpp
    Pack/project_launcher_uve_tests.cpp
    Pack/project_packager_uve_tests.cpp
)
target_link_libraries(uve_engine_tests PRIVATE
    nlohmann_json::nlohmann_json
    uve_core
    uve_math
    uve_pack_lib
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(uve_engine_tests PRIVATE cxx_std_20)
target_compile_definitions(uve_engine_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_engine_tests)

# Support/ holds the suite's shared test-only helpers. Test/ itself goes on the include path
# so they are reached as "Support/<name>.h" from any subdirectory.
target_include_directories(uve_engine_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(uve_engine_tests)

#
# uve_editor_tests covers Engine/Editor/EditorCore - EditorUVE, EditorBridgeUVE/
# EditorBridgeStdioUVE, DeveloperConsoleUVE, EditorToolSessionUVE, InspectorDrawerRegistryUVE,
# RegressionHarnessUVE. Every test in this suite either runs EngineCoreUVE in headlessUVE mode
# (no real GL/GLFW/ImGui context needed - EditorUVE skips ImGui init when the window manager
# isn't valid) or tests a standalone class with no EngineCoreUVE dependency at all
# (DeveloperConsoleUVE/EditorToolSessionUVE/InspectorDrawerRegistryUVE/RegressionHarnessUVE), so
# this doesn't need Xvfb or any GL/GLFW link wiring the way uve_engine_tests and
# gl_render_device_uve_tests.cpp do. Linking uve_editor is sufficient for every dependency - it
# PUBLIC-links uve_core, uve_scripting, and uve_plugins, which propagate transitively.
add_executable(uve_editor_tests
    Support/test_scratch_uve.cpp
    Editor/editor_bridge_stdio_uve_tests.cpp
    Editor/editor_bridge_uve_tests.cpp
    Editor/developer_console_uve_tests.cpp
    Editor/developer_console_bridge_uve_tests.cpp
    Editor/editor_tool_session_uve_tests.cpp
    Editor/editor_render_stats_uve_tests.cpp
    Editor/editor_chrome_layout_uve_tests.cpp
    Editor/editor_icon_registry_uve_tests.cpp
    Editor/editor_uve_tests.cpp
    Editor/editor_color_uve_tests.cpp
    Editor/entity_picker_uve_tests.cpp
    Editor/inspector_drawer_registry_uve_tests.cpp
    Editor/regression_harness_uve_tests.cpp
    Editor/data_table_registry_bridge_uve_tests.cpp
    Editor/prefab_maturity_editor_uve_tests.cpp
    Editor/scene_component_authoring_uve_tests.cpp
    Editor/scene_node_editor_uve_tests.cpp
)
target_link_libraries(uve_editor_tests PRIVATE
    nlohmann_json::nlohmann_json
    uve_editor
    uve_math
    GTest::gtest
    GTest::gtest_main
)

# The chrome-layout helper is shared between the editor's panel translation units but is not
# public API - it lives in EditorCore/Internal, so the test covering it needs that directory on
# the include path. Deliberately not promoted to Expose: nothing outside the editor has any
# business computing panel rects.
#
# uve_editor_imgui is linked directly because uve_editor keeps it PRIVATE, and the layout helper's
# signature is in ImGui's own types. Linking it here rather than making it PUBLIC on uve_editor
# keeps imgui out of every other consumer's include path, which is why it was PRIVATE to begin
# with - the test's need does not justify widening the library's surface.
target_include_directories(uve_editor_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal
)
target_link_libraries(uve_editor_tests PRIVATE uve_editor_imgui)

# EntityPickerUVE is the viewport module's engine bridge - the one target there allowed to see
# UVE::* types. Its header lives under Viewport/Internal (private to the module, like
# EditorMeshLayer), so the test reaches it as "integration/EntityPicker.h" the same way the
# module's own sources do.
target_link_libraries(uve_editor_tests PRIVATE univex_viewport_engine_bridge)
target_compile_features(uve_editor_tests PRIVATE cxx_std_20)
target_compile_definitions(uve_editor_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_editor_tests)

# Support/ holds the suite's shared test-only helpers. Test/ itself goes on the include path
# so they are reached as "Support/<name>.h" from any subdirectory.
target_include_directories(uve_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(uve_editor_tests)
