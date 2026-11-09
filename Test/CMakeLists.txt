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
    Support/test_scratch.cpp
    Core/Platform/editor_project_package_tests.cpp
    Core/Platform/application_runtime_tests.cpp
    Core/Platform/build_variant_tests.cpp
    Core/Platform/crash_reporter_tests.cpp
    Core/Logging/logger_tests.cpp
    Core/Logging/log_format_tests.cpp
    Core/Memory/heap_allocator_tests.cpp
    Core/Memory/pool_allocator_tests.cpp
    Core/Memory/stack_allocator_tests.cpp
    Core/Memory/memory_manager_tests.cpp
    Core/Memory/allocator_utils_tests.cpp
    Core/Memory/memory_alignment_tests.cpp
    Core/Memory/std_allocator_tests.cpp
    Core/Memory/linear_arena_tests.cpp
    Core/Memory/unique_ptr_tests.cpp
    Core/Memory/intrusive_ptr_tests.cpp
    Core/Memory/thread_safe_allocator_tests.cpp
    Core/Threading/job_counter_tests.cpp
    Core/Threading/job_graph_tests.cpp
    Core/Threading/thread_pool_tests.cpp
    Core/Threading/parallel_for_tests.cpp
    Core/Threading/spin_lock_tests.cpp
    Core/Threading/rw_lock_tests.cpp
    Core/Threading/semaphore_tests.cpp
    Core/Threading/thread_id_tests.cpp
    Core/Threading/thread_naming_tests.cpp
    Core/Threading/thread_control_tests.cpp
    Core/Utilities/timer_tests.cpp
    Core/Utilities/hash_tests.cpp
    Core/Utilities/topological_order_tests.cpp
    Core/Utilities/binary_buffer_tests.cpp
    Core/Utilities/uuid_tests.cpp
    Core/Utilities/scope_guard_tests.cpp
    Core/Utilities/non_copyable_tests.cpp
    Core/Utilities/flags_tests.cpp
    Core/Containers/fixed_array_tests.cpp
    Core/Containers/small_vector_tests.cpp
    Core/Containers/handle_table_tests.cpp
    Core/Containers/handle_table_lifetime_tests.cpp
    Core/Object/object_registry_tests.cpp
    Core/Containers/sparse_set_tests.cpp
    Core/Containers/ring_buffer_tests.cpp
    Core/Containers/bit_set_tests.cpp
    Core/Strings/string_id_tests.cpp
    Core/Strings/string_utils_tests.cpp
    Core/Scheduling/frame_scheduler_tests.cpp
    Core/Scheduling/frame_serial_tests.cpp
    Core/Diagnostics/profiler_diagnostics_tests.cpp
    Core/Events/event_system_tests.cpp
    Core/Asset/uve_file_envelope_tests.cpp
    Core/Asset/asset_database_tests.cpp
    Core/Asset/legacy_extension_migration_tests.cpp
    UVScript/uvscript_parser_tests.cpp
    UVScript/uvscript_vm_tests.cpp
    UVScript/uvscript_debugger_tests.cpp
    UVScript/uvscript_collections_tests.cpp
    Core/Asset/asset_manager_tests.cpp
    Core/Asset/file_system_tests.cpp
    Core/Asset/mesh_asset_tests.cpp
    Core/Asset/mesh_skinning_tests.cpp
    Core/Asset/texture_asset_tests.cpp
    Core/Asset/shader_asset_tests.cpp
    Core/Asset/font_asset_tests.cpp
    Core/Asset/ttf_importer_tests.cpp
    Core/Asset/material_asset_tests.cpp
    Core/Asset/asset_importer_tests.cpp
    Core/Asset/bmp_importer_tests.cpp
    Core/Asset/bmp_metadata_tests.cpp
    Core/Asset/tga_importer_tests.cpp
    Core/Asset/tga_metadata_tests.cpp
    Core/Asset/png_importer_tests.cpp
    Core/Asset/png_metadata_tests.cpp
    Core/Asset/jpeg_importer_tests.cpp
    Core/Asset/jpeg_metadata_tests.cpp
    Core/Asset/obj_importer_tests.cpp
    Core/Asset/fbx_mesh_converter_tests.cpp
    Core/Asset/fbx_material_converter_tests.cpp
    Core/Asset/embedded_texture_import_tests.cpp
    Core/Asset/fbx_importer_tests.cpp
    Core/Asset/obj_mesh_converter_tests.cpp
    Core/Asset/obj_metadata_tests.cpp
    Core/Asset/gltf_importer_tests.cpp
    Core/Asset/gltf_mesh_converter_tests.cpp
    Core/Asset/gltf_metadata_tests.cpp
    Core/Asset/gltf_skeleton_tests.cpp
    Core/Asset/skeleton_asset_tests.cpp
    Core/Asset/animation_library_asset_tests.cpp
    Core/Asset/gltf_skin_converter_tests.cpp
    Core/Asset/gltf_animation_converter_tests.cpp
    Core/Asset/mtl_importer_tests.cpp
    Core/Asset/mtl_material_converter_tests.cpp
    Core/Asset/mtl_metadata_tests.cpp
    Core/Asset/hot_reload_tests.cpp
    Core/Asset/hot_reload_backend_tests.cpp
    Core/Asset/asset_import_queue_tests.cpp
    Core/Asset/project_change_watcher_tests.cpp
    Core/Asset/project_file_index_tests.cpp
    Core/Asset/resource_dependency_graph_tests.cpp
    Core/Asset/animation_clip_asset_tests.cpp
    Core/Asset/animation_clip_compression_tests.cpp
    Core/Asset/data_table_tests.cpp
    Core/Asset/data_table_asset_tests.cpp
    Core/Asset/data_table_asset_manager_tests.cpp
    Core/Asset/data_table_asset_registration_tests.cpp
    Core/Asset/data_table_importer_tests.cpp
    Core/Asset/data_table_pipeline_tests.cpp
    Core/Asset/data_table_registry_tests.cpp
    Commandline/command_line_tests.cpp
    Config/config_manager_tests.cpp
    Config/settings_registry_tests.cpp
    Engine/engine_settings_layering_tests.cpp
    Engine/frame_pacing_tests.cpp
    Engine/dynamic_render_resolution_tests.cpp
    Engine/shader_cache_path_policy_tests.cpp
    Engine/render_backend_selection_tests.cpp
    Network/reliable_packet_window_tests.cpp
    Network/network_transport_tests.cpp
    Network/network_udp_tests.cpp
    Network/network_udp_listener_tests.cpp
    Network/network_session_tests.cpp
    Network/network_conditions_tests.cpp
    Network/network_replication_tests.cpp
    Network/network_rpc_tests.cpp
    Network/network_prediction_tests.cpp
    Network/network_inspector_tests.cpp
    Network/network_packet_capture_tests.cpp
    Network/network_packet_tap_tests.cpp
    Network/network_profiler_tests.cpp
    Network/network_shot_tests.cpp
    Network/network_interest_tests.cpp
    Network/network_interest_fanout_tests.cpp
    Network/network_value_codec_tests.cpp
    Network/network_multiplex_tests.cpp
    Network/network_relay_tests.cpp
    Network/network_lobby_tests.cpp
    Network/network_relay_rooms_tests.cpp
    Network/network_relay_interest_tests.cpp
    Network/network_crowd_tests.cpp
    Plugins/plugin_registry_tests.cpp
    Plugins/native_plugin_host_tests.cpp
    ProjectCheck/project_checker_tests.cpp
    Input/input_system_tests.cpp
    Gameplay/gameplay_input_tests.cpp
    Gameplay/gameplay_attributes_tests.cpp
    Gameplay/gameplay_tags_tests.cpp
    Gameplay/status_effects_tests.cpp
    Gameplay/trigger_volume_tests.cpp
    Gameplay/pawn_tests.cpp
    Gameplay/cinematic_tests.cpp
    Gameplay/camera_follow_tests.cpp
    AI/utility_ai_tests.cpp
    AI/perception_tests.cpp
    Input/input_names_tests.cpp
    Input/android_touch_router_tests.cpp
    Input/mobile_input_system_tests.cpp
    Input/mobile_gesture_system_tests.cpp
    Input/mobile_gesture_recognizer_tests.cpp
    RHI/null_render_device_tests.cpp
    RHI/device_operations_tests.cpp
    RHI/stencil_state_tests.cpp
    RHI/timeline_semaphore_tests.cpp
    RHI/geometry_pipeline_tests.cpp
    RHI/render_resource_lifetime_tests.cpp
    RHI/bind_group_tests.cpp
    RHI/Vulkan/vulkan_composite_alpha_policy_tests.cpp
    RHI/Vulkan/vulkan_present_mode_policy_tests.cpp
    RHI/Vulkan/vulkan_validation_policy_tests.cpp
    RHI/Shader/shader_binary_cache_tests.cpp
    RHI/Shader/shader_diagnostics_parser_tests.cpp
    RHI/Shader/shader_manager_tests.cpp
    RHI/Shader/shader_preprocessor_tests.cpp
    RHI/Shader/shader_program_tests.cpp
    Window/adaptive_render_resolution_tests.cpp
    Window/content_scale_policy_tests.cpp
    Window/cursor_policy_tests.cpp
    Window/display_mode_validation_tests.cpp
    Window/framebuffer_size_validation_tests.cpp
    Window/monitor_info_validation_tests.cpp
    Window/monitor_selection_policy_tests.cpp
    Window/null_window_manager_tests.cpp
    Window/presentation_layout_tests.cpp
    Window/vsync_policy_tests.cpp
    Window/window_desc_validation_tests.cpp
    Window/window_size_policy_tests.cpp
    Window/window_style_policy_tests.cpp
    Math/vector2_tests.cpp
    Math/vector3_tests.cpp
    Math/vector4_tests.cpp
    Math/quaternion_tests.cpp
    Math/matrix4x4_tests.cpp
    Math/point_transform_batch_tests.cpp
    Math/matrix3x3_tests.cpp
    Math/trs_tests.cpp
    Math/color_tests.cpp
    Math/vector2i_tests.cpp
    Math/vector3i_tests.cpp
    Math/rect_tests.cpp
    Math/rect_int_tests.cpp
    Math/aabb_tests.cpp
    Math/plane_tests.cpp
    Math/frustum_tests.cpp
    Math/scalar_tests.cpp
    Math/sphere_tests.cpp
    Math/oriented_box_tests.cpp
    Math/random_tests.cpp
    Math/easing_tests.cpp
    Math/noise_tests.cpp
    Math/curves_tests.cpp
    Retarget/retarget_names_tests.cpp
    Retarget/retarget_humanoid_tests.cpp
    Retarget/retarget_match_tests.cpp
    Retarget/retarget_conform_tests.cpp
    Retarget/retarget_files_tests.cpp
)
if(UVE_HAS_DESKTOP_GL)
    if(NOT WIN32)
        target_sources(uve_core_tests PRIVATE
            RHI/OpenGL/gl_render_device_tests.cpp
        )
    endif()
    target_sources(uve_core_tests PRIVATE
        RHI/Vulkan/vulkan_render_device_tests.cpp
        Window/window_manager_tests.cpp
    )
    target_link_libraries(uve_core_tests PRIVATE glfw)
endif()
target_link_libraries(uve_core_tests PRIVATE
    uve_object
    uve_uvscript
    uve_retarget
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
    uve_gameplay
    uve_ai
    uve_window
    uve_rhi
    uve_rhi_opengl
    uve_rhi_vulkan
    uve_rhi_null
    uve_rhi_shader
    uve_commandline
    uve_config
    uve_engine_settings
    uve_network
    uve_plugins
    # network_prediction_tests.cpp steps the production character predictor
    # (uve_objects_3d) as its mover - proof the layering holds with production movement code.
    uve_objects_3d
    uve_physics
    uve_project_check_lib
    ZLIB::ZLIB
    GTest::gtest
    GTest::gtest_main
)
if(NOT ANDROID)
    target_link_libraries(uve_core_tests PRIVATE JPEG::JPEG)
endif()
if(TARGET uve_jpeg_ep)
    add_dependencies(uve_core_tests uve_jpeg_ep)
endif()
target_compile_features(uve_core_tests PRIVATE cxx_std_23)
target_compile_definitions(uve_core_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")

# Native plugin host fixtures: tiny real shared libraries the host loads end to end.
add_library(uve_test_plugin_valid MODULE Plugins/Fixtures/uve_test_plugin_valid.cpp)
add_library(uve_test_plugin_bad_protocol MODULE Plugins/Fixtures/uve_test_plugin_bad_protocol.cpp)
add_library(uve_test_plugin_no_entry MODULE Plugins/Fixtures/uve_test_plugin_no_entry.cpp)
foreach(fixture IN ITEMS uve_test_plugin_valid uve_test_plugin_bad_protocol uve_test_plugin_no_entry)
    target_include_directories(${fixture} PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Runtime/Plugins/Expose)
    target_compile_features(${fixture} PRIVATE cxx_std_23)
endforeach()
target_compile_definitions(uve_core_tests PRIVATE
    UVE_TEST_PLUGIN_VALID_PATH="$<TARGET_FILE:uve_test_plugin_valid>"
    UVE_TEST_PLUGIN_BAD_PROTOCOL_PATH="$<TARGET_FILE:uve_test_plugin_bad_protocol>"
    UVE_TEST_PLUGIN_NO_ENTRY_PATH="$<TARGET_FILE:uve_test_plugin_no_entry>")
add_dependencies(uve_core_tests uve_test_plugin_valid uve_test_plugin_bad_protocol uve_test_plugin_no_entry)
uve_set_warnings(uve_core_tests)

# Support/ holds the suite's shared test-only helpers. Test/ itself goes on the include path
# so they are reached as "Support/<name>.h" from any subdirectory.
target_include_directories(uve_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The embedded-image helpers are module-private: they live in Engine/Runtime/Asset/Internal because
# nothing outside the asset importers has any business decoding somebody else's bytes into a
# texture. The tests that cover them need that directory on the include path, the same way the
# editor's chrome-layout tests reach EditorCore/Internal.
target_include_directories(uve_core_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Runtime/Asset/Internal)

# shader_binary_cache_tests.cpp/shader_diagnostics_parser_tests.cpp/
# shader_preprocessor_tests.cpp unit-test uve_rhi_shader's module-private classes directly -
# this include path exposes only those module-private headers, never anything from
# Engine/Runtime/RHI/Shader/Internal as part of uve_rhi_shader's own public interface (matching
# uve_integration_tests' identical need for Engine/Runtime/Entity/Internal below).
target_include_directories(uve_core_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Runtime/RHI/Shader/Internal)

# The UVScript native tests run each script both interpreted and as C++ that uvsc generated from it
# at build time, and compare the two.
uve_add_uvscript_native(uve_core_tests
    HOST UVScript/native/fake_object.uvhost
    SCRIPTS UVScript/native/player.uvs UVScript/native/math.uvs UVScript/native/waits.uvs
            UVScript/native/errors.uvs UVScript/native/methods.uvs UVScript/native/collections.uvs)
target_compile_definitions(uve_core_tests PRIVATE
    UVE_UVSCRIPT_NATIVE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/UVScript/native")

include(GoogleTest)
gtest_discover_tests(uve_core_tests)
# Apply resource ownership after discovery populates uve_core_tests_TESTS.
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/Support/ctest_resource_locks.cmake")

#
# uve_integration_tests covers Entity/Component/Scene/World - the node system - since the
# scratch-rebuild architecture's Test/ tree has no dedicated Entity/Component/Scene/World
# folders, only Test/Integration (their behavior is inherently cross-module: hierarchy +
# ticking + serialization).

add_executable(uve_integration_tests
    Support/test_scratch.cpp
    Objects/3D/objects_3d_tests.cpp
    Objects/3D/object_definitions_3d_tests.cpp
    Objects/3D/player_3d_tests.cpp
    Objects/3D/character_motion_tests.cpp
    Objects/3D/character_prediction_tests.cpp
    Objects/3D/replicated_entity_tests.cpp
    Objects/3D/interest_relevancy_tests.cpp
    Objects/3D/hitbox_lag_wiring_tests.cpp
    Objects/3D/ragdoll_3d_tests.cpp
    Objects/3D/collider_debug_scene_tests.cpp
    Samples/dedicated_server_sample_tests.cpp
    Samples/lag_comp_server_sample_tests.cpp
    Samples/packet_capture_sample_tests.cpp
    Samples/interest_fanout_sample_tests.cpp
    Objects/3D/health_tests.cpp
    Objects/3D/world_environment_tests.cpp
    Objects/3D/preview_lighting_tests.cpp
    Objects/3D/sky_lighting_tests.cpp
    Objects/3D/directional_light_3d_tests.cpp
    Objects/3D/fog_volume_3d_tests.cpp
    Objects/3D/water_3d_tests.cpp
    Objects/3D/terrain_3d_tests.cpp
    Objects/3D/world_partition_3d_tests.cpp
    Objects/3D/visibility_region_3d_tests.cpp
    Objects/3D/occluder_3d_tests.cpp
    Objects/3D/reflection_probe_3d_tests.cpp
    Objects/3D/animation_objects_tests.cpp
    Objects/3D/conformed_playback_tests.cpp
    Objects/3D/two_bone_ik_3d_tests.cpp
    Objects/3D/skin_component_tests.cpp
    Objects/3D/morph_target_component_tests.cpp
    Objects/3D/full_body_ik_3d_tests.cpp
    Objects/UI/object_definitions_canvas_tests.cpp
    Objects/2D/sprite_2d_tests.cpp
    Objects/2D/camera_2d_tests.cpp
    Integration/Entity/archetype_tests.cpp
    Integration/Entity/archetype_chunk_tests.cpp
    Integration/Entity/entity_manager_tests.cpp
    Integration/Entity/entity_enabled_tests.cpp
    Integration/Entity/entity_command_buffer_tests.cpp
    Integration/Entity/entity_view_tests.cpp
    Integration/Localization/localization_tests.cpp
    Integration/Scene/scene_graph_tests.cpp
    Integration/Scene/derived_pose_contract_tests.cpp
    Integration/Scene/children_index_tests.cpp
    Integration/Script/contact_script_events_tests.cpp
    Integration/Scene/bone_attachment_pass_tests.cpp
    Integration/Scene/two_bone_ik_pass_tests.cpp
    Integration/Scene/full_body_ik_pass_tests.cpp
    Integration/Scene/object_common_components_tests.cpp
    Integration/Scene/scene_component_metadata_tests.cpp
    Integration/Scene/scene_serializer_tests.cpp
    Integration/Scene/gameplay_serialization_tests.cpp
    Integration/Scene/ai_serialization_tests.cpp
    Integration/Scene/scene_object_type_tests.cpp
    Integration/Scene/prefab_system_tests.cpp
    Integration/Scene/prefab_maturity_tests.cpp
    Integration/Scene/prefab_revision_policy_tests.cpp
    Integration/Scene/particle_runtime_tests.cpp
    Integration/Scene/scene_object_registry_tests.cpp
    Integration/Scene/spawn_point_query_tests.cpp
    Integration/Scene/marker_3d_query_tests.cpp
    Integration/Animation/time_pose_contract_tests.cpp
    Integration/Animation/animation_clip_tests.cpp
    Integration/Animation/pose_graph_tests.cpp
    Integration/Animation/animation_state_machine_tests.cpp
    Integration/World/world_tests.cpp
    Integration/World/world_lifecycle_tests.cpp
    Integration/Object/type_metadata_tests.cpp
    Integration/Object/property_accessor_tests.cpp
    Integration/Object/type_metadata_bridge_tests.cpp
    Integration/Object/variant_tests.cpp
    Physics/angular_dynamics_tests.cpp
    Physics/area_3d_tests.cpp
    Physics/area_overlap_lifecycle_tracker_tests.cpp
    Physics/area_overlap_system_tests.cpp
    Physics/character_controller_tests.cpp
    Physics/character_body_motion_tests.cpp
    Physics/character_3d_tests.cpp
    Physics/uvscript_physics_forces_tests.cpp
    Script/uvscript_node_state_tests.cpp
    Script/uvscript_cross_node_tests.cpp
    Audio/uvscript_audio_bindings_tests.cpp
    Gameplay/uvscript_gameplay_bindings_tests.cpp
    Gameplay/uvscript_rpc_bindings_tests.cpp
    Gameplay/uvscript_lobby_bindings_tests.cpp
    Gameplay/uvscript_relay_bindings_tests.cpp
    Physics/collider_3d_tests.cpp
    Physics/collider_shape_tests.cpp
    Physics/collision_lifecycle_tracker_tests.cpp
    Physics/collision_system_tests.cpp
    Physics/continuous_collision_tests.cpp
    Physics/deterministic_physics_tests.cpp
    Physics/hinge_motor_limit_tests.cpp
    Physics/hitbox_3d_tests.cpp
    Physics/hitbox_lag_compensation_tests.cpp
    Physics/hitbox_strike_tests.cpp
    Physics/hurtbox_3d_tests.cpp
    Physics/interaction_area_3d_tests.cpp
    Physics/interaction_area_tests.cpp
    Physics/kinematic_3d_tests.cpp
    Physics/kinematic_body_tests.cpp
    Physics/projectile_3d_tests.cpp
    Physics/physics_constraint_system_tests.cpp
    Physics/physics_islands_tests.cpp
    Physics/physics_material_tests.cpp
    Physics/physics_query_system_tests.cpp
    Physics/physics_system_tests.cpp
    Physics/ray_cast_3d_tests.cpp
    Physics/ray_cast_3d_step_tests.cpp
    Physics/raycast_system_tests.cpp
    Physics/rigid_3d_tests.cpp
    Physics/ragdoll_solver_tests.cpp
    Physics/collider_debug_geometry_tests.cpp
    Physics/projectile_3d_step_tests.cpp
    Physics/shape_cast_system_tests.cpp
    Physics/spring_arm_3d_tests.cpp
    Physics/spring_arm_tests.cpp
    Physics/static_3d_tests.cpp
    Navigation/navmesh_tests.cpp
    Navigation/navmesh_bake_tests.cpp
    Navigation/nav_path_tests.cpp
    Navigation/nav_path_quality_tests.cpp
    Navigation/navmesh_agent_tests.cpp
    Navigation/navigation_runtime_tests.cpp
    Save/checkpoint_manager_tests.cpp
    Save/save_game_system_tests.cpp
    Save/save_payload_compression_tests.cpp
    Save/save_revision_policy_tests.cpp
    Audio/audio_attenuation_model_tests.cpp
    Audio/audio_listener_orientation_validation_tests.cpp
    Audio/audio_mixer_group_tests.cpp
    Audio/audio_dsp_tests.cpp
    Audio/audio_spatialization_tests.cpp
    Audio/audio_meter_tests.cpp
    Audio/audio_occlusion_tests.cpp
    Audio/audio_stream_tests.cpp
    Audio/audio_ambisonics_tests.cpp
    Audio/ambisonic_stream_source_tests.cpp
    Audio/audio_source_system_tests.cpp
    Audio/audio_system_tests.cpp
    Audio/null_audio_device_tests.cpp
    Audio/miniaudio_audio_device_tests.cpp
    Audio/pcm_gain_effect_tests.cpp
    Audio/wav_metadata_tests.cpp
    Audio/wav_pcm16_decoder_tests.cpp
    Audio/wav_importer_tests.cpp
    Audio/compressed_audio_importer_tests.cpp
    RHI/RenderSystems/camera_2d_system_tests.cpp
    RHI/RenderSystems/camera_system_tests.cpp
    RHI/RenderSystems/material_pipeline_state_tests.cpp
    RHI/RenderSystems/render_batch_tests.cpp
    RHI/RenderSystems/compute_kernel_portability_tests.cpp
    RHI/RenderSystems/compute_workloads_vulkan_tests.cpp
    RHI/RenderSystems/light_system_tests.cpp
    RHI/RenderSystems/decal_renderer_tests.cpp
    RHI/RenderSystems/debug_renderer_tests.cpp
    RHI/RenderSystems/mesh_renderer_tests.cpp
    RHI/RenderSystems/mesh_render_eligibility_tests.cpp
    RHI/RenderSystems/primitive_geometry_tests.cpp
    RHI/RenderSystems/render_graph_tests.cpp
    RHI/RenderSystems/render_graph_compiler_tests.cpp
    RHI/RenderSystems/render_queue_tests.cpp
    RHI/RenderSystems/render_system_tests.cpp
    RHI/RenderSystems/renderer_2d_tests.cpp
    RHI/RenderSystems/renderer_3d_tests.cpp
    RHI/RenderSystems/viewport_manager_tests.cpp
    RHI/RenderSystems/particle_render_bridge_tests.cpp
    RHI/RenderSystems/particle_draw_command_tests.cpp
    RHI/RenderSystems/dynamic_mesh_bvh_tests.cpp
    RHI/RenderSystems/shadow_atlas_manager_tests.cpp
    RHI/RenderSystems/light_shadow_planner_tests.cpp
    RHI/RenderSystems/contact_shadows_tests.cpp
    RHI/RenderSystems/mesh_renderer_spatial_bvh_tests.cpp
    RHI/RenderSystems/renderer_3d_indirect_instancing_tests.cpp
    UI/ui_font_atlas_tests.cpp
    UI/ui_runtime_tests.cpp
    UI/canvas_ancestry_tests.cpp
    UI/ui_layout_tests.cpp
    UI/ui_anchors_tests.cpp
    UI/ui_value_widgets_tests.cpp
    UI/ui_tween_tests.cpp
    UI/ui_nine_slice_tests.cpp
    UI/ui_checkbox_tooltip_tests.cpp
    UI/ui_dropdown_tests.cpp
    UI/ui_text_input_tests.cpp
    UI/ui_focus_navigation_tests.cpp
    UI/ui_scroll_tests.cpp
)
if(UVE_HAS_DESKTOP_GL)
    target_sources(uve_integration_tests PRIVATE
        RHI/RenderSystems/compute_system_tests.cpp
        RHI/RenderSystems/particle_compute_simulation_tests.cpp
        RHI/RenderSystems/frustum_cull_compute_tests.cpp
        RHI/RenderSystems/frustum_cull_indirect_tests.cpp
        RHI/RenderSystems/mesh_skin_compute_tests.cpp
    )
endif()
target_link_libraries(uve_integration_tests PRIVATE
    uve_dedicated_server_sample
    uve_lag_comp_server_sample
    uve_packet_capture_sample
    uve_interest_fanout_sample
    uve_retarget
    uve_navigation
    uve_localization
    nlohmann_json::nlohmann_json
    uve_component
    uve_entity
    uve_scene
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
    # compute_system_tests.cpp proves ComputeSystemUVE against a REAL GL device (program
    # compilation plus a byte-verified SSBO readback after a queued dispatch), skipping cleanly
    # when no display is available - so this executable needs the GL backend and the window
    # manager that brings up its context, exactly like uve_core_tests does for its own GL tests.
    # The raw <GL/gl.h> calls in that file resolve the same way engine_core_tests.cpp's do:
    # OpenGL::GL and glfw propagate through the static-library dependency chain.
    uve_rhi_opengl
    # compute_workloads_vulkan_tests.cpp runs CS4's particle simulation and CS5's culling
    # against a REAL Vulkan device (headless over VK_EXT_headless_surface), which is what proves
    # those workloads are backend-independent rather than GL-only - so this executable needs the
    # Vulkan backend too, alongside the GL one its other compute tests use.
    uve_rhi_vulkan
    uve_window
    uve_input
    uve_ui
    uve_gameplay
    uve_ai
    # uvscript_physics_forces_tests.cpp drives the real UVScriptObjectHostUVE (uve_core)
    # and script values (uve_uvscript) - the first runnable coverage of Engine's script host.
    uve_core
    uve_uvscript
    # uvscript_rpc_bindings_tests.cpp drives RPC peers plus the object host together -
    # script-initiated calls over loopback sessions, responses back into running scripts.
    uve_network
    # character_prediction_tests.cpp drives the predicted-pawn adapter (uve_objects_3d)
    # against loopback predictors - components to states and back, plus live reconciles.
    uve_objects_3d
    uve_objects_2d
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(uve_integration_tests PRIVATE cxx_std_23)
target_compile_definitions(uve_integration_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_integration_tests)

# Support/ holds the suite's shared test-only helpers. Test/ itself goes on the include path
# so they are reached as "Support/<name>.h" from any subdirectory.
target_include_directories(uve_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# archetype_tests.cpp and archetype_chunk_tests.cpp unit-test the module-private
# ArchetypeUVE/ArchetypeChunkUVE Detail:: classes directly - this include path exposes only those
# module-private headers, never anything from Engine/Runtime/Entity/Internal as part of uve_entity's
# own public interface.
target_include_directories(uve_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Runtime/Entity/Internal)

gtest_discover_tests(uve_integration_tests)

if(UVE_HAS_DESKTOP_GL)
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
# engine_core_tests.cpp includes <GL/gl.h> directly and calls raw glFinish/glReadBuffer/
# glReadPixels/glGetError in the handful of tests that exercise the real (non-headless) GL
# rendering path - this resolves the same way window_manager_tests.cpp's own direct
# <GLFW/glfw3.h> use already does elsewhere in this file with no explicit glfw/OpenGL entry here:
# the actual link requirement (OpenGL::GL, glfw) propagates through the static-library dependency
# chain regardless of PRIVATE/PUBLIC (CMake still needs it to link the final executable), and the
# system GL/GLFW headers are already on the default compiler include path.
add_executable(uve_engine_tests
    Support/test_scratch.cpp
    Engine/engine_state_tests.cpp
    Engine/version_tests.cpp
    Engine/engine_services_tests.cpp
    Engine/engine_core_tests.cpp
    Engine/engine_core_settings_layering_tests.cpp
    Engine/raycast_audio_occlusion_tests.cpp
    Engine/input_map_document_tests.cpp
    Pack/project_launcher_tests.cpp
    Pack/asset_cooker_tests.cpp
    Pack/project_packager_tests.cpp
)
target_link_libraries(uve_engine_tests PRIVATE
    nlohmann_json::nlohmann_json
    uve_core
    uve_math
    uve_pack_lib
    GTest::gtest
    GTest::gtest_main
)
if(UVE_HAS_DESKTOP_GL)
    target_link_libraries(uve_engine_tests PRIVATE glfw OpenGL::GL)
endif()
target_compile_features(uve_engine_tests PRIVATE cxx_std_23)
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
# (DeveloperConsoleUVE/EditorToolSessionUVE/InspectorDrawerRegistryUVE), so
# this doesn't need Xvfb or any GL/GLFW link wiring the way uve_engine_tests and
# gl_render_device_tests.cpp do. Linking uve_editor is sufficient for every dependency - it
# PUBLIC-links uve_core and uve_plugins, which propagate transitively.
add_executable(uve_editor_tests
    Support/test_scratch.cpp
    Editor/editor_bridge_stdio_tests.cpp
    Editor/editor_bridge_tests.cpp
    Editor/developer_console_tests.cpp
    Editor/developer_console_bridge_tests.cpp
    Editor/editor_tool_session_tests.cpp
    Editor/editor_render_stats_tests.cpp
    Editor/editor_audio_mixer_tests.cpp
    Editor/editor_chrome_layout_tests.cpp
    Editor/editor_icon_set_tests.cpp
    Editor/editor_tests.cpp
    Editor/editor_color_tests.cpp
    Editor/editor_settings_tests.cpp
    Editor/editor_layer_mask_tests.cpp
    Editor/editor_input_map_tests.cpp
    Editor/editor_commands_tests.cpp
    Editor/editor_hierarchy_view_tests.cpp
    Editor/entity_picker_tests.cpp
    Editor/editor_mesh_layer_tests.cpp
    Editor/overlay_raster_state_tests.cpp
    Editor/inspector_drawer_registry_tests.cpp
    Editor/data_table_registry_bridge_tests.cpp
    Editor/prefab_maturity_editor_tests.cpp
    Editor/content_catalogue_editor_tests.cpp
    Editor/uvscript_highlight_tests.cpp
    Editor/content_browser_model_tests.cpp
    Editor/model_source_placement_tests.cpp
    Editor/retarget_plan_tests.cpp
    Editor/animation_clip_editing_tests.cpp
    Editor/animation_graph_editing_tests.cpp
    Editor/editor_timeline_tracks_tests.cpp
    Editor/scene_component_authoring_tests.cpp
    Editor/file_picker_tests.cpp
    Editor/scene_object_editor_tests.cpp
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
target_compile_features(uve_editor_tests PRIVATE cxx_std_23)
target_compile_definitions(uve_editor_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_editor_tests)

# Support/ holds the suite's shared test-only helpers. Test/ itself goes on the include path
# so they are reached as "Support/<name>.h" from any subdirectory.
target_include_directories(uve_editor_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

gtest_discover_tests(uve_editor_tests DISCOVERY_TIMEOUT 60 DISCOVERY_MODE PRE_TEST)

add_executable(univex_viewport_2d_tests
    ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/tests/Viewport2DTests.cpp
)
target_link_libraries(univex_viewport_2d_tests PRIVATE univex_viewport_core GTest::gtest_main)
target_include_directories(univex_viewport_2d_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Expose)
target_compile_features(univex_viewport_2d_tests PRIVATE cxx_std_23)
target_compile_options(univex_viewport_2d_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(univex_viewport_2d_tests)
endif()

if(NOT UVE_HAS_DESKTOP_GL)
    # Exercise the actual EngineCoreUVE settings bootstrap in headless CPU-only builds. The full
    # engine test source includes raw GL calls; this focused suite covers the same settings path
    # without requiring those desktop-only tests or their runtime dependencies.
    add_executable(uve_engine_settings_integration_tests
        Support/test_scratch.cpp
        Engine/engine_core_settings_layering_tests.cpp
        Pack/asset_cooker_tests.cpp
    Pack/project_packager_tests.cpp
        Pack/project_launcher_tests.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal/editor_color.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal/editor_settings.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal/editor_layer_mask.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal/editor_input_map.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal/editor_hierarchy_view.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal/developer_console.cpp
        Editor/editor_color_tests.cpp
        Editor/editor_layer_mask_tests.cpp
        Editor/editor_input_map_tests.cpp
        Editor/editor_hierarchy_view_tests.cpp
        Editor/developer_console_tests.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Internal/Mat4.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Internal/OrbitCamera.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Internal/integration/SceneLightRulesUVE.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Internal/integration/EntityPicker.cpp
        ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Internal/integration/SelectionOutlineGeometry.cpp
        Editor/editor_mesh_layer_tests.cpp
        Editor/entity_picker_tests.cpp
    )
    target_link_libraries(uve_engine_settings_integration_tests PRIVATE
        nlohmann_json::nlohmann_json
        uve_core
        uve_world
        uve_diagnostics
        uve_network
        uve_plugins
        uve_retarget
        uve_pack_lib
        GTest::gtest
        GTest::gtest_main
    )
    target_include_directories(uve_engine_settings_integration_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Expose
        ${CMAKE_SOURCE_DIR}/Engine/Editor/EditorCore/Internal
        ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Expose
        ${CMAKE_SOURCE_DIR}/Engine/Editor/Viewport/Internal
    )
    target_compile_features(uve_engine_settings_integration_tests PRIVATE cxx_std_23)
    target_compile_definitions(uve_engine_settings_integration_tests PRIVATE
        UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
        UVE_EDITOR_SETTINGS_STANDALONE_UVE=1
    )
    uve_set_warnings(uve_engine_settings_integration_tests)
    gtest_discover_tests(uve_engine_settings_integration_tests)
endif()
