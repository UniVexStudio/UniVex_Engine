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
    GTest::gtest
    GTest::gtest_main
)
target_compile_features(uve_core_tests PRIVATE cxx_std_20)
target_compile_definitions(uve_core_tests PRIVATE UVE_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
uve_set_warnings(uve_core_tests)

include(GoogleTest)
gtest_discover_tests(uve_core_tests)
