# Copyright (c) 2026 UniVex Studios. All Rights Reserved.


add_library(uve_memory STATIC
    Internal/heap_allocator.cpp
    Internal/pool_allocator.cpp
    Internal/stack_allocator.cpp
    Internal/memory_manager.cpp
)
target_include_directories(uve_memory PUBLIC Expose)
target_link_libraries(uve_memory PUBLIC uve_platform uve_logging)
target_compile_features(uve_memory PUBLIC cxx_std_23)
uve_set_warnings(uve_memory)
