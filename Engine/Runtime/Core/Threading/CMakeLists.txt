# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# ThreadPoolUVE is the first module that spawns real std::thread's, so this is
# the first CMakeLists.txt in the project to explicitly find and link
# Threads::Threads rather than relying on implicit toolchain linkage.

find_package(Threads REQUIRED)

add_library(uve_threading STATIC
    Internal/job_counter_uve.cpp
    Internal/job_graph_uve.cpp
    Internal/thread_naming_uve.cpp
    Internal/thread_pool_uve.cpp
)
# pthread_setname_np/getname_np are glibc extensions: visible only with _GNU_SOURCE.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(uve_threading PRIVATE _GNU_SOURCE)
endif()
target_include_directories(uve_threading PUBLIC Expose)
target_link_libraries(uve_threading PUBLIC uve_platform uve_logging Threads::Threads)
target_compile_features(uve_threading PUBLIC cxx_std_23)
uve_set_warnings(uve_threading)
