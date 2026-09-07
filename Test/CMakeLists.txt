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
    Core/Events/event_system_uve_tests.cpp
    Core/Asset/uve_file_envelope_uve_tests.cpp
    Core/Asset/asset_database_uve_tests.cpp
    Input/input_system_uve_tests.cpp
    RHI/null_render_device_uve_tests.cpp
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
    uve_debug
    uve_memory
    uve_threading
    uve_math
    uve_utilities
    uve_events
    uve_asset
    uve_input
    uve_window
    uve_rhi
    uve_rhi_opengl
    uve_rhi_null
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(uve_core_tests PRIVATE cxx_std_20)
target_compile_definitions(uve_core_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_core_tests)

include(GoogleTest)
gtest_discover_tests(uve_core_tests)

#
# uve_integration_tests covers Entity/Component/Scene/World - the node system - since the
# scratch-rebuild architecture's Test/ tree has no dedicated Entity/Component/Scene/World
# folders, only Test/Integration (their behavior is inherently cross-module: hierarchy +
# ticking + serialization).

add_executable(uve_integration_tests
    Integration/Component/expanded_3d_node_components_uve_tests.cpp
    Integration/Entity/archetype_uve_tests.cpp
    Integration/Entity/chunk_uve_tests.cpp
    Integration/Entity/entity_manager_uve_tests.cpp
    Integration/Scene/scene_graph_uve_tests.cpp
    Integration/Scene/scene_serializer_uve_tests.cpp
    Integration/Scene/prefab_system_uve_tests.cpp
    Integration/Scene/prefab_maturity_uve_tests.cpp
    Integration/Scene/prefab_revision_policy_uve_tests.cpp
    Integration/Scene/particle_runtime_uve_tests.cpp
    Integration/Scene/scene_node_registry_uve_tests.cpp
    Integration/Animation/time_pose_contract_uve_tests.cpp
    Integration/Animation/animation_clip_uve_tests.cpp
    Integration/Animation/animation_tree_uve_tests.cpp
    Integration/World/world_uve_tests.cpp
    Integration/Object/type_metadata_uve_tests.cpp
    Integration/Object/property_accessor_uve_tests.cpp
    Physics/angular_dynamics_uve_tests.cpp
    Physics/area_overlap_lifecycle_tracker_uve_tests.cpp
    Physics/area_overlap_system_uve_tests.cpp
    Physics/character_controller_uve_tests.cpp
    Physics/collider_shape_uve_tests.cpp
    Physics/collision_system_uve_tests.cpp
    Physics/hinge_motor_limit_uve_tests.cpp
    Physics/physics_constraint_system_uve_tests.cpp
    Physics/physics_material_uve_tests.cpp
    Physics/physics_query_system_uve_tests.cpp
    Physics/physics_system_uve_tests.cpp
    Physics/raycast_system_uve_tests.cpp
    Physics/shape_cast_system_uve_tests.cpp
    Physics/trajectory_collision_prediction_uve_tests.cpp
)
target_link_libraries(uve_integration_tests PRIVATE
    nlohmann_json::nlohmann_json
    uve_component
    uve_entity
    uve_scene
    uve_animation
    uve_world
    uve_object
    uve_physics
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(uve_integration_tests PRIVATE cxx_std_20)
uve_set_warnings(uve_integration_tests)

# archetype_uve_tests.cpp and chunk_uve_tests.cpp unit-test the module-private
# ArchetypeUVE/ChunkUVE Detail:: classes directly - this include path exposes only those
# module-private headers, never anything from Engine/Runtime/Entity/src as part of uve_entity's
# own public interface.
target_include_directories(uve_integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/Engine/Runtime/Entity/src)

gtest_discover_tests(uve_integration_tests)
