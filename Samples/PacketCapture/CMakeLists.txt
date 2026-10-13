# Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#
# The tap-fed capture demo: the story as a reusable static library, a thin runnable on
# top, and a ctest entry so CI runs the binary like a player would. The story needs no
# files, no window, and no clock but the sim's, so it runs anywhere (including headless
# CI) without scratch directories or display servers.
#

add_library(uve_packet_capture_sample STATIC
    packet_capture_sample_uve.cpp
)
target_include_directories(uve_packet_capture_sample PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(uve_packet_capture_sample PRIVATE
    uve_network
    uve_math
    uve_uvscript
)
target_compile_features(uve_packet_capture_sample PUBLIC cxx_std_23)
uve_set_warnings(uve_packet_capture_sample)

add_executable(packet_capture_sample main.cpp)
target_link_libraries(packet_capture_sample PRIVATE uve_packet_capture_sample)
uve_set_warnings(packet_capture_sample)

add_test(NAME packet_capture_sample COMMAND packet_capture_sample)
