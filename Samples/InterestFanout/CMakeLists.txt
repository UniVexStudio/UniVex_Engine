# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The interest fan-out demo: the story as a reusable static library, a thin runnable on
# top, and a ctest entry so CI runs the binary like a player would. The story needs no
# files, no window, and no clock but the sim's, so it runs anywhere (including headless
# CI) without scratch directories or display servers.
#

add_library(uve_interest_fanout_sample STATIC
    interest_fanout_sample_uve.cpp
)
target_include_directories(uve_interest_fanout_sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uve_interest_fanout_sample PRIVATE
    uve_network
    uve_math
)
target_compile_features(uve_interest_fanout_sample PUBLIC cxx_std_23)
uve_set_warnings(uve_interest_fanout_sample)

add_executable(interest_fanout_sample main.cpp)
target_link_libraries(interest_fanout_sample PRIVATE uve_interest_fanout_sample)
uve_set_warnings(interest_fanout_sample)

add_test(NAME interest_fanout_sample COMMAND interest_fanout_sample)
