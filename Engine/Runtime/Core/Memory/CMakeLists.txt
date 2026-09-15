# Copyright (c) 2026 UniVex Studios. All Rights Reserved.


add_library(uve_memory STATIC
    Internal/heap_allocator_uve.cpp
    Internal/pool_allocator_uve.cpp
    Internal/stack_allocator_uve.cpp
    Internal/memory_manager_uve.cpp
)
target_include_directories(uve_memory PUBLIC Expose)
target_link_libraries(uve_memory PUBLIC uve_platform uve_debug)
target_compile_features(uve_memory PUBLIC cxx_std_20)
uve_set_warnings(uve_memory)
