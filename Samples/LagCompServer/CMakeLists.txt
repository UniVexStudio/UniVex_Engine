# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The lag-compensation server demo: the story as a reusable static library, a thin
# runnable on top, and a ctest entry so CI runs the binary like a player would. The story
# needs no files, no window, and no clock but the sim's, so it runs anywhere (including
# headless CI) without scratch directories or display servers.
#

add_library(uve_lag_comp_server_sample STATIC
    lag_comp_server_sample_uve.cpp
)
target_include_directories(uve_lag_comp_server_sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uve_lag_comp_server_sample PRIVATE
    uve_network
    uve_objects_3d
    uve_scene
    uve_entity
    uve_component
    uve_gameplay
    uve_physics
    uve_math
)
target_compile_features(uve_lag_comp_server_sample PUBLIC cxx_std_23)
uve_set_warnings(uve_lag_comp_server_sample)

add_executable(lag_comp_server_sample main.cpp)
target_link_libraries(lag_comp_server_sample PRIVATE uve_lag_comp_server_sample)
uve_set_warnings(lag_comp_server_sample)

add_test(NAME lag_comp_server_sample COMMAND lag_comp_server_sample)
